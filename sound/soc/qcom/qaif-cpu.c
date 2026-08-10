// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
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
#include <dt-bindings/sound/qcom,lpass.h>
#include "qaif-reg.h"
#include "qaif.h"

#define QAIF_AIF_REG_READ 1
#define QAIF_AIF_REG_WRITE 0

static int qaif_cif_cpu_init_bitfields(struct device *dev,
				       struct regmap *map)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);
	struct qaif_variant *v = drvdata->variant;
	struct qaif_dmactl *rd_dmactl;
	struct qaif_dmactl *wr_dmactl;
	struct qaif_cdc_intfctl *rd_intfctl;
	struct qaif_cdc_intfctl *wr_intfctl;

	/* Allocate RDDMA control structure */
	rd_dmactl = devm_kzalloc(dev, sizeof(struct qaif_dmactl), GFP_KERNEL);
	if (!rd_dmactl)
		return -ENOMEM;

	/* Allocate WRDMA control structure */
	wr_dmactl = devm_kzalloc(dev, sizeof(struct qaif_dmactl), GFP_KERNEL);
	if (!wr_dmactl)
		return -ENOMEM;

	/* Allocate RDDMA INTF control structure */
	rd_intfctl = devm_kzalloc(dev, sizeof(struct qaif_cdc_intfctl), GFP_KERNEL);
	if (!rd_intfctl)
		return -ENOMEM;

	/* Allocate WRDMA INTF control structure */
	wr_intfctl = devm_kzalloc(dev, sizeof(struct qaif_cdc_intfctl), GFP_KERNEL);
	if (!wr_intfctl)
		return -ENOMEM;

	/* ===================================================================
	 * Allocate RDDMA (RX/Playback) regmap fields for all 4 channels
	 * ===================================================================
	 */

	/* CTL register fields */
	rd_dmactl->enable = devm_regmap_field_alloc(dev, map, v->cif_rddma_enable);
	rd_dmactl->reset = devm_regmap_field_alloc(dev, map, v->cif_rddma_reset);

	/* CFG register fields */
	rd_dmactl->shram_wm = devm_regmap_field_alloc(dev, map, v->cif_rddma_shram_wm);
	rd_dmactl->burst1 = devm_regmap_field_alloc(dev, map, v->cif_rddma_burst1);
	rd_dmactl->burst2 = devm_regmap_field_alloc(dev, map, v->cif_rddma_burst2);
	rd_dmactl->burst4 = devm_regmap_field_alloc(dev, map, v->cif_rddma_burst4);
	rd_dmactl->burst8 = devm_regmap_field_alloc(dev, map, v->cif_rddma_burst8);
	rd_dmactl->burst16 = devm_regmap_field_alloc(dev, map, v->cif_rddma_burst16);
	rd_dmactl->dma_dyncclk = devm_regmap_field_alloc(dev, map, v->cif_rddma_dma_dyncclk);
	rd_dmactl->num_ot = devm_regmap_field_alloc(dev, map, v->cif_rddma_num_ot);

	/* INTF_CFG register fields */
	rd_intfctl->en_16bit_unpack =
		devm_regmap_field_alloc(dev, map, v->cif_rddma_en_16bit_unpack);
	rd_intfctl->intf_dyncclk = devm_regmap_field_alloc(dev, map, v->cif_rddma_intf_dyncclk);
	rd_intfctl->fs_out_gate = devm_regmap_field_alloc(dev, map, v->cif_rddma_fs_out_gate);
	rd_intfctl->fs_sel = devm_regmap_field_alloc(dev, map, v->cif_rddma_fs_sel);
	rd_intfctl->fs_delay = devm_regmap_field_alloc(dev, map, v->cif_rddma_fs_delay);
	rd_intfctl->active_ch_en = devm_regmap_field_alloc(dev, map, v->cif_rddma_active_ch_en);

	/* ===================================================================
	 * Allocate WRDMA (TX/Capture) regmap fields for all 4 channels
	 * ===================================================================
	 */

	/* CTL register fields */
	wr_dmactl->enable = devm_regmap_field_alloc(dev, map, v->cif_wrdma_enable);
	wr_dmactl->reset = devm_regmap_field_alloc(dev, map, v->cif_wrdma_reset);

	/* CFG register fields */
	wr_dmactl->shram_wm = devm_regmap_field_alloc(dev, map, v->cif_wrdma_shram_wm);
	wr_dmactl->burst1 = devm_regmap_field_alloc(dev, map, v->cif_wrdma_burst1);
	wr_dmactl->burst2 = devm_regmap_field_alloc(dev, map, v->cif_wrdma_burst2);
	wr_dmactl->burst4 = devm_regmap_field_alloc(dev, map, v->cif_wrdma_burst4);
	wr_dmactl->burst8 = devm_regmap_field_alloc(dev, map, v->cif_wrdma_burst8);
	wr_dmactl->burst16 = devm_regmap_field_alloc(dev, map, v->cif_wrdma_burst16);
	wr_dmactl->dma_dyncclk = devm_regmap_field_alloc(dev, map, v->cif_wrdma_dma_dyncclk);
	wr_dmactl->num_ot = devm_regmap_field_alloc(dev, map, v->cif_wrdma_num_ot);

	/* INTF_CFG register fields */
	wr_intfctl->en_16bit_unpack =
		devm_regmap_field_alloc(dev, map, v->cif_wrdma_en_16bit_unpack);
	wr_intfctl->intf_dyncclk = devm_regmap_field_alloc(dev, map, v->cif_wrdma_intf_dyncclk);
	wr_intfctl->fs_out_gate = devm_regmap_field_alloc(dev, map, v->cif_wrdma_fs_out_gate);
	wr_intfctl->fs_sel = devm_regmap_field_alloc(dev, map, v->cif_wrdma_fs_sel);
	wr_intfctl->fs_delay = devm_regmap_field_alloc(dev, map, v->cif_wrdma_fs_delay);
	wr_intfctl->active_ch_en = devm_regmap_field_alloc(dev, map, v->cif_wrdma_active_ch_en);

	/* ===================================================================
	 * Check for allocation errors
	 * ===================================================================
	 */
	if (IS_ERR(rd_dmactl->enable) || IS_ERR(wr_dmactl->enable) ||
	    IS_ERR(rd_dmactl->reset) || IS_ERR(wr_dmactl->reset) ||
	    IS_ERR(rd_dmactl->num_ot) || IS_ERR(wr_dmactl->num_ot) ||
	    IS_ERR(rd_dmactl->dma_dyncclk) || IS_ERR(wr_dmactl->dma_dyncclk) ||
	    IS_ERR(rd_dmactl->burst16) || IS_ERR(wr_dmactl->burst16) ||
	    IS_ERR(rd_dmactl->burst8) || IS_ERR(wr_dmactl->burst8) ||
	    IS_ERR(rd_dmactl->burst4) || IS_ERR(wr_dmactl->burst4) ||
	    IS_ERR(rd_dmactl->burst2) || IS_ERR(wr_dmactl->burst2) ||
	    IS_ERR(rd_dmactl->burst1) || IS_ERR(wr_dmactl->burst1) ||
	    IS_ERR(rd_dmactl->shram_wm) || IS_ERR(wr_dmactl->shram_wm) ||
	    IS_ERR(rd_intfctl->active_ch_en) || IS_ERR(wr_intfctl->active_ch_en) ||
	    IS_ERR(rd_intfctl->fs_sel) || IS_ERR(wr_intfctl->fs_sel) ||
	    IS_ERR(rd_intfctl->fs_delay) || IS_ERR(wr_intfctl->fs_delay) ||
	    IS_ERR(rd_intfctl->fs_out_gate) || IS_ERR(wr_intfctl->fs_out_gate) ||
	    IS_ERR(rd_intfctl->intf_dyncclk) || IS_ERR(wr_intfctl->intf_dyncclk) ||
	    IS_ERR(rd_intfctl->en_16bit_unpack) || IS_ERR(wr_intfctl->en_16bit_unpack)) {
		dev_err(dev, "error allocating codec dma regmap fields\n");
		return -EINVAL;
	}

	/* Store in variant data */
	v->cif_rd_dmactl = rd_dmactl;
	v->cif_wr_dmactl = wr_dmactl;
	v->cif_rddma_intfctl = rd_intfctl;
	v->cif_wrdma_intfctl = wr_intfctl;

	return 0;
}

