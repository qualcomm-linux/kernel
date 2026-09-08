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
