// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * qaif-platform.c -- ALSA SoC PCM platform driver for the Qualcomm Audio Interface (QAIF)
 */

#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "qaif-reg.h"
#include "qaif.h"

#define DRV_NAME "qaif-platform"

/* period: 5ms @ 48kHz/1ch/S16 min, 20ms @ 48kHz/8ch/S32 max */
#define QAIF_PLATFORM_PERIOD_BYTES_MIN		(48 * 5 * 1 * 2)	/* 480 B */
#define QAIF_PLATFORM_PERIOD_BYTES_MAX		(48 * 20 * 8 * 4)	/* 30720 B */
#define QAIF_PLATFORM_PERIODS_MIN		2
#define QAIF_PLATFORM_PERIODS_MAX		4
/* QAIF_PLATFORM_PERIODS_MAX * QAIF_PLATFORM_PERIOD_BYTES_MAX = 122880 B */
#define QAIF_PLATFORM_BUFFER_BYTES_MAX		\
	(QAIF_PLATFORM_PERIODS_MAX * QAIF_PLATFORM_PERIOD_BYTES_MAX)

/* DMA CFG register bit positions */
#define QAIF_DMACFG_SHRAM_WM_MASK	GENMASK(11, 0)
#define QAIF_DMACFG_BURST4_BIT		BIT(18)
#define QAIF_DMACFG_DYNCLK_BIT		BIT(24)

/* DMA CTL register bit positions */
#define QAIF_DMACTL_ENABLE_BIT		BIT(0)
#define QAIF_ALL_CLIENTS_MASK		(QAIF_BITMASK_AIF_RDDMA_WRDMA | \
					 QAIF_BITMASK_CIF_RDDMA_WRDMA)

static const struct snd_pcm_hardware qaif_platform_hardware = {
	.info			=	SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_MMAP_VALID |
					SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_PAUSE |
					SNDRV_PCM_INFO_RESUME,
	.formats		=	SNDRV_PCM_FMTBIT_S16 |
					SNDRV_PCM_FMTBIT_S24 |
					SNDRV_PCM_FMTBIT_S32,
	.rates			=	SNDRV_PCM_RATE_8000_192000,
	.rate_min		=	8000,
	.rate_max		=	192000,
	.channels_min		=	1,
	.channels_max		=	8,
	.buffer_bytes_max	=	QAIF_PLATFORM_BUFFER_BYTES_MAX,
	.period_bytes_min	=	QAIF_PLATFORM_PERIOD_BYTES_MIN,
	.period_bytes_max	=	QAIF_PLATFORM_PERIOD_BYTES_MAX,
	.periods_min		=	QAIF_PLATFORM_PERIODS_MIN,
	.periods_max		=	QAIF_PLATFORM_PERIODS_MAX,
	.fifo_size		=	0,
};

static int qaif_map_ee_resource(struct qaif_drv_data *drvdata)
{
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	int ret;

	ret = regmap_write(map, QAIF_EE_RDDMA_MAP_REG(v), GENMASK(v->num_rddma - 1, 0));
	if (ret)
		return ret;
	ret = regmap_write(map, QAIF_EE_WRDMA_MAP_REG(v), GENMASK(v->num_wrdma - 1, 0));
	if (ret)
		return ret;
	if (v->num_intf > 0) {
		ret = regmap_write(map, QAIF_EE_INTF_MAP_REG(v), GENMASK(v->num_intf - 1, 0));
		if (ret)
			return ret;
	}
	ret = regmap_write(map, QAIF_EE_CODEC_RDDMA_MAP_REG(v),
			   GENMASK(v->num_codec_rddma - 1, 0));
	if (ret)
		return ret;
	return regmap_write(map, QAIF_EE_CODEC_WRDMA_MAP_REG(v),
			    GENMASK(v->num_codec_wrdma - 1, 0));
}

static int qaif_map_dma_path(struct qaif_drv_data *drvdata)
{
	struct regmap *map = drvdata->audio_qaif_map;
	int ret;

	if (drvdata->variant->qxm_type != QAIF_QXM0) {
		dev_err(regmap_get_device(map),
			"only QXM0 is supported, qxm_type=%d\n",
			drvdata->variant->qxm_type);
		return -EINVAL;
	}

	ret = regmap_write(map, QAIF_RDDMA_MAP_QXM, QAIF_QXM0);
	if (ret)
		return ret;
	ret = regmap_write(map, QAIF_WRDMA_MAP_QXM, QAIF_QXM0);
	if (ret)
		return ret;
	ret = regmap_write(map, QAIF_CDC_RDDMA_MAP_QXM, QAIF_QXM0);
	if (ret)
		return ret;
	return regmap_write(map, QAIF_CDC_WRDMA_MAP_QXM, QAIF_QXM0);
}

