// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * qaif-cpu.c -- ALSA SoC CPU-Platform DAI driver for QTi QAIF
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include "qaif-reg.h"
#include "qaif.h"
#include "common.h"

static int qaif_cif_cpu_init_bitfields(struct device *dev,
				       struct regmap *map)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_dmactl *rd_dmactl;
	struct qaif_dmactl *wr_dmactl;
	struct qaif_cdc_intfctl *rd_intfctl;
	struct qaif_cdc_intfctl *wr_intfctl;

	rd_dmactl = devm_kzalloc(dev, sizeof(struct qaif_dmactl), GFP_KERNEL);
	if (!rd_dmactl)
		return -ENOMEM;

	wr_dmactl = devm_kzalloc(dev, sizeof(struct qaif_dmactl), GFP_KERNEL);
	if (!wr_dmactl)
		return -ENOMEM;

	rd_intfctl = devm_kzalloc(dev, sizeof(struct qaif_cdc_intfctl), GFP_KERNEL);
	if (!rd_intfctl)
		return -ENOMEM;

	wr_intfctl = devm_kzalloc(dev, sizeof(struct qaif_cdc_intfctl), GFP_KERNEL);
	if (!wr_intfctl)
		return -ENOMEM;

	/*
	 * Bulk-allocate CIF RDDMA dmactl fields.
	 * Order must match struct qaif_dmactl member order:
	 * enable, reset, num_ot, dma_dyncclk, burst16, burst8, burst4, burst2, burst1, shram_wm
	 */
	{
		const struct reg_field cif_rd_dmactl_fields[] = {
			v->cif_rddma_enable,
			v->cif_rddma_reset,
			v->cif_rddma_num_ot,
			v->cif_rddma_dma_dyncclk,
			v->cif_rddma_burst16,
			v->cif_rddma_burst8,
			v->cif_rddma_burst4,
			v->cif_rddma_burst2,
			v->cif_rddma_burst1,
			v->cif_rddma_shram_wm,
		};
		int ret = devm_regmap_field_bulk_alloc(dev, map,
					&rd_dmactl->enable,
					cif_rd_dmactl_fields,
					ARRAY_SIZE(cif_rd_dmactl_fields));
		if (ret) {
			dev_err(dev, "error allocating CIF RDDMA dmactl regmap fields: %d\n", ret);
			return ret;
		}
	}

	/*
	 * Bulk-allocate CIF RDDMA intfctl fields.
	 * Order must match struct qaif_cdc_intfctl member order:
	 * active_ch_en, fs_sel, fs_delay, fs_out_gate, intf_dyncclk, en_16bit_unpack
	 */
	{
		const struct reg_field cif_rd_intfctl_fields[] = {
			v->cif_rddma_active_ch_en,
			v->cif_rddma_fs_sel,
			v->cif_rddma_fs_delay,
			v->cif_rddma_fs_out_gate,
			v->cif_rddma_intf_dyncclk,
			v->cif_rddma_en_16bit_unpack,
		};
		int ret = devm_regmap_field_bulk_alloc(dev, map,
					&rd_intfctl->active_ch_en,
					cif_rd_intfctl_fields,
					ARRAY_SIZE(cif_rd_intfctl_fields));
		if (ret) {
			dev_err(dev, "error allocating CIF RDDMA intfctl regmap fields: %d\n", ret);
			return ret;
		}
	}

	/*
	 * Bulk-allocate CIF WRDMA dmactl fields.
	 * Order must match struct qaif_dmactl member order:
	 * enable, reset, num_ot, dma_dyncclk, burst16, burst8, burst4, burst2, burst1, shram_wm
	 */
	{
		const struct reg_field cif_wr_dmactl_fields[] = {
			v->cif_wrdma_enable,
			v->cif_wrdma_reset,
			v->cif_wrdma_num_ot,
			v->cif_wrdma_dma_dyncclk,
			v->cif_wrdma_burst16,
			v->cif_wrdma_burst8,
			v->cif_wrdma_burst4,
			v->cif_wrdma_burst2,
			v->cif_wrdma_burst1,
			v->cif_wrdma_shram_wm,
		};
		int ret = devm_regmap_field_bulk_alloc(dev, map,
					&wr_dmactl->enable,
					cif_wr_dmactl_fields,
					ARRAY_SIZE(cif_wr_dmactl_fields));
		if (ret) {
			dev_err(dev, "error allocating CIF WRDMA dmactl regmap fields: %d\n", ret);
			return ret;
		}
	}

	/*
	 * Bulk-allocate CIF WRDMA intfctl fields.
	 * Order must match struct qaif_cdc_intfctl member order:
	 * active_ch_en, fs_sel, fs_delay, fs_out_gate, intf_dyncclk, en_16bit_unpack
	 */
	{
		const struct reg_field cif_wr_intfctl_fields[] = {
			v->cif_wrdma_active_ch_en,
			v->cif_wrdma_fs_sel,
			v->cif_wrdma_fs_delay,
			v->cif_wrdma_fs_out_gate,
			v->cif_wrdma_intf_dyncclk,
			v->cif_wrdma_en_16bit_unpack,
		};
		int ret = devm_regmap_field_bulk_alloc(dev, map,
					&wr_intfctl->active_ch_en,
					cif_wr_intfctl_fields,
					ARRAY_SIZE(cif_wr_intfctl_fields));
		if (ret) {
			dev_err(dev, "error allocating CIF WRDMA intfctl regmap fields: %d\n", ret);
			return ret;
		}
	}

	drvdata->cif_rd_dmactl = rd_dmactl;
	drvdata->cif_wr_dmactl = wr_dmactl;
	drvdata->cif_rddma_intfctl = rd_intfctl;
	drvdata->cif_wrdma_intfctl = wr_intfctl;

	return 0;
}