static struct qaif_cdc_intfctl *
qaif_get_cif_intfctl_handle(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	struct qaif_variant *v = drvdata->variant;
	unsigned int dai_id = cpu_dai->driver->id;
	struct qaif_cdc_intfctl *intfctl = NULL;

	if (!v) {
		dev_err(soc_runtime->dev, "No variant data\n");
		return intfctl;
	}

	switch (dai_id) {
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
		intfctl = v->cif_rddma_intfctl;
		break;
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		intfctl = v->cif_wrdma_intfctl;
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
	struct qaif_variant *v = drvdata->variant;
	struct qaif_cdc_intfctl *intfctl = NULL;
	unsigned int dai_id = cpu_dai->driver->id;
	unsigned int ret, regval;
	unsigned int channels = params_channels(params);
	int idx;

	pr_err("%s:%d: dai_id=%u stream=%d channels=%u rate=%u\n",
	       __func__, __LINE__, dai_id, substream->stream, channels,
	       params_rate(params));

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
	//active channel mask
	ret = regmap_fields_write(intfctl->active_ch_en, idx, regval);
	if (ret) {
		dev_err(soc_runtime->dev,
			"error writing to intfctl active_ch_en reg field: %d\n", ret);
		return ret;
	}
	pr_err("%s:%d: configured active_ch_en idx=%d val=0x%x\n",
	       __func__, __LINE__, idx, regval);

	return 0;
}

static int qaif_cif_daiops_trigger(struct snd_pcm_substream *substream,
				   int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	struct qaif_variant *v = drvdata->variant;
	unsigned int dai_id = cpu_dai->driver->id;
	struct qaif_cdc_intfctl *intfctl = NULL;
	int ret = 0, idx;

	pr_err("%s:%d: dai_id=%u stream=%d cmd=%d\n",
	       __func__, __LINE__, dai_id, substream->stream, cmd);

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
		ret = regmap_fields_write(intfctl->intf_dyncclk, idx, 1);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dmactl intf_dyncclk reg field: %d\n",
				ret);
			return ret;
		}
		/* ToDo: Hardcoded for now, Later to modify dynamically */
		ret = regmap_fields_write(intfctl->fs_sel, idx, 0x0);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dmactl codec_fs_sel reg field: %d\n",
				ret);
			return ret;
		}

		ret = regmap_fields_write(intfctl->en_16bit_unpack, idx, 0x1);
		/* ToDo: based on bw and packing enable flag */
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dmactl en_16bit_unpack reg field: %d\n",
				ret);
			return ret;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = regmap_fields_write(intfctl->intf_dyncclk, idx, 0);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dmactl intf_dyncclk reg field: %d\n",
				ret);
			return ret;
		}
		ret = regmap_fields_write(intfctl->en_16bit_unpack, idx, 0);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dmactl en_16bit_unpack reg field: %d\n",
				ret);
			return ret;
		}
		break;
	default:
		ret = -EINVAL;
		dev_err(soc_runtime->dev, "%s: invalid %d interface\n", __func__, cmd);
		break;
	}
	pr_err("%s:%d: cmd=%d ret=%d idx=%d\n",
	       __func__, __LINE__, cmd, ret, idx);
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
	struct qaif_variant *v = drvdata->variant;
	struct qaif_aud_intfctl *aif_intfctl;

	/* Allocate AIF interface control structure */
	aif_intfctl = devm_kzalloc(dev, sizeof(struct qaif_aud_intfctl), GFP_KERNEL);
	if (!aif_intfctl)
		return -ENOMEM;

	/* ===================================================================
	 * Allocate regmap fields for AUD_INTF SYNC_CFG register
	 * ===================================================================
	 */
	aif_intfctl->inv_sync = devm_regmap_field_alloc(dev, map, v->aif_inv_sync);
	aif_intfctl->sync_delay = devm_regmap_field_alloc(dev, map, v->aif_sync_delay);
	aif_intfctl->sync_mode = devm_regmap_field_alloc(dev, map, v->aif_sync_mode);
	aif_intfctl->sync_src = devm_regmap_field_alloc(dev, map, v->aif_sync_src);

	/* ===================================================================
	 * Allocate regmap fields for AUD_INTF BIT_WIDTH_CFG register
	 * ===================================================================
	 */
	aif_intfctl->slot_width_rx = devm_regmap_field_alloc(dev, map, v->aif_slot_width_rx);
	aif_intfctl->slot_width_tx = devm_regmap_field_alloc(dev, map, v->aif_slot_width_tx);
	aif_intfctl->sample_width_rx = devm_regmap_field_alloc(dev, map, v->aif_sample_width_rx);
	aif_intfctl->sample_width_tx = devm_regmap_field_alloc(dev, map, v->aif_sample_width_tx);

	/* ===================================================================
	 * Allocate regmap fields for AUD_INTF MI2S_CFG register
	 * ===================================================================
	 */
	aif_intfctl->mono_mode_rx = devm_regmap_field_alloc(dev, map, v->aif_mono_mode_rx);
	aif_intfctl->mono_mode_tx = devm_regmap_field_alloc(dev, map, v->aif_mono_mode_tx);

	/* ===================================================================
	 * Allocate regmap fields for AUD_INTF LANE_CFG register
	 * ===================================================================
	 */
	aif_intfctl->lane_en = devm_regmap_field_alloc(dev, map, v->aif_lane_en);
	aif_intfctl->lane_dir = devm_regmap_field_alloc(dev, map, v->aif_lane_dir);
	aif_intfctl->loopback_en = devm_regmap_field_alloc(dev, map, v->aif_loopback_en);
	aif_intfctl->ctrl_data_oe = devm_regmap_field_alloc(dev, map, v->aif_ctrl_data_oe);

	/* ===================================================================
	 * Allocate regmap fields for AUD_INTF SLOT_EN registers
	 * ===================================================================
	 */
	aif_intfctl->slot_en_rx_mask = devm_regmap_field_alloc(dev, map, v->aif_slot_en_rx_mask);
	aif_intfctl->slot_en_tx_mask = devm_regmap_field_alloc(dev, map, v->aif_slot_en_tx_mask);

	/* ===================================================================
	 * Allocate regmap fields for AUD_INTF FRAME_CFG register
	 * ===================================================================
	 */
	aif_intfctl->bits_per_lane = devm_regmap_field_alloc(dev, map, v->aif_bits_per_lane);

	/* ===================================================================
	 * Allocate regmap fields for AUD_INTF CFG register
	 * ===================================================================
	 */
	aif_intfctl->full_cycle_en = devm_regmap_field_alloc(dev, map, v->aif_full_cycle_en);

	/* ===================================================================
	 * Check for allocation errors
	 * ===================================================================
	 */
	if (IS_ERR(aif_intfctl->inv_sync) || IS_ERR(aif_intfctl->sync_delay) ||
	    IS_ERR(aif_intfctl->sync_mode) || IS_ERR(aif_intfctl->sync_src) ||
	    IS_ERR(aif_intfctl->slot_width_rx) || IS_ERR(aif_intfctl->slot_width_tx) ||
	    IS_ERR(aif_intfctl->sample_width_rx) || IS_ERR(aif_intfctl->sample_width_tx) ||
	    IS_ERR(aif_intfctl->mono_mode_rx) || IS_ERR(aif_intfctl->mono_mode_tx) ||
	    IS_ERR(aif_intfctl->lane_en) || IS_ERR(aif_intfctl->lane_dir) ||
	    IS_ERR(aif_intfctl->loopback_en) || IS_ERR(aif_intfctl->ctrl_data_oe) ||
	    IS_ERR(aif_intfctl->slot_en_rx_mask) || IS_ERR(aif_intfctl->slot_en_tx_mask) ||
	    IS_ERR(aif_intfctl->bits_per_lane) || IS_ERR(aif_intfctl->full_cycle_en)) {
		dev_err(dev, "error allocating AIF interface regmap fields\n");
		return -EINVAL;
	}

	/* Store in variant data */
	v->aif_intfctl = aif_intfctl;

	dev_info(dev, "Successfully initialized AIF interface control bitfields\n");
	return 0;
}