static int qaif_config_shram(struct qaif_drv_data *drvdata)
{
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	u32 start_addr, shram_len;
	int ret, i;

	if (v->qxm_type != QAIF_QXM0) {
		dev_err(regmap_get_device(map),
			"only QXM0 is supported, qxm_type=%d\n",
			v->qxm_type);
		return -EINVAL;
	}

	start_addr = v->rddma_shram_start_addr[QAIF_AIF_DMA];
	shram_len = v->rddma_shram_len;
	for (i = 0; i < v->num_rddma; i++) {
		ret = regmap_write(map, QAIF_RDDMA_QXM0_SHRAM_ST_ADDR(i),
				   start_addr + (shram_len * i));
		if (ret)
			return ret;
		ret = regmap_write(map, QAIF_RDDMA_QXM0_SHRAM_LEN(i), shram_len);
		if (ret)
			return ret;
	}

	start_addr = v->wrdma_shram_start_addr[QAIF_AIF_DMA];
	shram_len = v->wrdma_shram_len;
	for (i = 0; i < v->num_wrdma; i++) {
		ret = regmap_write(map, QAIF_WRDMA_QXM0_SHRAM_ST_ADDR(i),
				   start_addr + (shram_len * i));
		if (ret)
			return ret;
		ret = regmap_write(map, QAIF_WRDMA_QXM0_SHRAM_LEN(i), shram_len);
		if (ret)
			return ret;
	}

	start_addr = v->rddma_shram_start_addr[QAIF_CIF_DMA];
	shram_len = v->rddma_shram_len;
	for (i = 0; i < v->num_codec_rddma; i++) {
		ret = regmap_write(map, QAIF_CDC_RDDMA_QXM0_SHRAM_ST_ADDR(i),
				   start_addr + (shram_len * i));
		if (ret)
			return ret;
		ret = regmap_write(map, QAIF_CDC_RDDMA_QXM0_SHRAM_LEN(i), shram_len);
		if (ret)
			return ret;
	}

	start_addr = v->wrdma_shram_start_addr[QAIF_CIF_DMA];
	shram_len = v->wrdma_shram_len;
	for (i = 0; i < v->num_codec_wrdma; i++) {
		ret = regmap_write(map, QAIF_CDC_WRDMA_QXM0_SHRAM_ST_ADDR(i),
				   start_addr + (shram_len * i));
		if (ret)
			return ret;
		ret = regmap_write(map, QAIF_CDC_WRDMA_QXM0_SHRAM_LEN(i), shram_len);
		if (ret)
			return ret;
	}
	return 0;
}

static int qaif_init(struct snd_soc_component *component)
{
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	int ret;

	if (drvdata->qaif_hw_configured)
		return 0;

	ret = qaif_config_shram(drvdata);
	if (ret) {
		dev_err(component->dev, "failed to config shram: %d\n", ret);
		return ret;
	}

	ret = qaif_map_ee_resource(drvdata);
	if (ret) {
		dev_err(component->dev, "failed to map EE resources: %d\n", ret);
		return ret;
	}

	ret = qaif_map_dma_path(drvdata);
	if (ret)
		return ret;

	drvdata->qaif_hw_configured = true;
	return 0;
}

