// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, Linaro Limited

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/div64.h>
#include <asm/dma.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include "q6apm.h"

#define DRV_NAME "q6apm-dai"

#define PLAYBACK_MIN_NUM_PERIODS	2
#define PLAYBACK_MAX_NUM_PERIODS	8
#define PLAYBACK_MAX_PERIOD_SIZE	65536
#define PLAYBACK_MIN_PERIOD_SIZE	128
#define CAPTURE_MIN_NUM_PERIODS		2
#define CAPTURE_MAX_NUM_PERIODS		8
#define CAPTURE_MAX_PERIOD_SIZE		65536
#define CAPTURE_MIN_PERIOD_SIZE		6144
#define BUFFER_BYTES_MAX (PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE)
#define BUFFER_BYTES_MIN (PLAYBACK_MIN_NUM_PERIODS * PLAYBACK_MIN_PERIOD_SIZE)
#define COMPR_PLAYBACK_MAX_FRAGMENT_SIZE (128 * 1024)
#define COMPR_PLAYBACK_MAX_NUM_FRAGMENTS (16 * 4)
#define COMPR_PLAYBACK_MIN_FRAGMENT_SIZE (8 * 1024)
#define COMPR_PLAYBACK_MIN_NUM_FRAGMENTS (4)
#define Q6APM_MAX_VMIDS 8
#define Q6APM_RESERVED_BUFFER_BYTES_MAX (256 * 1024)
#define Q6APM_SCM_MAX_VMID QCOM_SCM_VMID_ADSP_HEAP
#define Q6APM_STATIC_AUDIO_CARVEOUT_ADDR 0x86200000ULL
#define Q6APM_STATIC_AUDIO_CARVEOUT_SIZE 0x40000
#define SID_MASK_DEFAULT	0xF

static const struct snd_compr_codec_caps q6apm_compr_caps = {
	.num_descriptors = 1,
	.descriptor[0].max_ch = 2,
	.descriptor[0].sample_rates = {	8000, 11025, 12000, 16000, 22050,
					24000, 32000, 44100, 48000, 88200,
					96000, 176400, 192000 },
	.descriptor[0].num_sample_rates = 13,
	.descriptor[0].bit_rate[0] = 320,
	.descriptor[0].bit_rate[1] = 128,
	.descriptor[0].num_bitrates = 2,
	.descriptor[0].profiles = 0,
	.descriptor[0].modes = SND_AUDIOCHANMODE_MP3_STEREO,
	.descriptor[0].formats = 0,
};

enum stream_state {
	Q6APM_STREAM_IDLE = 0,
	Q6APM_STREAM_STOPPED,
	Q6APM_STREAM_RUNNING,
};

struct q6apm_dai_rtd {
	struct snd_pcm_substream *substream;
	struct snd_compr_stream *cstream;
	struct snd_codec codec;
	struct snd_compr_params codec_param;
	struct snd_dma_buffer dma_buffer;
	phys_addr_t phys;
	phys_addr_t dma_addr;
	bool dma_addr_valid;
	unsigned int pcm_size;
	unsigned int pcm_count;
	unsigned int periods;
	unsigned int scm_size;
	uint64_t bytes_sent;
	uint64_t bytes_received;
	uint64_t copied_total;
	uint16_t bits_per_sample;
	snd_pcm_uframes_t queue_ptr;
	bool next_track;
	bool scm_assigned;
	bool memory_mapped;
	u64 scm_src_perms;
	enum stream_state state;
	struct q6apm_graph *graph;
	spinlock_t lock;
	bool notify_on_drain;
};

struct q6apm_dai_data {
	struct device *dev;
	long long sid;
	int num_vmids;
	u32 vmids[Q6APM_MAX_VMIDS];
	u32 src_vmid;
	bool use_scm_assign;
	bool use_reserved_mem;
	bool reserved_mem_assigned;
	phys_addr_t reserved_mem_addr;
	size_t reserved_mem_size;
	bool aux_mem_assigned;
	phys_addr_t aux_mem_addr;
	size_t aux_mem_size;
	size_t buffer_bytes_max;
	u64 reserved_mem_src_perms;
	u64 aux_mem_src_perms;
};

static int q6apm_dai_assign_memory(struct q6apm_dai_rtd *prtd,
				   const struct q6apm_dai_data *pdata)
{
	struct qcom_scm_vmperm *dst_vmids;
	phys_addr_t aligned_addr;
	size_t aligned_size;
	phys_addr_t addr;
	size_t offset;
	int dst_count = 0;
	int ret;
	int i;
	struct device *dev = prtd->substream->pcm->card->dev;

	if (!pdata->use_scm_assign || pdata->use_reserved_mem ||
	    pdata->num_vmids <= 0 || prtd->scm_assigned)
		return 0;

	/* hw_params() validates and sets dma_addr_valid before prepare(). */
	if (!prtd->dma_addr_valid || !prtd->pcm_size) {
		dev_err(dev,
			"SCM assign: buffer not ready (dma_addr_valid=%d pcm_size=%u)\n",
			prtd->dma_addr_valid, prtd->pcm_size);
		return -EINVAL;
	}

	dst_vmids = kcalloc(pdata->num_vmids + 1, sizeof(*dst_vmids), GFP_KERNEL);
	if (!dst_vmids)
		return -ENOMEM;

	/* Always keep HLOS RW so CPU can continue buffer access. */
	dst_vmids[dst_count].vmid = QCOM_SCM_VMID_HLOS;
	dst_vmids[dst_count].perm = QCOM_SCM_PERM_RW;
	dst_count++;

	for (i = 0; i < pdata->num_vmids; i++) {
		/* HLOS is already added above; skip it if listed in qcom,vmid. */
		if (pdata->vmids[i] == QCOM_SCM_VMID_HLOS)
			continue;

		dst_vmids[dst_count].vmid = pdata->vmids[i];
		dst_vmids[dst_count].perm = QCOM_SCM_PERM_RW;
		dst_count++;
	}

	/* Nothing to assign beyond HLOS access. */
	if (dst_count == 1) {
		kfree(dst_vmids);
		return 0;
	}

	addr = prtd->dma_addr;
	aligned_addr = ALIGN_DOWN(addr, PAGE_SIZE);
	offset = addr - aligned_addr;
	aligned_size = PAGE_ALIGN(prtd->pcm_size + offset);

	prtd->scm_size = aligned_size;
	prtd->scm_src_perms = BIT_ULL(pdata->src_vmid);

	dev_dbg(dev,
		"SCM assign: addr=%pa size=%zu src_vmid=0x%x dst_count=%d\n",
		&aligned_addr, prtd->scm_size, pdata->src_vmid, dst_count);
	for (i = 0; i < dst_count; i++)
		dev_dbg(dev, "SCM assign: dst[%d] vmid=0x%x perm=0x%x\n",
			i, dst_vmids[i].vmid, dst_vmids[i].perm);

	ret = qcom_scm_assign_mem(aligned_addr, prtd->scm_size,
				  &prtd->scm_src_perms, dst_vmids, dst_count);
	kfree(dst_vmids);
	if (ret) {
		dev_err(dev,
			"SCM assign failed: addr=%pa size=%zu src_vmid=0x%x ret=%d\n",
			&aligned_addr, prtd->scm_size, pdata->src_vmid, ret);
		return ret;
	}

	dev_dbg(dev,
		"SCM assign ok: addr=%pa size=%zu src_perms_after=0x%llx\n",
		&aligned_addr, prtd->scm_size, prtd->scm_src_perms);
	prtd->scm_assigned = true;
	return 0;
}