static int qaif_aif_cpu_init_bitfields(struct device *dev,
				       struct regmap *map)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);
	struct qaif_variant *v = drvdata->variant;
	struct qaif_dmactl *rd_dmactl;
	struct qaif_dmactl *wr_dmactl;

	/* Allocate RDDMA control structure */
	rd_dmactl = devm_kzalloc(dev, sizeof(struct qaif_dmactl), GFP_KERNEL);
	if (!rd_dmactl)
		return -ENOMEM;

	/* Allocate WRDMA control structure */
	wr_dmactl = devm_kzalloc(dev, sizeof(struct qaif_dmactl), GFP_KERNEL);
	if (!wr_dmactl)
		return -ENOMEM;

	/* ===================================================================
	 * Allocate RDDMA (RX/Playback) regmap fields for all 4 channels
	 * ===================================================================
	 */

	/* CTL register fields */
	rd_dmactl->enable = devm_regmap_field_alloc(dev, map, v->rddma_enable);
	rd_dmactl->reset = devm_regmap_field_alloc(dev, map, v->rddma_reset);

	/* CFG register fields */
	rd_dmactl->shram_wm = devm_regmap_field_alloc(dev, map, v->rddma_shram_wm);
	rd_dmactl->burst1 = devm_regmap_field_alloc(dev, map, v->rddma_burst1);
	rd_dmactl->burst2 = devm_regmap_field_alloc(dev, map, v->rddma_burst2);
	rd_dmactl->burst4 = devm_regmap_field_alloc(dev, map, v->rddma_burst4);
	rd_dmactl->burst8 = devm_regmap_field_alloc(dev, map, v->rddma_burst8);
	rd_dmactl->burst16 = devm_regmap_field_alloc(dev, map, v->rddma_burst16);
	rd_dmactl->dma_dyncclk = devm_regmap_field_alloc(dev, map, v->rddma_dma_dyncclk);
	rd_dmactl->num_ot = devm_regmap_field_alloc(dev, map, v->rddma_num_ot);

	/* ===================================================================
	 * Allocate WRDMA (TX/Capture) regmap fields for all 4 channels
	 * ===================================================================
	 */

	/* CTL register fields */
	wr_dmactl->enable = devm_regmap_field_alloc(dev, map, v->wrdma_enable);
	wr_dmactl->reset = devm_regmap_field_alloc(dev, map, v->wrdma_reset);

	/* CFG register fields */
	wr_dmactl->shram_wm = devm_regmap_field_alloc(dev, map, v->wrdma_shram_wm);
	wr_dmactl->burst1 = devm_regmap_field_alloc(dev, map, v->wrdma_burst1);
	wr_dmactl->burst2 = devm_regmap_field_alloc(dev, map, v->wrdma_burst2);
	wr_dmactl->burst4 = devm_regmap_field_alloc(dev, map, v->wrdma_burst4);
	wr_dmactl->burst8 = devm_regmap_field_alloc(dev, map, v->wrdma_burst8);
	wr_dmactl->burst16 = devm_regmap_field_alloc(dev, map, v->wrdma_burst16);
	wr_dmactl->dma_dyncclk = devm_regmap_field_alloc(dev, map, v->wrdma_dma_dyncclk);
	wr_dmactl->num_ot = devm_regmap_field_alloc(dev, map, v->wrdma_num_ot);

	/* ===================================================================
	 * Check for allocation errors
	 * ===================================================================
	 */
	if (IS_ERR(rd_dmactl->enable) || IS_ERR(wr_dmactl->enable) ||
	    IS_ERR(rd_dmactl->reset) || IS_ERR(wr_dmactl->reset) ||
	    IS_ERR(rd_dmactl->num_ot) || IS_ERR(wr_dmactl->num_ot) ||
	    IS_ERR(rd_dmactl->dma_dyncclk) || IS_ERR(wr_dmactl->dma_dyncclk) ||
	    IS_ERR(rd_dmactl->burst16) || IS_ERR(wr_dmactl->burst16) ||
	    IS_ERR(rd_dmactl->burst8) || IS_ERR(wr_dmactl->burst8) ||
	    IS_ERR(rd_dmactl->burst4) || IS_ERR(wr_dmactl->burst4) ||
	    IS_ERR(rd_dmactl->burst2) || IS_ERR(wr_dmactl->burst2) ||
	    IS_ERR(rd_dmactl->burst1) || IS_ERR(wr_dmactl->burst1) ||
	    IS_ERR(rd_dmactl->shram_wm) || IS_ERR(wr_dmactl->shram_wm)) {
		dev_err(dev, "error allocating AIF dma regmap fields\n");
		return -EINVAL;
	}

	/* Store in variant data */
	v->aif_rd_dmactl = rd_dmactl;
	v->aif_wr_dmactl = wr_dmactl;

	return 0;
}