static int qaif_platform_pcmops_open(struct snd_soc_component *component,
				     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	unsigned int dai_id = cpu_dai->driver->id;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	struct qaif_dma_mem_info *dma_mem_info;
	struct qaif_pcm_data *data;
	int ret, stream_dma_idx, dma_reg_idx, dir = substream->stream;

	dma_reg_idx = v->get_dma_idx(dai_id);
	if (dma_reg_idx < 0) {
		dev_err(component->dev, "invalid DMA register index for DAI %u: %d\n",
			dai_id, dma_reg_idx);
		return dma_reg_idx;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	runtime->private_data = data;

	buf->dev.dev = component->dev;
	buf->private_data = NULL;
	buf->dev.type = SNDRV_DMA_TYPE_DEV;

	dma_mem_info = kzalloc(sizeof(*dma_mem_info), GFP_KERNEL);
	if (!dma_mem_info) {
		ret = -ENOMEM;
		goto err_free_data;
	}
	dma_mem_info->alloc_size = qaif_platform_hardware.buffer_bytes_max;
	dma_mem_info->vaddr = dma_alloc_coherent(component->dev,
						 dma_mem_info->alloc_size,
						 &dma_mem_info->dma_addr,
						 GFP_KERNEL);
	if (!dma_mem_info->vaddr) {
		ret = -ENOMEM;
		goto err_free_mem_info;
	}

	mutex_lock(&drvdata->stream_lock);
	if (!v->alloc_stream_dma_idx) {
		ret = -EINVAL;
		goto err_unlock;
	}
	stream_dma_idx = v->alloc_stream_dma_idx(drvdata, dir, dai_id);
	if (stream_dma_idx < 0) {
		ret = stream_dma_idx;
		goto err_unlock;
	}
	mutex_unlock(&drvdata->stream_lock);

	data->stream_dma_idx = stream_dma_idx;
	data->dma_reg_idx = dma_reg_idx;

	buf->bytes = qaif_platform_hardware.buffer_bytes_max;
	buf->addr = dma_mem_info->dma_addr;
	buf->area = (unsigned char *)dma_mem_info->vaddr;

	if (qaif_is_cif_dma_port(dai_id)) {
		WRITE_ONCE(drvdata->cif_substream[stream_dma_idx], substream);
		drvdata->cif_dma_heap[stream_dma_idx] = dma_mem_info;
	} else {
		WRITE_ONCE(drvdata->aif_substream[stream_dma_idx], substream);
		drvdata->aif_dma_heap[stream_dma_idx] = dma_mem_info;
	}

	snd_soc_set_runtime_hwparams(substream, &qaif_platform_hardware);
	runtime->dma_bytes = qaif_platform_hardware.buffer_bytes_max;
	snd_pcm_set_runtime_buffer(substream, buf);

	/*
	 * The DMA buffer/period length registers are programmed in 64-bit
	 * (8-byte) words, so constrain buffer and period sizes to that step
	 * to keep the ALSA and hardware sizes identical.
	 */
	ret = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					 BIT(QAIF_DMA_BYTES_TO_WORDS_SHIFT));
	if (ret >= 0)
		ret = snd_pcm_hw_constraint_step(runtime, 0,
						 SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
						 BIT(QAIF_DMA_BYTES_TO_WORDS_SHIFT));
	if (ret >= 0)
		ret = snd_pcm_hw_constraint_integer(runtime,
						    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(soc_runtime->dev, "setting constraints failed: %d\n", ret);
		if (qaif_is_cif_dma_port(dai_id)) {
			WRITE_ONCE(drvdata->cif_substream[stream_dma_idx], NULL);
			drvdata->cif_dma_heap[stream_dma_idx] = NULL;
		} else {
			WRITE_ONCE(drvdata->aif_substream[stream_dma_idx], NULL);
			drvdata->aif_dma_heap[stream_dma_idx] = NULL;
		}
		mutex_lock(&drvdata->stream_lock);
		if (v->free_stream_dma_idx)
			v->free_stream_dma_idx(drvdata, stream_dma_idx, dai_id);
		mutex_unlock(&drvdata->stream_lock);
		goto err_free_dma;
	}
	return 0;

err_unlock:
	mutex_unlock(&drvdata->stream_lock);
err_free_dma:
	dma_free_coherent(component->dev, dma_mem_info->alloc_size,
			  dma_mem_info->vaddr, dma_mem_info->dma_addr);
err_free_mem_info:
	kfree(dma_mem_info);
err_free_data:
	kfree(data);
	return ret;
}

static int qaif_platform_pcmops_close(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_pcm_data *data = substream->runtime->private_data;
	struct qaif_dma_mem_info *dma_mem_info;
	unsigned int dai_id = cpu_dai->driver->id;

	if (qaif_is_cif_dma_port(dai_id)) {
		WRITE_ONCE(drvdata->cif_substream[data->stream_dma_idx], NULL);
		dma_mem_info = drvdata->cif_dma_heap[data->stream_dma_idx];
		drvdata->cif_dma_heap[data->stream_dma_idx] = NULL;
	} else {
		WRITE_ONCE(drvdata->aif_substream[data->stream_dma_idx], NULL);
		dma_mem_info = drvdata->aif_dma_heap[data->stream_dma_idx];
		drvdata->aif_dma_heap[data->stream_dma_idx] = NULL;
	}

	mutex_lock(&drvdata->stream_lock);
	if (v->free_stream_dma_idx)
		v->free_stream_dma_idx(drvdata, data->stream_dma_idx, dai_id);
	mutex_unlock(&drvdata->stream_lock);

	if (dma_mem_info) {
		dma_free_coherent(component->dev, dma_mem_info->alloc_size,
				  dma_mem_info->vaddr, dma_mem_info->dma_addr);
		kfree(dma_mem_info);
	}

	snd_pcm_set_runtime_buffer(substream, NULL);
	substream->runtime->private_data = NULL;

	kfree(data);
	return 0;
}

