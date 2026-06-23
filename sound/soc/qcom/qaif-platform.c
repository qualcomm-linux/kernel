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
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "qaif-reg.h"
#include "qaif.h"

#define DRV_NAME "qaif-platform"

/* 20 ms period at 48 kHz S16 stereo = 3840 bytes */
#define QAIF_PLATFORM_BUFFER_MIN_SIZE		(960 * 2 * 2)
#define QAIF_PLATFORM_PERIOD_BYTES_MIN		(960 * 2 * 2)
#define QAIF_PLATFORM_BUFFER_SIZE			(4 * QAIF_PLATFORM_BUFFER_MIN_SIZE)
#define QAIF_PLATFORM_PERIODS_MIN			2
#define QAIF_PLATFORM_PERIODS_MAX			4

static const struct snd_pcm_hardware qaif_platform_aif_hardware = {
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
	.buffer_bytes_max	=	QAIF_PLATFORM_BUFFER_SIZE,
	.period_bytes_min	=	QAIF_PLATFORM_PERIOD_BYTES_MIN,
	.period_bytes_max	=	QAIF_PLATFORM_BUFFER_SIZE / QAIF_PLATFORM_PERIODS_MIN,
	.periods_min		=	QAIF_PLATFORM_PERIODS_MIN,
	.periods_max		=	QAIF_PLATFORM_PERIODS_MAX,
	.fifo_size		=	0,
};

static const struct snd_pcm_hardware qaif_platform_cif_hardware = {
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
	.buffer_bytes_max	=	QAIF_PLATFORM_BUFFER_SIZE,
	.period_bytes_min	=	QAIF_PLATFORM_PERIOD_BYTES_MIN,
	.period_bytes_max	=	QAIF_PLATFORM_BUFFER_SIZE / QAIF_PLATFORM_PERIODS_MIN,
	.periods_min		=	QAIF_PLATFORM_PERIODS_MIN,
	.periods_max		=	QAIF_PLATFORM_PERIODS_MAX,
	.fifo_size		=	0,
};

static struct qaif_dma_mem_info *qaif_mem_alloc_attach(struct snd_soc_component *component,
						       size_t alloc_size)
{
	struct device *dev = component->dev;
	struct qaif_dma_mem_info *dma_mem_info;

	dma_mem_info = kzalloc(sizeof(*dma_mem_info), GFP_KERNEL);
	if (!dma_mem_info)
		return NULL;

	dma_mem_info->alloc_size = alloc_size;

	dma_mem_info->vaddr = dma_alloc_coherent(dev, alloc_size,
						 &dma_mem_info->dma_addr,
						 GFP_KERNEL);
	if (!dma_mem_info->vaddr) {
		dev_err(dev, "dma_alloc_coherent failed for %zu bytes\n", alloc_size);
		kfree(dma_mem_info);
		return NULL;
	}

	dev_dbg(dev, "%s: dma_addr=%pad vaddr=%p\n", __func__,
		&dma_mem_info->dma_addr,
		dma_mem_info->vaddr);
	return dma_mem_info;
}

static void qaif_mem_dealloc_detach(struct device *dev,
				    struct qaif_dma_mem_info *dma_info)
{
	if (!dma_info)
		return;

	if (dma_info->vaddr)
		dma_free_coherent(dev, dma_info->alloc_size,
				  dma_info->vaddr, dma_info->dma_addr);

	kfree(dma_info);
}

static struct qaif_dmactl *qaif_get_dmactl_handle(const struct snd_pcm_substream *substream,
						  struct snd_soc_component *component)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	struct qaif_dmactl *dmactl = NULL;

	switch (cpu_dai->driver->id) {
	case QAIF_MI2S_TDM_AIF0 ... QAIF_MI2S_TDM_AIF12:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			dmactl = drvdata->aif_rd_dmactl;
		else
			dmactl = drvdata->aif_wr_dmactl;
		break;
	case QAIF_CDC_DMA_RX0 ... QAIF_CDC_DMA_RX9:
		dmactl = drvdata->cif_rd_dmactl;
		break;
	case QAIF_CDC_DMA_TX0 ... QAIF_CDC_DMA_TX9:
	case QAIF_CDC_DMA_VA_TX0 ... QAIF_CDC_DMA_VA_TX9:
		dmactl = drvdata->cif_wr_dmactl;
		break;
	}

	return dmactl;
}

static int qaif_map_ee_resource(struct qaif_drv_data *drvdata)
{
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	int ret = 0;
	u32 mask;

	mask = GENMASK(v->num_rddma - 1, 0);
	ret |= regmap_write(map, QAIF_EE_RDDMA_MAP_REG(v), mask);

	mask = GENMASK(v->num_wrdma - 1, 0);
	ret |= regmap_write(map, QAIF_EE_WRDMA_MAP_REG(v), mask);

	if (v->num_intf > 0) {
		mask = GENMASK(v->num_intf - 1, 0);
		ret |= regmap_write(map, QAIF_EE_INTF_MAP_REG(v), mask);
	}

	mask = GENMASK(v->num_codec_rddma - 1, 0);
	ret |= regmap_write(map, QAIF_EE_CODEC_RDDMA_MAP_REG(v), mask);

	mask = GENMASK(v->num_codec_wrdma - 1, 0);
	ret |= regmap_write(map, QAIF_EE_CODEC_WRDMA_MAP_REG(v), mask);

	if (ret)
		return ret;
	return 0;
}