static int qaif_aif_cpu_daiops_set_sysclk(struct snd_soc_dai *dai, int clk_id,
					  unsigned int freq, int dir)
{
	return 0;
}

static int qaif_aif_cpu_daiops_startup(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	const struct qaif_variant *v = drvdata->variant;
	int idx, ret = 0;

	pr_err("%s:%d: dai_id=%d stream=%d\n",
	       __func__, __LINE__, dai->driver->id, substream->stream);

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
	pr_err("%s:%d: prepared bit_clk idx=%d\n",
	       __func__, __LINE__, idx);
	return 0;
}

static void qaif_aif_cpu_daiops_shutdown(struct snd_pcm_substream *substream,
					 struct snd_soc_dai *dai)
{
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	const struct qaif_variant *v = drvdata->variant;
	int idx = v->get_dma_idx(dai->driver->id);

	pr_err("%s:%d: dai_id=%d stream=%d\n",
	       __func__, __LINE__, dai->driver->id, substream->stream);

	if (idx < 0) {
		dev_err(dai->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		regmap_write(drvdata->audio_qaif_map,
			     QAIF_AUD_INTF_CTL_REG(idx), 0);
	else
		regmap_write(drvdata->audio_qaif_map,
			     QAIF_AUD_INTF_CTL_REG(idx), 0);

	if (drvdata->mi2s_was_prepared[idx]) {
		drvdata->mi2s_was_prepared[idx] = false;
		clk_disable(drvdata->mi2s_bit_clk[idx]);
	}

	clk_unprepare(drvdata->mi2s_bit_clk[idx]);
}

static int qaif_aif_cpu_daiops_trigger(struct snd_pcm_substream *substream,
				       int cmd, struct snd_soc_dai *dai)
{
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	const struct qaif_variant *v = drvdata->variant;
	int idx, ret = -EINVAL;

	idx = v->get_dma_idx(dai->driver->id);

	if (idx < 0) {
		dev_err(dai->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return -EINVAL;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			ret = regmap_write(drvdata->audio_qaif_map,
					   QAIF_AUD_INTF_CTL_REG(idx), QAIF_AUD_INTF_CTL_ENABLE_TX);
		} else {
			ret = regmap_write(drvdata->audio_qaif_map,
					   QAIF_AUD_INTF_CTL_REG(idx), QAIF_AUD_INTF_CTL_ENABLE_RX);
		}
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

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			ret = regmap_write(drvdata->audio_qaif_map,
					   QAIF_AUD_INTF_CTL_REG(idx), 0);
		} else {
			ret = regmap_write(drvdata->audio_qaif_map,
					   QAIF_AUD_INTF_CTL_REG(idx), 0);
		}
		if (ret)
			dev_err(dai->dev, "error writing to AIF CTL reg: %d\n", ret);

		clk_disable(drvdata->mi2s_bit_clk[idx]);

		break;
	}

	return ret;
}

static int qaif_aif_cpu_daiops_prepare(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	return 0;
}

static int qaif_aif_cpu_daiops_probe(struct snd_soc_dai *dai)
{
	return 0;
}

static int qaif_aif_cpu_daiops_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *params,
						 struct snd_soc_dai *dai)
{
	struct qaif_drv_data *drvdata = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct qaif_variant *v = drvdata->variant;
	unsigned int idx;
	struct qaif_aif_config *aif_intf_cfg = NULL;
	snd_pcm_format_t format = params_format(params);
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	unsigned int slot_width = 32;
	int bitwidth, ret;
	u32 sync_cfg_val, lane_cfg_val, mi2s_cfg_val, frame_cfg_val;
	u32 tx_bw_fields, rx_bw_fields, bit_width_cfg_val;

	idx = v->get_dma_idx(dai->driver->id);

	if (idx < 0) {
		dev_err(dai->dev, "%s: Invalid DMA index: %d\n", __func__, idx);
		return -EINVAL;
	}

	aif_intf_cfg = &v->aif_intf_cfg[idx];

	if (!aif_intf_cfg) {
		dev_err(dai->dev, "AIF interface config not found\n");
		return -EINVAL;
	}
	bitwidth = snd_pcm_format_width(format);
	if (bitwidth < 0) {
		dev_err(dai->dev, "invalid bit width given: %d\n", bitwidth);
		return bitwidth;
	}

	/* Combine all fields into single value */
	sync_cfg_val = ((aif_intf_cfg->invert_sync << QAIF_AUD_INTF_SYNC_CFG_INV_SYNC_SHFT) &
					QAIF_AUD_INTF_SYNC_CFG_INV_SYNC_MASK) |
				((aif_intf_cfg->sync_delay <<
				QAIF_AUD_INTF_SYNC_CFG_SYNC_DELAY_SHFT) &
					QAIF_AUD_INTF_SYNC_CFG_SYNC_DELAY_MASK) |
				((aif_intf_cfg->sync_mode <<
				QAIF_AUD_INTF_SYNC_CFG_SYNC_MODE_SHFT) &
					QAIF_AUD_INTF_SYNC_CFG_SYNC_MODE_MASK) |
				((aif_intf_cfg->sync_src << QAIF_AUD_INTF_SYNC_CFG_SYNC_SRC_SHFT) &
					QAIF_AUD_INTF_SYNC_CFG_SYNC_SRC_MASK);

	ret = regmap_write(drvdata->audio_qaif_map,
			   QAIF_AUD_INTF_SYNC_CFG_REG(idx), sync_cfg_val);
	if (ret) {
		dev_err(dai->dev, "Failed to write QAIF_AUD_INTF_SYNC_CFG_REG: %d\n", ret);
		return ret;
	}

	lane_cfg_val = ((aif_intf_cfg->loopback_en << QAIF_AUD_INTF_LANE_CFG_LOOPBACK_SHFT) &
					QAIF_AUD_INTF_LANE_CFG_LOOPBACK_MASK) |
				((aif_intf_cfg->ctrl_data_oe <<
				QAIF_AUD_INTF_LANE_CFG_CTRL_DATA_OE_SHFT) &
					QAIF_AUD_INTF_LANE_CFG_CTRL_DATA_OE_MASK) |
				((aif_intf_cfg->lane_en_mask <<
				QAIF_AUD_INTF_LANE_CFG_LANE_EN_SHFT) &
					QAIF_AUD_INTF_LANE_CFG_LANE_EN_MASK) |
				((aif_intf_cfg->lane_dir_mask <<
				QAIF_AUD_INTF_LANE_CFG_LANE_DIR_SHFT) &
					QAIF_AUD_INTF_LANE_CFG_LANE_DIR_MASK);

	ret = regmap_write(drvdata->audio_qaif_map,
			   QAIF_AUD_INTF_LANE_CFG_REG(idx), lane_cfg_val);
	if (ret) {
		dev_err(dai->dev, "Failed to write QAIF_AUD_INTF_LANE_CFG_REG: %d\n", ret);
		return ret;
	}

