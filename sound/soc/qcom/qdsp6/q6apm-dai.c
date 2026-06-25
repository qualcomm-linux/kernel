// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, Linaro Limited

#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/spinlock.h>
#include <sound/pcm.h>
#include <asm/div64.h>
#include <asm/dma.h>
#include <linux/dma-mapping.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/of_reserved_mem.h>
#include <sound/pcm_params.h>
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
#define Q6APM_SCM_MAX_VMID	31
#define Q6APM_MAX_VMIDS		8
#define Q6APM_MAX_CARVEOUTS	8
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
	unsigned int pcm_size;
	unsigned int pcm_count;
	unsigned int periods;
	uint64_t bytes_sent;
	uint64_t bytes_received;
	uint64_t copied_total;
	uint16_t bits_per_sample;
	snd_pcm_uframes_t queue_ptr;
	bool next_track;
	enum stream_state state;
	struct q6apm_graph *graph;
	spinlock_t lock;
	bool notify_on_drain;
};

struct q6apm_scm_region {
	phys_addr_t dma_addr;
	unsigned int size;
	u64 src_perms;
	bool assigned;
};

struct q6apm_dai_data {
	long long sid;
	int num_vmids;
	u32 vmids[Q6APM_MAX_VMIDS];
	bool use_scm_assign;
	struct q6apm_scm_region scm_regions[SNDRV_PCM_STREAM_LAST + 1];
	/*
	 * carveout regions from memory-region DT property
	 * (index 0: control path, index 1+: data path)
	 */
	struct q6apm_scm_region carveout_regions[Q6APM_MAX_CARVEOUTS];
	int num_carveouts;
	/* true when memory-region DT property is present and DMA pool attached */
	bool has_reserved_mem;
	/* size of the data-path reserved region, capped at BUFFER_BYTES_MAX */
	size_t reserved_buf_size;
};

static int q6apm_dai_assign_memory(struct snd_pcm_substream *substream,
				   struct q6apm_dai_data *pdata)
{
	struct q6apm_scm_region *scm_region = &pdata->scm_regions[substream->stream];
	struct qcom_scm_vmperm *dst_vmids;
	int dst_count = 0;
	int ret;
	int i;

	if (!pdata->use_scm_assign || pdata->num_vmids <= 0 || scm_region->assigned)
		return 0;

	if (!substream->dma_buffer.addr)
		return -ENOMEM;

	dst_vmids = kcalloc(pdata->num_vmids + 1, sizeof(*dst_vmids), GFP_KERNEL);
	if (!dst_vmids)
		return -ENOMEM;

	/* Always keep HLOS RW so CPU can continue buffer access. */
	dst_vmids[dst_count].vmid = QCOM_SCM_VMID_HLOS;
	dst_vmids[dst_count].perm = QCOM_SCM_PERM_RW;
	dst_count++;

	for (i = 0; i < pdata->num_vmids; i++) {
		/*
		 * Probe-time validation rejects HLOS in qcom,vmid, so this is
		 * only a defensive check for future non-DT vmids[] population.
		 */
		if (WARN_ON_ONCE(pdata->vmids[i] == QCOM_SCM_VMID_HLOS))
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

	scm_region->dma_addr = substream->dma_buffer.addr;
	scm_region->size = ALIGN(pdata->reserved_buf_size, PAGE_SIZE);
	scm_region->src_perms = BIT_ULL(QCOM_SCM_VMID_HLOS);

	ret = qcom_scm_assign_mem(scm_region->dma_addr, scm_region->size,
				  &scm_region->src_perms, dst_vmids, dst_count);
	kfree(dst_vmids);
	if (ret)
		return ret;

	scm_region->assigned = true;
	return 0;
}

static int q6apm_dai_unassign_memory(struct snd_soc_component *component,
				     struct snd_pcm_substream *substream,
				     struct q6apm_dai_data *pdata)
{
	struct q6apm_scm_region *scm_region = &pdata->scm_regions[substream->stream];
	struct qcom_scm_vmperm hlos = {
		.vmid = QCOM_SCM_VMID_HLOS,
		.perm = QCOM_SCM_PERM_RW,
	};
	struct device *dev = component->dev;
	int ret;

