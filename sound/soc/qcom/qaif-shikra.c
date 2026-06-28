// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * qaif-shikra.c -- ALSA SoC CPU-Platform DAI driver for QTi QAIF
 */

#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <linux/pm.h>
#include "qaif.h"

struct qaif_dmaidx_dai_map shikra_aif_dma_dai_map[] = {
		{ MI2S_QUATERNARY },
		{ MI2S_QUINARY },
		{ MI2S_SENARY },
		{ MI2S_SEPTENARY }
};

struct qaif_dmaidx_dai_map shikra_cif_rx_dma_dai_map[] = {
		{ LPASS_CDC_DMA_RX0 },
		{ LPASS_CDC_DMA_RX1 },
		{ LPASS_CDC_DMA_RX2 },
		{ LPASS_CDC_DMA_RX3 }
};

struct qaif_dmaidx_dai_map shikra_cif_tx_dma_dai_map[] = {
		{ LPASS_CDC_DMA_TX0 },
		{ LPASS_CDC_DMA_TX1 },
		{ LPASS_CDC_DMA_TX2 },
		{ LPASS_CDC_DMA_TX3 }
};

struct qaif_dmaidx_dai_map shikra_cif_va_dma_dai_map[] = {
		{ LPASS_CDC_DMA_VA_TX0 },
		{ LPASS_CDC_DMA_VA_TX1 },
		{ LPASS_CDC_DMA_VA_TX2 },
		{ LPASS_CDC_DMA_VA_TX3 }
};