static int qaif_map_dma_path(struct qaif_drv_data *drvdata)
{
	struct regmap *map = drvdata->audio_qaif_map;
	const struct qaif_variant *v = drvdata->variant;
	int ret = 0;
	int qxm_sel = v->qxm_type;

	if (qxm_sel != QXM0) {
		dev_err(regmap_get_device(map),
			"%s: only QXM0 is supported, qxm_type=%d\n",
			__func__, qxm_sel);
		return -EINVAL;
	}

	ret |= regmap_write(map, QAIF_RDDMA_MAP_QXM, qxm_sel);
	ret |= regmap_write(map, QAIF_WRDMA_MAP_QXM, qxm_sel);
	ret |= regmap_write(map, QAIF_CODEC_RDDMA_MAP_QXM, qxm_sel);
	ret |= regmap_write(map, QAIF_CODEC_WRDMA_MAP_QXM, qxm_sel);

	if (ret)
		return ret;

	return 0;
}

static int qaif_config_shram(struct qaif_drv_data *drvdata)
{
	const struct qaif_variant *v = drvdata->variant;
	u32 start_addr, shram_len;
	int ret = 0, i = 0;
	struct regmap *map = drvdata->audio_qaif_map;

	if (v->qxm_type != QXM0) {
		dev_err(regmap_get_device(map),
			"%s: only QXM0 is supported, qxm_type=%d\n",
			__func__, v->qxm_type);
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
		ret = regmap_write(map, QAIF_CODEC_RDDMA_QXM0_SHRAM_ST_ADDR(i),
				   start_addr + (shram_len * i));
		if (ret)
			return ret;
		ret = regmap_write(map, QAIF_CODEC_RDDMA_QXM0_SHRAM_LEN(i), shram_len);
		if (ret)
			return ret;
	}
	start_addr = v->wrdma_shram_start_addr[QAIF_CIF_DMA];
	shram_len = v->wrdma_shram_len;
	for (i = 0; i < v->num_codec_wrdma; i++) {
		ret = regmap_write(map, QAIF_CODEC_WRDMA_QXM0_SHRAM_ST_ADDR(i),
				   start_addr + (shram_len * i));
		if (ret)
			return ret;
		ret = regmap_write(map, QAIF_CODEC_WRDMA_QXM0_SHRAM_LEN(i), shram_len);

		if (ret)
			return ret;
	}
	return 0;
}

static int qaif_init(struct snd_soc_component *component)
{
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	int ret = 0;

	if (drvdata->qaif_init_ref_cnt) {
		dev_dbg(component->dev,
			"%s: QAIF init is done already: ref cnt: %d\n",
			__func__, drvdata->qaif_init_ref_cnt);
		return 0;
	}

	ret = qaif_config_shram(drvdata);
	if (ret) {
		dev_err(component->dev, "QAIF: Failed to config shram: %d\n", ret);
		return ret;
	}

	ret = qaif_map_ee_resource(drvdata);
	if (ret) {
		dev_err(component->dev, "QAIF: Failed to map EE resources: %d\n", ret);
		return ret;
	}

	ret = qaif_map_dma_path(drvdata);
	if (ret) {
		dev_err(component->dev, "QAIF: Failed to map DMA path: %d\n", ret);
		return ret;
	}
	dev_dbg(component->dev,
		"%s: QAIF init is done ref cnt: %d\n",
		__func__, drvdata->qaif_init_ref_cnt);
	return 0;
}

