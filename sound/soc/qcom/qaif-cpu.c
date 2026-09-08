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