static struct snd_soc_dai_driver shikra_qaif_cpu_dai_driver[] = {
	{
		.id = MI2S_PRIMARY,
		.name = "Primary MI2S",
		.playback = {
			.stream_name = "Primary MI2S Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.capture = {
			.stream_name = "Primary MI2S Capture",
			.formats = SNDRV_PCM_FMTBIT_S16 |
				SNDRV_PCM_FMTBIT_S32,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = MI2S_SECONDARY,
		.name = "Secondary MI2S",
		.playback = {
			.stream_name = "Secondary MI2S Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.capture = {
			.stream_name = "Secondary MI2S Capture",
			.formats = SNDRV_PCM_FMTBIT_S16 |
				SNDRV_PCM_FMTBIT_S32,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = MI2S_TERTIARY,
		.name = "Tertiary MI2S",
		.playback = {
			.stream_name = "Tertiary MI2S Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.capture = {
			.stream_name = "Tertiary MI2S Capture",
			.formats = SNDRV_PCM_FMTBIT_S16 |
				SNDRV_PCM_FMTBIT_S32,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = MI2S_QUATERNARY,
		.name = "Quaternary MI2S",
		.playback = {
			.stream_name = "Quaternary MI2S Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.capture = {
			.stream_name = "Quaternary MI2S Capture",
			.formats = SNDRV_PCM_FMTBIT_S16 |
				SNDRV_PCM_FMTBIT_S32,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = MI2S_QUINARY,
		.name = "Quinary MI2S",
		.playback = {
			.stream_name = "Quinary MI2S Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.capture = {
			.stream_name = "Quinary MI2S Capture",
			.formats = SNDRV_PCM_FMTBIT_S16 |
				SNDRV_PCM_FMTBIT_S32,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = MI2S_SENARY,
		.name = "Senary MI2S",
		.playback = {
			.stream_name = "Senary MI2S Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.capture = {
			.stream_name = "Senary MI2S Capture",
			.formats = SNDRV_PCM_FMTBIT_S16 |
				SNDRV_PCM_FMTBIT_S32,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = MI2S_SEPTENARY,
		.name = "Septenary MI2S",
		.playback = {
			.stream_name = "Septenary MI2S Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.capture = {
			.stream_name = "Septenary MI2S Capture",
			.formats = SNDRV_PCM_FMTBIT_S16 |
				SNDRV_PCM_FMTBIT_S32,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = LPASS_CDC_DMA_RX0,
		.name = "CDC DMA RX0",
		.playback = {
			.stream_name = "WCD Playback0",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_qaif_cif_dai_ops,
	}, {
		.id = LPASS_CDC_DMA_RX1,
		.name = "CDC DMA RX1",
		.playback = {
			.stream_name = "WCD Playback1",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_qaif_cif_dai_ops,
	}, {
		.id = LPASS_CDC_DMA_TX0,
		.name = "CDC DMA TX0",
		.capture = {
			.stream_name = "WCD Capture0",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_qaif_cif_dai_ops,
	}, {
		.id = LPASS_CDC_DMA_TX3,
		.name = "CDC DMA TX3",
		.capture = {
			.stream_name = "WCD Capture1",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_qaif_cif_dai_ops,
	}, {
		.id = LPASS_CDC_DMA_VA_TX0,
		.name = "CDC DMA VA0",
		.capture = {
			.stream_name = "DMIC Capture0",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 1,
			.channels_max	= 4,
		},
		.ops	= &asoc_qcom_qaif_cif_dai_ops,
	}, {
		.id = LPASS_CDC_DMA_VA_TX1,
		.name = "CDC DMA VA1",
		.capture = {
			.stream_name = "DMIC Capture1",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 1,
			.channels_max	= 4,
		},
		.ops	= &asoc_qcom_qaif_cif_dai_ops,
	},
};

static int shikra_qaif_get_dma_idx(unsigned int dai_id)
{
	int i;

	switch (dai_id) {
	case MI2S_PRIMARY ... MI2S_QUINARY:
	case MI2S_SENARY:
	case MI2S_SEPTENARY:
		for (i = 0; i < ARRAY_SIZE(shikra_aif_dma_dai_map); i++) {
			if (shikra_aif_dma_dai_map[i].dai_id == dai_id)
				return i;
		}
		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
		for (i = 0; i < ARRAY_SIZE(shikra_cif_rx_dma_dai_map); i++) {
			if (shikra_cif_rx_dma_dai_map[i].dai_id == dai_id)
				return i;
		}
		break;
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		for (i = 0; i < ARRAY_SIZE(shikra_cif_tx_dma_dai_map); i++) {
			if (shikra_cif_tx_dma_dai_map[i].dai_id == dai_id)
				return i;
		}
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		for (i = 0; i < ARRAY_SIZE(shikra_cif_va_dma_dai_map); i++) {
			if (shikra_cif_va_dma_dai_map[i].dai_id == dai_id)
				return i;
		}
		break;
	default:
		pr_debug("DAI ID not Supported\n");
		break;
	}

	pr_debug("DAI ID %u not found in map\n", dai_id);
	return -EINVAL;
}

static int shikra_qaif_alloc_stream_dma_idx(struct qaif_drv_data *drvdata,
					    int direction, unsigned int dai_id)
{
	struct qaif_variant *v = drvdata->variant;
	int dma_idx;
	int index = 0;

	if (!v)
		return -EINVAL;

	switch (dai_id) {
	case MI2S_PRIMARY ... MI2S_QUINARY:
	case MI2S_SENARY:
	case MI2S_SEPTENARY:
		dma_idx = shikra_qaif_get_dma_idx(dai_id);
		if (dma_idx < 0)
			return dma_idx;

		if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
			index = dma_idx;
			if (index >= v->num_rddma)
				return -EBUSY;
		} else {
			index = v->wrdma_start + dma_idx;
			if (index >= v->wrdma_start + v->num_wrdma)
				return -EBUSY;
		}
		if (test_bit(index, &drvdata->aif_dma_idx_bit_map))
			return -EBUSY;

		set_bit(index, &drvdata->aif_dma_idx_bit_map);
		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		dma_idx = shikra_qaif_get_dma_idx(dai_id);
		if (dma_idx < 0)
			return dma_idx;

		if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
			index = dma_idx;
			if (index >= v->num_codec_rddma)
				return -EBUSY;
		} else {
			index = v->codec_wrdma_start + dma_idx;
			if (index >= v->codec_wrdma_start + v->num_codec_wrdma)
				return -EBUSY;
		}
		if (test_bit(index, &drvdata->cif_dma_idx_bit_map))
			return -EBUSY;

		set_bit(index, &drvdata->cif_dma_idx_bit_map);
		break;
	default:
		return -EINVAL;
	}

	return index;
}

static int shikra_qaif_free_stream_dma_idx(struct qaif_drv_data *drvdata,
					   int index, unsigned int dai_id)
{
	switch (dai_id) {
	case MI2S_PRIMARY ... MI2S_QUINARY:
	case MI2S_SENARY:
	case MI2S_SEPTENARY:
		clear_bit(index, &drvdata->aif_dma_idx_bit_map);
		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		clear_bit(index, &drvdata->cif_dma_idx_bit_map);
		break;
	default:
		break;
	}

	return 0;
}

static int shikra_qaif_init(struct platform_device *pdev)
{
	struct qaif_drv_data *drvdata = platform_get_drvdata(pdev);
	struct qaif_variant *v = drvdata->variant;
	struct device *dev = &pdev->dev;
	int ret, i;

	if (!v) {
		dev_err(dev, "No variant data\n");
		return -EINVAL;
	}
	if (v->num_clks == 0 || v->num_clks > 32) {
		dev_err(dev, "Invalid clock count: %d\n", v->num_clks);
		return -EINVAL;
	}
	drvdata->clks = devm_kcalloc(dev, v->num_clks,
				     sizeof(*drvdata->clks), GFP_KERNEL);
	if (!drvdata->clks)
		return -ENOMEM;

	drvdata->num_clks = v->num_clks;

	for (i = 0; i < drvdata->num_clks; i++)
		drvdata->clks[i].id = v->clk_name[i];

	ret = devm_clk_bulk_get(dev, drvdata->num_clks, drvdata->clks);
	if (ret) {
		dev_err(dev, "Failed to get clocks %d\n", ret);
		return ret;
	}

	ret = clk_bulk_prepare_enable(drvdata->num_clks, drvdata->clks);
	if (ret) {
		dev_err(dev, "shikra clk_enable failed\n");
		return ret;
	}

	return 0;
}

static int shikra_qaif_exit(struct platform_device *pdev)
{
	struct qaif_drv_data *drvdata = platform_get_drvdata(pdev);

	if (!drvdata || !drvdata->clks)
		return -EINVAL;

	clk_bulk_disable_unprepare(drvdata->num_clks, drvdata->clks);
	return 0;
}

static int __maybe_unused shikra_qaif_dev_resume(struct device *dev)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);

	if (!drvdata || !drvdata->clks) {
		dev_err(dev, "Invalid drvdata in resume\n");
		return -EINVAL;
	}
	return clk_bulk_prepare_enable(drvdata->num_clks, drvdata->clks);
}

static int __maybe_unused shikra_qaif_dev_suspend(struct device *dev)
{
	struct qaif_drv_data *drvdata = dev_get_drvdata(dev);

	if (!drvdata || !drvdata->clks) {
		dev_err(dev, "Invalid drvdata in suspend\n");
		return -EINVAL;
	}
	clk_bulk_disable_unprepare(drvdata->num_clks, drvdata->clks);
	return 0;
}

static const struct dev_pm_ops shikra_qaif_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(shikra_qaif_dev_suspend,
				shikra_qaif_dev_resume)
};

static struct qaif_variant shikra_qaif_data = {
	.ee = 0,
	.qaif_type = QAIF,

	.num_rddma = 4,
	.num_wrdma = 4,
	.wrdma_start = 4,

	.num_codec_rddma = 4,	//RX
	.num_codec_wrdma = 4,	//TX
	.codec_wrdma_start = 4,
	.num_intf = 0,

	.rddma_reg_base = 0x8000,
	.rddma_stride = 0x1000,
	.codec_rddma_reg_base = 0xC000,
	.codec_rddma_stride = 0x1000,

	.wrdma_reg_base = 0x11000,
	.wrdma_stride = 0x1000,
	.codec_wrdma_reg_base = 0x15000,
	.codec_wrdma_stride = 0x1000,

	.rddma_irq_reg_base = 0x19000,
	.rddma_irq_stride = 0x1000,
	.codec_rddma_irq_reg_base = 0x191A0,
	.codec_rddma_irq_stride = 0x1000,

	.wrdma_irq_reg_base = 0x19078,
	.wrdma_irq_stride = 0x1000,
	.codec_wrdma_irq_reg_base = 0x19290,
	.codec_wrdma_irq_stride = 0x1000,

	.qxm_type = QXM0,
	.rd_len = 512,
	.rddma_shram_len = 64,
	.rddma_shram_start_addr = {0, 256},
	.wr_len = 512,
	.wrdma_shram_len = 64,
	.wrdma_shram_start_addr = {0, 256},

	/* ============================================================================ */
	/* AIF RDDMA (Audio Interface Read DMA) Register Fields */
	/* Base: 0x8000, 4 channels, stride 0x1000 */
	/* ============================================================================ */

	/* CTL register (0x8000) */
	.rddma_enable = REG_FIELD_ID(0x8000, 0, 0, 4, 0x1000), /* ENABLE [0] */
	.rddma_reset = REG_FIELD_ID(0x8000, 4, 4, 4, 0x1000), /* RESET [4] */

	/* CFG register (0x8004) */
	.rddma_shram_wm = REG_FIELD_ID(0x8004, 0, 11, 4, 0x1000), /* SHRAM_WATERMRK [11:0] */
	.rddma_burst1 = REG_FIELD_ID(0x8004, 16, 16, 4, 0x1000), /* BURST1_EN [16] */
	.rddma_burst2 = REG_FIELD_ID(0x8004, 17, 17, 4, 0x1000), /* BURST2_EN [17] */
	.rddma_burst4 = REG_FIELD_ID(0x8004, 18, 18, 4, 0x1000), /* BURST4_EN [18] */
	.rddma_burst8 = REG_FIELD_ID(0x8004, 19, 19, 4, 0x1000), /* BURST8_EN [19] */
	.rddma_burst16 = REG_FIELD_ID(0x8004, 20, 20, 4, 0x1000), /* BURST16_EN [20] */
	.rddma_dma_dyncclk = REG_FIELD_ID(0x8004, 24, 24, 4, 0x1000), /* DYNAMIC_CLOCK [24] */
	.rddma_num_ot = REG_FIELD_ID(0x8004, 28, 29, 4, 0x1000), /* RDDMA_NUM_OT [29:28] */

	/* ============================================================================ */
	/* AIF WRDMA (Audio Interface Write DMA) Register Fields */
	/* Base: 0x11000, 4 channels, stride 0x1000 */
	/* ============================================================================ */

	/* CTL register (0x11000) */
	.wrdma_enable = REG_FIELD_ID(0x11000, 0, 0, 4, 0x1000), /* ENABLE [0] */
	.wrdma_reset = REG_FIELD_ID(0x11000, 4, 4, 4, 0x1000), /* RESET [4] */

	/* CFG register (0x11004) */
	.wrdma_shram_wm = REG_FIELD_ID(0x11004, 0, 11, 4, 0x1000), /* SHRAM_WATERMRK [11:0] */
	.wrdma_burst1 = REG_FIELD_ID(0x11004, 16, 16, 4, 0x1000), /* BURST1_EN [16] */
	.wrdma_burst2 = REG_FIELD_ID(0x11004, 17, 17, 4, 0x1000), /* BURST2_EN [17] */
	.wrdma_burst4 = REG_FIELD_ID(0x11004, 18, 18, 4, 0x1000), /* BURST4_EN [18] */
	.wrdma_burst8 = REG_FIELD_ID(0x11004, 19, 19, 4, 0x1000), /* BURST8_EN [19] */
	.wrdma_burst16 = REG_FIELD_ID(0x11004, 20, 20, 4, 0x1000), /* BURST16_EN [20] */
	.wrdma_dma_dyncclk = REG_FIELD_ID(0x11004, 24, 24, 4, 0x1000), /* DYNAMIC_CLOCK [24] */
	.wrdma_num_ot = REG_FIELD_ID(0x11004, 28, 29, 4, 0x1000), /* WRDMA_NUM_OT [29:28] */

	/* ============================================================================ */
	/* CODEC RDDMA (RX/Playback) Register Fields */
	/* Base: 0xC000, 4 channels, stride 0x1000 */
	/* ============================================================================ */

	/* CTL register (0xC000) */
	.cif_rddma_enable = REG_FIELD_ID(0xC000, 0, 0, 4, 0x1000),
	.cif_rddma_reset = REG_FIELD_ID(0xC000, 4, 4, 4, 0x1000),

	/* CFG register (0xC004) */
	.cif_rddma_shram_wm = REG_FIELD_ID(0xC004, 0, 11, 4, 0x1000), /* SHRAM_WATERMRK [11:0] */
	.cif_rddma_burst1 = REG_FIELD_ID(0xC004, 16, 16, 4, 0x1000), /* BURST1_EN [16] */
	.cif_rddma_burst2 = REG_FIELD_ID(0xC004, 17, 17, 4, 0x1000), /* BURST2_EN [17] */
	.cif_rddma_burst4 = REG_FIELD_ID(0xC004, 18, 18, 4, 0x1000), /* BURST4_EN [18] */
	.cif_rddma_burst8 = REG_FIELD_ID(0xC004, 19, 19, 4, 0x1000), /* BURST8_EN [19] */
	.cif_rddma_burst16 = REG_FIELD_ID(0xC004, 20, 20, 4, 0x1000), /* BURST16_EN [20] */
	.cif_rddma_dma_dyncclk = REG_FIELD_ID(0xC004, 24, 24, 4, 0x1000), /* DYNAMIC_CLOCK [24] */
	.cif_rddma_num_ot = REG_FIELD_ID(0xC004, 28, 29, 4, 0x1000), /* RDDMA_NUM_OT [29:28] */

	/* INTF_CFG register (0xC05C) */
	.cif_rddma_en_16bit_unpack = REG_FIELD_ID(0xC05C, 0, 0, 4, 0x1000),
	/* ENABLE_16B_UNPACKING [0] */
	.cif_rddma_intf_dyncclk = REG_FIELD_ID(0xC05C, 2, 2, 4, 0x1000), /* DYNAMIC_CLOCK [2] */
	.cif_rddma_fs_out_gate = REG_FIELD_ID(0xC05C, 3, 3, 4, 0x1000), /* FS_OUT_GATING_EN [3] */
	.cif_rddma_fs_sel = REG_FIELD_ID(0xC05C, 4, 7, 4, 0x1000), /* FS_SEL [7:4] */
	.cif_rddma_fs_delay = REG_FIELD_ID(0xC05C, 8, 11, 4, 0x1000), /* FS_DELAY [11:8] */
	.cif_rddma_active_ch_en = REG_FIELD_ID(0xC05C, 12, 27, 4, 0x1000),
	/* ACTIVE_CHANNEL_EN [27:12] */

	/* ============================================================================ */
	/* CODEC WRDMA (TX/Capture) Register Fields */
	/* Base: 0x15000, 4 channels, stride 0x1000 */
	/* ============================================================================ */

	/* CTL register (0x15000) */
	.cif_wrdma_enable = REG_FIELD_ID(0x15000, 0, 0, 4, 0x1000),
	.cif_wrdma_reset = REG_FIELD_ID(0x15000, 4, 4, 4, 0x1000),

	/* CFG register (0x15004) */
	.cif_wrdma_shram_wm = REG_FIELD_ID(0x15004, 0, 11, 4, 0x1000), /* SHRAM_WATERMRK [11:0] */
	.cif_wrdma_burst1 = REG_FIELD_ID(0x15004, 16, 16, 4, 0x1000), /* BURST1_EN [16] */
	.cif_wrdma_burst2 = REG_FIELD_ID(0x15004, 17, 17, 4, 0x1000), /* BURST2_EN [17] */
	.cif_wrdma_burst4 = REG_FIELD_ID(0x15004, 18, 18, 4, 0x1000), /* BURST4_EN [18] */
	.cif_wrdma_burst8 = REG_FIELD_ID(0x15004, 19, 19, 4, 0x1000), /* BURST8_EN [19] */
	.cif_wrdma_burst16 = REG_FIELD_ID(0x15004, 20, 20, 4, 0x1000), /* BURST16_EN [20] */
	.cif_wrdma_dma_dyncclk = REG_FIELD_ID(0x15004, 24, 24, 4, 0x1000), /* DYNAMIC_CLOCK [24] */
	.cif_wrdma_num_ot = REG_FIELD_ID(0x15004, 28, 29, 4, 0x1000), /* WRDMA_NUM_OT [29:28] */

	/* INTF_CFG register (0x15058) */
	.cif_wrdma_en_16bit_unpack = REG_FIELD_ID(0x15058, 0, 0, 4, 0x1000),
	/* ENABLE_16B_PACKING [0] */
	.cif_wrdma_intf_dyncclk = REG_FIELD_ID(0x15058, 2, 2, 4, 0x1000), /* DYNAMIC_CLOCK [2] */
	.cif_wrdma_fs_out_gate = REG_FIELD_ID(0x15058, 3, 3, 4, 0x1000), /* FS_OUT_GATING_EN [3] */
	.cif_wrdma_fs_sel = REG_FIELD_ID(0x15058, 4, 7, 4, 0x1000), /* FS_SEL [7:4] */
	.cif_wrdma_fs_delay = REG_FIELD_ID(0x15058, 8, 11, 4, 0x1000), /* FS_DELAY [11:8] */
	.cif_wrdma_active_ch_en = REG_FIELD_ID(0x15058, 12, 27, 4, 0x1000),
	/* ACTIVE_CHANNEL_EN [27:12] */

	/* AUDIO_CORE_QAIF_AUD_INTFa_SYNC_CFG (0x4004 + 0x1000*a) */
	.aif_inv_sync = REG_FIELD_ID(0x4004, 12, 12, 4, 0x1000),  /* bit 12 */
	.aif_sync_delay = REG_FIELD_ID(0x4004, 8, 9, 4, 0x1000),    /* bits 9:8 */
	.aif_sync_mode = REG_FIELD_ID(0x4004, 4, 5, 4, 0x1000),    /* bits 5:4 */
	.aif_sync_src = REG_FIELD_ID(0x4004, 0, 0, 4, 0x1000),    /* bit 0 */

	/* AUDIO_CORE_QAIF_AUD_INTFa_BIT_WIDTH_CFG (0x4008 + 0x1000*a) */
	.aif_sample_width_rx = REG_FIELD_ID(0x4008, 24, 28, 4, 0x1000), /* bits 28:24 */
	.aif_sample_width_tx = REG_FIELD_ID(0x4008, 16, 20, 4, 0x1000), /* bits 20:16 */
	.aif_slot_width_rx = REG_FIELD_ID(0x4008, 8, 12, 4, 0x1000),  /* bits 12:8 */
	.aif_slot_width_tx = REG_FIELD_ID(0x4008, 0, 4, 4, 0x1000),   /* bits 4:0 */

	/* AUDIO_CORE_QAIF_AUD_INTFa_FRAME_CFG (0x400C + 0x1000*a) */
	.aif_bits_per_lane = REG_FIELD_ID(0x400C, 0, 9, 4, 0x1000),   /* bits 9:0 */

	/* AUDIO_CORE_QAIF_AUD_INTFa_ACTV_SLOT_EN_TX (0x4010 + 0x1000*a) */
	.aif_slot_en_tx_mask = REG_FIELD_ID(0x4010, 0, 31, 4, 0x1000),  /* bits 31:0 */

	/* AUDIO_CORE_QAIF_AUD_INTFa_ACTV_SLOT_EN_RX (0x4030 + 0x1000*a) */
	.aif_slot_en_rx_mask = REG_FIELD_ID(0x4030, 0, 31, 4, 0x1000),  /* bits 31:0 */

	/* AUDIO_CORE_QAIF_AUD_INTFa_LANE_CFG (0x4050 + 0x1000*a) */
	.aif_loopback_en = REG_FIELD_ID(0x4050, 31, 31, 4, 0x1000), /* bit 31 */
	.aif_ctrl_data_oe = REG_FIELD_ID(0x4050, 16, 16, 4, 0x1000), /* bit 16 */
	.aif_lane_en = REG_FIELD_ID(0x4050, 8, 15, 4, 0x1000),  /* bits 15:8 */
	.aif_lane_dir = REG_FIELD_ID(0x4050, 0, 7, 4, 0x1000),   /* bits 7:0 */

	/* AUDIO_CORE_QAIF_AUD_INTFa_MI2S_CFG (0x4054 + 0x1000*a) */
	.aif_mono_mode_rx = REG_FIELD_ID(0x4054, 1, 1, 4, 0x1000),   /* bit 1 */
	.aif_mono_mode_tx = REG_FIELD_ID(0x4054, 0, 0, 4, 0x1000),   /* bit 0 */

	/* AUDIO_CORE_QAIF_AUD_INTFa_CFG (0x4058 + 0x1000*a) */
	.aif_full_cycle_en = REG_FIELD_ID(0x4058, 0, 0, 4, 0x1000),   /* bit 0 */

	.clk_name			= (const char*[]) {
							"gcc_lpass_config_clk",
							"gcc_lpass_core_axim_clk",
							"audio_core_cc_bus_clk"
						},
	.num_clks			= 3,

	.dai_driver			= shikra_qaif_cpu_dai_driver,
	.num_dai			= ARRAY_SIZE(shikra_qaif_cpu_dai_driver),
	/* Below Clocks should be mapped as per interface index */
	.dai_osr_clk_names		= (const char *[]) {
							"null"
							},
	.dai_bit_clk_names		= (const char *[]) {
							"audio_core_cc_aif_if0_ibit_clk", /* if0 */
							"audio_core_cc_aif_if1_ibit_clk", /* if1 */
							"audio_core_cc_aif_if2_ibit_clk", /* if2 */
							"audio_core_cc_aif_if3_ibit_clk" /* if3 */
							},
	.init					= shikra_qaif_init,
	.exit					= shikra_qaif_exit,
	.alloc_stream_dma_idx	= shikra_qaif_alloc_stream_dma_idx,
	.free_stream_dma_idx	= shikra_qaif_free_stream_dma_idx,
	.get_dma_idx			= shikra_qaif_get_dma_idx,
};

static const struct of_device_id shikra_qaif_cpu_device_id[] = {
	{.compatible = "qcom,shikra-qaif-cpu", .data = &shikra_qaif_data},
	{}
};
MODULE_DEVICE_TABLE(of, shikra_qaif_cpu_device_id);

static struct platform_driver shikra_qaif_cpu_platform_driver = {
	.driver = {
		.name = "shikra-qaif-cpu",
		.of_match_table = of_match_ptr(shikra_qaif_cpu_device_id),
		.pm = &shikra_qaif_pm_ops,
	},
	.probe = asoc_qcom_qaif_cpu_platform_probe,
	.remove = asoc_qcom_qaif_cpu_platform_remove,
	.shutdown = asoc_qcom_qaif_cpu_platform_shutdown,
};
module_platform_driver(shikra_qaif_cpu_platform_driver);

MODULE_DESCRIPTION("SHIKRA QAIF CPU DRIVER");
MODULE_LICENSE("GPL");