static int q6apm_dai_unassign_memory(struct q6apm_dai_rtd *prtd,
				     const struct q6apm_dai_data *pdata)
{
	struct qcom_scm_vmperm hlos = {
		.vmid = QCOM_SCM_VMID_HLOS,
		.perm = QCOM_SCM_PERM_RW,
	};
	struct device *dev = prtd->substream->pcm->card->dev;
	phys_addr_t aligned_addr;
	size_t aligned_size;
	phys_addr_t addr;
	size_t offset;
	int ret;

	if (!pdata->use_scm_assign || !prtd->scm_assigned)
		return 0;

	addr = prtd->dma_addr;
	aligned_addr = ALIGN_DOWN(addr, PAGE_SIZE);
	offset = addr - aligned_addr;
	aligned_size = PAGE_ALIGN(prtd->pcm_size + offset);

	dev_dbg(dev,
		"SCM unassign: addr=%pa size=%zu src_perms=0x%llx\n",
		&aligned_addr, aligned_size, prtd->scm_src_perms);

	ret = qcom_scm_assign_mem(aligned_addr, aligned_size,
				  &prtd->scm_src_perms, &hlos, 1);
	if (!ret) {
		prtd->scm_assigned = false;
		prtd->scm_src_perms = BIT_ULL(QCOM_SCM_VMID_HLOS);
		dev_dbg(dev, "SCM unassign ok: addr=%pa\n", &aligned_addr);
	} else {
		dev_err(dev, "Failed to unassign DMA buffer %pa from VMIDs: %d\n",
			&prtd->dma_addr, ret);
		dev_err(dev,
			"SCM unassign failed: addr=%pa size=%zu src_perms=0x%llx ret=%d\n",
			&aligned_addr, aligned_size, prtd->scm_src_perms, ret);
	}

	return ret;
}

static int q6apm_dai_assign_reserved_memory(struct q6apm_dai_data *pdata)
{
	struct qcom_scm_vmperm *dst_vmids;
	struct device *dev = pdata->dev;
	int dst_count = 0;
	int ret;
	int i;

	if (!pdata->use_reserved_mem || !pdata->use_scm_assign ||
	    pdata->reserved_mem_assigned)
		return 0;

	dst_vmids = kcalloc(pdata->num_vmids + 1, sizeof(*dst_vmids), GFP_KERNEL);
	if (!dst_vmids)
		return -ENOMEM;

	dst_vmids[dst_count].vmid = QCOM_SCM_VMID_HLOS;
	dst_vmids[dst_count].perm = QCOM_SCM_PERM_RW;
	dst_count++;

	for (i = 0; i < pdata->num_vmids; i++) {
		if (pdata->vmids[i] == QCOM_SCM_VMID_HLOS)
			continue;

		dst_vmids[dst_count].vmid = pdata->vmids[i];
		dst_vmids[dst_count].perm = QCOM_SCM_PERM_RW;
		dst_count++;
	}
	if (dst_count == 1) {
		kfree(dst_vmids);
		return 0;
	}

	pdata->reserved_mem_src_perms = BIT_ULL(pdata->src_vmid);

	dev_dbg(dev,
		"SCM reserved assign begin: addr=%pa size=%zu src_vmid=0x%x dst_count=%d already_assigned=%d\n",
		&pdata->reserved_mem_addr, pdata->reserved_mem_size,
		pdata->src_vmid, dst_count, pdata->reserved_mem_assigned);
	for (i = 0; i < dst_count; i++)
		dev_dbg(dev, "SCM reserved assign dst[%d]: vmid=0x%x perm=0x%x\n",
			i, dst_vmids[i].vmid, dst_vmids[i].perm);

	ret = qcom_scm_assign_mem(pdata->reserved_mem_addr,
				  pdata->reserved_mem_size,
				&pdata->reserved_mem_src_perms,
				  dst_vmids, dst_count);
	if (ret) {
		dev_err(dev,
			"SCM reserved assign failed: addr=%pa size=%zu src_vmid=0x%x ret=%d\n",
			&pdata->reserved_mem_addr, pdata->reserved_mem_size,
			pdata->src_vmid, ret);
	} else {
		pdata->reserved_mem_assigned = true;
		dev_dbg(dev,
			"SCM reserved assign ok: addr=%pa size=%zu src_perms_after=0x%llx\n",
			&pdata->reserved_mem_addr, pdata->reserved_mem_size,
			pdata->reserved_mem_src_perms);
	}

	kfree(dst_vmids);
	return ret;
}

static int q6apm_dai_assign_aux_memory(struct q6apm_dai_data *pdata)
{
	struct qcom_scm_vmperm *dst_vmids;
	struct device *dev = pdata->dev;
	int dst_count = 0;
	int ret;
	int i;

	if (!pdata->use_scm_assign || pdata->aux_mem_assigned ||
	    !pdata->aux_mem_addr || !pdata->aux_mem_size)
		return 0;

	dst_vmids = kcalloc(pdata->num_vmids + 1, sizeof(*dst_vmids), GFP_KERNEL);
	if (!dst_vmids)
		return -ENOMEM;

	dst_vmids[dst_count].vmid = QCOM_SCM_VMID_HLOS;
	dst_vmids[dst_count].perm = QCOM_SCM_PERM_RW;
	dst_count++;

	for (i = 0; i < pdata->num_vmids; i++) {
		if (pdata->vmids[i] == QCOM_SCM_VMID_HLOS)
			continue;

		dst_vmids[dst_count].vmid = pdata->vmids[i];
		dst_vmids[dst_count].perm = QCOM_SCM_PERM_RW;
		dst_count++;
	}
	if (dst_count == 1) {
		kfree(dst_vmids);
		return 0;
	}

	pdata->aux_mem_src_perms = BIT_ULL(pdata->src_vmid);

	dev_dbg(dev,
		"SCM aux assign begin: addr=%pa size=%zu src_vmid=0x%x dst_count=%d already_assigned=%d\n",
		&pdata->aux_mem_addr, pdata->aux_mem_size,
		pdata->src_vmid, dst_count, pdata->aux_mem_assigned);
	for (i = 0; i < dst_count; i++)
		dev_dbg(dev, "SCM aux assign dst[%d]: vmid=0x%x perm=0x%x\n",
			i, dst_vmids[i].vmid, dst_vmids[i].perm);

	ret = qcom_scm_assign_mem(pdata->aux_mem_addr, pdata->aux_mem_size,
				  &pdata->aux_mem_src_perms, dst_vmids, dst_count);
	if (ret) {
		dev_err(dev,
			"SCM aux assign failed: addr=%pa size=%zu src_vmid=0x%x ret=%d\n",
			&pdata->aux_mem_addr, pdata->aux_mem_size,
			pdata->src_vmid, ret);
	} else {
		pdata->aux_mem_assigned = true;
		dev_dbg(dev,
			"SCM aux assign ok: addr=%pa size=%zu src_perms_after=0x%llx\n",
			&pdata->aux_mem_addr, pdata->aux_mem_size,
			pdata->aux_mem_src_perms);
	}

	kfree(dst_vmids);
	return ret;
}