static struct qaif_cdc_intfctl *qaif_get_cif_intfctl_handle(struct snd_pcm_substream *substream,
							    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	const struct qaif_variant *v = drvdata->variant;
	unsigned int dai_id = cpu_dai->driver->id;
	struct qaif_cdc_intfctl *intfctl = NULL;

	if (!v) {
		dev_err(soc_runtime->dev, "No variant data\n");
		return intfctl;
	}

	switch (dai_id) {
	case QAIF_CDC_DMA_RX0 ... QAIF_CDC_DMA_RX9:
		intfctl = drvdata->cif_rddma_intfctl;
		break;
	case QAIF_CDC_DMA_TX0 ... QAIF_CDC_DMA_TX9:
	case QAIF_CDC_DMA_VA_TX0 ... QAIF_CDC_DMA_VA_TX9:
		intfctl = drvdata->cif_wrdma_intfctl;
		break;
	default:
		dev_err(soc_runtime->dev, "invalid dai id for dma ctl: %d\n", dai_id);
		break;
	}
	return intfctl;
}

static int qaif_cif_daiops_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_cdc_intfctl *intfctl = NULL;
	unsigned int dai_id = cpu_dai->driver->id;
	int ret;
	unsigned int regval;
	unsigned int channels = params_channels(params);
	int idx;

	switch (channels) {
	case 1:
		regval = QAIF_CIF_DMA_INTF_ONE_CHANNEL;
		break;
	case 2:
		regval = QAIF_CIF_DMA_INTF_TWO_CHANNEL;
		break;
	case 4:
		regval = QAIF_CIF_DMA_INTF_FOUR_CHANNEL;
		break;
	case 6:
		regval = QAIF_CIF_DMA_INTF_SIX_CHANNEL;
		break;
	case 8:
		regval = QAIF_CIF_DMA_INTF_EIGHT_CHANNEL;
		break;
	default:
		dev_err(soc_runtime->dev, "invalid PCM config\n");
		return -EINVAL;
	}

	intfctl = qaif_get_cif_intfctl_handle(substream, dai);
	if (!intfctl) {
		dev_err(soc_runtime->dev, "Invalid intfctl: %d\n", dai_id);
		return -EINVAL;
	}
	idx = v->get_dma_idx(dai_id);
	if (idx < 0) {
		dev_err(soc_runtime->dev, "Invalid DMA index: %d\n", idx);
		return -EINVAL;
	}
	ret = regmap_fields_write(intfctl->active_ch_en, idx, regval);
	if (ret) {
		dev_err(soc_runtime->dev,
			"error writing to intfctl active_ch_en reg field: %d\n", ret);
		return ret;
	}

	return 0;
}

static int qaif_cif_daiops_trigger(struct snd_pcm_substream *substream,
				   int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	const struct qaif_variant *v = drvdata->variant;
	unsigned int dai_id = cpu_dai->driver->id;
	struct qaif_cdc_intfctl *intfctl = NULL;
	int ret = 0, idx;

	intfctl = qaif_get_cif_intfctl_handle(substream, dai);
	if (!intfctl) {
		dev_err(soc_runtime->dev, "Invalid intfctl: %d\n", dai_id);
		return -EINVAL;
	}
	idx = v->get_dma_idx(dai_id);
	if (idx < 0) {
		dev_err(soc_runtime->dev, "Invalid DMA index: %d\n", idx);
		return -EINVAL;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = regmap_fields_write(intfctl->intf_dyncclk, idx, QAIF_DMACTL_DYNCLK_ON);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dmactl intf_dyncclk reg field: %d\n", ret);
			return ret;
		}
		ret = regmap_fields_write(intfctl->fs_sel, idx, QAIF_CIF_DMA_FS_SEL_DEFAULT);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dmactl codec_fs_sel reg field: %d\n", ret);
			return ret;
		}

		ret = regmap_fields_write(intfctl->en_16bit_unpack, idx,
					  QAIF_CIF_16BIT_UNPACK_ENABLE);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dmactl en_16bit_unpack reg field: %d\n", ret);
			return ret;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = regmap_fields_write(intfctl->intf_dyncclk, idx, QAIF_DMACTL_DYNCLK_OFF);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dmactl intf_dyncclk reg field: %d\n", ret);
			return ret;
		}
		ret = regmap_fields_write(intfctl->en_16bit_unpack, idx,
					  QAIF_CIF_16BIT_UNPACK_DISABLE);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dmactl en_16bit_unpack reg field: %d\n", ret);
			return ret;
		}
		break;
	default:
		ret = -EINVAL;
		dev_err(soc_runtime->dev, "%s: invalid %d interface\n", __func__, cmd);
		break;
	}
	return ret;
}

const struct snd_soc_dai_ops asoc_qcom_qaif_cif_dai_ops = {
	.hw_params	= qaif_cif_daiops_hw_params,
	.trigger	= qaif_cif_daiops_trigger,
};
EXPORT_SYMBOL_GPL(asoc_qcom_qaif_cif_dai_ops);