	ret = regmap_write(drvdata->audio_qaif_map,
			   QAIF_AUD_INTF_CFG_REG(idx), aif_intf_cfg->full_cycle_en);
	if (ret) {
		dev_err(dai->dev, "Failed to write QAIF_AUD_INTF_CFG_REG: %d\n", ret);
		return ret;
	}
	dev_dbg(dai->dev, "%s: sync_cfg_val: %x, lane_cfg_val: %x, full_cycle_en: %x\n",
		__func__, sync_cfg_val, lane_cfg_val, aif_intf_cfg->full_cycle_en);

	ret = regmap_read(drvdata->audio_qaif_map,
			  QAIF_AUD_INTF_BIT_WIDTH_CFG_REG(idx),
		&bit_width_cfg_val);
	if (ret) {
		dev_err(dai->dev, "Failed to read QAIF_AUD_INTF_BIT_WIDTH_CFG_REG: %d\n", ret);
		return ret;
	}

	ret = regmap_read(drvdata->audio_qaif_map,
			  QAIF_AUD_INTF_MI2S_CFG_REG(idx), &mi2s_cfg_val);
	if (ret) {
		dev_err(dai->dev, "Failed to read QAIF_AUD_INTF_MI2S_CFG_REG: %d\n", ret);
		return ret;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		slot_width = aif_intf_cfg->slot_width_tx;
		/* Prepare TX field values */
		tx_bw_fields =
			((QAIF_AIF_SAMPLE_WIDTH(bitwidth) <<
			QAIF_AUD_INTF_BIT_WIDTH_CFG_SAMPLE_WIDTH_TX_SHFT) &
					QAIF_AUD_INTF_BIT_WIDTH_CFG_SAMPLE_WIDTH_TX_MASK) |
					((QAIF_AIF_SLOT_WIDTH(slot_width) <<
					QAIF_AUD_INTF_BIT_WIDTH_CFG_SLOT_WIDTH_TX_SHFT) &
					QAIF_AUD_INTF_BIT_WIDTH_CFG_SLOT_WIDTH_TX_MASK);

		/* Clear TX fields, preserve RX fields, write new TX values */
		bit_width_cfg_val =
			(bit_width_cfg_val &
			~QAIF_AUD_INTF_BIT_WIDTH_CFG_TX_FIELDS_MASK) |
			tx_bw_fields;

		ret = regmap_write(drvdata->audio_qaif_map,
				   QAIF_AUD_INTF_BIT_WIDTH_CFG_REG(idx), bit_width_cfg_val);
		if (ret) {
			dev_err(dai->dev,
				"Write to read QAIF_AUD_INTF_BIT_WIDTH_CFG_REG: %d\n",
				ret);
			return ret;
		}

		ret = regmap_write(drvdata->audio_qaif_map,
				   QAIF_AUD_INTF_ACTV_SLOT_EN_TX_REG(idx),
			aif_intf_cfg->slot_en_tx_mask);
		if (ret) {
			dev_err(dai->dev,
				"Write to read QAIF_AUD_INTF_ACTV_SLOT_EN_TX_REG: %d\n",
				ret);
			return ret;
		}

		frame_cfg_val = (slot_width * aif_intf_cfg->bits_per_lane) - 1;
		ret = regmap_write(drvdata->audio_qaif_map,
				   QAIF_AUD_INTF_FRAME_CFG_REG(idx), frame_cfg_val);
		if (ret) {
			dev_err(dai->dev, "Failed to write QAIF_AUD_INTF_FRAME_CFG_REG: %d\n", ret);
			return ret;
		}

		/* Clear TX field, preserve RX field */
		mi2s_cfg_val &= ~QAIF_AUD_INTF_MI2S_CFG_TX_FIELDS_MASK;
		if (channels >= 2) {
			/* Set new TX mono mode value */
			mi2s_cfg_val |=
				((QAIF_AUD_INTF_CTL_STEREO <<
				QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_TX_SHFT) &
				QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_TX_MASK);
		} else {
			/* Set new TX mono mode value */
			mi2s_cfg_val |=
				((QAIF_AUD_INTF_CTL_MONO <<
				QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_TX_SHFT) &
				QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_TX_MASK);
		}

		ret = regmap_write(drvdata->audio_qaif_map,
				   QAIF_AUD_INTF_MI2S_CFG_REG(idx),
			mi2s_cfg_val);
		if (ret) {
			dev_err(dai->dev, "Write to read QAIF_AUD_INTF_MI2S_CFG_REG: %d\n", ret);
			return ret;
		}
		dev_err(dai->dev, "%s: TX bw_cfg=%x slot_en=%x frame=%x mi2s=%x\n",
			__func__, bit_width_cfg_val,
			aif_intf_cfg->slot_en_tx_mask,
			frame_cfg_val, mi2s_cfg_val);
	} else {
		slot_width = aif_intf_cfg->slot_width_tx;
		/* Prepare RX field values */
		rx_bw_fields =
			((QAIF_AIF_SAMPLE_WIDTH(bitwidth) <<
			QAIF_AUD_INTF_BIT_WIDTH_CFG_SAMPLE_WIDTH_RX_SHFT) &
					QAIF_AUD_INTF_BIT_WIDTH_CFG_SAMPLE_WIDTH_RX_MASK) |
					((QAIF_AIF_SLOT_WIDTH(slot_width) <<
					QAIF_AUD_INTF_BIT_WIDTH_CFG_SLOT_WIDTH_RX_SHFT) &
					QAIF_AUD_INTF_BIT_WIDTH_CFG_SLOT_WIDTH_RX_MASK);

		/* Clear RX fields, preserve TX fields, write new RX values */
		bit_width_cfg_val =
			(bit_width_cfg_val &
			~QAIF_AUD_INTF_BIT_WIDTH_CFG_RX_FIELDS_MASK) |
			rx_bw_fields;

		ret = regmap_write(drvdata->audio_qaif_map,
				   QAIF_AUD_INTF_BIT_WIDTH_CFG_REG(idx), bit_width_cfg_val);
		if (ret) {
			dev_err(dai->dev,
				"Write to read QAIF_AUD_INTF_BIT_WIDTH_CFG_REG: %d\n",
				ret);
			return ret;
		}

		ret = regmap_write(drvdata->audio_qaif_map,
				   QAIF_AUD_INTF_ACTV_SLOT_EN_RX_REG(idx),
			aif_intf_cfg->slot_en_rx_mask);
		if (ret) {
			dev_err(dai->dev,
				"Write to read QAIF_AUD_INTF_ACTV_SLOT_EN_RX_REG: %d\n",
				ret);
			return ret;
		}

		frame_cfg_val = (slot_width * aif_intf_cfg->bits_per_lane) - 1;
		ret = regmap_write(drvdata->audio_qaif_map,
				   QAIF_AUD_INTF_FRAME_CFG_REG(idx), frame_cfg_val);
		if (ret) {
			dev_err(dai->dev, "Failed to write QAIF_AUD_INTF_FRAME_CFG_REG: %d\n", ret);
			return ret;
		}

		/* Clear RX field, preserve TX field */
		mi2s_cfg_val &= ~QAIF_AUD_INTF_MI2S_CFG_RX_FIELDS_MASK;
		if (channels >= 2) {
			/* Set new RX mono mode value */
			mi2s_cfg_val |=
				((QAIF_AUD_INTF_CTL_STEREO <<
				QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_RX_SHFT) &
				QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_RX_MASK);
		} else {
			/* Set new RX mono mode value */
			mi2s_cfg_val |=
				((QAIF_AUD_INTF_CTL_MONO <<
				QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_RX_SHFT) &
				QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_RX_MASK);
		}
		ret = regmap_write(drvdata->audio_qaif_map,
				   QAIF_AUD_INTF_MI2S_CFG_REG(idx),
			mi2s_cfg_val);
		if (ret) {
			dev_err(dai->dev, "Write to read QAIF_AUD_INTF_MI2S_CFG_REG: %d\n", ret);
			return ret;
		}
		dev_err(dai->dev, "%s: RX bw_cfg=%x slot_en=%x frame=%x mi2s=%x\n",
			__func__, bit_width_cfg_val,
			aif_intf_cfg->slot_en_rx_mask,
			frame_cfg_val, mi2s_cfg_val);
	}