static int qaif_platform_pcmops_open(struct snd_soc_component *component,
				     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct snd_dma_buffer *buf;
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	int ret, stream_dma_idx, dir = substream->stream;
	struct qaif_pcm_data *data;
	struct qaif_dmactl *dmactl;
	struct qaif_dma_mem_info *dma_mem_info;
	struct regmap *map;
	unsigned int dai_id = cpu_dai->driver->id;

	if (v->alloc_stream_dma_idx)
		stream_dma_idx = v->alloc_stream_dma_idx(drvdata, dir, dai_id);
	else
		return -EINVAL;

	if (stream_dma_idx < 0)
		return stream_dma_idx;
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		if (v->free_stream_dma_idx)
			v->free_stream_dma_idx(drvdata, stream_dma_idx, dai_id);
		return -ENOMEM;
	}

	data->stream_dma_idx = stream_dma_idx;

	runtime->private_data = data;
	map = drvdata->audio_qaif_map;
	dmactl = qaif_get_dmactl_handle(substream, component);
	if (!dmactl) {
		kfree(data);
		if (v->free_stream_dma_idx)
			v->free_stream_dma_idx(drvdata, stream_dma_idx, dai_id);
		return -EINVAL;
	}
	buf = &substream->dma_buffer;
	buf->dev.dev = component->dev;
	buf->private_data = NULL;
	buf->dev.type = SNDRV_DMA_TYPE_CONTINUOUS;

	dma_mem_info = qaif_mem_alloc_attach(component,
					     qaif_platform_aif_hardware.buffer_bytes_max);
	if (!dma_mem_info) {
		kfree(data);
		if (v->free_stream_dma_idx)
			v->free_stream_dma_idx(drvdata, stream_dma_idx, dai_id);
		return -ENOMEM;
	}

	ret = clk_prepare_enable(drvdata->aud_dma_clk);
	if (ret) {
		dev_err(soc_runtime->dev, "failed to enable aud_dma_clk: %d\n", ret);
		qaif_mem_dealloc_detach(component->dev, dma_mem_info);
		kfree(data);
		if (v->free_stream_dma_idx)
			v->free_stream_dma_idx(drvdata, stream_dma_idx, dai_id);
		return ret;
	}
	ret = clk_prepare_enable(drvdata->aud_dma_mem_clk);
	if (ret) {
		dev_err(soc_runtime->dev, "failed to enable aud_dma_mem_clk: %d\n", ret);
		clk_disable_unprepare(drvdata->aud_dma_clk);
		qaif_mem_dealloc_detach(component->dev, dma_mem_info);
		kfree(data);
		if (v->free_stream_dma_idx)
			v->free_stream_dma_idx(drvdata, stream_dma_idx, dai_id);
		return ret;
	}

	ret = qaif_init(component);
	if (ret) {
		dev_err(soc_runtime->dev, "qaif_init failed: %d\n", ret);
		clk_disable_unprepare(drvdata->aud_dma_mem_clk);
		clk_disable_unprepare(drvdata->aud_dma_clk);
		qaif_mem_dealloc_detach(component->dev, dma_mem_info);
		kfree(data);
		return -EINVAL;
	}
	drvdata->qaif_init_ref_cnt++;

	switch (dai_id) {
	case QAIF_MI2S_TDM_AIF0 ... QAIF_MI2S_TDM_AIF12:
		drvdata->aif_substream[stream_dma_idx] = substream;
		drvdata->aif_dma_heap[stream_dma_idx] = dma_mem_info;
		buf->bytes = qaif_platform_aif_hardware.buffer_bytes_max;
		buf->addr = drvdata->aif_dma_heap[stream_dma_idx]->dma_addr;
		buf->area = (unsigned char *)drvdata->aif_dma_heap[stream_dma_idx]->vaddr;

		snd_soc_set_runtime_hwparams(substream, &qaif_platform_aif_hardware);
		runtime->dma_bytes = qaif_platform_aif_hardware.buffer_bytes_max;
		break;
	case QAIF_CDC_DMA_RX0 ... QAIF_CDC_DMA_RX9:
	case QAIF_CDC_DMA_TX0 ... QAIF_CDC_DMA_TX9:
	case QAIF_CDC_DMA_VA_TX0 ... QAIF_CDC_DMA_VA_TX9:
		drvdata->cif_substream[stream_dma_idx] = substream;
		drvdata->cif_dma_heap[stream_dma_idx] = dma_mem_info;
		buf->bytes = qaif_platform_cif_hardware.buffer_bytes_max;
		buf->addr = drvdata->cif_dma_heap[stream_dma_idx]->dma_addr;
		buf->area = (unsigned char *)drvdata->cif_dma_heap[stream_dma_idx]->vaddr;

		snd_soc_set_runtime_hwparams(substream, &qaif_platform_cif_hardware);
		runtime->dma_bytes = qaif_platform_cif_hardware.buffer_bytes_max;
		break;
	default:
		break;
	}

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(soc_runtime->dev, "setting constraints failed: %d\n", ret);
		if (is_cif_dma_port(dai_id)) {
			drvdata->cif_substream[stream_dma_idx] = NULL;
			drvdata->cif_dma_heap[stream_dma_idx] = NULL;
		} else {
			drvdata->aif_substream[stream_dma_idx] = NULL;
			drvdata->aif_dma_heap[stream_dma_idx] = NULL;
		}
		drvdata->qaif_init_ref_cnt--;
		clk_disable_unprepare(drvdata->aud_dma_mem_clk);
		clk_disable_unprepare(drvdata->aud_dma_clk);
		qaif_mem_dealloc_detach(component->dev, dma_mem_info);
		if (v->free_stream_dma_idx)
			v->free_stream_dma_idx(drvdata, stream_dma_idx, dai_id);
		kfree(data);
		return ret;
	}

	return 0;
}