	if (!pdata->use_scm_assign || !scm_region->assigned)
		return 0;

	ret = qcom_scm_assign_mem(scm_region->dma_addr, scm_region->size,
				  &scm_region->src_perms, &hlos, 1);
	if (!ret) {
		scm_region->assigned = false;
		scm_region->src_perms = BIT_ULL(QCOM_SCM_VMID_HLOS);
	} else {
		dev_err(dev, "Failed to unassign DMA buffer %pa from VMIDs: %d\n",
			&scm_region->dma_addr, ret);
	}

	return ret;
}

static int q6apm_dai_assign_one_region(struct q6apm_scm_region *region,
				       struct q6apm_dai_data *pdata)
{
	struct qcom_scm_vmperm *dst_vmids;
	int dst_count = 0;
	int ret, i;

	if (region->assigned)
		return 0;

	dst_vmids = kcalloc(pdata->num_vmids + 1, sizeof(*dst_vmids),
			    GFP_KERNEL);
	if (!dst_vmids)
		return -ENOMEM;

	/* Always keep HLOS RW so CPU can continue carveout access. */
	dst_vmids[dst_count].vmid = QCOM_SCM_VMID_HLOS;
	dst_vmids[dst_count].perm = QCOM_SCM_PERM_RW;
	dst_count++;

	for (i = 0; i < pdata->num_vmids; i++) {
		if (WARN_ON_ONCE(pdata->vmids[i] == QCOM_SCM_VMID_HLOS))
			continue;
		dst_vmids[dst_count].vmid = pdata->vmids[i];
		dst_vmids[dst_count].perm = QCOM_SCM_PERM_RW;
		dst_count++;
	}

	if (dst_count == 1) {
		/* Nothing to assign beyond HLOS access. */
		kfree(dst_vmids);
		return 0;
	}

	ret = qcom_scm_assign_mem(region->dma_addr, region->size,
				  &region->src_perms, dst_vmids, dst_count);
	kfree(dst_vmids);
	if (!ret)
		region->assigned = true;
	return ret;
}

static int q6apm_dai_assign_carveout(struct q6apm_dai_data *pdata)
{
	int i, ret;

	if (!pdata->use_scm_assign || !pdata->num_carveouts)
		return 0;

	for (i = 0; i < pdata->num_carveouts; i++) {
		ret = q6apm_dai_assign_one_region(&pdata->carveout_regions[i],
						  pdata);
		if (ret)
			return ret;
	}
	return 0;
}

static void q6apm_dai_unassign_one_region(struct snd_soc_component *component,
					  struct q6apm_scm_region *region)
{
	struct device *dev = component->dev;
	struct qcom_scm_vmperm hlos = {
		.vmid = QCOM_SCM_VMID_HLOS,
		.perm = QCOM_SCM_PERM_RW,
	};
	int ret;

	if (!region->assigned)
		return;

	ret = qcom_scm_assign_mem(region->dma_addr, region->size,
				  &region->src_perms, &hlos, 1);
	if (!ret) {
		region->assigned = false;
		region->src_perms = BIT_ULL(QCOM_SCM_VMID_HLOS);
	} else {
		dev_err(dev,
			"Failed to unassign carveout %pa from VMIDs: %d\n",
			&region->dma_addr, ret);
	}
}

static void q6apm_dai_unassign_carveout(struct snd_soc_component *component,
					struct q6apm_dai_data *pdata)
{
	int i;

	if (!pdata->use_scm_assign || !pdata->num_carveouts)
		return;

	for (i = 0; i < pdata->num_carveouts; i++)
		q6apm_dai_unassign_one_region(component,
					      &pdata->carveout_regions[i]);
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
	unsigned long flags;
	uint32_t wflags = 0;
	uint64_t avail;
	uint32_t bytes_written, bytes_to_write;
	bool is_last_buffer = false;

