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

static int qaif_aif_cfg_cpu_init_bitfields(struct device *dev,
					   struct regmap *map)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_aud_intfctl *aif_intfctl;

	aif_intfctl = devm_kzalloc(dev, sizeof(struct qaif_aud_intfctl), GFP_KERNEL);
	if (!aif_intfctl)
		return -ENOMEM;

	/*
	 * Bulk-allocate all AIF intfctl fields in one call.
	 * Order must match struct qaif_aud_intfctl member order:
	 * inv_sync, sync_delay, sync_mode, sync_src,
	 * slot_width_rx, slot_width_tx, sample_width_rx, sample_width_tx,
	 * mono_mode_rx, mono_mode_tx,
	 * lane_en, lane_dir, loopback_en, ctrl_data_oe,
	 * slot_en_rx_mask, slot_en_tx_mask,
	 * full_cycle_en, bits_per_lane,
	 * enable, enable_tx, enable_rx,
	 * reset, reset_tx, reset_rx
	 */
	{
		const struct reg_field aif_intfctl_fields[] = {
			v->aif_inv_sync,
			v->aif_sync_delay,
			v->aif_sync_mode,
			v->aif_sync_src,
			v->aif_slot_width_rx,
			v->aif_slot_width_tx,
			v->aif_sample_width_rx,
			v->aif_sample_width_tx,
			v->aif_mono_mode_rx,
			v->aif_mono_mode_tx,
			v->aif_lane_en,
			v->aif_lane_dir,
			v->aif_loopback_en,
			v->aif_ctrl_data_oe,
			v->aif_slot_en_rx_mask,
			v->aif_slot_en_tx_mask,
			v->aif_full_cycle_en,
			v->aif_bits_per_lane,
			v->aif_enable,
			v->aif_enable_tx,
			v->aif_enable_rx,
			v->aif_reset,
			v->aif_reset_tx,
			v->aif_reset_rx,
		};
		int ret = devm_regmap_field_bulk_alloc(dev, map,
					&aif_intfctl->inv_sync,
					aif_intfctl_fields,
					ARRAY_SIZE(aif_intfctl_fields));
		if (ret) {
			dev_err(dev, "error allocating AIF interface regmap fields: %d\n", ret);
			return ret;
		}
	}

	drvdata->aif_intfctl = aif_intfctl;

	return 0;
}

static int qaif_aif_cpu_init_bitfields(struct device *dev,
				       struct regmap *map)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_dmactl *rd_dmactl;
	struct qaif_dmactl *wr_dmactl;

	rd_dmactl = devm_kzalloc(dev, sizeof(struct qaif_dmactl), GFP_KERNEL);
	if (!rd_dmactl)
		return -ENOMEM;

	wr_dmactl = devm_kzalloc(dev, sizeof(struct qaif_dmactl), GFP_KERNEL);
	if (!wr_dmactl)
		return -ENOMEM;

	/*
	 * Bulk-allocate AIF RDDMA dmactl fields.
	 * Order must match struct qaif_dmactl member order:
	 * enable, reset, num_ot, dma_dyncclk, burst16, burst8, burst4, burst2, burst1, shram_wm
	 */
	{
		const struct reg_field aif_rd_dmactl_fields[] = {
			v->rddma_enable,
			v->rddma_reset,
			v->rddma_num_ot,
			v->rddma_dma_dyncclk,
			v->rddma_burst16,
			v->rddma_burst8,
			v->rddma_burst4,
			v->rddma_burst2,
			v->rddma_burst1,
			v->rddma_shram_wm,
		};
		int ret = devm_regmap_field_bulk_alloc(dev, map,
					&rd_dmactl->enable,
					aif_rd_dmactl_fields,
					ARRAY_SIZE(aif_rd_dmactl_fields));
		if (ret) {
			dev_err(dev, "error allocating AIF RDDMA dmactl regmap fields: %d\n", ret);
			return ret;
		}
	}

	/*
	 * Bulk-allocate AIF WRDMA dmactl fields.
	 * Order must match struct qaif_dmactl member order:
	 * enable, reset, num_ot, dma_dyncclk, burst16, burst8, burst4, burst2, burst1, shram_wm
	 */
	{
		const struct reg_field aif_wr_dmactl_fields[] = {
			v->wrdma_enable,
			v->wrdma_reset,
			v->wrdma_num_ot,
			v->wrdma_dma_dyncclk,
			v->wrdma_burst16,
			v->wrdma_burst8,
			v->wrdma_burst4,
			v->wrdma_burst2,
			v->wrdma_burst1,
			v->wrdma_shram_wm,
		};
		int ret = devm_regmap_field_bulk_alloc(dev, map,
					&wr_dmactl->enable,
					aif_wr_dmactl_fields,
					ARRAY_SIZE(aif_wr_dmactl_fields));
		if (ret) {
			dev_err(dev, "error allocating AIF WRDMA dmactl regmap fields: %d\n", ret);
			return ret;
		}
	}

	drvdata->aif_rd_dmactl = rd_dmactl;
	drvdata->aif_wr_dmactl = wr_dmactl;

	return 0;
}