static int qaif_platform_pcmops_close(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_pcm_data *data;
	unsigned int dai_id = cpu_dai->driver->id;

	data = runtime->private_data;

	switch (dai_id) {
	case QAIF_MI2S_TDM_AIF0 ... QAIF_MI2S_TDM_AIF12:
		drvdata->aif_substream[data->stream_dma_idx] = NULL;
		qaif_mem_dealloc_detach(component->dev,
					drvdata->aif_dma_heap[data->stream_dma_idx]);
		drvdata->aif_dma_heap[data->stream_dma_idx] = NULL;
		break;
	case QAIF_CDC_DMA_RX0 ... QAIF_CDC_DMA_RX9:
	case QAIF_CDC_DMA_TX0 ... QAIF_CDC_DMA_TX9:
	case QAIF_CDC_DMA_VA_TX0 ... QAIF_CDC_DMA_VA_TX9:
		drvdata->cif_substream[data->stream_dma_idx] = NULL;
		qaif_mem_dealloc_detach(component->dev,
					drvdata->cif_dma_heap[data->stream_dma_idx]);
		drvdata->cif_dma_heap[data->stream_dma_idx] = NULL;
		break;
	default:
		break;
	}

	if (drvdata->qaif_init_ref_cnt > 0)
		drvdata->qaif_init_ref_cnt--;
	else
		dev_dbg(component->dev, "%s: QAIF init ref cnt: %d, skipping decrement\n",
			__func__, drvdata->qaif_init_ref_cnt);

	if (v->free_stream_dma_idx)
		v->free_stream_dma_idx(drvdata, data->stream_dma_idx, dai_id);
	clk_disable_unprepare(drvdata->aud_dma_clk);
	clk_disable_unprepare(drvdata->aud_dma_mem_clk);
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
	struct qaif_dmactl *dmactl;
	unsigned int dai_id = cpu_dai->driver->id;
	int idx;
	int ret;

	dmactl = qaif_get_dmactl_handle(substream, component);
	if (!dmactl)
		return -EINVAL;
	idx = v->get_dma_idx(dai_id);

	if (idx < 0) {
		dev_err(soc_runtime->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return -EINVAL;
	}

	ret = regmap_fields_write(dmactl->burst4, idx, QAIF_DMACTL_BURSTEN);
	if (ret) {
		dev_err(soc_runtime->dev, "error updating burst4 field: %d\n", ret);
		return ret;
	}

	ret = regmap_fields_write(dmactl->shram_wm, idx, QAIF_DMACTL_WM_5);
	if (ret) {
		dev_err(soc_runtime->dev, "error updating shram_wm field: %d\n", ret);
		return ret;
	}

	return 0;
}

static int qaif_platform_pcmops_hw_free(struct snd_soc_component *component,
					struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	unsigned int reg;
	int ret, idx;
	unsigned int dai_id = cpu_dai->driver->id;
	struct regmap *map = drvdata->audio_qaif_map;
	struct qaif_dmactl *dmactl;

	dmactl = qaif_get_dmactl_handle(substream, component);
	if (!dmactl)
		return -EINVAL;
	idx = v->get_dma_idx(dai_id);

	if (idx < 0) {
		dev_err(soc_runtime->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return -EINVAL;
	}

	ret = regmap_fields_write(dmactl->enable, idx, QAIF_DMACTL_ENABLE_OFF);
	if (ret)
		dev_err(soc_runtime->dev, "error writing to rdmactl reg: %d\n", ret);

	reg = QAIF_DMACFG_REG(v, idx, substream->stream, dai_id);
	ret = regmap_write(map, reg, 0);
	if (ret)
		dev_err(soc_runtime->dev, "error writing to rdmactl reg: %d\n", ret);

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
	struct qaif_dmactl *dmactl;
	struct regmap *map;
	int bitwidth = QAIF_DMA_DEFAULT_BIT_WIDTH;
	unsigned int channels = runtime->channels;
	unsigned int rate = runtime->rate;
	int ret, idx, dir = substream->stream;
	unsigned int dai_id = cpu_dai->driver->id;

	dmactl = qaif_get_dmactl_handle(substream, component);
	if (!dmactl)
		return -EINVAL;
	idx = v->get_dma_idx(dai_id);
	map = drvdata->audio_qaif_map;

	if (idx < 0) {
		dev_err(soc_runtime->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return -EINVAL;
	}

	clk_set_rate(drvdata->aud_dma_clk,
		     rate * bitwidth * channels * QAIF_DMA_CLK_RATE_MULTIPLIER);
	clk_set_rate(drvdata->aud_dma_mem_clk,
		     rate * bitwidth * channels * QAIF_DMA_CLK_RATE_MULTIPLIER);

	ret = regmap_write(map, QAIF_SID_MAP_REG(dir, dai_id),
			   drvdata->smmu_csid_bits);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to SID MAP reg: %d\n",
			ret);
		return ret;
	}

	ret = regmap_write(map, QAIF_DMABASE_REG(v, idx, dir, dai_id),
			   runtime->dma_addr);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmabase reg: %d\n",
			ret);
		return ret;
	}

	ret = regmap_write(map, QAIF_DMABUFF_REG(v, idx, dir, dai_id),
			   (snd_pcm_lib_buffer_bytes(substream) >>
			   QAIF_DMA_BYTES_TO_WORDS_SHIFT) - 1);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmabuff reg: %d\n",
			ret);
		return ret;
	}

	ret = regmap_write(map, QAIF_DMAPER_LEN_REG(v, idx, dir, dai_id),
			   (snd_pcm_lib_period_bytes(substream) >>
			   QAIF_DMA_BYTES_TO_WORDS_SHIFT) - 1);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmaper reg: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int qaif_platform_irq_clear(struct qaif_drv_data *drvdata,
				   int dir, enum qaif_irq_type irq_type, int idx)
{
	int ret = 0;
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	unsigned int val_irqclr = BIT(idx);

	if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
		ret |= regmap_write(map,
				    QAIF_EE_RDDMA_PERIOD_IRQ_CLR_REG(v, irq_type), val_irqclr);
		ret |= regmap_write(map,
				    QAIF_EE_RDDMA_UNDERFLOW_IRQ_CLR_REG(v, irq_type), val_irqclr);
		ret |= regmap_write(map,
				    QAIF_EE_RDDMA_ERR_RSP_IRQ_CLR_REG(v, irq_type), val_irqclr);
	} else {
		ret |= regmap_write(map,
				    QAIF_EE_WRDMA_PERIOD_IRQ_CLR_REG(v, irq_type), val_irqclr);
		ret |= regmap_write(map,
				    QAIF_EE_WRDMA_OVERFLOW_IRQ_CLR_REG(v, irq_type), val_irqclr);
		ret |= regmap_write(map,
				    QAIF_EE_WRDMA_ERR_RSP_IRQ_CLR_REG(v, irq_type), val_irqclr);
	}
	return ret;
}