	switch (opcode) {
	case APM_CLIENT_EVENT_CMD_EOS_DONE:
		spin_lock_irqsave(&prtd->lock, flags);
		if (prtd->notify_on_drain) {
			snd_compr_drain_notify(prtd->cstream);
			prtd->notify_on_drain = false;
		} else {
			prtd->state = Q6APM_STREAM_STOPPED;
		}
		spin_unlock_irqrestore(&prtd->lock, flags);
		break;
	case APM_CLIENT_EVENT_DATA_WRITE_DONE:
		spin_lock_irqsave(&prtd->lock, flags);
		bytes_written = token >> APM_WRITE_TOKEN_LEN_SHIFT;
		prtd->copied_total += bytes_written;
		snd_compr_fragment_elapsed(substream);

		if (prtd->state != Q6APM_STREAM_RUNNING) {
			spin_unlock_irqrestore(&prtd->lock, flags);
			break;
		}

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

		spin_unlock_irqrestore(&prtd->lock, flags);
		break;
	default:
		break;
	}
}

static int q6apm_dai_prepare(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct q6apm_dai_rtd *prtd = runtime->private_data;
	struct audioreach_module_config cfg;
	struct device *dev = component->dev;
	struct q6apm_dai_data *pdata;
	int ret;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata)
		return -EINVAL;

	if (!prtd || !prtd->graph) {
		dev_err(dev, "%s: private data null or audio client freed\n", __func__);
		return -EINVAL;
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
	/* rate and channels are sent to audio driver */
	ret = q6apm_graph_media_format_shmem(prtd->graph, &cfg);
	if (ret < 0) {
		dev_err(dev, "%s: q6apm_open_write failed\n", __func__);
		return ret;
	}

	ret = q6apm_graph_media_format_pcm(prtd->graph, &cfg);
	if (ret < 0)
		dev_err(dev, "%s: CMD Format block failed\n", __func__);

	ret = q6apm_alloc_fragments(prtd->graph, substream->stream, prtd->phys,
				    (prtd->pcm_size / prtd->periods), prtd->periods);
	if (ret < 0) {
		dev_err(dev, "Audio Start: Buffer Allocation failed rc = %d\n",	ret);
		return -ENOMEM;
	}

	ret = q6apm_graph_prepare(prtd->graph);
	if (ret) {
		dev_err(dev, "Failed to prepare Graph %d\n", ret);
		return ret;
	}

	ret = q6apm_graph_start(prtd->graph);
	if (ret) {
		dev_err(dev, "Failed to Start Graph %d\n", ret);
		return ret;
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		int i;
		/* Queue the buffers for Capture ONLY after graph is started */
		for (i = 0; i < runtime->periods; i++)
			q6apm_read(prtd->graph);

	}

	/* Now that graph as been prepared and started update the internal state accordingly */
	prtd->state = Q6APM_STREAM_RUNNING;

	return 0;
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
				dev_err(component->dev, "Error queuing playback buffer %d\n", ret);
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
		dev_err(dev, "Drv data not found ..\n");
		return -EINVAL;
	}

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&prtd->lock);
	prtd->substream = substream;
	prtd->graph = q6apm_graph_open(dev, event_handler, prtd, graph_id);
	if (IS_ERR(prtd->graph)) {
		dev_err(dev, "%s: Could not allocate memory\n", __func__);
		ret = PTR_ERR(prtd->graph);
		goto err;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = q6apm_dai_hardware_playback;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		runtime->hw = q6apm_dai_hardware_capture;

	/* Ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(dev, "snd_pcm_hw_constraint_integer failed\n");
		goto err;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
						   BUFFER_BYTES_MIN,
						   pdata->has_reserved_mem ?
						   pdata->reserved_buf_size : BUFFER_BYTES_MAX);
		if (ret < 0) {
			dev_err(dev, "constraint for buffer bytes min max ret = %d\n", ret);
			goto err;
		}
	}

	/* setup 10ms latency to accommodate DSP restrictions */
	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 480);
	if (ret < 0) {
		dev_err(dev, "constraint for period bytes step ret = %d\n", ret);
		goto err;
	}

	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 480);
	if (ret < 0) {
		dev_err(dev, "constraint for buffer bytes step ret = %d\n", ret);
		goto err;
	}

	runtime->private_data = prtd;
	runtime->dma_bytes = pdata->has_reserved_mem ?
			pdata->reserved_buf_size : BUFFER_BYTES_MAX;
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
	struct q6apm_dai_rtd *prtd = runtime->private_data;

	if (prtd->state) { /* only stop graph that is started */
		q6apm_graph_stop(prtd->graph);
		q6apm_free_fragments(prtd->graph, substream->stream);
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
	struct q6apm_dai_rtd *prtd = runtime->private_data;

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

	return 0;
}