static int qaif_platform_pcmops_hw_params(struct snd_soc_component *component,
					  struct snd_pcm_substream *substream,
					  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_pcm_data *data = substream->runtime->private_data;
	unsigned int dai_id = cpu_dai->driver->id;
	int idx = data->dma_reg_idx;
	int ret;

	mutex_lock(&drvdata->stream_lock);
	ret = qaif_init(component);
	mutex_unlock(&drvdata->stream_lock);
	if (ret) {
		dev_err(soc_runtime->dev, "qaif_init failed: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(drvdata->audio_qaif_map,
				 qaif_dmacfg_reg(v, idx, substream->stream, dai_id),
				 QAIF_DMACFG_BURST4_BIT | QAIF_DMACFG_SHRAM_WM_MASK,
				 QAIF_DMACFG_BURST4_BIT | QAIF_DMACTL_WM_5);
	if (ret)
		dev_err(soc_runtime->dev, "error updating DMA CFG: %d\n", ret);
	return ret;
}

static int qaif_platform_pcmops_hw_free(struct snd_soc_component *component,
					struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_pcm_data *data = substream->runtime->private_data;
	unsigned int dai_id = cpu_dai->driver->id;
	int idx = data->dma_reg_idx;
	int ret;

	ret = regmap_update_bits(drvdata->audio_qaif_map,
				 qaif_dmactl_reg(v, idx, substream->stream, dai_id),
				 QAIF_DMACTL_ENABLE_BIT, 0);
	if (ret) {
		dev_err(soc_runtime->dev, "error disabling DMA: %d\n", ret);
		return ret;
	}

	ret = regmap_write(drvdata->audio_qaif_map,
			   qaif_dmacfg_reg(v, idx, substream->stream, dai_id), 0);
	if (ret)
		dev_err(soc_runtime->dev, "error clearing DMA CFG: %d\n", ret);
	return ret;
}

static int qaif_platform_pcmops_prepare(struct snd_soc_component *component,
					struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	struct qaif_pcm_data *data = runtime->private_data;
	unsigned int dai_id = cpu_dai->driver->id;
	int idx = data->dma_reg_idx;
	int ret, dir = substream->stream;

	ret = clk_set_rate(drvdata->aud_dma_clk, QAIF_DMA_CLOCK_FREQ);
	if (ret) {
		dev_err(soc_runtime->dev, "error setting aud_dma_clk rate: %d\n", ret);
		return ret;
	}
	ret = clk_set_rate(drvdata->aud_dma_mem_clk, QAIF_DMA_CLOCK_FREQ);
	if (ret) {
		dev_err(soc_runtime->dev, "error setting aud_dma_mem_clk rate: %d\n", ret);
		return ret;
	}

	ret = regmap_write(map, qaif_sid_map_reg(dir, dai_id), drvdata->smmu_csid_bits);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to SID MAP reg: %d\n", ret);
		return ret;
	}

	ret = regmap_write(map, qaif_dmabase_reg(v, idx, dir, dai_id),
			   lower_32_bits(runtime->dma_addr));
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to dma base reg: %d\n", ret);
		return ret;
	}

	ret = regmap_write(map, qaif_dmabuff_reg(v, idx, dir, dai_id),
			   (snd_pcm_lib_buffer_bytes(substream) >>
			    QAIF_DMA_BYTES_TO_WORDS_SHIFT) - 1);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to dma buf reg: %d\n", ret);
		return ret;
	}

	ret = regmap_write(map, qaif_dmaper_len_reg(v, idx, dir, dai_id),
			   (snd_pcm_lib_period_bytes(substream) >>
			    QAIF_DMA_BYTES_TO_WORDS_SHIFT) - 1);
	if (ret)
		dev_err(soc_runtime->dev, "error writing to dma period reg: %d\n", ret);
	return ret;
}

static snd_pcm_uframes_t qaif_platform_pcmops_pointer(struct snd_soc_component *component,
						      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	struct qaif_pcm_data *data = substream->runtime->private_data;
	unsigned int dai_id = cpu_dai->driver->id;
	unsigned int base_addr, curr_addr;
	int idx = data->dma_reg_idx;
	int ret, dir = substream->stream;

	ret = regmap_read(map, qaif_dmabase_reg(v, idx, dir, dai_id), &base_addr);
	if (ret) {
		dev_err(soc_runtime->dev, "error reading from rdmabase reg: %d\n", ret);
		return SNDRV_PCM_POS_XRUN;
	}

	ret = regmap_read(map, qaif_dmacurr_reg(v, idx, dir, dai_id), &curr_addr);
	if (ret) {
		dev_err(soc_runtime->dev, "error reading from rdmacurr reg: %d\n", ret);
		return SNDRV_PCM_POS_XRUN;
	}

	return bytes_to_frames(substream->runtime,
			       (curr_addr - base_addr) %
			       snd_pcm_lib_buffer_bytes(substream));
}