static int q6apm_dai_unassign_reserved_memory(struct q6apm_dai_data *pdata)
{
	struct qcom_scm_vmperm hlos = {
		.vmid = QCOM_SCM_VMID_HLOS,
		.perm = QCOM_SCM_PERM_RW,
	};
	struct device *dev = pdata->dev;
	int ret;

	if (!pdata->use_reserved_mem || !pdata->reserved_mem_assigned) {
		dev_dbg(dev,
			"SCM reserved unassign skipped: use_reserved=%d assigned=%d\n",
			pdata->use_reserved_mem, pdata->reserved_mem_assigned);
		return 0;
	}

	dev_dbg(dev,
		"SCM reserved unassign begin: addr=%pa size=%zu src_perms=0x%llx\n",
		&pdata->reserved_mem_addr, pdata->reserved_mem_size,
		pdata->reserved_mem_src_perms);

	ret = qcom_scm_assign_mem(pdata->reserved_mem_addr,
				  pdata->reserved_mem_size,
				&pdata->reserved_mem_src_perms, &hlos, 1);
	if (ret) {
		dev_err(dev,
			"SCM reserved unassign failed: addr=%pa size=%zu src_perms=0x%llx ret=%d\n",
			&pdata->reserved_mem_addr, pdata->reserved_mem_size,
			pdata->reserved_mem_src_perms, ret);
		return ret;
	}

	pdata->reserved_mem_assigned = false;
	pdata->reserved_mem_src_perms = BIT_ULL(QCOM_SCM_VMID_HLOS);
	dev_dbg(dev, "SCM reserved unassign ok: addr=%pa\n",
		&pdata->reserved_mem_addr);

	return 0;
}

static int q6apm_dai_unassign_aux_memory(struct q6apm_dai_data *pdata)
{
	struct qcom_scm_vmperm hlos = {
		.vmid = QCOM_SCM_VMID_HLOS,
		.perm = QCOM_SCM_PERM_RW,
	};
	struct device *dev = pdata->dev;
	int ret;

	if (!pdata->aux_mem_assigned) {
		dev_dbg(dev, "SCM aux unassign skipped: assigned=%d\n",
			pdata->aux_mem_assigned);
		return 0;
	}

	dev_dbg(dev,
		"SCM aux unassign begin: addr=%pa size=%zu src_perms=0x%llx\n",
		&pdata->aux_mem_addr, pdata->aux_mem_size,
		pdata->aux_mem_src_perms);

	ret = qcom_scm_assign_mem(pdata->aux_mem_addr, pdata->aux_mem_size,
				  &pdata->aux_mem_src_perms, &hlos, 1);
	if (ret) {
		dev_err(dev,
			"SCM aux unassign failed: addr=%pa size=%zu src_perms=0x%llx ret=%d\n",
			&pdata->aux_mem_addr, pdata->aux_mem_size,
			pdata->aux_mem_src_perms, ret);
		return ret;
	}

	pdata->aux_mem_assigned = false;
	pdata->aux_mem_src_perms = BIT_ULL(QCOM_SCM_VMID_HLOS);
	dev_dbg(dev, "SCM aux unassign ok: addr=%pa\n",
		&pdata->aux_mem_addr);

	return 0;
}

static void q6apm_dai_release_reserved_memory(void *data)
{
	struct q6apm_dai_data *pdata = data;

	q6apm_dai_unassign_aux_memory(pdata);
	q6apm_dai_unassign_reserved_memory(pdata);
	of_reserved_mem_device_release(pdata->dev);
}

static const struct snd_pcm_hardware q6apm_dai_hardware_capture = {
	.info =                 (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_NO_REWINDS | SNDRV_PCM_INFO_SYNC_APPLPTR |
				 SNDRV_PCM_INFO_BATCH),
	.formats =              (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE),
	.rates =                SNDRV_PCM_RATE_8000_48000,
	.rate_min =             8000,
	.rate_max =             48000,
	.channels_min =         2,
	.channels_max =         4,
	.buffer_bytes_max =     CAPTURE_MAX_NUM_PERIODS * CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min =	CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max =     CAPTURE_MAX_PERIOD_SIZE,
	.periods_min =          CAPTURE_MIN_NUM_PERIODS,
	.periods_max =          CAPTURE_MAX_NUM_PERIODS,
	.fifo_size =            0,
};

static const struct snd_pcm_hardware q6apm_dai_hardware_playback = {
	.info =                 (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_NO_REWINDS | SNDRV_PCM_INFO_SYNC_APPLPTR |
				 SNDRV_PCM_INFO_BATCH),
	.formats =              (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE),
	.rates =                SNDRV_PCM_RATE_8000_192000,
	.rate_min =             8000,
	.rate_max =             192000,
	.channels_min =         2,
	.channels_max =         8,
	.buffer_bytes_max =     (PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE),
	.period_bytes_min =	PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max =     PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min =          PLAYBACK_MIN_NUM_PERIODS,
	.periods_max =          PLAYBACK_MAX_NUM_PERIODS,
	.fifo_size =            0,
};

static void event_handler(uint32_t opcode, uint32_t token, void *payload, void *priv)
{
	struct q6apm_dai_rtd *prtd = priv;
	struct snd_pcm_substream *substream = prtd->substream;

	switch (opcode) {
	case APM_CLIENT_EVENT_CMD_EOS_DONE:
		prtd->state = Q6APM_STREAM_STOPPED;
		break;
	case APM_CLIENT_EVENT_DATA_WRITE_DONE:
		snd_pcm_period_elapsed(substream);

		break;
	case APM_CLIENT_EVENT_DATA_READ_DONE:
		snd_pcm_period_elapsed(substream);
		if (prtd->state == Q6APM_STREAM_RUNNING)
			q6apm_read(prtd->graph);

		break;
	default:
		break;
	}
}

static void event_handler_compr(uint32_t opcode, uint32_t token,
					void *payload, void *priv)
{
	struct q6apm_dai_rtd *prtd = priv;
	struct snd_compr_stream *substream = prtd->cstream;
	uint32_t wflags = 0;
	uint64_t avail;
	uint32_t bytes_written, bytes_to_write;
	bool is_last_buffer = false;

	guard(spinlock_irqsave)(&prtd->lock);
	switch (opcode) {
	case APM_CLIENT_EVENT_CMD_EOS_DONE:
		if (prtd->notify_on_drain) {
			snd_compr_drain_notify(prtd->cstream);
			prtd->notify_on_drain = false;
		} else {
			prtd->state = Q6APM_STREAM_STOPPED;
		}
		break;
	case APM_CLIENT_EVENT_DATA_WRITE_DONE:
		bytes_written = token >> APM_WRITE_TOKEN_LEN_SHIFT;
		prtd->copied_total += bytes_written;
		snd_compr_fragment_elapsed(substream);

		if (prtd->state != Q6APM_STREAM_RUNNING)
			break;

		avail = prtd->bytes_received - prtd->bytes_sent;

		if (avail > prtd->pcm_count) {
			bytes_to_write = prtd->pcm_count;
		} else {
			if (substream->partial_drain || prtd->notify_on_drain)
				is_last_buffer = true;
			bytes_to_write = avail;
		}

		if (bytes_to_write) {
			if (substream->partial_drain && is_last_buffer)
				wflags |= APM_LAST_BUFFER_FLAG;

			q6apm_write_async(prtd->graph,
						bytes_to_write, 0, 0, wflags);

			prtd->bytes_sent += bytes_to_write;

			if (prtd->notify_on_drain && is_last_buffer)
				audioreach_shared_memory_send_eos(prtd->graph);
		}

		break;
	default:
		break;
	}
}