static int qaif_platform_irq_enable(struct qaif_drv_data *drvdata,
				    int dir, enum qaif_irq_type irq_type, int idx)
{
	int ret = 0;
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	unsigned int val_irqen = BIT(idx);

	if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
		ret |= regmap_write_bits(map,
					 QAIF_EE_RDDMA_PERIOD_IRQ_EN_REG(v, irq_type),
					 val_irqen, val_irqen);
		ret |= regmap_write_bits(map,
					 QAIF_EE_RDDMA_UNDERFLOW_IRQ_EN_REG(v, irq_type),
					 val_irqen, val_irqen);
		ret |= regmap_write_bits(map,
					 QAIF_EE_RDDMA_ERR_RSP_IRQ_EN_REG(v, irq_type),
					 val_irqen, val_irqen);
	} else {
		ret |= regmap_write_bits(map,
					 QAIF_EE_WRDMA_PERIOD_IRQ_EN_REG(v, irq_type),
					 val_irqen, val_irqen);
		ret |= regmap_write_bits(map,
					 QAIF_EE_WRDMA_OVERFLOW_IRQ_EN_REG(v, irq_type),
					 val_irqen, val_irqen);
		ret |= regmap_write_bits(map,
					 QAIF_EE_WRDMA_ERR_RSP_IRQ_EN_REG(v, irq_type),
					 val_irqen, val_irqen);
	}
	return ret;
}

static int qaif_platform_irq_disable(struct qaif_drv_data *drvdata,
				     int dir, enum qaif_irq_type irq_type, int idx)
{
	int ret = 0;
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	unsigned int val_irq_disable = BIT(idx);

	if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
		ret |= regmap_write_bits(map,
					 QAIF_EE_RDDMA_PERIOD_IRQ_EN_REG(v, irq_type),
					 val_irq_disable, 0);
		ret |= regmap_write_bits(map,
					 QAIF_EE_RDDMA_UNDERFLOW_IRQ_EN_REG(v, irq_type),
					 val_irq_disable, 0);
		ret |= regmap_write_bits(map,
					 QAIF_EE_RDDMA_ERR_RSP_IRQ_EN_REG(v, irq_type),
					 val_irq_disable, 0);
	} else {
		ret |= regmap_write_bits(map,
					 QAIF_EE_WRDMA_PERIOD_IRQ_EN_REG(v, irq_type),
					 val_irq_disable, 0);
		ret |= regmap_write_bits(map,
					 QAIF_EE_WRDMA_OVERFLOW_IRQ_EN_REG(v, irq_type),
					 val_irq_disable, 0);
		ret |= regmap_write_bits(map,
					 QAIF_EE_WRDMA_ERR_RSP_IRQ_EN_REG(v, irq_type),
					 val_irq_disable, 0);
	}
	return ret;
}