	if (ret) {
		dev_err(dai->dev, "error writing to aif_intfctl channels mode: %d\n",
			ret);
		return ret;
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

	if (!drvdata->mi2s_was_prepared[idx]) {
		ret = clk_enable(drvdata->mi2s_bit_clk[idx]);
		if (ret) {
			dev_err(dai->dev, "error in enabling mi2s bit clk: %d\n", ret);
			return ret;
		}
		drvdata->mi2s_was_prepared[idx] = true;

		dev_dbg(rtd->card->dev,	"%s: substream = %s  stream = %d\n",
			__func__, substream->name, substream->stream);
			snd_soc_dai_set_tdm_slot(codec_dai, 0x0f, 0b11,
						 aif_intf_cfg->bits_per_lane, slot_width);
			snd_soc_dai_set_sysclk(codec_dai, 0,
					       rate * aif_intf_cfg->bits_per_lane * slot_width,
				0);
	}

	return 0;
}

const struct snd_soc_dai_ops asoc_qcom_qaif_aif_cpu_dai_ops = {
	.probe		= qaif_aif_cpu_daiops_probe,
	.set_sysclk	= qaif_aif_cpu_daiops_set_sysclk,
	.startup	= qaif_aif_cpu_daiops_startup,
	.shutdown	= qaif_aif_cpu_daiops_shutdown,
	.hw_params	= qaif_aif_cpu_daiops_hw_params,
	.trigger	= qaif_aif_cpu_daiops_trigger,
	.prepare	= qaif_aif_cpu_daiops_prepare,
};
EXPORT_SYMBOL_GPL(asoc_qcom_qaif_aif_cpu_dai_ops);

static int asoc_qcom_of_xlate_dai_name(struct snd_soc_component *component,
				       const struct of_phandle_args *args,
				   const char **dai_name)
{
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	struct qaif_variant *v = drvdata->variant;
	int id = args->args[0];
	int ret = -EINVAL;
	int i;

	for (i = 0; i  < v->num_dai; i++) {
		if (v->dai_driver[i].id == id) {
			*dai_name = v->dai_driver[i].name;
			ret = 0;
			break;
		}
	}

	return ret;
}

static const struct snd_soc_component_driver qaif_cpu_comp_driver = {
	.name = "qaif-cpu",
	.of_xlate_dai_name = asoc_qcom_of_xlate_dai_name,
	.legacy_dai_naming = 1,
};

static bool __audio_qaif_regmap_accessible(struct device *dev, unsigned int reg, bool rw)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);
	struct qaif_variant *v = drvdata->variant;
	int i;

	if (reg == QAIF_EE_OVERLAP_IRQ_EN_REG)
		return true;
	if (reg == QAIF_EE_OVERLAP_IRQ_RAW_STATUS_REG)
		return true;
	if (reg == QAIF_EE_OVERLAP_IRQ_CLEAR_REG)
		return true;
	if (reg == QAIF_EE_OVERLAP_IRQ_FORCE_REG)
		return true;

	for (i = 0; i < DMA_TYPE_MAX; i++) {
		//RDDMA IRQ
		if (reg == QAIF_EE_RDDMA_PERIOD_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_PERIOD_IRQ_FORCE_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_UNDERFLOW_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_UNDERFLOW_IRQ_FORCE_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_ERR_RSP_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_ERR_RSP_IRQ_FORCE_REG(v, i))
			return true;

		//WRDMA IRQ
		if (reg == QAIF_EE_WRDMA_PERIOD_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_PERIOD_IRQ_FORCE_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_OVERFLOW_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_OVERFLOW_IRQ_FORCE_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_ERR_RSP_IRQ_EN_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_ERR_RSP_IRQ_FORCE_REG(v, i))
			return true;
	}

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
		if (rw == QAIF_AIF_REG_READ) {
			if (reg == QAIF_RDDMA_CURR_ADDR_REG(v, i))
				return true;
			if (reg == QAIF_RDDMA_PERIOD_CNT_REG(v, i))
				return true;
		}
	}

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
		if (rw == QAIF_AIF_REG_READ) {
			if (reg == QAIF_WRDMA_CURR_ADDR_REG(v, i))
				return true;
			if (reg == QAIF_WRDMA_PERIOD_CNT_REG(v, i))
				return true;
		}
	}

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
		if (rw == QAIF_AIF_REG_READ) {
			if (reg == QAIF_CODEC_RDDMA_CURR_ADDR_REG(v, i))
				return true;
			if (reg == QAIF_CODEC_RDDMA_PERIOD_CNT_REG(v, i))
				return true;
		}
	}

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
		if (rw == QAIF_AIF_REG_READ) {
			if (reg == QAIF_CODEC_WRDMA_CURR_ADDR_REG(v, i))
				return true;
			if (reg == QAIF_CODEC_WRDMA_PERIOD_CNT_REG(v, i))
				return true;
		}
	}
	return true;
}

static bool audio_qaif_regmap_writeable(struct device *dev, unsigned int reg)
{
	return __audio_qaif_regmap_accessible(dev, reg, QAIF_AIF_REG_WRITE);
}

static bool audio_qaif_regmap_readable(struct device *dev, unsigned int reg)
{
	return __audio_qaif_regmap_accessible(dev, reg, QAIF_AIF_REG_READ);
}