static int qaif_platform_pcmops_mmap(struct snd_soc_component *component,
				     struct snd_pcm_substream *substream,
				     struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_coherent(component->dev, vma,
				 runtime->dma_area, runtime->dma_addr,
				 runtime->dma_bytes);
}

static int qaif_platform_copy(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream,
			      int channel, unsigned long pos,
			      struct iov_iter *buf, unsigned long bytes)
{
	struct snd_pcm_runtime *rt = substream->runtime;
	size_t buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
	size_t channel_bytes = buffer_bytes / rt->channels;
	unsigned long offset;
	void *dma_buf;
	size_t copied;

	if (channel < 0 || channel >= rt->channels)
		return -EINVAL;

	/* Absolute byte offset into the configured (not the max) buffer. */
	offset = pos + channel * channel_bytes;
	if (offset > buffer_bytes || bytes > buffer_bytes - offset)
		return -EINVAL;

	dma_buf = rt->dma_area + offset;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		copied = copy_from_iter(dma_buf, bytes, buf);
	else
		copied = copy_to_iter(dma_buf, bytes, buf);

	return (copied != bytes) ? -EFAULT : 0;
}

static int qaif_platform_irq_op(struct qaif_drv_data *drvdata, int dir,
				enum qaif_irq_type irq_type, int idx,
				enum qaif_irq_op op)
{
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	unsigned int bit = BIT(idx);
	unsigned int val = op == QAIF_IRQ_ENABLE ? bit : 0;
	u32 per_reg, err_reg, xrun_reg;
	int ret;

	if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
		per_reg = op == QAIF_IRQ_CLEAR ? QAIF_EE_RDDMA_PERIOD_IRQ_CLR_REG(v, irq_type)
					       : QAIF_EE_RDDMA_PERIOD_IRQ_EN_REG(v, irq_type);
		xrun_reg = op == QAIF_IRQ_CLEAR ? QAIF_EE_RDDMA_UF_IRQ_CLR_REG(v, irq_type)
						: QAIF_EE_RDDMA_UF_IRQ_EN_REG(v, irq_type);
		err_reg = op == QAIF_IRQ_CLEAR ? QAIF_EE_RDDMA_ERR_RSP_IRQ_CLR_REG(v, irq_type)
					       : QAIF_EE_RDDMA_ERR_RSP_IRQ_EN_REG(v, irq_type);
	} else {
		per_reg = op == QAIF_IRQ_CLEAR ? QAIF_EE_WRDMA_PERIOD_IRQ_CLR_REG(v, irq_type)
					       : QAIF_EE_WRDMA_PERIOD_IRQ_EN_REG(v, irq_type);
		xrun_reg = op == QAIF_IRQ_CLEAR ? QAIF_EE_WRDMA_OF_IRQ_CLR_REG(v, irq_type)
						: QAIF_EE_WRDMA_OF_IRQ_EN_REG(v, irq_type);
		err_reg = op == QAIF_IRQ_CLEAR ? QAIF_EE_WRDMA_ERR_RSP_IRQ_CLR_REG(v, irq_type)
					       : QAIF_EE_WRDMA_ERR_RSP_IRQ_EN_REG(v, irq_type);
	}

	if (op == QAIF_IRQ_CLEAR) {
		ret = regmap_write(map, per_reg, bit);
		if (ret)
			return ret;
		ret = regmap_write(map, xrun_reg, bit);
		if (ret)
			return ret;
		return regmap_write(map, err_reg, bit);
	}

	ret = regmap_write_bits(map, per_reg, bit, val);
	if (ret)
		return ret;
	ret = regmap_write_bits(map, xrun_reg, bit, val);
	if (ret)
		return ret;
	return regmap_write_bits(map, err_reg, bit, val);
}