static int q6apm_dai_memory_unmap(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream);

static int q6apm_dai_memory_map(struct snd_soc_component *component,
				struct snd_pcm_substream *substream, int graph_id,
					size_t size);

static int q6apm_dai_prepare(struct snd_soc_component *component,
				     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	struct audioreach_module_config cfg;
	struct device *dev = component->dev;
	bool reserved_mapped_now = false;
	struct q6apm_dai_data *pdata;
	bool assigned_now = false;
	int ret;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata)
		return -EINVAL;

	if (!prtd || !prtd->graph) {
		dev_err(dev, "private data null or audio client freed\n");
		return -EINVAL;
	}

	if (pdata->use_reserved_mem && !prtd->memory_mapped) {
		dev_dbg(dev,
			"reserved prepare entry: assigned=%d mapped=%d dma=%pa phys=%pa pcm_size=%zu reserved=%pa/%zu graph=%d\n",
			 pdata->reserved_mem_assigned, prtd->memory_mapped,
			 &prtd->dma_addr, &prtd->phys, (size_t)prtd->pcm_size,
			 &pdata->reserved_mem_addr, pdata->reserved_mem_size,
			prtd->graph->id);

		ret = q6apm_dai_assign_aux_memory(pdata);
		if (ret) {
			dev_err(dev, "SCM aux assign failed at prepare: ret=%d\n",
				ret);
			return ret;
		}

		if (!pdata->reserved_mem_assigned) {
			ret = q6apm_dai_assign_reserved_memory(pdata);
			if (ret) {
				dev_err(dev, "SCM reserved assign failed at prepare: ret=%d\n",
					ret);
				return ret;
			}
		}

		dev_dbg(dev,
			"reserved prepare: dma=%pa phys=%pa size=%zu graph=%d\n",
			 &prtd->dma_addr, &prtd->phys, (size_t)prtd->pcm_size,
			prtd->graph->id);

		ret = q6apm_dai_memory_map(component, substream,
					   prtd->graph->id, prtd->pcm_size);
		if (ret)
			return ret;
		prtd->memory_mapped = true;
		reserved_mapped_now = true;
		dev_dbg(dev, "reserved prepare map ok: graph=%d\n",
			prtd->graph->id);
	}

	cfg.direction = substream->stream;
	cfg.sample_rate = runtime->rate;
	cfg.num_channels = runtime->channels;
	cfg.bit_width = prtd->bits_per_sample;
	cfg.fmt = SND_AUDIOCODEC_PCM;
	audioreach_set_default_channel_mapping(cfg.channel_map, runtime->channels);
	if (prtd->state) {
		/* clear the previous setup if any  */
		q6apm_graph_stop(prtd->graph);
		q6apm_free_fragments(prtd->graph, substream->stream);
	}

	prtd->pcm_count = snd_pcm_lib_period_bytes(substream);
	if (!prtd->scm_assigned) {
		ret = q6apm_dai_assign_memory(prtd, pdata);
		if (ret) {
			dev_err(dev, "SCM memory assign failed at prepare: ret=%d\n",
				ret);
			return ret;
		}
		assigned_now = prtd->scm_assigned;
	}

	/* rate and channels are sent to audio driver */
	dev_dbg(dev,
		"media_format_shmem begin: graph=%d dir=%u rate=%u channels=%u bit_width=%u\n",
		prtd->graph->id, cfg.direction, cfg.sample_rate,
		cfg.num_channels, cfg.bit_width);
	ret = q6apm_graph_media_format_shmem(prtd->graph, &cfg);
	if (ret < 0) {
		dev_err(dev, "q6apm_graph_media_format_shmem failed ret=%d\n", ret);
		goto err_unassign;
	}
	dev_dbg(dev, "media_format_shmem success: graph=%d\n",
		prtd->graph->id);

	dev_dbg(dev,
		"media_format_pcm begin: graph=%d dir=%u rate=%u channels=%u bit_width=%u\n",
		prtd->graph->id, cfg.direction, cfg.sample_rate,
		cfg.num_channels, cfg.bit_width);
	ret = q6apm_graph_media_format_pcm(prtd->graph, &cfg);
	if (ret < 0) {
		dev_err(dev, "q6apm_graph_media_format_pcm failed ret=%d\n", ret);
		goto err_unassign;
	}
	dev_dbg(dev, "media_format_pcm success: graph=%d\n",
		prtd->graph->id);

	dev_dbg(dev,
		"alloc_fragments begin: graph=%d dir=%u phys=%pa period_size=%u periods=%u pcm_size=%u\n",
		prtd->graph->id, substream->stream, &prtd->phys,
		prtd->pcm_size / prtd->periods, prtd->periods, prtd->pcm_size);
	ret = q6apm_alloc_fragments(prtd->graph, substream->stream, prtd->phys,
					(prtd->pcm_size / prtd->periods), prtd->periods);

	if (ret < 0) {
		dev_err(dev, "q6apm_alloc_fragments failed rc=%d\n", ret);
		ret = -ENOMEM;
		goto err_unassign;
	}
	dev_dbg(dev, "alloc_fragments success: graph=%d\n",
		prtd->graph->id);

	dev_dbg(dev, "graph_prepare begin: graph=%d\n",
		prtd->graph->id);
	ret = q6apm_graph_prepare(prtd->graph);
	if (ret) {
		dev_err(dev, "q6apm_graph_prepare failed ret=%d\n", ret);
		goto err_unassign;
	}
	dev_dbg(dev, "graph_prepare success: graph=%d\n",
		prtd->graph->id);

	dev_dbg(dev, "graph_start begin: graph=%d\n",
		prtd->graph->id);
	ret = q6apm_graph_start(prtd->graph);
	if (ret) {
		dev_err(dev, "q6apm_graph_start failed ret=%d\n", ret);
		goto err_unassign;
	}
	dev_dbg(dev, "graph_start success: graph=%d\n",
		prtd->graph->id);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		int i;
		/* Queue the buffers for Capture ONLY after graph is started */
		for (i = 0; i < runtime->periods; i++)
			q6apm_read(prtd->graph);

	}

	/* Now that graph as been prepared and started update the internal state accordingly */
	prtd->state = Q6APM_STREAM_RUNNING;

	return 0;

err_unassign:
	if (assigned_now)
		q6apm_dai_unassign_memory(prtd, pdata);
	if (reserved_mapped_now) {
		dev_err(dev, "reserved prepare error: unmapping graph after ret=%d\n", ret);
		if (!q6apm_dai_memory_unmap(component, substream))
			prtd->memory_mapped = false;
	}

	return ret;
}

static int q6apm_dai_ack(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	int i, ret = 0, avail_periods;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		avail_periods = (runtime->control->appl_ptr - prtd->queue_ptr)/runtime->period_size;
		for (i = 0; i < avail_periods; i++) {
			ret = q6apm_write_async(prtd->graph, prtd->pcm_count, 0, 0, NO_TIMESTAMP);
			if (ret < 0) {
				dev_err(component->dev, "q6apm_write_async failed ret=%d\n", ret);
				return ret;
			}
			prtd->queue_ptr += runtime->period_size;
		}
	}

	return ret;
}