static void q6apm_dai_memory_unmap(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream);

static int q6apm_dai_memory_map(struct snd_soc_component *component,
				struct snd_pcm_substream *substream, int graph_id)
{
	struct q6apm_dai_data *pdata;
	struct device *dev = component->dev;
	phys_addr_t phys;
	int ret;

	pdata = snd_soc_component_get_drvdata(component);
	if (!pdata) {
		dev_err(component->dev, "Drv data not found ..\n");
		return -EINVAL;
	}

	if (pdata->sid < 0)
		phys = substream->dma_buffer.addr;
	else
		phys = substream->dma_buffer.addr | (pdata->sid << 32);

	ret = q6apm_map_memory_fixed_region(dev, graph_id, phys, pdata->reserved_buf_size);
	if (ret < 0)
		dev_err(dev, "Audio Start: Buffer Allocation failed rc = %d\n", ret);

	return ret;
}

static int q6apm_dai_pcm_new(struct snd_soc_component *component, struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct q6apm_dai_data *pdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm *pcm = rtd->pcm;
	int size = BUFFER_BYTES_MAX;
	int graph_id, ret;
	struct snd_pcm_substream *substream;

	if (!pdata)
		return -EINVAL;

	graph_id = cpu_dai->driver->id;

	/*
	 * When a reserved DMA pool is attached (memory-region in DT), allocate
	 * PCM buffers from it so the DSP accesses the carveout address directly.
	 * Fall back to the standard fixed system-RAM buffer on other platforms.
	 */
	if (pdata->has_reserved_mem)
		ret = snd_pcm_set_fixed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
						   component->dev,
						   pdata->reserved_buf_size);
	else
		ret = snd_pcm_set_fixed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
						   component->dev, size);
	if (ret)
		return ret;

	/* Note: DSP backend dais are uni-directional ONLY(either playback or capture) */
	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
		ret = q6apm_dai_memory_map(component, substream, graph_id);
		if (ret)
			return ret;
		if (pdata->use_scm_assign) {
			ret = q6apm_dai_assign_memory(substream, pdata);
			if (ret) {
				q6apm_dai_memory_unmap(component, substream);
				return ret;
			}
		}
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
		ret = q6apm_dai_memory_map(component, substream, graph_id);
		if (ret)
			return ret;
		if (pdata->use_scm_assign) {
			ret = q6apm_dai_assign_memory(substream, pdata);
			if (ret) {
				q6apm_dai_memory_unmap(component, substream);
				return ret;
			}
		}
	}

	if (pdata->use_scm_assign && pdata->num_carveouts) {
		ret = q6apm_dai_assign_carveout(pdata);
		if (ret)
			return ret;
	}

	return 0;
}

static void q6apm_dai_memory_unmap(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_prtd;
	struct snd_soc_dai *cpu_dai;
	int graph_id;

	soc_prtd = snd_soc_substream_to_rtd(substream);
	if (!soc_prtd)
		return;

	cpu_dai = snd_soc_rtd_to_cpu(soc_prtd, 0);
	if (!cpu_dai)
		return;

	graph_id = cpu_dai->driver->id;
	q6apm_unmap_memory_fixed_region(component->dev, graph_id);
}