static int qaif_platform_pcmops_trigger(struct snd_soc_component *component,
					struct snd_pcm_substream *substream,
					int cmd)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_dmactl *dmactl;
	struct regmap *map;
	int ret, idx;
	unsigned int dai_id = cpu_dai->driver->id;

	dmactl = qaif_get_dmactl_handle(substream, component);
	if (!dmactl)
		return -EINVAL;
	idx = v->get_dma_idx(dai_id);
	map = drvdata->audio_qaif_map;

	if (idx < 0) {
		dev_err(soc_runtime->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return -EINVAL;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = regmap_fields_write(dmactl->dma_dyncclk, idx, QAIF_DMACTL_DYNCLK_ON);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dma_dyncclk reg field: %d\n", ret);
			return ret;
		}
		ret = regmap_fields_write(dmactl->enable, idx, QAIF_DMACTL_ENABLE_ON);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dma enable reg: %d\n", ret);
			return ret;
		}
		switch (dai_id) {
		case QAIF_MI2S_TDM_AIF0 ... QAIF_MI2S_TDM_AIF12:
			ret = qaif_platform_irq_clear(drvdata,
						      substream->stream, QAIF_AIF_IRQ, idx);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to clear irq reg: %d\n", ret);
				return ret;
			}
			ret = qaif_platform_irq_enable(drvdata,
						       substream->stream, QAIF_AIF_IRQ, idx);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to enable irq reg: %d\n", ret);
				return ret;
			}
			break;
		case QAIF_CDC_DMA_RX0 ... QAIF_CDC_DMA_RX9:
		case QAIF_CDC_DMA_TX0 ... QAIF_CDC_DMA_TX9:
		case QAIF_CDC_DMA_VA_TX0 ... QAIF_CDC_DMA_VA_TX9:
			ret = qaif_platform_irq_clear(drvdata,
						      substream->stream, QAIF_CIF_IRQ, idx);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to clear irq reg: %d\n", ret);
				return ret;
			}
			ret = qaif_platform_irq_enable(drvdata,
						       substream->stream, QAIF_CIF_IRQ, idx);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to enable irq reg: %d\n", ret);
				return ret;
			}
			break;
		default:
			dev_err(soc_runtime->dev, "%s: invalid %d interface\n", __func__, dai_id);
			return -EINVAL;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = regmap_fields_write(dmactl->dma_dyncclk, idx, QAIF_DMACTL_DYNCLK_OFF);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dma_dyncclk reg field: %d\n", ret);
			return ret;
		}
		ret = regmap_fields_write(dmactl->enable, idx, QAIF_DMACTL_ENABLE_OFF);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dma enable reg: %d\n", ret);
			return ret;
		}
		switch (dai_id) {
		case QAIF_MI2S_TDM_AIF0 ... QAIF_MI2S_TDM_AIF12:
			ret = qaif_platform_irq_disable(drvdata,
							substream->stream, QAIF_AIF_IRQ, idx);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to enable irq reg: %d\n", ret);
				return ret;
			}
			break;
		case QAIF_CDC_DMA_RX0 ... QAIF_CDC_DMA_RX9:
		case QAIF_CDC_DMA_TX0 ... QAIF_CDC_DMA_TX9:
		case QAIF_CDC_DMA_VA_TX0 ... QAIF_CDC_DMA_VA_TX9:
			ret = qaif_platform_irq_disable(drvdata,
							substream->stream, QAIF_CIF_IRQ, idx);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to enable irq reg: %d\n", ret);
				return ret;
			}
			break;
		default:
			dev_err(soc_runtime->dev, "%s: invalid %d interface\n", __func__, dai_id);
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t qaif_platform_pcmops_pointer(struct snd_soc_component *component,
						      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	unsigned int base_addr, curr_addr;
	int ret, idx, dir = substream->stream;
	struct regmap *map;
	unsigned int dai_id = cpu_dai->driver->id;

	map = drvdata->audio_qaif_map;
	idx = v->get_dma_idx(dai_id);

	if (idx < 0) {
		dev_err(soc_runtime->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return -EINVAL;
	}

	ret = regmap_read(map, QAIF_DMABASE_REG(v, idx, dir, dai_id), &base_addr);
	if (ret) {
		dev_err(soc_runtime->dev,
			"error reading from rdmabase reg: %d\n", ret);
		return ret;
	}

	ret = regmap_read(map, QAIF_DMACURR_REG(v, idx, dir, dai_id), &curr_addr);
	if (ret) {
		dev_err(soc_runtime->dev,
			"error reading from rdmacurr reg: %d\n", ret);
		return ret;
	}

	return bytes_to_frames(substream->runtime, curr_addr - base_addr);
}

static int qaif_platform_cdc_dma_mmap(struct snd_pcm_substream *substream,
				      struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_coherent(substream->pcm->card->dev, vma,
				 runtime->dma_area, runtime->dma_addr,
				 runtime->dma_bytes);
}

static int qaif_platform_pcmops_mmap(struct snd_soc_component *component,
				     struct snd_pcm_substream *substream,
				     struct vm_area_struct *vma)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	unsigned int dai_id = cpu_dai->driver->id;

	if (is_cif_dma_port(dai_id))
		return qaif_platform_cdc_dma_mmap(substream, vma);

	return snd_pcm_lib_default_mmap(substream, vma);
}

static irqreturn_t qaif_process_dma_irq(struct qaif_drv_data *drvdata,
					u32 stat_reg_addr,
					u32 clr_reg_addr,
					enum qaif_irq_type irq_type,
					enum dma_type dma_type,
					enum qaif_irq irq,
					struct snd_pcm_substream **substream)
{
	const struct qaif_variant *v = drvdata->variant;
	struct snd_pcm_substream *stream = NULL;
	unsigned int reg = 0;
	int dma_idx, stream_dma_idx, rv, num_dma = 0;
	int stream_offset;
	irqreturn_t ret = IRQ_NONE;
	u32 mask = 0;

	if (dma_type == DMA_TYPE_WRDMA)
		stream_offset = (irq_type == QAIF_AIF_IRQ) ? v->wrdma_start
							    : v->codec_wrdma_start;
	else
		stream_offset = 0;

	if (dma_type == DMA_TYPE_WRDMA)
		num_dma = (irq_type == QAIF_AIF_IRQ) ? v->num_wrdma : v->num_codec_wrdma;
	else
		num_dma = (irq_type == QAIF_AIF_IRQ) ? v->num_rddma : v->num_codec_rddma;
	mask = GENMASK(num_dma - 1, 0);
	rv = regmap_read(drvdata->audio_qaif_map, stat_reg_addr, &reg);
	if (rv) {
		dev_err(regmap_get_device(drvdata->audio_qaif_map),
			"error reading stat reg 0x%x: %d\n", stat_reg_addr, rv);
		return IRQ_NONE;
	}

	regmap_write(drvdata->audio_qaif_map, clr_reg_addr, reg & mask);

	for (dma_idx = 0; dma_idx < num_dma; dma_idx++) {
		stream_dma_idx = dma_idx + stream_offset;
		if ((reg & BIT(dma_idx)) && substream[stream_dma_idx]) {
			stream = substream[stream_dma_idx];
			switch (irq) {
			case QAIF_IRQ_PERIOD:
				snd_pcm_period_elapsed(stream);
				ret = IRQ_HANDLED;
				break;

			case QAIF_IRQ_OVERFLOW:
			case QAIF_IRQ_UNDERFLOW:
				dev_warn_ratelimited(regmap_get_device(drvdata->audio_qaif_map),
						     "QAIF DMA xRun\n");
				ret = IRQ_HANDLED;
				break;

			case QAIF_IRQ_ERROR:
				snd_pcm_stop(stream, SNDRV_PCM_STATE_DISCONNECTED);
				dev_err(regmap_get_device(drvdata->audio_qaif_map),
					"QAIF bus error\n");
				ret = IRQ_HANDLED;
				break;
			}
		}
	}
	return ret;
}