static bool audio_qaif_regmap_volatile(struct device *dev, unsigned int reg)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);
	struct qaif_variant *v = drvdata->variant;
	int i;

	for (i = 0; i < DMA_TYPE_MAX; i++) {
		//RDDMA IRQ
		if (reg == QAIF_EE_RDDMA_PERIOD_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_PERIOD_IRQ_RAW_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_PERIOD_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_UNDERFLOW_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_UNDERFLOW_IRQ_RAW_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_UNDERFLOW_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_ERR_RSP_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_ERR_RSP_IRQ_RAW_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_RDDMA_ERR_RSP_IRQ_CLR_REG(v, i))
			return true;

		//WRDMA IRQ
		if (reg == QAIF_EE_WRDMA_PERIOD_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_PERIOD_IRQ_RAW_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_PERIOD_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_OVERFLOW_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_OVERFLOW_IRQ_RAW_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_OVERFLOW_IRQ_CLR_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_ERR_RSP_IRQ_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_ERR_RSP_IRQ_RAW_STAT_REG(v, i))
			return true;
		if (reg == QAIF_EE_WRDMA_ERR_RSP_IRQ_CLR_REG(v, i))
			return true;
	}

	for (i = 0; i < v->num_rddma; i++)
		if (reg == QAIF_RDDMA_CURR_ADDR_REG(v, i))
			return true;

	for (i = 0; i < v->num_wrdma; i++)
		if (reg == QAIF_WRDMA_CURR_ADDR_REG(v, i))
			return true;

	for (i = 0; i < v->num_codec_rddma; i++)
		if (reg == QAIF_CODEC_RDDMA_CURR_ADDR_REG(v, i))
			return true;

	for (i = 0; i < v->num_codec_wrdma; i++)
		if (reg == QAIF_CODEC_WRDMA_CURR_ADDR_REG(v, i))
			return true;

	return true;
}

static struct regmap_config audio_qaif_regmap_config = {
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
	struct qaif_variant *v = data->variant;
	struct device_node *np = dev->of_node;
	struct device_node *intf_np;
	struct qaif_aif_config *cfg;
	const __be32 *lane_cfg_prop;
	int num_interfaces, ret, i, j;
	int lane_cfg_len;
	int dai_id, intf_idx;

	if (!v) {
		dev_err(dev, "No variant data\n");
		return -EINVAL;
	}
	/* Get count of interface phandles from aif-interface property */
	num_interfaces = of_count_phandle_with_args(np, "aif-interface", NULL);
	if (num_interfaces <= 0) {
		dev_err(dev, "No aif-interface property found or invalid: %d\n", num_interfaces);
		return -EINVAL;
	}

	if (num_interfaces > QAIF_MAX_AIF_CFG_CNT) {
		dev_warn(dev, "Too many interfaces (%d), limiting to %d\n",
			 num_interfaces, QAIF_MAX_AIF_CFG_CNT);
		num_interfaces = QAIF_MAX_AIF_CFG_CNT;
	}

	dev_info(dev, "Found %d AIF interfaces to parse\n", num_interfaces);

	/* Parse each interface node */
	for (i = 0; i < num_interfaces; i++) {
		intf_np = of_parse_phandle(np, "aif-interface", i);
		if (!intf_np) {
			dev_err(dev, "Failed to get interface node %d\n", i);
			continue;
		}

		dev_dbg(dev, "Parsing interface %d: %s\n", i, intf_np->name);

		ret = of_property_read_u32(intf_np, "qcom,qaif-intf-dai-id", &dai_id);
		if (ret) {
			dev_err(dev, "Missing dai-id for interface %d: %s ===\n", i, intf_np->name);
			continue;
		}

		if (v->get_dma_idx) {
			intf_idx = v->get_dma_idx(dai_id);
			if (intf_idx < 0) {
				dev_err(dev,
					"invalid intf idx for : %d: %s ===\n",
					i, intf_np->name);
				continue;
			}
		} else {
			dev_err(dev, "can not get intf idx for : %d: %s ===\n", i, intf_np->name);
			return -EINVAL;
		}
		cfg = &v->aif_intf_cfg[intf_idx];

		/* Parse sync configuration */
		ret = of_property_read_u32(intf_np, "qcom,qaif-aif-sync-mode", &cfg->sync_mode);
		if (ret) {
			dev_warn(dev, "Missing sync-mode for interface %d\n", i);
			cfg->sync_mode = 0;
		}

		ret = of_property_read_u32(intf_np, "qcom,qaif-aif-sync-src", &cfg->sync_src);
		if (ret) {
			dev_warn(dev, "Missing sync-src for interface %d\n", i);
			cfg->sync_src = 0;
		}

		ret = of_property_read_u32(intf_np, "qcom,qaif-aif-invert-sync", &cfg->invert_sync);
		if (ret) {
			dev_warn(dev, "Missing invert-sync for interface %d\n", i);
			cfg->invert_sync = 0;
		}

		ret = of_property_read_u32(intf_np, "qcom,qaif-aif-sync-delay", &cfg->sync_delay);
		if (ret) {
			dev_warn(dev, "Missing sync-delay for interface %d\n", i);
			cfg->sync_delay = 0;
		}

		/* Parse slot and sample width configuration */
		ret = of_property_read_u32(intf_np,
					   "qcom,qaif-aif-slot-width-rx",
			&cfg->slot_width_rx);
		if (ret) {
			dev_warn(dev, "Missing slot-width-rx for interface %d\n", i);
			cfg->slot_width_rx = 0;
		}

		ret = of_property_read_u32(intf_np,
					   "qcom,qaif-aif-slot-width-tx",
			&cfg->slot_width_tx);
		if (ret) {
			dev_warn(dev, "Missing slot-width-tx for interface %d\n", i);
			cfg->slot_width_tx = 0;
		}

		ret = of_property_read_u32(intf_np,
					   "qcom,qaif-aif-sample-width-rx",
			&cfg->sample_width_rx);
		if (ret) {
			dev_warn(dev, "Missing sample-width-rx for interface %d\n", i);
			cfg->sample_width_rx = 0;
		}

		ret = of_property_read_u32(intf_np,
					   "qcom,qaif-aif-sample-width-tx",
			&cfg->sample_width_tx);
		if (ret) {
			dev_warn(dev, "Missing sample-width-tx for interface %d\n", i);
			cfg->sample_width_tx = 0;
		}

		/* Parse slot enable masks */
		ret = of_property_read_u32(intf_np,
					   "qcom,qaif-aif-slot-en-rx-mask",
			&cfg->slot_en_rx_mask);
		if (ret) {
			dev_warn(dev, "Missing slot-en-rx-mask for interface %d\n", i);
			cfg->slot_en_rx_mask = 0;
		}

		ret = of_property_read_u32(intf_np,
					   "qcom,qaif-aif-slot-en-tx-mask",
			&cfg->slot_en_tx_mask);
		if (ret) {
			dev_warn(dev, "Missing slot-en-tx-mask for interface %d\n", i);
			cfg->slot_en_tx_mask = 0;
		}

		/* Parse control configuration */
		ret = of_property_read_u32(intf_np, "qcom,qaif-aif-loopback-en", &cfg->loopback_en);
		if (ret) {
			dev_warn(dev, "Missing loopback-en for interface %d\n", i);
			cfg->loopback_en = 0;
		}

		ret = of_property_read_u32(intf_np,
					   "qcom,qaif-aif-ctrl-data-oe",
			&cfg->ctrl_data_oe);
		if (ret) {
			dev_warn(dev, "Missing ctrl-data-oe for interface %d\n", i);
			cfg->ctrl_data_oe = 0;
		}

		/* Parse lane configuration */
		lane_cfg_prop = of_get_property(intf_np,
						"qcom,qaif-aif-lane-config",
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
				cfg->lane_cfg[j].enable =
					be32_to_cpup(lane_cfg_prop + (j * 2));
				if (cfg->lane_cfg[j].enable)
					cfg->lane_en_mask |= BIT(j);
					/* Set bit j for lane enable */

				cfg->lane_cfg[j].direction =
					be32_to_cpup(lane_cfg_prop + (j * 2 + 1));
				if (cfg->lane_cfg[j].direction)
					cfg->lane_dir_mask |= BIT(j);
					/* Set bit j for RX direction */
			}

		} else {
			dev_warn(dev, "Missing lane-config for interface %d\n", i);
			cfg->num_lanes = 0;
		}