static int q6apm_dai_trigger(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		/* TODO support be handled via SoftPause Module */
		prtd->state = Q6APM_STREAM_STOPPED;
		prtd->queue_ptr = 0;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int q6apm_dai_open(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_prtd, 0);
	struct device *dev = component->dev;
	struct q6apm_dai_data *pdata;
	struct q6apm_dai_rtd *prtd;
	int graph_id, ret;

	graph_id = cpu_dai->driver->id;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata) {
		dev_err(dev, "Drv data not found\n");
		return -EINVAL;
	}

	prtd = kzalloc_obj(*prtd);
	if (prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&prtd->lock);
	prtd->substream = substream;
	prtd->graph = q6apm_graph_open(dev, event_handler, prtd, graph_id, substream->stream);
	if (IS_ERR(prtd->graph)) {
		dev_err(dev, "q6apm_graph_open failed stream=%d\n", substream->stream);
		ret = PTR_ERR(prtd->graph);
		goto err;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = q6apm_dai_hardware_playback;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		runtime->hw = q6apm_dai_hardware_capture;
	runtime->hw.buffer_bytes_max = pdata->buffer_bytes_max;

	/* Ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(dev, "snd_pcm_hw_constraint_integer failed ret=%d\n", ret);
		goto err;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
							   BUFFER_BYTES_MIN,
							   pdata->buffer_bytes_max);
		if (ret < 0) {
			dev_err(dev, "buffer bytes minmax constraint failed ret=%d max=%zu\n",
				ret, pdata->buffer_bytes_max);
			goto err;
		}
	}

	/* setup 10ms latency to accommodate DSP restrictions */
	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 480);
	if (ret < 0) {
		dev_err(dev, "period size step constraint failed ret=%d\n", ret);
		goto err;
	}

	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 480);
	if (ret < 0) {
		dev_err(dev, "buffer size step constraint failed ret=%d\n", ret);
		goto err;
	}

	runtime->private_data = prtd;
	runtime->dma_bytes = pdata->buffer_bytes_max;
	prtd->scm_assigned = false;
	prtd->dma_addr_valid = false;
	prtd->dma_addr = substream->dma_buffer.addr;
	if (pdata->sid < 0)
		prtd->phys = substream->dma_buffer.addr;
	else
		prtd->phys = substream->dma_buffer.addr | (pdata->sid << 32);

	return 0;
err:
	kfree(prtd);

	return ret;
}

static int q6apm_dai_close(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_data *pdata = snd_soc_component_get_drvdata(component);
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	int ret;

	if (prtd->state) {
		/* only stop graph that is started */
		q6apm_graph_stop(prtd->graph);
		q6apm_free_fragments(prtd->graph, substream->stream);
	}

	if (prtd->memory_mapped) {
		ret = q6apm_dai_memory_unmap(component, substream);
		if (ret)
			dev_err(component->dev,
				"close(): memory unmap failed graph=%d ret=%d\n",
				 prtd->graph->id, ret);
		else
			prtd->memory_mapped = false;
	}

	if (pdata && prtd->scm_assigned) {
		ret = q6apm_dai_unassign_memory(prtd, pdata);
		if (ret) {
			dev_err(component->dev,
				"close(): VMID unassign failed for DMA buffer %pa: %d\n",
				 &prtd->dma_addr, ret);
			WARN_ONCE(1, "q6apm-dai: SCM VMID unassign leak for DMA buffer %pa\n",
				&prtd->dma_addr);
		}
	}

	q6apm_graph_close(prtd->graph);
	prtd->graph = NULL;
	kfree(prtd);
	runtime->private_data = NULL;

	return 0;
}

static snd_pcm_uframes_t q6apm_dai_pointer(struct snd_soc_component *component,
					   struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	snd_pcm_uframes_t ptr;

	ptr = q6apm_get_hw_pointer(prtd->graph, substream->stream) * runtime->period_size;
	if (ptr)
		return ptr - 1;

	return 0;
}

static int q6apm_dai_hw_params(struct snd_soc_component *component,
				       struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	struct q6apm_dai_data *pdata = snd_soc_component_get_drvdata(component);
	dma_addr_t dma_addr;
	int ret;

	if (!pdata)
		return -EINVAL;

	if (prtd->scm_assigned) {
		ret = q6apm_dai_unassign_memory(prtd, pdata);
		if (ret) {
			dev_err(component->dev,
				"hw_params(): failed to release previous VMID assignment: %d\n",
				 ret);
			return ret;
		}
	}

	dma_addr = runtime->dma_addr ? runtime->dma_addr : substream->dma_buffer.addr;
	if (!dma_addr)
		return -ENOMEM;

	prtd->dma_addr = dma_addr;
	prtd->dma_addr_valid = true;
	if (pdata->sid < 0)
		prtd->phys = dma_addr;
	else
		prtd->phys = dma_addr | (pdata->sid << 32);

	prtd->pcm_size = params_buffer_bytes(params);
	prtd->periods = params_periods(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		prtd->bits_per_sample = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		prtd->bits_per_sample = 24;
		break;
	default:
		return -EINVAL;
	}

	if (pdata->use_reserved_mem) {
		if (!soc_prtd)
			return -EINVAL;

		cpu_dai = snd_soc_rtd_to_cpu(soc_prtd, 0);
		if (!cpu_dai)
			return -EINVAL;

		dev_dbg(component->dev,
			"reserved hw_params: dma=%pad size=%zu periods=%u\n",
			 &dma_addr, (size_t)prtd->pcm_size, prtd->periods);
	}

	return 0;
}

static int q6apm_dai_hw_free(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct q6apm_dai_data *pdata = snd_soc_component_get_drvdata(component);
	struct q6apm_dai_rtd *prtd = substream->runtime->private_data;
	int ret;

	if (pdata && prtd->scm_assigned) {
		ret = q6apm_dai_unassign_memory(prtd, pdata);
		if (ret)
			dev_err(component->dev,
				"hw_free(): VMID unassign failed for DMA buffer %pa: %d\n",
				 &prtd->dma_addr, ret);
	}

	/* PCM DMA buffer is released by the ALSA managed buffer helpers. */
	prtd->dma_addr_valid = false;

	return 0;
}

static int q6apm_dai_memory_map(struct snd_soc_component *component,
				struct snd_pcm_substream *substream, int graph_id,
					size_t size)
{
	struct q6apm_dai_data *pdata;
	struct device *dev = component->dev;
	struct snd_pcm_runtime *runtime = substream->runtime;
	dma_addr_t dma_addr;
	phys_addr_t phys;
	int ret;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata) {
		dev_err(component->dev, "Drv data not found\n");
		return -EINVAL;
	}

	if (pdata->use_reserved_mem) {
		phys = pdata->reserved_mem_addr;
		size = pdata->reserved_mem_size;
		dev_dbg(dev, "reserved map begin: phys=%pa size=%zu graph=%d\n",
			&phys, size, graph_id);
	} else {
		if (runtime && runtime->dma_addr)
			dma_addr = runtime->dma_addr;
		else
			dma_addr = substream->dma_buffer.addr;
		if (!dma_addr)
			return -ENOMEM;

		if (pdata->sid < 0)
			phys = dma_addr;
		else
			phys = dma_addr | (pdata->sid << 32);
	}

	ret = q6apm_map_memory_fixed_region(dev, graph_id, phys, size);
	if (ret < 0)
		dev_err(dev, "q6apm_map_memory_fixed_region failed rc=%d phys=%pa size=%zu graph=%d\n",
			ret, &phys, size, graph_id);
	else
		dev_dbg(dev, "memory map done: phys=%pa size=%zu graph=%d\n",
			&phys, size, graph_id);

	return ret;
}