static irqreturn_t qaif_aif_irq_handler(struct qaif_drv_data *drvdata, u32 summary_irq_status)
{
	const struct qaif_variant *v = drvdata->variant;
	irqreturn_t ret = IRQ_NONE;
	struct snd_pcm_substream **substream = drvdata->aif_substream;

	if (summary_irq_status & QAIF_SUMMARY_BITMASK_AIF_PERIOD_RDDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_RDDMA_PERIOD_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
			QAIF_EE_RDDMA_PERIOD_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
			QAIF_AIF_IRQ, DMA_TYPE_RDDMA, QAIF_IRQ_PERIOD, substream);
	}
	if (summary_irq_status & QAIF_SUMMARY_BITMASK_AIF_PERIOD_WRDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_WRDMA_PERIOD_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
			QAIF_EE_WRDMA_PERIOD_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
			QAIF_AIF_IRQ, DMA_TYPE_WRDMA, QAIF_IRQ_PERIOD, substream);
	}
	if (summary_irq_status & QAIF_SUMMARY_BITMASK_AIF_OVERFLOW_WRDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_WRDMA_OVERFLOW_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
			QAIF_EE_WRDMA_OVERFLOW_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
			QAIF_AIF_IRQ, DMA_TYPE_WRDMA, QAIF_IRQ_OVERFLOW, substream);
	}
	if (summary_irq_status & QAIF_SUMMARY_BITMASK_AIF_UNDERFLOW_RDDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_RDDMA_UNDERFLOW_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
			QAIF_EE_RDDMA_UNDERFLOW_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
			QAIF_AIF_IRQ, DMA_TYPE_RDDMA, QAIF_IRQ_UNDERFLOW, substream);
	}
	if (summary_irq_status & QAIF_SUMMARY_BITMASK_AIF_ERR_RSP_RDDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_RDDMA_ERR_RSP_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
			QAIF_EE_RDDMA_ERR_RSP_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
			QAIF_AIF_IRQ, DMA_TYPE_RDDMA, QAIF_IRQ_ERROR, substream);
	}
	if (summary_irq_status & QAIF_SUMMARY_BITMASK_AIF_ERR_RSP_WRDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_WRDMA_ERR_RSP_IRQ_STAT_REG(v, QAIF_AIF_IRQ),
			QAIF_EE_WRDMA_ERR_RSP_IRQ_CLR_REG(v, QAIF_AIF_IRQ),
			QAIF_AIF_IRQ, DMA_TYPE_WRDMA, QAIF_IRQ_ERROR, substream);
	}
	return ret;
}

static irqreturn_t qaif_cif_irq_handler(struct qaif_drv_data *drvdata, u32 summary_irq_status)
{
	const struct qaif_variant *v = drvdata->variant;
	irqreturn_t ret = IRQ_NONE;
	struct snd_pcm_substream **substream = drvdata->cif_substream;

	if (summary_irq_status & QAIF_SUMMARY_BITMASK_CIF_PERIOD_RDDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_RDDMA_PERIOD_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
			QAIF_EE_RDDMA_PERIOD_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
			QAIF_CIF_IRQ, DMA_TYPE_RDDMA, QAIF_IRQ_PERIOD, substream);
	}
	if (summary_irq_status & QAIF_SUMMARY_BITMASK_CIF_PERIOD_WRDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_WRDMA_PERIOD_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
			QAIF_EE_WRDMA_PERIOD_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
			QAIF_CIF_IRQ, DMA_TYPE_WRDMA, QAIF_IRQ_PERIOD, substream);
	}

	if (summary_irq_status & QAIF_SUMMARY_BITMASK_CIF_OVERFLOW_WRDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_WRDMA_OVERFLOW_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
			QAIF_EE_WRDMA_OVERFLOW_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
			QAIF_CIF_IRQ, DMA_TYPE_WRDMA, QAIF_IRQ_OVERFLOW, substream);
	}
	if (summary_irq_status & QAIF_SUMMARY_BITMASK_CIF_UNDERFLOW_RDDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_RDDMA_UNDERFLOW_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
			QAIF_EE_RDDMA_UNDERFLOW_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
			QAIF_CIF_IRQ, DMA_TYPE_RDDMA, QAIF_IRQ_UNDERFLOW, substream);
	}

	if (summary_irq_status & QAIF_SUMMARY_BITMASK_CIF_ERR_RSP_RDDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_RDDMA_ERR_RSP_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
			QAIF_EE_RDDMA_ERR_RSP_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
			QAIF_CIF_IRQ, DMA_TYPE_RDDMA, QAIF_IRQ_ERROR, substream);
	}
	if (summary_irq_status & QAIF_SUMMARY_BITMASK_CIF_ERR_RSP_WRDMA) {
		ret |= qaif_process_dma_irq(drvdata,
			QAIF_EE_WRDMA_ERR_RSP_IRQ_STAT_REG(v, QAIF_CIF_IRQ),
			QAIF_EE_WRDMA_ERR_RSP_IRQ_CLR_REG(v, QAIF_CIF_IRQ),
			QAIF_CIF_IRQ, DMA_TYPE_WRDMA, QAIF_IRQ_ERROR, substream);
	}

	return ret;
}

#define QAIF_ALL_CLIENTS_MASK (QAIF_BITMASK_AIF_RDDMA_WRDMA | QAIF_BITMASK_CIF_RDDMA_WRDMA)