		/* Parse mono/stereo mode */
		ret = of_property_read_u32(intf_np,
					   "qcom,qaif-aif-mono-mode-tx",
			&cfg->mono_mode_tx);
		if (ret) {
			dev_warn(dev, "Missing mono-mode-tx for interface %d\n", i);
			cfg->mono_mode_tx = 0;
		}

		ret = of_property_read_u32(intf_np,
					   "qcom,qaif-aif-mono-mode-rx",
			&cfg->mono_mode_rx);
		if (ret) {
			dev_warn(dev, "Missing mono-mode-rx for interface %d\n", i);
			cfg->mono_mode_rx = 0;
		}

		/* Parse frame configuration */
		ret = of_property_read_u32(intf_np,
					   "qcom,qaif-aif-full-cycle-en",
			&cfg->full_cycle_en);
		if (ret) {
			dev_warn(dev, "Missing full-cycle-en for interface %d\n", i);
			cfg->full_cycle_en = 0;
		}

		ret = of_property_read_u32(intf_np,
					   "qcom,qaif-aif-bits-per-lane",
			&cfg->bits_per_lane);
		if (ret) {
			dev_warn(dev, "Missing bits-per-lane for interface %d\n", i);
			cfg->bits_per_lane = 0;
		}

		/* Debug dump of parsed properties. */
		dev_dbg(dev, "Interface %d configuration:\n", i);
		dev_dbg(dev, "  Node name: %s\n", intf_np->name);

		/* Sync configuration */
		dev_dbg(dev, "  Sync Configuration:\n");
		dev_dbg(dev, "    sync_mode      = %u\n", cfg->sync_mode);
		dev_dbg(dev, "    sync_src       = %u\n", cfg->sync_src);
		dev_dbg(dev, "    invert_sync    = %u\n", cfg->invert_sync);
		dev_dbg(dev, "    sync_delay     = %u\n", cfg->sync_delay);

		/* Slot and sample width */
		dev_dbg(dev, "  Width Configuration:\n");
		dev_dbg(dev, "    slot_width_rx  = %u\n", cfg->slot_width_rx);
		dev_dbg(dev, "    slot_width_tx  = %u\n", cfg->slot_width_tx);
		dev_dbg(dev, "    sample_width_rx= %u\n", cfg->sample_width_rx);
		dev_dbg(dev, "    sample_width_tx= %u\n", cfg->sample_width_tx);

		/* Slot enable masks */
		dev_dbg(dev, "  Slot Enable Masks:\n");
		dev_dbg(dev, "    slot_en_rx_mask= 0x%08X\n", cfg->slot_en_rx_mask);
		dev_dbg(dev, "    slot_en_tx_mask= 0x%08X\n", cfg->slot_en_tx_mask);

		/* Control configuration */
		dev_dbg(dev, "  Control Configuration:\n");
		dev_dbg(dev, "    loopback_en    = %u\n", cfg->loopback_en);
		dev_dbg(dev, "    ctrl_data_oe   = %u\n", cfg->ctrl_data_oe);

		/* Lane configuration */
		dev_dbg(dev, "  Lane Configuration (num_lanes=%u):\n", cfg->num_lanes);
		for (j = 0; j < cfg->num_lanes; j++) {
			dev_dbg(dev, "    Lane %d: enable=%u, direction=%u (%s)\n",
				j, cfg->lane_cfg[j].enable, cfg->lane_cfg[j].direction,
				 cfg->lane_cfg[j].direction ? "RX_MIC" : "TX_SPKR");
		}
		dev_dbg(dev, "  Lane Configuration (lane_en_mask=0x%x):\n", cfg->lane_en_mask);
		dev_dbg(dev, "  Lane Configuration (lane_dir_mask=0x%x):\n", cfg->lane_dir_mask);

		/* Mono/Stereo mode */
		dev_dbg(dev, "  Mono/Stereo Mode:\n");
		dev_dbg(dev, "    mono_mode_tx   = %u (%s)\n",
			cfg->mono_mode_tx, cfg->mono_mode_tx ? "MONO" : "STEREO");
		dev_dbg(dev, "    mono_mode_rx   = %u (%s)\n",
			cfg->mono_mode_rx, cfg->mono_mode_rx ? "MONO" : "STEREO");

		/* Frame configuration */
		dev_dbg(dev, "  Frame Configuration:\n");
		dev_dbg(dev, "    full_cycle_en  = %u\n", cfg->full_cycle_en);
		dev_dbg(dev, "    bits_per_lane  = %u\n", cfg->bits_per_lane);

		dev_dbg(dev, "End interface %d\n", i);

		of_node_put(intf_np);
	}

	dev_info(dev, "Successfully parsed %d AIF interfaces\n", num_interfaces);
	return 0;
}

static int of_qaif_cdc_dma_clks_parse(struct device *dev,
				      struct qaif_drv_data *data)
{
	data->aud_dma_clk = devm_clk_get(dev, "audio_core_cc_aud_dma_clk");
	if (IS_ERR(data->aud_dma_clk))
		return PTR_ERR(data->aud_dma_clk);

	data->aud_dma_mem_clk = devm_clk_get(dev, "audio_core_cc_aud_dma_mem_clk");
	if (IS_ERR(data->aud_dma_mem_clk))
		return PTR_ERR(data->aud_dma_mem_clk);

	return 0;
}

int asoc_qcom_qaif_cpu_platform_probe(struct platform_device *pdev)
{
	struct qaif_drv_data *drvdata;
	struct resource *res;
	struct qaif_variant *variant;
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

	drvdata->variant = (struct qaif_variant *)match->data;
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
			devm_platform_ioremap_resource_byname(pdev, "audio-qaif-core");
	if (IS_ERR(drvdata->audio_qaif))
		return PTR_ERR(drvdata->audio_qaif);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "audio-qaif-core");
	if (!res)
		return -EINVAL;

	audio_qaif_regmap_config.max_register = resource_size(res);

	drvdata->audio_qaif_map = devm_regmap_init_mmio(dev, drvdata->audio_qaif,
							&audio_qaif_regmap_config);
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

	/* Initialize bitfields for dai AIF CFG register */
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
	dev_info(&pdev->dev, "%s: QAIF CPU-Platform Driver Registered Successfully\n", __func__);
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

MODULE_DESCRIPTION("QTi QAIF CPU Driver");
MODULE_LICENSE("GPL");