static void q6apm_dai_pcm_free(struct snd_soc_component *component, struct snd_pcm *pcm)
{
	struct q6apm_dai_data *pdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_substream *substream;

	if (!pdata)
		return;

	if (pdata->use_scm_assign && pdata->num_carveouts)
		q6apm_dai_unassign_carveout(component, pdata);

	substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	if (substream) {
		if (pdata->use_scm_assign)
			q6apm_dai_unassign_memory(component, substream, pdata);
		q6apm_dai_memory_unmap(component, substream);
	}

	substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	if (substream) {
		if (pdata->use_scm_assign)
			q6apm_dai_unassign_memory(component, substream, pdata);
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

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	prtd->cstream = stream;
	prtd->graph = q6apm_graph_open(dev, event_handler_compr, prtd, graph_id);
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
	unsigned long flags;
	uint64_t temp_copied_total;

	spin_lock_irqsave(&prtd->lock, flags);
	tstamp->copied_total = prtd->copied_total;
	temp_copied_total = tstamp->copied_total;
	tstamp->byte_offset = do_div(temp_copied_total, prtd->pcm_size);
	spin_unlock_irqrestore(&prtd->lock, flags);

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
	unsigned long flags;

	spin_lock_irqsave(&prtd->lock, flags);
	prtd->bytes_received += count;
	spin_unlock_irqrestore(&prtd->lock, flags);

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
	unsigned long flags;
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

	spin_lock_irqsave(&prtd->lock, flags);
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

	spin_unlock_irqrestore(&prtd->lock, flags);

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
	.pcm_construct	= q6apm_dai_pcm_new,
	.pcm_destruct	= q6apm_dai_pcm_free,
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
	int vmids;
	int rc;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	rc = of_parse_phandle_with_fixed_args(node, "iommus", 1, 0, &args);
	if (rc < 0)
		pdata->sid = -1;
	else
		pdata->sid = args.args[0] & SID_MASK_DEFAULT;

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
	}

	/*
	 * Attach the data-path reserved memory region (index 1 in
	 * memory-region, e.g. audio_mdsp_carveout_mem on shikra) as a DMA
	 * pool so that snd_pcm_set_managed_buffer_all() allocates PCM
	 * buffers from the carveout instead of system RAM. The size is read
	 * from the DT node and capped at BUFFER_BYTES_MAX.
	 * Index 0 is the control-path carveout (SCM-assigned separately).
	 * Platforms without memory-region are completely unaffected.
	 */
	if (of_property_present(node, "memory-region")) {
		struct device_node *rmem_node;
		struct reserved_mem *rmem = NULL;

		/* index 1 = data path (PCM DMA buffer pool) */
		rmem_node = of_parse_phandle(node, "memory-region", 1);
		if (rmem_node) {
			rmem = of_reserved_mem_lookup(rmem_node);
			of_node_put(rmem_node);
		}

		if (rmem) {
			rc = of_reserved_mem_device_init_by_idx(dev, node, 1);
			if (rc) {
				dev_err(dev,
					"failed to attach reserved memory pool: %d\n",
					rc);
				return rc;
			}
			rc = devm_add_action_or_reset(dev,
						      (void (*)(void *))
						      of_reserved_mem_device_release,
						      dev);
			if (rc)
				return rc;
			pdata->reserved_buf_size = min_t(size_t, rmem->size / 4,
							 BUFFER_BYTES_MAX);
			pdata->has_reserved_mem = true;
		} else {
			dev_warn(dev,
				 "memory-region index 1 not found, using system RAM\n");
		}
	}

	if (pdata->use_scm_assign) {
		struct device_node *mem_node;
		int idx = 0;

		while ((mem_node = of_parse_phandle(node, "memory-region",
						    idx++))) {
			struct reserved_mem *rmem;
			struct q6apm_scm_region *r;

			/*
			 * Only index 0 (control-path carveout) is
			 * SCM-assigned via carveout_regions[]. Index 1+
			 * are data-path DMA pools handled per-substream
			 * by q6apm_dai_assign_memory() in pcm_new().
			 * Including them here causes a double-assignment
			 * of the same physical region which TZ rejects
			 * with -EINVAL.
			 */
			if (idx > 1) {
				of_node_put(mem_node);
				break;
			}

			if (pdata->num_carveouts >= Q6APM_MAX_CARVEOUTS) {
				dev_warn(dev,
					 "memory-region: too many entries, ignoring rest\n");
				of_node_put(mem_node);
				break;
			}

			rmem = of_reserved_mem_lookup(mem_node);
			of_node_put(mem_node);
			if (!rmem)
				continue;

			r = &pdata->carveout_regions[pdata->num_carveouts++];
			r->dma_addr  = rmem->base;
			r->size      = ALIGN(rmem->size, PAGE_SIZE);
			r->src_perms = BIT_ULL(QCOM_SCM_VMID_HLOS);
		}
	}

	if (pdata->use_scm_assign && !qcom_scm_is_available())
		return -EPROBE_DEFER;

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