static int q6apm_dai_pcm_new(struct snd_soc_component *component, struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_pcm *pcm = rtd->pcm;
	struct q6apm_dai_data *pdata = snd_soc_component_get_drvdata(component);
	int graph_id, ret;
	struct snd_pcm_substream *substream;

	if (!pdata)
		return -EINVAL;

	graph_id = cpu_dai->driver->id;

	if (pdata->use_reserved_mem) {
		ret = snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
						     component->dev, 0,
							 pdata->buffer_bytes_max);
		if (ret)
			return ret;

		return 0;
	}

	/*
	 * Allocate one extra page as a workaround for a DSP bug where 32-bit
	 * address arithmetic can overflow when the buffer is placed near the
	 * end of the addressable range.
	 */
	ret = snd_pcm_set_fixed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV, component->dev,
					   BUFFER_BYTES_MAX + PAGE_SIZE);
	if (ret)
		return ret;

	/* Note: DSP backend dais are uni-directional ONLY(either playback or capture) */
	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
		ret = q6apm_dai_memory_map(component, substream, graph_id,
					   BUFFER_BYTES_MAX);
		if (ret)
			return ret;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
		ret = q6apm_dai_memory_map(component, substream, graph_id,
					   BUFFER_BYTES_MAX);
		if (ret)
			return ret;
	}

	return 0;
}

static int q6apm_dai_memory_unmap(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_prtd;
	struct snd_soc_dai *cpu_dai;
	int graph_id;
	int ret;

	soc_prtd = snd_soc_substream_to_rtd(substream);
	if (!soc_prtd)
		return -EINVAL;

	cpu_dai = snd_soc_rtd_to_cpu(soc_prtd, 0);
	if (!cpu_dai)
		return -EINVAL;

	graph_id = cpu_dai->driver->id;
	dev_dbg(component->dev, "memory unmap begin: graph=%d\n", graph_id);
	ret = q6apm_unmap_memory_fixed_region(component->dev, graph_id);
	if (ret)
		dev_err(component->dev,
			"memory unmap failed: graph=%d ret=%d\n", graph_id, ret);
	else
		dev_dbg(component->dev, "memory unmap done: graph=%d\n", graph_id);

	return ret;
}

static void q6apm_dai_pcm_free(struct snd_soc_component *component, struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;

	substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	if (substream) {
		dev_dbg(component->dev, "pcm_free(): capture memory unmap\n");
		q6apm_dai_memory_unmap(component, substream);
	}

	substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	if (substream) {
		dev_dbg(component->dev, "pcm_free(): playback memory unmap\n");
		q6apm_dai_memory_unmap(component, substream);
	}
}

static int q6apm_dai_compr_open(struct snd_soc_component *component,
				struct snd_compr_stream *stream)
{
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd;
	struct q6apm_dai_data *pdata;
	struct device *dev = component->dev;
	int ret, size;
	int graph_id;

	graph_id = cpu_dai->driver->id;
	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata)
		return -EINVAL;

	prtd = kzalloc_obj(*prtd);
	if (prtd == NULL)
		return -ENOMEM;

	prtd->cstream = stream;
	prtd->graph = q6apm_graph_open(dev, event_handler_compr, prtd, graph_id,
					SNDRV_PCM_STREAM_PLAYBACK);
	if (IS_ERR(prtd->graph)) {
		ret = PTR_ERR(prtd->graph);
		kfree(prtd);
		return ret;
	}

	runtime->private_data = prtd;
	runtime->dma_bytes = BUFFER_BYTES_MAX;
	size = COMPR_PLAYBACK_MAX_FRAGMENT_SIZE * COMPR_PLAYBACK_MAX_NUM_FRAGMENTS;
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, dev, size, &prtd->dma_buffer);
	if (ret)
		return ret;

	if (pdata->sid < 0)
		prtd->phys = prtd->dma_buffer.addr;
	else
		prtd->phys = prtd->dma_buffer.addr | (pdata->sid << 32);

	snd_compr_set_runtime_buffer(stream, &prtd->dma_buffer);
	spin_lock_init(&prtd->lock);

	q6apm_enable_compress_module(dev, prtd->graph, true);
	return 0;
}

static int q6apm_dai_compr_free(struct snd_soc_component *component,
				struct snd_compr_stream *stream)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;

	q6apm_graph_stop(prtd->graph);
	q6apm_free_fragments(prtd->graph, SNDRV_PCM_STREAM_PLAYBACK);
	q6apm_unmap_memory_fixed_region(component->dev, prtd->graph->id);
	q6apm_graph_close(prtd->graph);
	snd_dma_free_pages(&prtd->dma_buffer);
	prtd->graph = NULL;
	kfree(prtd);
	runtime->private_data = NULL;

	return 0;
}

static int q6apm_dai_compr_get_caps(struct snd_soc_component *component,
				    struct snd_compr_stream *stream,
				    struct snd_compr_caps *caps)
{
	caps->direction = SND_COMPRESS_PLAYBACK;
	caps->min_fragment_size = COMPR_PLAYBACK_MIN_FRAGMENT_SIZE;
	caps->max_fragment_size = COMPR_PLAYBACK_MAX_FRAGMENT_SIZE;
	caps->min_fragments = COMPR_PLAYBACK_MIN_NUM_FRAGMENTS;
	caps->max_fragments = COMPR_PLAYBACK_MAX_NUM_FRAGMENTS;
	caps->num_codecs = 4;
	caps->codecs[0] = SND_AUDIOCODEC_MP3;
	caps->codecs[1] = SND_AUDIOCODEC_AAC;
	caps->codecs[2] = SND_AUDIOCODEC_FLAC;
	caps->codecs[3] = SND_AUDIOCODEC_OPUS_RAW;

	return 0;
}

static int q6apm_dai_compr_get_codec_caps(struct snd_soc_component *component,
					  struct snd_compr_stream *stream,
					  struct snd_compr_codec_caps *codec)
{
	switch (codec->codec) {
	case SND_AUDIOCODEC_MP3:
		*codec = q6apm_compr_caps;
		break;
	default:
		break;
	}

	return 0;
}

static int q6apm_dai_compr_pointer(struct snd_soc_component *component,
				   struct snd_compr_stream *stream,
				   struct snd_compr_tstamp64 *tstamp)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	uint64_t temp_copied_total;

	guard(spinlock_irqsave)(&prtd->lock);
	tstamp->copied_total = prtd->copied_total;
	temp_copied_total = tstamp->copied_total;
	tstamp->byte_offset = do_div(temp_copied_total, prtd->pcm_size);

	return 0;
}