static int qaif_platform_pcmops_trigger(struct snd_soc_component *component,
					struct snd_pcm_substream *substream,
					int cmd)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_pcm_data *data = substream->runtime->private_data;
	unsigned int dai_id = cpu_dai->driver->id;
	enum qaif_irq_type irq_type = qaif_is_cif_dma_port(dai_id) ? QAIF_CIF_IRQ : QAIF_AIF_IRQ;
	struct regmap *map = drvdata->audio_qaif_map;
	int idx = data->dma_reg_idx;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = qaif_platform_irq_op(drvdata, substream->stream, irq_type,
					   idx, QAIF_IRQ_CLEAR);
		if (ret) {
			dev_err(soc_runtime->dev, "error clearing irq regs: %d\n", ret);
			return ret;
		}
		ret = regmap_update_bits(map,
					 qaif_dmacfg_reg(v, idx, substream->stream, dai_id),
					 QAIF_DMACFG_DYNCLK_BIT, QAIF_DMACFG_DYNCLK_BIT);
		if (ret) {
			dev_err(soc_runtime->dev, "error enabling dma_dynclk: %d\n", ret);
			return ret;
		}
		ret = regmap_update_bits(map,
					 qaif_dmactl_reg(v, idx, substream->stream, dai_id),
					 QAIF_DMACTL_ENABLE_BIT, QAIF_DMACTL_ENABLE_BIT);
		if (ret) {
			dev_err(soc_runtime->dev, "error enabling DMA: %d\n", ret);
			return ret;
		}
		ret = qaif_platform_irq_op(drvdata, substream->stream, irq_type,
					   idx, QAIF_IRQ_ENABLE);
		if (ret)
			dev_err(soc_runtime->dev, "error enabling irq regs: %d\n", ret);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = regmap_update_bits(map,
					 qaif_dmactl_reg(v, idx, substream->stream, dai_id),
					 QAIF_DMACTL_ENABLE_BIT, 0);
		if (ret) {
			dev_err(soc_runtime->dev, "error disabling DMA: %d\n", ret);
			return ret;
		}
		ret = regmap_update_bits(map,
					 qaif_dmacfg_reg(v, idx, substream->stream, dai_id),
					 QAIF_DMACFG_DYNCLK_BIT, 0);
		if (ret)
			dev_err(soc_runtime->dev, "error disabling dma_dynclk: %d\n", ret);
		ret = qaif_platform_irq_op(drvdata, substream->stream, irq_type,
					   idx, QAIF_IRQ_DISABLE);
		if (ret)
			dev_err(soc_runtime->dev, "error disabling irq regs: %d\n", ret);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int qaif_platform_pcmops_sync_stop(struct snd_soc_component *component,
					  struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	struct qaif_pcm_data *data = substream->runtime->private_data;
	unsigned int dai_id = cpu_dai->driver->id;
	enum qaif_irq_type irq_type = qaif_is_cif_dma_port(dai_id) ? QAIF_CIF_IRQ : QAIF_AIF_IRQ;
	int idx = data->dma_reg_idx;
	int ret;

	synchronize_irq(drvdata->audio_qaif_irq);
	ret = qaif_platform_irq_op(drvdata, substream->stream, irq_type,
				   idx, QAIF_IRQ_CLEAR);
	if (ret)
		dev_err(soc_runtime->dev, "error clearing irq regs: %d\n", ret);
	return ret;
}

static irqreturn_t qaif_process_dma_irq(struct qaif_drv_data *drvdata,
					u32 stat_reg_addr,
					u32 clr_reg_addr,
					enum qaif_irq_type irq_type,
					enum qaif_dma_dir dma_type,
					enum qaif_irq irq,
					struct snd_pcm_substream **substream)
{
	const struct qaif_variant *v = drvdata->variant;
	unsigned int reg = 0;
	int dma_idx, stream_dma_idx, rv, num_dma, stream_offset, array_size;
	u32 mask;
	struct snd_pcm_substream *ss;

	stream_offset = (dma_type == QAIF_DMA_WRDMA) ?
		((irq_type == QAIF_AIF_IRQ) ? v->wrdma_start : v->codec_wrdma_start) : 0;
	num_dma = (dma_type == QAIF_DMA_WRDMA) ?
		((irq_type == QAIF_AIF_IRQ) ? v->num_wrdma : v->num_codec_wrdma) :
		((irq_type == QAIF_AIF_IRQ) ? v->num_rddma : v->num_codec_rddma);
	if (!num_dma)
		return IRQ_NONE;

	array_size = (irq_type == QAIF_AIF_IRQ) ?
		QAIF_MAX_AIF_DMA_IDX : QAIF_MAX_CIF_DMA_IDX;
	if (stream_offset + num_dma > array_size) {
		dev_err(regmap_get_device(drvdata->audio_qaif_map),
			"DMA index range %d+%d exceeds substream table %d\n",
			stream_offset, num_dma, array_size);
		return IRQ_NONE;
	}
	mask = GENMASK(num_dma - 1, 0);

	rv = regmap_read(drvdata->audio_qaif_map, stat_reg_addr, &reg);
	if (rv) {
		dev_err_ratelimited(regmap_get_device(drvdata->audio_qaif_map),
				    "error reading stat reg 0x%x: %d\n", stat_reg_addr, rv);
		return IRQ_NONE;
	}

	if (!(reg & mask)) {
		dev_warn_ratelimited(regmap_get_device(drvdata->audio_qaif_map),
				     "spurious IRQ: stat reg 0x%x status 0x%x mask 0x%x\n",
				     stat_reg_addr, reg, mask);
		return IRQ_NONE;
	}

	if (regmap_write(drvdata->audio_qaif_map, clr_reg_addr, reg & mask))
		return IRQ_NONE;