static int qaif_aif_cpu_daiops_startup(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	const struct qaif_variant *v = drvdata->variant;
	int idx, ret = 0;

	idx = v->get_dma_idx(dai->driver->id);
	if (idx < 0) {
		dev_err(dai->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return -EINVAL;
	}

	ret = clk_prepare(drvdata->mi2s_bit_clk[idx]);
	if (ret) {
		dev_err(dai->dev, "error in enabling mi2s bit clk: %d\n", ret);
		return ret;
	}
	return 0;
}

static void qaif_aif_cpu_daiops_shutdown(struct snd_pcm_substream *substream,
					 struct snd_soc_dai *dai)
{
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_aud_intfctl *aif_intfctl = drvdata->aif_intfctl;
	const struct qaif_aif_config *aif_intf_cfg;
	int idx = v->get_dma_idx(dai->driver->id);

	if (idx < 0) {
		dev_err(dai->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return;
	}

	aif_intf_cfg = &drvdata->aif_intf_cfg[idx];

	if (aif_intf_cfg->loopback_en)
		regmap_fields_write(aif_intfctl->enable, idx, QAIF_AIF_CTL_ENABLE_OFF);
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		regmap_fields_write(aif_intfctl->enable_tx, idx, QAIF_AIF_CTL_ENABLE_OFF);
	else
		regmap_fields_write(aif_intfctl->enable_rx, idx, QAIF_AIF_CTL_ENABLE_OFF);

	clk_unprepare(drvdata->mi2s_bit_clk[idx]);
}

static int qaif_aif_cpu_daiops_hw_free(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	const struct qaif_variant *v = drvdata->variant;
	int idx = v->get_dma_idx(dai->driver->id);

	if (idx < 0)
		return 0;

	clk_disable(drvdata->mi2s_bit_clk[idx]);
	return 0;
}

static int qaif_aif_cpu_daiops_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *params,
					 struct snd_soc_dai *dai)
{
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_aud_intfctl *aif_intfctl = drvdata->aif_intfctl;
	const struct qaif_aif_config *aif_intf_cfg = NULL;
	int idx;
	snd_pcm_format_t format = params_format(params);
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	unsigned int slot_width = 32;
	int bitwidth, ret;

	if (!aif_intfctl) {
		dev_err(dai->dev, "AIF interface control not initialized\n");
		return -EINVAL;
	}

	idx = v->get_dma_idx(dai->driver->id);

	if (idx < 0) {
		dev_err(dai->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return -EINVAL;
	}

	aif_intf_cfg = &drvdata->aif_intf_cfg[idx];

	if (!aif_intf_cfg) {
		dev_err(dai->dev, "AIF interface config not found\n");
		return -EINVAL;
	}
	bitwidth = snd_pcm_format_width(format);
	if (bitwidth < 0) {
		dev_err(dai->dev, "invalid bit width given: %d\n", bitwidth);
		return bitwidth;
	}

	/* SYNC_CFG: write all four sync fields */
	ret = regmap_fields_write(aif_intfctl->inv_sync, idx, aif_intf_cfg->invert_sync);
	if (ret) {
		dev_err(dai->dev, "Failed to write inv_sync: %d\n", ret);
		return ret;
	}
	ret = regmap_fields_write(aif_intfctl->sync_delay, idx, aif_intf_cfg->sync_delay);
	if (ret) {
		dev_err(dai->dev, "Failed to write sync_delay: %d\n", ret);
		return ret;
	}
	ret = regmap_fields_write(aif_intfctl->sync_mode, idx, aif_intf_cfg->sync_mode);
	if (ret) {
		dev_err(dai->dev, "Failed to write sync_mode: %d\n", ret);
		return ret;
	}
	ret = regmap_fields_write(aif_intfctl->sync_src, idx, aif_intf_cfg->sync_src);
	if (ret) {
		dev_err(dai->dev, "Failed to write sync_src: %d\n", ret);
		return ret;
	}

	/* LANE_CFG: write all four lane fields */
	ret = regmap_fields_write(aif_intfctl->loopback_en, idx, aif_intf_cfg->loopback_en);
	if (ret) {
		dev_err(dai->dev, "Failed to write loopback_en: %d\n", ret);
		return ret;
	}
	ret = regmap_fields_write(aif_intfctl->ctrl_data_oe, idx, aif_intf_cfg->ctrl_data_oe);
	if (ret) {
		dev_err(dai->dev, "Failed to write ctrl_data_oe: %d\n", ret);
		return ret;
	}
	ret = regmap_fields_write(aif_intfctl->lane_en, idx, aif_intf_cfg->lane_en_mask);
	if (ret) {
		dev_err(dai->dev, "Failed to write lane_en (mask=0x%02X): %d\n",
			aif_intf_cfg->lane_en_mask, ret);
		return ret;
	}
	ret = regmap_fields_write(aif_intfctl->lane_dir, idx, aif_intf_cfg->lane_dir_mask);
	if (ret) {
		dev_err(dai->dev, "Failed to write lane_dir (mask=0x%02X): %d\n",
			aif_intf_cfg->lane_dir_mask, ret);
		return ret;
	}

	/* CFG: full_cycle_en */
	ret = regmap_fields_write(aif_intfctl->full_cycle_en, idx, aif_intf_cfg->full_cycle_en);
	if (ret) {
		dev_err(dai->dev, "Failed to write full_cycle_en: %d\n", ret);
		return ret;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		slot_width = aif_intf_cfg->slot_width_tx;
		/* BIT_WIDTH_CFG: TX slot width and sample width */
		ret = regmap_fields_write(aif_intfctl->slot_width_tx, idx,
					  QAIF_AIF_SLOT_WIDTH(slot_width));
		if (ret) {
			dev_err(dai->dev, "Failed to write slot_width_tx: %d\n", ret);
			return ret;
		}
		ret = regmap_fields_write(aif_intfctl->sample_width_tx, idx,
					  QAIF_AIF_SAMPLE_WIDTH(bitwidth));
		if (ret) {
			dev_err(dai->dev, "Failed to write sample_width_tx: %d\n", ret);
			return ret;
		}

		/* ACTV_SLOT_EN_TX */
		ret = regmap_fields_write(aif_intfctl->slot_en_tx_mask, idx,
					  aif_intf_cfg->slot_en_tx_mask);
		if (ret) {
			dev_err(dai->dev, "Failed to write slot_en_tx_mask (0x%08X): %d\n",
				aif_intf_cfg->slot_en_tx_mask, ret);
			return ret;
		}

		/* FRAME_CFG: bits_per_lane */
		ret = regmap_fields_write(aif_intfctl->bits_per_lane, idx,
					  (slot_width * aif_intf_cfg->bits_per_lane) - 1);
		if (ret) {
			dev_err(dai->dev, "Failed to write bits_per_lane: %d\n", ret);
			return ret;
		}

		/* MI2S_CFG: TX mono mode */
		ret = regmap_fields_write(aif_intfctl->mono_mode_tx, idx,
					  (channels >= 2) ? QAIF_AUD_INTF_CTL_STEREO
							  : QAIF_AUD_INTF_CTL_MONO);
		if (ret) {
			dev_err(dai->dev, "Failed to write mono_mode_tx: %d\n", ret);
			return ret;
		}
	} else {
		slot_width = aif_intf_cfg->slot_width_rx;
		/* BIT_WIDTH_CFG: RX slot width and sample width */
		ret = regmap_fields_write(aif_intfctl->slot_width_rx, idx,
					  QAIF_AIF_SLOT_WIDTH(slot_width));
		if (ret) {
			dev_err(dai->dev, "Failed to write slot_width_rx: %d\n", ret);
			return ret;
		}
		ret = regmap_fields_write(aif_intfctl->sample_width_rx, idx,
					  QAIF_AIF_SAMPLE_WIDTH(bitwidth));
		if (ret) {
			dev_err(dai->dev, "Failed to write sample_width_rx: %d\n", ret);
			return ret;
		}

		/* ACTV_SLOT_EN_RX */
		ret = regmap_fields_write(aif_intfctl->slot_en_rx_mask, idx,
					  aif_intf_cfg->slot_en_rx_mask);
		if (ret) {
			dev_err(dai->dev, "Failed to write slot_en_rx_mask (0x%08X): %d\n",
				aif_intf_cfg->slot_en_rx_mask, ret);
			return ret;
		}

		/* FRAME_CFG: bits_per_lane */
		ret = regmap_fields_write(aif_intfctl->bits_per_lane, idx,
					  (slot_width * aif_intf_cfg->bits_per_lane) - 1);
		if (ret) {
			dev_err(dai->dev, "Failed to write bits_per_lane: %d\n", ret);
			return ret;
		}

		/* MI2S_CFG: RX mono mode */
		ret = regmap_fields_write(aif_intfctl->mono_mode_rx, idx,
					  (channels >= 2) ? QAIF_AUD_INTF_CTL_STEREO
							  : QAIF_AUD_INTF_CTL_MONO);
		if (ret) {
			dev_err(dai->dev, "Failed to write mono_mode_rx: %d\n", ret);
			return ret;
		}
	}

	ret = clk_set_rate(drvdata->mi2s_bit_clk[idx],
			   rate * slot_width * aif_intf_cfg->bits_per_lane);
	if (ret) {
		dev_err(dai->dev, "error setting mi2s bitclk to %u: %d\n",
			rate * slot_width * aif_intf_cfg->bits_per_lane, ret);
		return ret;
	}
	dev_dbg(dai->dev, "setting IBIT clock to %u\n",
		rate * slot_width * aif_intf_cfg->bits_per_lane);

	ret = clk_enable(drvdata->mi2s_bit_clk[idx]);
	if (ret) {
		dev_err(dai->dev, "error in enabling mi2s bit clk: %d\n", ret);
		return ret;
	}
	snd_soc_dai_set_tdm_slot(codec_dai, 0x0f, 0b11, aif_intf_cfg->bits_per_lane, slot_width);
	snd_soc_dai_set_sysclk(codec_dai, 0, rate * aif_intf_cfg->bits_per_lane * slot_width, 0);

	return 0;
}

static int qaif_aif_cpu_daiops_trigger(struct snd_pcm_substream *substream,
				       int cmd, struct snd_soc_dai *dai)
{
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	const struct qaif_variant *v = drvdata->variant;
	int idx, ret = -EINVAL;
	const struct qaif_aif_config *aif_intf_cfg;

	idx = v->get_dma_idx(dai->driver->id);
	if (idx < 0) {
		dev_err(dai->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return -EINVAL;
	}

	aif_intf_cfg = &drvdata->aif_intf_cfg[idx];

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (aif_intf_cfg->loopback_en)
			ret = regmap_fields_write(drvdata->aif_intfctl->enable,
						  idx, QAIF_AIF_CTL_ENABLE_ON);
		else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			ret = regmap_fields_write(drvdata->aif_intfctl->enable_tx,
						  idx, QAIF_AIF_CTL_ENABLE_ON);
		else
			ret = regmap_fields_write(drvdata->aif_intfctl->enable_rx,
						  idx, QAIF_AIF_CTL_ENABLE_ON);
		if (ret)
			dev_err(dai->dev, "error writing to AIF CTL reg: %d\n", ret);

		ret = clk_enable(drvdata->mi2s_bit_clk[idx]);
		if (ret) {
			dev_err(dai->dev, "error in enabling mi2s bit clk: %d\n", ret);
			return ret;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:

		if (aif_intf_cfg->loopback_en)
			ret = regmap_fields_write(drvdata->aif_intfctl->enable,
						  idx, QAIF_AIF_CTL_ENABLE_OFF);
		else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			ret = regmap_fields_write(drvdata->aif_intfctl->enable_tx,
						  idx, QAIF_AIF_CTL_ENABLE_OFF);
		else
			ret = regmap_fields_write(drvdata->aif_intfctl->enable_rx,
						  idx, QAIF_AIF_CTL_ENABLE_OFF);
		if (ret)
			dev_err(dai->dev, "error writing to AIF CTL reg: %d\n", ret);

		clk_disable(drvdata->mi2s_bit_clk[idx]);

		break;
	}

	return ret;
}

const struct snd_soc_dai_ops asoc_qcom_qaif_aif_cpu_dai_ops = {
	.startup	= qaif_aif_cpu_daiops_startup,
	.shutdown	= qaif_aif_cpu_daiops_shutdown,
	.hw_free	= qaif_aif_cpu_daiops_hw_free,
	.hw_params	= qaif_aif_cpu_daiops_hw_params,
	.trigger	= qaif_aif_cpu_daiops_trigger,
};
EXPORT_SYMBOL_GPL(asoc_qcom_qaif_aif_cpu_dai_ops);

static int qaif_cpu_of_xlate_dai_name(struct snd_soc_component *component,
				      const struct of_phandle_args *args,
				      const char **dai_name)
{
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;

	return asoc_qcom_of_xlate_dai_name(v->dai_driver,
					   v->num_dai, args, dai_name);
}

static const struct snd_soc_component_driver qaif_cpu_comp_driver = {
	.name = "qaif-cpu",
	.of_xlate_dai_name = qaif_cpu_of_xlate_dai_name,
};

static bool audio_qaif_regmap_writeable(struct device *dev, unsigned int reg)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);
	const struct qaif_variant *v = drvdata->variant;
	int i;

	/* EE maps */
	if (reg == QAIF_EE_RDDMA_MAP_REG(v))
		return true;
	if (reg == QAIF_EE_WRDMA_MAP_REG(v))
		return true;
	if (reg == QAIF_EE_INTF_MAP_REG(v))
		return true;
	if (reg == QAIF_EE_CODEC_RDDMA_MAP_REG(v))
		return true;
	if (reg == QAIF_EE_CODEC_WRDMA_MAP_REG(v))
		return true;

	/* QXM DMA path mapping */
	if (reg == QAIF_RDDMA_MAP_QXM)
		return true;
	if (reg == QAIF_WRDMA_MAP_QXM)
		return true;
	if (reg == QAIF_CODEC_RDDMA_MAP_QXM)
		return true;
	if (reg == QAIF_CODEC_WRDMA_MAP_QXM)
		return true;

	/* SID maps */
	if (reg == QAIF_WRDMA_SID_MAP_REG)
		return true;
	if (reg == QAIF_CODEC_WRDMA_SID_MAP_REG)
		return true;
	if (reg == QAIF_RDDMA_SID_MAP_REG)
		return true;
	if (reg == QAIF_CODEC_RDDMA_SID_MAP_REG)
		return true;

	/* SHRAM QXM0 start address and length */
	for (i = 0; i < v->num_rddma; i++) {
		if (reg == QAIF_RDDMA_QXM0_SHRAM_ST_ADDR(i))
			return true;
		if (reg == QAIF_RDDMA_QXM0_SHRAM_LEN(i))
			return true;
	}
	for (i = 0; i < v->num_codec_rddma; i++) {
		if (reg == QAIF_CODEC_RDDMA_QXM0_SHRAM_ST_ADDR(i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_QXM0_SHRAM_LEN(i))
			return true;
	}
	for (i = 0; i < v->num_wrdma; i++) {
		if (reg == QAIF_WRDMA_QXM0_SHRAM_ST_ADDR(i))
			return true;
		if (reg == QAIF_WRDMA_QXM0_SHRAM_LEN(i))
			return true;
	}
	for (i = 0; i < v->num_codec_wrdma; i++) {
		if (reg == QAIF_CODEC_WRDMA_QXM0_SHRAM_ST_ADDR(i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_QXM0_SHRAM_LEN(i))
			return true;
	}

	/* EE IRQ EN and CLR */
	for (i = 0; i < DMA_TYPE_MAX; i++) {
		if (reg == QAIF_EE_RDDMA_PERIOD_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_PERIOD_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_UNDERFLOW_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_UNDERFLOW_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_ERR_RSP_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_ERR_RSP_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_PERIOD_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_PERIOD_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_OVERFLOW_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_OVERFLOW_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_ERR_RSP_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_ERR_RSP_IRQ_CLR_REG(v, i))
			return true;
	}

	/* AUD_INTF control and configuration */
	for (i = 0; i < v->num_intf; i++) {
		if (reg == QAIF_AUD_INTF_CTL_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_SYNC_CFG_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_BIT_WIDTH_CFG_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_FRAME_CFG_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_ACTV_SLOT_EN_TX_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_ACTV_SLOT_EN_RX_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_LANE_CFG_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_MI2S_CFG_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_CFG_REG(i))
			return true;
	}

	/* RDDMA control and configuration */
	for (i = 0; i < v->num_rddma; i++) {
		if (reg == QAIF_RDDMA_CTL_REG(v, i))
			return true;
		if (reg == QAIF_RDDMA_CFG_REG(v, i))
			return true;
		if (reg == QAIF_RDDMA_BASE_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_RDDMA_BUFF_LEN_REG(v, i))
			return true;
		if (reg == QAIF_RDDMA_PERIOD_LEN_REG(v, i))
			return true;
	}

	/* CODEC RDDMA control and configuration */
	for (i = 0; i < v->num_codec_rddma; i++) {
		if (reg == QAIF_CODEC_RDDMA_CTL_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_CFG_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_BASE_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_BUFF_LEN_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_PERIOD_LEN_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_INTF_CFG_REG(v, i))
			return true;
	}

	/* WRDMA control and configuration */
	for (i = 0; i < v->num_wrdma; i++) {
		if (reg == QAIF_WRDMA_CTL_REG(v, i))
			return true;
		if (reg == QAIF_WRDMA_CFG_REG(v, i))
			return true;
		if (reg == QAIF_WRDMA_BASE_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_WRDMA_BUFF_LEN_REG(v, i))
			return true;
		if (reg == QAIF_WRDMA_PERIOD_LEN_REG(v, i))
			return true;
	}

	/* CODEC WRDMA control and configuration */
	for (i = 0; i < v->num_codec_wrdma; i++) {
		if (reg == QAIF_CODEC_WRDMA_CTL_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_CFG_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_BASE_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_BUFF_LEN_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_PERIOD_LEN_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_INTF_CFG_REG(v, i))
			return true;
	}

	return false;
}

static bool audio_qaif_regmap_readable(struct device *dev, unsigned int reg)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);
	const struct qaif_variant *v = drvdata->variant;
	int i;

	/* Summary IRQ status */
	if (reg == QAIF_SUMMARY_IRQSTAT_REG(v))
		return true;

	/* EE maps */
	if (reg == QAIF_EE_RDDMA_MAP_REG(v))
		return true;
	if (reg == QAIF_EE_WRDMA_MAP_REG(v))
		return true;
	if (reg == QAIF_EE_INTF_MAP_REG(v))
		return true;
	if (reg == QAIF_EE_CODEC_RDDMA_MAP_REG(v))
		return true;
	if (reg == QAIF_EE_CODEC_WRDMA_MAP_REG(v))
		return true;

	/* QXM DMA path mapping */
	if (reg == QAIF_RDDMA_MAP_QXM)
		return true;
	if (reg == QAIF_WRDMA_MAP_QXM)
		return true;
	if (reg == QAIF_CODEC_RDDMA_MAP_QXM)
		return true;
	if (reg == QAIF_CODEC_WRDMA_MAP_QXM)
		return true;

	/* SID maps */
	if (reg == QAIF_WRDMA_SID_MAP_REG)
		return true;
	if (reg == QAIF_CODEC_WRDMA_SID_MAP_REG)
		return true;
	if (reg == QAIF_RDDMA_SID_MAP_REG)
		return true;
	if (reg == QAIF_CODEC_RDDMA_SID_MAP_REG)
		return true;

	/* SHRAM QXM0 start address and length */
	for (i = 0; i < v->num_rddma; i++) {
		if (reg == QAIF_RDDMA_QXM0_SHRAM_ST_ADDR(i))
			return true;
		if (reg == QAIF_RDDMA_QXM0_SHRAM_LEN(i))
			return true;
	}
	for (i = 0; i < v->num_codec_rddma; i++) {
		if (reg == QAIF_CODEC_RDDMA_QXM0_SHRAM_ST_ADDR(i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_QXM0_SHRAM_LEN(i))
			return true;
	}
	for (i = 0; i < v->num_wrdma; i++) {
		if (reg == QAIF_WRDMA_QXM0_SHRAM_ST_ADDR(i))
			return true;
		if (reg == QAIF_WRDMA_QXM0_SHRAM_LEN(i))
			return true;
	}
	for (i = 0; i < v->num_codec_wrdma; i++) {
		if (reg == QAIF_CODEC_WRDMA_QXM0_SHRAM_ST_ADDR(i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_QXM0_SHRAM_LEN(i))
			return true;
	}

	/* EE IRQ EN, CLR and STATUS */
	for (i = 0; i < DMA_TYPE_MAX; i++) {
		if (reg == QAIF_EE_RDDMA_PERIOD_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_PERIOD_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_PERIOD_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_UNDERFLOW_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_UNDERFLOW_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_UNDERFLOW_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_ERR_RSP_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_ERR_RSP_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_ERR_RSP_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_PERIOD_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_PERIOD_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_PERIOD_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_OVERFLOW_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_OVERFLOW_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_OVERFLOW_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_ERR_RSP_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_ERR_RSP_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_ERR_RSP_IRQ_STAT_REG(v, i))
			return true;
	}

	/* AUD_INTF control and configuration */
	for (i = 0; i < v->num_intf; i++) {
		if (reg == QAIF_AUD_INTF_CTL_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_SYNC_CFG_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_BIT_WIDTH_CFG_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_FRAME_CFG_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_ACTV_SLOT_EN_TX_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_ACTV_SLOT_EN_RX_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_LANE_CFG_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_MI2S_CFG_REG(i))
			return true;
		if (reg == QAIF_AUD_INTF_CFG_REG(i))
			return true;
	}

	/* RDDMA control, configuration and current address */
	for (i = 0; i < v->num_rddma; i++) {
		if (reg == QAIF_RDDMA_CTL_REG(v, i))
			return true;
		if (reg == QAIF_RDDMA_CFG_REG(v, i))
			return true;
		if (reg == QAIF_RDDMA_BASE_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_RDDMA_BUFF_LEN_REG(v, i))
			return true;
		if (reg == QAIF_RDDMA_CURR_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_RDDMA_PERIOD_LEN_REG(v, i))
			return true;
	}

	/* CODEC RDDMA control, configuration and current address */
	for (i = 0; i < v->num_codec_rddma; i++) {
		if (reg == QAIF_CODEC_RDDMA_CTL_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_CFG_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_BASE_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_BUFF_LEN_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_CURR_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_PERIOD_LEN_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_RDDMA_INTF_CFG_REG(v, i))
			return true;
	}

	/* WRDMA control, configuration and current address */
	for (i = 0; i < v->num_wrdma; i++) {
		if (reg == QAIF_WRDMA_CTL_REG(v, i))
			return true;
		if (reg == QAIF_WRDMA_CFG_REG(v, i))
			return true;
		if (reg == QAIF_WRDMA_BASE_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_WRDMA_BUFF_LEN_REG(v, i))
			return true;
		if (reg == QAIF_WRDMA_CURR_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_WRDMA_PERIOD_LEN_REG(v, i))
			return true;
	}

	/* CODEC WRDMA control, configuration and current address */
	for (i = 0; i < v->num_codec_wrdma; i++) {
		if (reg == QAIF_CODEC_WRDMA_CTL_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_CFG_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_BASE_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_BUFF_LEN_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_CURR_ADDR_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_PERIOD_LEN_REG(v, i))
			return true;
		if (reg == QAIF_CODEC_WRDMA_INTF_CFG_REG(v, i))
			return true;
	}

	return false;
}

static bool audio_qaif_regmap_volatile(struct device *dev, unsigned int reg)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);
	const struct qaif_variant *v = drvdata->variant;
	int i;

	/* Summary IRQ status - hardware updated on any interrupt */
	if (reg == QAIF_SUMMARY_IRQSTAT_REG(v))
		return true;

	/* EE IRQ status - hardware updated on interrupt */
	for (i = 0; i < DMA_TYPE_MAX; i++) {
		if (reg == QAIF_EE_RDDMA_PERIOD_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_UNDERFLOW_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_ERR_RSP_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_PERIOD_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_OVERFLOW_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_ERR_RSP_IRQ_STAT_REG(v, i))
			return true;
	}

	/* DMA current address - hardware updated during streaming */
	for (i = 0; i < v->num_rddma; i++) {
		if (reg == QAIF_RDDMA_CURR_ADDR_REG(v, i))
			return true;
	}
	for (i = 0; i < v->num_wrdma; i++) {
		if (reg == QAIF_WRDMA_CURR_ADDR_REG(v, i))
			return true;
	}
	for (i = 0; i < v->num_codec_rddma; i++) {
		if (reg == QAIF_CODEC_RDDMA_CURR_ADDR_REG(v, i))
			return true;
	}
	for (i = 0; i < v->num_codec_wrdma; i++) {
		if (reg == QAIF_CODEC_WRDMA_CURR_ADDR_REG(v, i))
			return true;
	}

	return false;
}

static const struct regmap_config audio_qaif_regmap_config = {
	.name = "audio_qaif_cpu",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.writeable_reg = audio_qaif_regmap_writeable,
	.readable_reg = audio_qaif_regmap_readable,
	.volatile_reg = audio_qaif_regmap_volatile,
	.cache_type = REGCACHE_FLAT,
};

static int of_qaif_parse_aif_intf_cfg(struct device *dev,
				      struct qaif_drv_data *data)
{
	const struct qaif_variant *v = data->variant;
	struct device_node *np = dev->of_node;
	struct device_node *intf_np;
	struct qaif_aif_config *cfg;
	const __be32 *lane_cfg_prop;
	int ret, j;
	int lane_cfg_len;
	u32 dai_id, intf_idx;
	int num_interfaces = 0;

	if (!v) {
		dev_err(dev, "No variant data\n");
		return -EINVAL;
	}

	/*
	 * Iterate over all child nodes of qaif_cpu and process only those
	 * with a recognised AIF interface compatible. The compatible string
	 * identifies the serial protocol the interface is wired for on the
	 * board: qcom,qaif-pcm-dai, qcom,qaif-tdm-dai or qcom,qaif-mi2s-dai.
	 * Other child nodes are silently skipped.
	 */
	for_each_child_of_node(np, intf_np) {
		enum qaif_aif_mode mode;

		if (of_device_is_compatible(intf_np, "qcom,qaif-pcm-dai"))
			mode = QAIF_AIF_MODE_PCM;
		else if (of_device_is_compatible(intf_np, "qcom,qaif-tdm-dai"))
			mode = QAIF_AIF_MODE_TDM;
		else if (of_device_is_compatible(intf_np, "qcom,qaif-mi2s-dai"))
			mode = QAIF_AIF_MODE_MI2S;
		else
			continue;

		if (num_interfaces >= QAIF_MAX_AIF_CFG_CNT) {
			dev_warn(dev, "Too many AIF interfaces, limiting to %d\n",
				 QAIF_MAX_AIF_CFG_CNT);
			of_node_put(intf_np);
			break;
		}

		ret = of_property_read_u32(intf_np, "reg", &dai_id);
		if (ret) {
			dev_err(dev, "Missing reg for interface %d: %s\n",
				num_interfaces, intf_np->name);
			continue;
		}

		if (v->get_dma_idx) {
			intf_idx = v->get_dma_idx(dai_id);
			if (intf_idx < 0) {
				dev_err(dev, "Invalid DAI ID %d for interface '%s' (node %d)\n",
					dai_id, intf_np->name, num_interfaces);
				continue;
			}
			if (intf_idx >= ARRAY_SIZE(data->aif_intf_cfg)) {
				dev_err(dev, "DAI ID %d maps to out-of-range intf_idx %d\n",
					dai_id, intf_idx);
				continue;
			}
		} else {
			dev_err(dev, "can not get intf idx for : %d: %s\n",
				num_interfaces, intf_np->name);
			of_node_put(intf_np);
			return -EINVAL;
		}
		cfg = &data->aif_intf_cfg[intf_idx];
		cfg->mode = mode;

		/* Parse sync configuration — mode-specific */
		if (mode == QAIF_AIF_MODE_MI2S) {
			/* MI2S: sync mode is fixed (long sync = 1, WS-based) */
			cfg->sync_mode = 1;
		} else {
			/* PCM/TDM: sync mode comes from DT (0=short, 1=long) */
			ret = of_property_read_u32(intf_np, "qcom,qaif-aif-sync-mode",
						   &cfg->sync_mode);
			if (ret) {
				dev_err(dev, "Missing sync-mode for interface %d\n",
					num_interfaces);
				of_node_put(intf_np);
				return -EINVAL;
			}
		}

		ret = of_property_read_u32(intf_np, "qcom,qaif-aif-sync-src", &cfg->sync_src);
		if (ret) {
			dev_warn(dev, "Missing sync-src for interface %d\n", num_interfaces);
			cfg->sync_src = 0;
		}

		cfg->invert_sync = of_property_read_bool(intf_np, "qcom,qaif-aif-invert-sync");

		ret = of_property_read_u32(intf_np, "qcom,qaif-aif-sync-delay", &cfg->sync_delay);
		if (ret) {
			dev_warn(dev, "Missing sync-delay for interface %d\n", num_interfaces);
			cfg->sync_delay = 0;
		}

		/* Parse slot and sample width configuration */
		ret = of_property_read_u32(intf_np, "qcom,qaif-aif-slot-width-rx",
					   &cfg->slot_width_rx);
		if (ret) {
			dev_warn(dev, "Missing slot-width-rx for interface %d\n", num_interfaces);
			cfg->slot_width_rx = 0;
		}

		ret = of_property_read_u32(intf_np, "qcom,qaif-aif-slot-width-tx",
					   &cfg->slot_width_tx);
		if (ret) {
			dev_warn(dev, "Missing slot-width-tx for interface %d\n", num_interfaces);
			cfg->slot_width_tx = 0;
		}

		/* Parse slot enable masks — mode-specific */
		if (mode == QAIF_AIF_MODE_MI2S) {
			/* MI2S: always 2 active slots (left + right) */
			cfg->slot_en_rx_mask = 0x3;
			cfg->slot_en_tx_mask = 0x3;
		} else {
			/* PCM/TDM: active slot mask comes from DT */
			ret = of_property_read_u32(intf_np, "qcom,qaif-aif-slot-en-rx-mask",
						   &cfg->slot_en_rx_mask);
			if (ret) {
				dev_warn(dev, "Missing slot-en-rx-mask for interface %d\n",
					 num_interfaces);
				cfg->slot_en_rx_mask = 0;
			}
			ret = of_property_read_u32(intf_np, "qcom,qaif-aif-slot-en-tx-mask",
						   &cfg->slot_en_tx_mask);
			if (ret) {
				dev_warn(dev, "Missing slot-en-tx-mask for interface %d\n",
					 num_interfaces);
				cfg->slot_en_tx_mask = 0;
			}
		}

		/* Parse control configuration */
		cfg->loopback_en = of_property_read_bool(intf_np, "qcom,qaif-aif-loopback");

		cfg->ctrl_data_oe = of_property_read_bool(intf_np, "qcom,qaif-aif-ctrl-data-oe");

		/* Parse lane configuration */
		lane_cfg_prop = of_get_property(intf_np, "qcom,qaif-aif-lane-config",
						&lane_cfg_len);
		if (lane_cfg_prop) {
			/* Each lane config has 2 u32 values: enable and direction */
			cfg->num_lanes = lane_cfg_len / (2 * sizeof(u32));
			if (cfg->num_lanes > QAIF_MAX_LANES) {
				dev_warn(dev, "Too many lanes (%d), limiting to %d\n",
					 cfg->num_lanes, QAIF_MAX_LANES);
				cfg->num_lanes = QAIF_MAX_LANES;
			}

			for (j = 0; j < cfg->num_lanes; j++) {
				cfg->lane_cfg[j].enable = be32_to_cpup(lane_cfg_prop + (j * 2));
				if (cfg->lane_cfg[j].enable)
					cfg->lane_en_mask |= BIT(j);

				cfg->lane_cfg[j].direction =
					be32_to_cpup(lane_cfg_prop + (j * 2 + 1));
				if (cfg->lane_cfg[j].direction)
					cfg->lane_dir_mask |= BIT(j);
			}

		} else {
			dev_warn(dev, "Missing lane-config for interface %d\n", num_interfaces);
			cfg->num_lanes = 0;
		}

		/* Mono/stereo mode is written directly from params_channels() in hw_params */

		/* Parse frame configuration */
		cfg->full_cycle_en = of_property_read_bool(intf_np, "qcom,qaif-aif-full-cycle-en");

		ret = of_property_read_u32(intf_np, "qcom,qaif-aif-bits-per-lane",
					   &cfg->bits_per_lane);
		if (ret) {
			dev_warn(dev, "Missing bits-per-lane for interface %d\n", num_interfaces);
			cfg->bits_per_lane = 0;
		}

		num_interfaces++;
	}

	if (num_interfaces == 0) {
		dev_err(dev, "No AIF child nodes with qcom,qaif-{pcm,tdm,mi2s}-dai compatible found\n");
		return -EINVAL;
	}

	return 0;
}

static int of_qaif_cdc_dma_clks_parse(struct device *dev,
				      struct qaif_drv_data *data)
{
	data->aud_dma_clk = devm_clk_get(dev, "aud_dma_clk");
	if (IS_ERR(data->aud_dma_clk))
		return PTR_ERR(data->aud_dma_clk);

	data->aud_dma_mem_clk = devm_clk_get(dev, "aud_dma_mem_clk");
	if (IS_ERR(data->aud_dma_mem_clk))
		return PTR_ERR(data->aud_dma_mem_clk);

	return 0;
}

int asoc_qcom_qaif_cpu_platform_probe(struct platform_device *pdev)
{
	struct qaif_drv_data *drvdata;
	struct resource *res;
	const struct qaif_variant *variant;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	int ret, i, dai_id, idx;
	bool variant_init_done = false;

	drvdata = devm_kzalloc(dev, sizeof(struct qaif_drv_data), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;
	platform_set_drvdata(pdev, drvdata);

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data)
		return -EINVAL;

	drvdata->variant = (const struct qaif_variant *)match->data;
	variant = drvdata->variant;
	if (!variant) {
		dev_err(dev, "No variant data\n");
		return -EINVAL;
	}

	ret = of_qaif_parse_aif_intf_cfg(dev, drvdata);
	if (ret) {
		dev_err(dev, "Failed to parse aif interfaces: %d\n", ret);
		return -EINVAL;
	}

	drvdata->audio_qaif =
			devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->audio_qaif))
		return PTR_ERR(drvdata->audio_qaif);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	const struct regmap_config qaif_regmap_cfg = {
		.name = audio_qaif_regmap_config.name,
		.reg_bits = audio_qaif_regmap_config.reg_bits,
		.reg_stride = audio_qaif_regmap_config.reg_stride,
		.val_bits = audio_qaif_regmap_config.val_bits,
		.writeable_reg = audio_qaif_regmap_config.writeable_reg,
		.readable_reg = audio_qaif_regmap_config.readable_reg,
		.volatile_reg = audio_qaif_regmap_config.volatile_reg,
		.cache_type = audio_qaif_regmap_config.cache_type,
		.max_register = resource_size(res),
	};

	drvdata->audio_qaif_map = devm_regmap_init_mmio(dev, drvdata->audio_qaif,
							&qaif_regmap_cfg);
	if (IS_ERR(drvdata->audio_qaif_map))
		return PTR_ERR(drvdata->audio_qaif_map);

	ret = of_qaif_cdc_dma_clks_parse(dev, drvdata);
	if (ret) {
		dev_err(dev, "failed to get cdc dma clocks %d\n", ret);
		return ret;
	}

	if (variant->init) {
		ret = variant->init(pdev);
		if (ret) {
			dev_err(dev, "error initializing variant: %d\n", ret);
			return ret;
		}
		variant_init_done = true;
	}

	for (i = 0; i < variant->num_dai; i++) {
		dai_id = variant->dai_driver[i].id;
		if (is_cif_dma_port(dai_id))
			continue;
		idx = variant->get_dma_idx(dai_id);
		if (idx < 0)
			continue;

		drvdata->mi2s_bit_clk[idx] = devm_clk_get(dev,
							  variant->dai_bit_clk_names[idx]);
		if (IS_ERR(drvdata->mi2s_bit_clk[idx])) {
			dev_err(dev,
				"error getting %s: %ld\n",
				variant->dai_bit_clk_names[idx],
				PTR_ERR(drvdata->mi2s_bit_clk[idx]));
			ret = PTR_ERR(drvdata->mi2s_bit_clk[idx]);
			goto err;
		}
	}

	ret = qaif_aif_cpu_init_bitfields(dev, drvdata->audio_qaif_map);
	if (ret) {
		dev_err(dev, "error init cif bitfield: %d\n", ret);
		goto err;
	}

	ret = qaif_aif_cfg_cpu_init_bitfields(dev, drvdata->audio_qaif_map);
	if (ret) {
		dev_err(dev, "error init aif_intfctl field: %d\n", ret);
		goto err;
	}

	ret = qaif_cif_cpu_init_bitfields(dev, drvdata->audio_qaif_map);
	if (ret) {
		dev_err(dev, "error init cif bitfield: %d\n", ret);
		goto err;
	}

	ret = devm_snd_soc_register_component(dev, &qaif_cpu_comp_driver,
					      variant->dai_driver,
					      variant->num_dai);
	if (ret) {
		dev_err(dev, "error registering cpu driver: %d\n", ret);
		goto err;
	}

	ret = asoc_qcom_qaif_platform_register(pdev);
	if (ret) {
		dev_err(dev, "error registering platform driver: %d\n", ret);
		goto err;
	}
	dev_dbg(&pdev->dev, "%s: QAIF CPU-Platform Driver Registered Successfully\n", __func__);
err:
	if (ret && variant_init_done && variant->exit)
		variant->exit(pdev);
	return ret;
}
EXPORT_SYMBOL_GPL(asoc_qcom_qaif_cpu_platform_probe);

void asoc_qcom_qaif_cpu_platform_remove(struct platform_device *pdev)
{
	struct qaif_drv_data *drvdata = platform_get_drvdata(pdev);

	if (drvdata->variant->exit)
		drvdata->variant->exit(pdev);
}
EXPORT_SYMBOL_GPL(asoc_qcom_qaif_cpu_platform_remove);

void asoc_qcom_qaif_cpu_platform_shutdown(struct platform_device *pdev)
{
	struct qaif_drv_data *drvdata = platform_get_drvdata(pdev);

	if (drvdata->variant->exit)
		drvdata->variant->exit(pdev);
}
EXPORT_SYMBOL_GPL(asoc_qcom_qaif_cpu_platform_shutdown);

MODULE_DESCRIPTION("Qualcomm Audio Interface (QAIF) CPU DAI driver");
MODULE_AUTHOR("Harendra Gautam <harendra.gautam@oss.qualcomm.com>");
MODULE_LICENSE("GPL");