static int q6apm_dai_compr_trigger(struct snd_soc_component *component,
			    struct snd_compr_stream *stream, int cmd)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = q6apm_write_async(prtd->graph, prtd->pcm_count, 0, 0, NO_TIMESTAMP);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	case SND_COMPR_TRIGGER_NEXT_TRACK:
		prtd->next_track = true;
		break;
	case SND_COMPR_TRIGGER_DRAIN:
	case SND_COMPR_TRIGGER_PARTIAL_DRAIN:
		prtd->notify_on_drain = true;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int q6apm_dai_compr_ack(struct snd_soc_component *component, struct snd_compr_stream *stream,
			size_t count)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;

	guard(spinlock_irqsave)(&prtd->lock);
	prtd->bytes_received += count;

	return count;
}

static int q6apm_dai_compr_set_params(struct snd_soc_component *component,
				      struct snd_compr_stream *stream,
				      struct snd_compr_params *params)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	struct q6apm_dai_data *pdata;
	struct audioreach_module_config cfg;
	struct snd_codec *codec = &params->codec;
	int dir = stream->direction;
	int ret;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata)
		return -EINVAL;

	prtd->periods = runtime->fragments;
	prtd->pcm_count = runtime->fragment_size;
	prtd->pcm_size = runtime->fragments * runtime->fragment_size;
	prtd->bits_per_sample = 16;

	if (prtd->next_track != true) {
		memcpy(&prtd->codec, codec, sizeof(*codec));

		ret = q6apm_set_real_module_id(component->dev, prtd->graph, codec->id);
		if (ret)
			return ret;

		cfg.direction = dir;
		cfg.sample_rate = codec->sample_rate;
		cfg.num_channels = 2;
		cfg.bit_width = prtd->bits_per_sample;
		cfg.fmt = codec->id;
		audioreach_set_default_channel_mapping(cfg.channel_map,
						       cfg.num_channels);
		memcpy(&cfg.codec, codec, sizeof(*codec));

		ret = q6apm_graph_media_format_shmem(prtd->graph, &cfg);
		if (ret < 0)
			return ret;

		ret = q6apm_graph_media_format_pcm(prtd->graph, &cfg);
		if (ret)
			return ret;

		ret = q6apm_alloc_fragments(prtd->graph, SNDRV_PCM_STREAM_PLAYBACK,
					prtd->phys, (prtd->pcm_size / prtd->periods),
					prtd->periods);
		if (ret < 0)
			return -ENOMEM;

		ret = q6apm_graph_prepare(prtd->graph);
		if (ret)
			return ret;

		ret = q6apm_graph_start(prtd->graph);
		if (ret)
			return ret;

	} else {
		cfg.direction = dir;
		cfg.sample_rate = codec->sample_rate;
		cfg.num_channels = 2;
		cfg.bit_width = prtd->bits_per_sample;
		cfg.fmt = codec->id;
		memcpy(&cfg.codec, codec, sizeof(*codec));

		ret = audioreach_compr_set_param(prtd->graph,  &cfg);
		if (ret < 0)
			return ret;
	}
	prtd->state = Q6APM_STREAM_RUNNING;

	return 0;
}

static int q6apm_dai_compr_set_metadata(struct snd_soc_component *component,
					struct snd_compr_stream *stream,
					struct snd_compr_metadata *metadata)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	int ret = 0;

	switch (metadata->key) {
	case SNDRV_COMPRESS_ENCODER_PADDING:
		q6apm_remove_trailing_silence(component->dev, prtd->graph,
					      metadata->value[0]);
		break;
	case SNDRV_COMPRESS_ENCODER_DELAY:
		q6apm_remove_initial_silence(component->dev, prtd->graph,
					     metadata->value[0]);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int q6apm_dai_compr_mmap(struct snd_soc_component *component,
				struct snd_compr_stream *stream,
				struct vm_area_struct *vma)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	struct device *dev = component->dev;

	return dma_mmap_coherent(dev, vma, prtd->dma_buffer.area, prtd->dma_buffer.addr,
				 prtd->dma_buffer.bytes);
}

static int q6apm_compr_copy(struct snd_soc_component *component,
			    struct snd_compr_stream *stream, char __user *buf,
			    size_t count)
{
	struct snd_compr_runtime *runtime = stream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	void *dstn;
	size_t copy;
	u32 wflags = 0;
	u32 app_pointer;
	uint64_t bytes_received;
	uint64_t temp_bytes_received;
	uint32_t bytes_to_write;
	uint64_t avail, bytes_in_flight = 0;

	bytes_received = prtd->bytes_received;
	temp_bytes_received = bytes_received;

	/**
	 * Make sure that next track data pointer is aligned at 32 bit boundary
	 * This is a Mandatory requirement from DSP data buffers alignment
	 */
	if (prtd->next_track) {
		bytes_received = ALIGN(prtd->bytes_received, prtd->pcm_count);
		temp_bytes_received = bytes_received;
	}

	app_pointer = do_div(temp_bytes_received, prtd->pcm_size);
	dstn = prtd->dma_buffer.area + app_pointer;

	if (count < prtd->pcm_size - app_pointer) {
		if (copy_from_user(dstn, buf, count))
			return -EFAULT;
	} else {
		copy = prtd->pcm_size - app_pointer;
		if (copy_from_user(dstn, buf, copy))
			return -EFAULT;
		if (copy_from_user(prtd->dma_buffer.area, buf + copy, count - copy))
			return -EFAULT;
	}

	guard(spinlock_irqsave)(&prtd->lock);
	bytes_in_flight = prtd->bytes_received - prtd->copied_total;

	if (prtd->next_track) {
		prtd->next_track = false;
		prtd->copied_total = ALIGN(prtd->copied_total, prtd->pcm_count);
		prtd->bytes_sent = ALIGN(prtd->bytes_sent, prtd->pcm_count);
	}

	prtd->bytes_received = bytes_received + count;

	/* Kick off the data to dsp if its starving!! */
	if (prtd->state == Q6APM_STREAM_RUNNING && (bytes_in_flight == 0)) {
		bytes_to_write = prtd->pcm_count;
		avail = prtd->bytes_received - prtd->bytes_sent;

		if (avail < prtd->pcm_count)
			bytes_to_write = avail;

		q6apm_write_async(prtd->graph, bytes_to_write, 0, 0, wflags);
		prtd->bytes_sent += bytes_to_write;
	}

	return count;
}

static const struct snd_compress_ops q6apm_dai_compress_ops = {
	.open		= q6apm_dai_compr_open,
	.free		= q6apm_dai_compr_free,
	.get_caps	= q6apm_dai_compr_get_caps,
	.get_codec_caps	= q6apm_dai_compr_get_codec_caps,
	.pointer	= q6apm_dai_compr_pointer,
	.trigger	= q6apm_dai_compr_trigger,
	.ack		= q6apm_dai_compr_ack,
	.set_params	= q6apm_dai_compr_set_params,
	.set_metadata	= q6apm_dai_compr_set_metadata,
	.mmap		= q6apm_dai_compr_mmap,
	.copy		= q6apm_compr_copy,
};

static const struct snd_soc_component_driver q6apm_fe_dai_component = {
	.name		= DRV_NAME,
	.open		= q6apm_dai_open,
	.close		= q6apm_dai_close,
	.prepare	= q6apm_dai_prepare,
	.hw_free	= q6apm_dai_hw_free,
	.pcm_new	= q6apm_dai_pcm_new,
	.pcm_free	= q6apm_dai_pcm_free,
	.hw_params	= q6apm_dai_hw_params,
	.pointer	= q6apm_dai_pointer,
	.trigger	= q6apm_dai_trigger,
	.ack		= q6apm_dai_ack,
	.compress_ops	= &q6apm_dai_compress_ops,
	.use_dai_pcm_id = true,
	.remove_order   = SND_SOC_COMP_ORDER_EARLY,
};