	for (dma_idx = 0; dma_idx < num_dma; dma_idx++) {
		stream_dma_idx = dma_idx + stream_offset;
		ss = READ_ONCE(substream[stream_dma_idx]);
		if (!(reg & BIT(dma_idx)) || !ss)
			continue;
		switch (irq) {
		case QAIF_IRQ_PERIOD:
			snd_pcm_period_elapsed(ss);
			break;
		case QAIF_IRQ_OVERFLOW:
		case QAIF_IRQ_UNDERFLOW:
			dev_warn_ratelimited(regmap_get_device(drvdata->audio_qaif_map),
					     "QAIF DMA xRun\n");
			snd_pcm_stop_xrun(ss);
			break;
		case QAIF_IRQ_ERROR:
			snd_pcm_stop_xrun(ss);
			dev_err_ratelimited(regmap_get_device(drvdata->audio_qaif_map),
					    "QAIF bus error\n");
			break;
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t qaif_aif_irq_handler(struct qaif_drv_data *drvdata, u32 status)
{
	const struct qaif_variant *v = drvdata->variant;
	struct snd_pcm_substream **ss = drvdata->aif_substream;
	irqreturn_t ret = IRQ_NONE;

	if (status & QAIF_SUMMARY_BITMASK_AIF_PERIOD_RDDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_RDDMA_PERIOD_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
					    QAIF_EE_RDDMA_PERIOD_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
					    QAIF_AIF_IRQ, QAIF_DMA_RDDMA, QAIF_IRQ_PERIOD, ss);
	if (status & QAIF_SUMMARY_BITMASK_AIF_PERIOD_WRDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_WRDMA_PERIOD_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
					    QAIF_EE_WRDMA_PERIOD_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
					    QAIF_AIF_IRQ, QAIF_DMA_WRDMA, QAIF_IRQ_PERIOD, ss);
	if (status & QAIF_SUMMARY_BITMASK_AIF_OVERFLOW_WRDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_WRDMA_OF_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
					    QAIF_EE_WRDMA_OF_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
					    QAIF_AIF_IRQ, QAIF_DMA_WRDMA, QAIF_IRQ_OVERFLOW, ss);
	if (status & QAIF_SUMMARY_BITMASK_AIF_UNDERFLOW_RDDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_RDDMA_UF_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
					    QAIF_EE_RDDMA_UF_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
					    QAIF_AIF_IRQ, QAIF_DMA_RDDMA, QAIF_IRQ_UNDERFLOW, ss);
	if (status & QAIF_SUMMARY_BITMASK_AIF_ERR_RSP_RDDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_RDDMA_ERR_RSP_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
					    QAIF_EE_RDDMA_ERR_RSP_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
					    QAIF_AIF_IRQ, QAIF_DMA_RDDMA, QAIF_IRQ_ERROR, ss);
	if (status & QAIF_SUMMARY_BITMASK_AIF_ERR_RSP_WRDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_WRDMA_ERR_RSP_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
					    QAIF_EE_WRDMA_ERR_RSP_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
					    QAIF_AIF_IRQ, QAIF_DMA_WRDMA, QAIF_IRQ_ERROR, ss);
	return ret;
}

static irqreturn_t qaif_cif_irq_handler(struct qaif_drv_data *drvdata, u32 status)
{
	const struct qaif_variant *v = drvdata->variant;
	struct snd_pcm_substream **ss = drvdata->cif_substream;
	irqreturn_t ret = IRQ_NONE;

	if (status & QAIF_SUMMARY_BITMASK_CIF_PERIOD_RDDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_RDDMA_PERIOD_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
					    QAIF_EE_RDDMA_PERIOD_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
					    QAIF_CIF_IRQ, QAIF_DMA_RDDMA, QAIF_IRQ_PERIOD, ss);
	if (status & QAIF_SUMMARY_BITMASK_CIF_PERIOD_WRDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_WRDMA_PERIOD_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
					    QAIF_EE_WRDMA_PERIOD_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
					    QAIF_CIF_IRQ, QAIF_DMA_WRDMA, QAIF_IRQ_PERIOD, ss);
	if (status & QAIF_SUMMARY_BITMASK_CIF_OVERFLOW_WRDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_WRDMA_OF_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
					    QAIF_EE_WRDMA_OF_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
					    QAIF_CIF_IRQ, QAIF_DMA_WRDMA, QAIF_IRQ_OVERFLOW, ss);
	if (status & QAIF_SUMMARY_BITMASK_CIF_UNDERFLOW_RDDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_RDDMA_UF_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
					    QAIF_EE_RDDMA_UF_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
					    QAIF_CIF_IRQ, QAIF_DMA_RDDMA, QAIF_IRQ_UNDERFLOW, ss);
	if (status & QAIF_SUMMARY_BITMASK_CIF_ERR_RSP_RDDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_RDDMA_ERR_RSP_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
					    QAIF_EE_RDDMA_ERR_RSP_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
					    QAIF_CIF_IRQ, QAIF_DMA_RDDMA, QAIF_IRQ_ERROR, ss);
	if (status & QAIF_SUMMARY_BITMASK_CIF_ERR_RSP_WRDMA)
		ret |= qaif_process_dma_irq(drvdata,
					    QAIF_EE_WRDMA_ERR_RSP_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
					    QAIF_EE_WRDMA_ERR_RSP_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
					    QAIF_CIF_IRQ, QAIF_DMA_WRDMA, QAIF_IRQ_ERROR, ss);
	return ret;
}

static const struct qaif_irq_map qaif_irq_clients[] = {
	{ QAIF_CLIENT_ID_AIF_DMA, QAIF_BITMASK_AIF_RDDMA_WRDMA, qaif_aif_irq_handler },
	{ QAIF_CLIENT_ID_CIF_DMA, QAIF_BITMASK_CIF_RDDMA_WRDMA, qaif_cif_irq_handler },
};

static irqreturn_t asoc_platform_qaif_irq(int irq, void *data)
{
	struct qaif_drv_data *drvdata = data;
	const struct qaif_variant *v = drvdata->variant;
	u32 summary_irq_status;
	int rv, client;
	irqreturn_t ret = IRQ_NONE;

	rv = regmap_read(drvdata->audio_qaif_map, QAIF_SUMMARY_IRQSTAT_REG(v),
			 &summary_irq_status);
	if (rv) {
		dev_err(regmap_get_device(drvdata->audio_qaif_map),
			"error reading from irqstat reg: %d\n", rv);
		return IRQ_NONE;
	}

	if (!(summary_irq_status & QAIF_ALL_CLIENTS_MASK))
		return IRQ_NONE;

	for (client = 0; client < ARRAY_SIZE(qaif_irq_clients); client++) {
		if (summary_irq_status & qaif_irq_clients[client].mask)
			ret |= qaif_irq_clients[client].client_irq_handler(drvdata,
									   summary_irq_status);
	}
	return ret;
}

static const struct snd_soc_component_driver qaif_component_driver = {
	.name		= DRV_NAME,
	.open		= qaif_platform_pcmops_open,
	.close		= qaif_platform_pcmops_close,
	.hw_params	= qaif_platform_pcmops_hw_params,
	.hw_free	= qaif_platform_pcmops_hw_free,
	.prepare	= qaif_platform_pcmops_prepare,
	.trigger	= qaif_platform_pcmops_trigger,
	.sync_stop	= qaif_platform_pcmops_sync_stop,
	.pointer	= qaif_platform_pcmops_pointer,
	.mmap		= qaif_platform_pcmops_mmap,
	.copy		= qaif_platform_copy,
};

int asoc_qcom_qaif_platform_register(struct platform_device *pdev)
{
	struct qaif_drv_data *drvdata = platform_get_drvdata(pdev);
	struct of_phandle_args iommu_spec;
	u32 sid;
	int ret;

	if (!drvdata || !drvdata->variant)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "Invalid drvdata or variant\n");

	ret = of_parse_phandle_with_fixed_args(pdev->dev.of_node,
					       "iommus", 2, 0,
					       &iommu_spec);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to parse iommus property\n");
	sid = iommu_spec.args[0];
	of_node_put(iommu_spec.np);
	drvdata->smmu_csid_bits = sid & QAIF_CSID_MASK;

	/*
	 * The DMA base register only holds the low 32 bits of the buffer
	 * address, so the coherent buffer must come from the low 32-bit
	 * address space.
	 */
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to set 32-bit DMA mask\n");

	drvdata->audio_qaif_irq = platform_get_irq(pdev, 0);
	if (drvdata->audio_qaif_irq < 0)
		return dev_err_probe(&pdev->dev, drvdata->audio_qaif_irq,
				     "Failed to get IRQ\n");

	drvdata->qaif_hw_configured = false;
	mutex_init(&drvdata->stream_lock);

	ret = devm_request_irq(&pdev->dev, drvdata->audio_qaif_irq,
			       asoc_platform_qaif_irq, 0,
			       "qaif-irq-audio-core", drvdata);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "irq request failed\n");

	ret = devm_snd_soc_register_component(&pdev->dev, &qaif_component_driver, NULL, 0);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to register platform component\n");
	return 0;
}
EXPORT_SYMBOL_GPL(asoc_qcom_qaif_platform_register);

MODULE_DESCRIPTION("Qualcomm Audio Interface (QAIF) PCM platform driver");
MODULE_AUTHOR("Harendra Gautam <harendra.gautam@oss.qualcomm.com>");
MODULE_LICENSE("GPL");