static const struct qaif_irq_map qaif_irq_clients[] = {
	{ QAIF_CLIENT_ID_AIF_DMA,	QAIF_BITMASK_AIF_RDDMA_WRDMA, qaif_aif_irq_handler},
	{ QAIF_CLIENT_ID_CIF_DMA,	QAIF_BITMASK_CIF_RDDMA_WRDMA, qaif_cif_irq_handler},
};

static irqreturn_t asoc_platform_qaif_irq(int irq, void *data)
{
	struct qaif_drv_data *drvdata = data;
	const struct qaif_variant *v = drvdata->variant;
	u32 summary_irq_status;
	int rv, client;
	irqreturn_t ret = IRQ_NONE;

	rv = regmap_read(drvdata->audio_qaif_map,
			 QAIF_SUMMARY_IRQSTAT_REG(v), &summary_irq_status);
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

static int qaif_platform_pcmops_suspend(struct snd_soc_component *component)
{
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	struct regmap *map;

	map = drvdata->audio_qaif_map;

	regcache_cache_only(map, true);
	regcache_mark_dirty(map);
	if (drvdata->qaif_init_ref_cnt > 0) {
		clk_disable(drvdata->aud_dma_clk);
		clk_disable(drvdata->aud_dma_mem_clk);
	}
	return 0;
}

static int qaif_platform_pcmops_resume(struct snd_soc_component *component)
{
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	struct regmap *map = drvdata->audio_qaif_map;
	int ret;

	if (drvdata->qaif_init_ref_cnt > 0) {
		ret = clk_enable(drvdata->aud_dma_clk);
		if (ret)
			return ret;
		ret = clk_enable(drvdata->aud_dma_mem_clk);
		if (ret) {
			clk_disable(drvdata->aud_dma_clk);
			return ret;
		}
	}
	regcache_cache_only(map, false);
	return regcache_sync(map);
}

static int qaif_platform_copy(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream, int channel,
			      unsigned long pos, struct iov_iter *buf,
			      unsigned long bytes)
{
	struct snd_pcm_runtime *rt = substream->runtime;
	size_t copied;
	void *dma_buf;

	dma_buf = (void *)(rt->dma_area + pos +
			   channel * (rt->dma_bytes / rt->channels));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		copied = copy_from_iter(dma_buf, bytes, buf);
		if (copied != bytes)
			return -EFAULT;
	} else {
		copied = copy_to_iter(dma_buf, bytes, buf);
		if (copied != bytes)
			return -EFAULT;
	}

	return 0;
}

static const struct snd_soc_component_driver qaif_component_driver = {
	.name		= DRV_NAME,
	.open		= qaif_platform_pcmops_open,
	.close		= qaif_platform_pcmops_close,
	.hw_params	= qaif_platform_pcmops_hw_params,
	.hw_free	= qaif_platform_pcmops_hw_free,
	.prepare	= qaif_platform_pcmops_prepare,
	.trigger	= qaif_platform_pcmops_trigger,
	.pointer	= qaif_platform_pcmops_pointer,
	.mmap		= qaif_platform_pcmops_mmap,
	.suspend	= qaif_platform_pcmops_suspend,
	.resume		= qaif_platform_pcmops_resume,
	.copy		= qaif_platform_copy,
};

static int qaif_parse_smmu_sid(struct platform_device *pdev,
			       struct qaif_drv_data *drvdata)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;
	u32 sid;

	ret = of_property_read_u32_index(np, "iommus", 1, &sid);
	if (ret) {
		dev_err(&pdev->dev, "failed to read SMMU SID from iommus property: %d\n", ret);
		return ret;
	}

	drvdata->smmu_csid_bits = sid & 0x1;
	dev_dbg(&pdev->dev, "sid=0x%x csid=0x%x\n", sid, drvdata->smmu_csid_bits);
	return 0;
}

int asoc_qcom_qaif_platform_register(struct platform_device *pdev)
{
	struct qaif_drv_data *drvdata = platform_get_drvdata(pdev);
	int ret = 0;

	if (!drvdata || !drvdata->variant) {
		dev_err(&pdev->dev, "Invalid drvdata or variant\n");
		return -EINVAL;
	}

	ret = qaif_parse_smmu_sid(pdev, drvdata);
	if (ret)
		return ret;

	drvdata->audio_qaif_irq = platform_get_irq(pdev, 0);
	if (drvdata->audio_qaif_irq < 0)
		return drvdata->audio_qaif_irq;

	ret = devm_request_irq(&pdev->dev, drvdata->audio_qaif_irq,
			       asoc_platform_qaif_irq, IRQF_TRIGGER_HIGH,
			       "qaif-irq-audio-core", drvdata);
	if (ret) {
		dev_err(&pdev->dev, "irq request failed: %d\n", ret);
		return ret;
	}
	drvdata->qaif_init_ref_cnt = 0;
	dev_dbg(&pdev->dev, "%s: Register QAIF Platform\n", __func__);
	return devm_snd_soc_register_component(&pdev->dev,
			&qaif_component_driver, NULL, 0);
}
EXPORT_SYMBOL_GPL(asoc_qcom_qaif_platform_register);

MODULE_DESCRIPTION("Qualcomm Audio Interface (QAIF) PCM platform driver");
MODULE_AUTHOR("Harendra Gautam <harendra.gautam@oss.qualcomm.com>");
MODULE_LICENSE("GPL");