static int q6apm_dai_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct q6apm_dai_data *pdata;
	struct of_phandle_args args;
	struct resource reserved_mem;
	size_t required_mem_size = max_t(size_t, BUFFER_BYTES_MIN,
					 CAPTURE_MIN_NUM_PERIODS * CAPTURE_MIN_PERIOD_SIZE);
	u32 src_vmid;
	int vmids;
	int rc;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	pdata->dev = dev;
	pdata->buffer_bytes_max = BUFFER_BYTES_MAX;
	pdata->aux_mem_addr = Q6APM_STATIC_AUDIO_CARVEOUT_ADDR;
	pdata->aux_mem_size = Q6APM_STATIC_AUDIO_CARVEOUT_SIZE;
	pdata->aux_mem_src_perms = BIT_ULL(QCOM_SCM_VMID_HLOS);
	dev_dbg(dev, "probe: static aux memory addr=%pa size=%zu\n",
		&pdata->aux_mem_addr, pdata->aux_mem_size);

	rc = of_parse_phandle_with_fixed_args(node, "iommus", 1, 0, &args);
	if (rc < 0)
		pdata->sid = -1;
	else
		pdata->sid = args.args[0] & SID_MASK_DEFAULT;
	dev_dbg(dev, "probe: sid=%lld iommus_rc=%d\n", pdata->sid, rc);

	if (of_property_present(node, "memory-region")) {
		rc = of_reserved_mem_region_to_resource(node, 0, &reserved_mem);
		if (rc) {
			dev_err(dev, "failed to parse reserved memory region: %d\n", rc);
			return rc;
		}

		rc = of_reserved_mem_device_init_by_idx(dev, node, 0);
		if (rc) {
			dev_err(dev, "failed to attach reserved memory region: %d\n", rc);
			return rc;
		}

		pdata->use_reserved_mem = true;
		pdata->reserved_mem_addr = reserved_mem.start;
		pdata->reserved_mem_size = resource_size(&reserved_mem);
		pdata->buffer_bytes_max = min_t(size_t, pdata->reserved_mem_size / 2,
						BUFFER_BYTES_MAX);
		pdata->buffer_bytes_max = min_t(size_t, pdata->buffer_bytes_max,
						Q6APM_RESERVED_BUFFER_BYTES_MAX);
		pdata->reserved_mem_src_perms = BIT_ULL(QCOM_SCM_VMID_HLOS);
		dev_dbg(dev,
			"probe: reserved memory addr=%pa size=%zu buffer_bytes_max=%zu\n",
			&pdata->reserved_mem_addr, pdata->reserved_mem_size,
			pdata->buffer_bytes_max);

		rc = devm_add_action_or_reset(dev,
					      q6apm_dai_release_reserved_memory, pdata);
		if (rc)
			return rc;
	}

	/*
	 * Default src_vmid to HLOS. Platforms where PCM DMA buffers reside in
	 * S2-only memory pre-owned by a non-HLOS VMID must override this via
	 * qcom,src-vmid so qcom_scm_assign_mem() receives the correct srcvm.
	 */
	pdata->src_vmid = QCOM_SCM_VMID_HLOS;
	if (!of_property_read_u32(node, "qcom,src-vmid", &src_vmid)) {
		if (src_vmid == 0 || src_vmid > Q6APM_SCM_MAX_VMID) {
			dev_err(dev, "qcom,src-vmid=%u out of range [1..%u]\n",
				src_vmid, Q6APM_SCM_MAX_VMID);
			return -EINVAL;
		}
		pdata->src_vmid = src_vmid;
		dev_dbg(dev, "probe: SCM src_vmid set to 0x%x from DT\n", src_vmid);
	}

	vmids = of_property_count_u32_elems(node, "qcom,vmid");
	if (vmids == -EINVAL) {
		pdata->num_vmids = 0;
		pdata->use_scm_assign = false;
	} else if (vmids < 0) {
		return vmids;
	} else if (vmids == 0) {
		dev_err(dev, "qcom,vmid must contain at least one VMID\n");
		return -EINVAL;
	} else if (vmids > Q6APM_MAX_VMIDS) {
		dev_err(dev, "qcom,vmid: %d VMIDs exceeds maximum of %d\n",
			vmids, Q6APM_MAX_VMIDS);
		return -EINVAL;
	}

	if (vmids > 0) {
		int i;

		rc = of_property_read_u32_array(node, "qcom,vmid",
						pdata->vmids, vmids);
		if (rc)
			return rc;
		for (i = 0; i < vmids; i++) {
			if (pdata->vmids[i] == QCOM_SCM_VMID_HLOS) {
				dev_err(dev, "qcom,vmid must not include HLOS VMID (%u)\n",
					QCOM_SCM_VMID_HLOS);
				return -EINVAL;
			}
			if (pdata->vmids[i] > Q6APM_SCM_MAX_VMID) {
				dev_err(dev, "qcom,vmid[%d]=%u exceeds SCM max VMID %u\n",
					i, pdata->vmids[i], Q6APM_SCM_MAX_VMID);
				return -EINVAL;
			}
		}
		pdata->num_vmids = vmids;
		pdata->use_scm_assign = true;
		dev_dbg(dev, "probe: SCM assign enabled src_vmid=0x%x num_dst=%d\n",
			pdata->src_vmid, pdata->num_vmids);
		for (rc = 0; rc < vmids; rc++)
			dev_dbg(dev, "probe: SCM dst vmid[%d]=0x%x\n", rc,
				pdata->vmids[rc]);
	}

	if (pdata->use_scm_assign && !qcom_scm_is_available())
		return -EPROBE_DEFER;

	if (pdata->use_reserved_mem && pdata->reserved_mem_size < required_mem_size) {
		dev_err(dev, "reserved memory size %zu is smaller than required %zu\n",
			pdata->reserved_mem_size, required_mem_size);
		return -EINVAL;
	}

	dev_dbg(dev,
		"probe done: use_reserved=%d reserved=%pa/%zu use_scm=%d src_vmid=0x%x num_vmids=%d buffer_bytes_max=%zu\n",
		pdata->use_reserved_mem, &pdata->reserved_mem_addr,
		pdata->reserved_mem_size, pdata->use_scm_assign,
		pdata->src_vmid, pdata->num_vmids, pdata->buffer_bytes_max);

	dev_set_drvdata(dev, pdata);

	return devm_snd_soc_register_component(dev, &q6apm_fe_dai_component, NULL, 0);
}

#ifdef CONFIG_OF
static const struct of_device_id q6apm_dai_device_id[] = {
	{ .compatible = "qcom,q6apm-dais" },
	{},
};
MODULE_DEVICE_TABLE(of, q6apm_dai_device_id);
#endif

static struct platform_driver q6apm_dai_platform_driver = {
	.driver = {
		.name = "q6apm-dai",
		.of_match_table = of_match_ptr(q6apm_dai_device_id),
	},
	.probe = q6apm_dai_probe,
};
module_platform_driver(q6apm_dai_platform_driver);

MODULE_DESCRIPTION("Q6APM dai driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("DMA_BUF");
