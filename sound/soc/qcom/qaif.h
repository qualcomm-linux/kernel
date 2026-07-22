/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011,2013-2015,2020 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * qaif.h -- ALSA SoC CPU-Platform DAI driver header file for QTi QAIF
 */
#ifndef __QAIF_H__
#define __QAIF_H__

#include <linux/clk.h>
#include <linux/dma-heap.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/version.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <dt-bindings/sound/qcom,lpass.h>

#define MI2S_SEPTENARY			35

#define SMMU_SID_OFFSET				32
#define LPASS_MAX_MI2S_PORTS			(8)
#define LPASS_MAX_AIF_DMA_IDX			(8)
#define LPASS_MAX_CIF_DMA_IDX		(8)
#define QAIF_CIF_DMA_INTF_ONE_CHANNEL		(0x01)
#define QAIF_CIF_DMA_INTF_TWO_CHANNEL		(0x03)
#define QAIF_CIF_DMA_INTF_FOUR_CHANNEL		(0x0F)
#define QAIF_CIF_DMA_INTF_SIX_CHANNEL		(0x3F)
#define QAIF_CIF_DMA_INTF_EIGHT_CHANNEL		(0xFF)

#define QAIF_DMACTL_ENABLE_ON		1
#define QAIF_DMACTL_ENABLE_OFF		0

#define QAIF_DMACTL_DYNCLK_ON		1
#define QAIF_DMACTL_DYNCLK_OFF		0

#define QAIF_MAX_AIF_CFG_CNT (LPASS_MAX_AIF_DMA_IDX / 2)

/* TODO: confirm if only dma0...3 are active. */
#define QAIF_IRQ_DMA_ALL				(0xf)

/* Individual bit masks (hex) */
#define QAIF_AUD_INTF_CTL_ENABLE			0x00000001  /* bit 0 ENABLE RX and TX*/
#define QAIF_AUD_INTF_CTL_ENABLE_TX			0x00000010  /* bit 4 */
#define QAIF_AUD_INTF_CTL_ENABLE_RX			0x00000100  /* bit 8 */
#define QAIF_AUD_INTF_CTL_RESET				0x00001000  /* bit 12 RESET RX and TX*/
#define QAIF_AUD_INTF_CTL_RESET_TX			0x00010000  /* bit 16 */
#define QAIF_AUD_INTF_CTL_RESET_RX			0x00100000  /* bit 20 */

/* Combined masks */
#define QAIF_AUD_INTF_CTL_ENABLE_ALL		0x00000110  /* bits 4,8 */
#define QAIF_AUD_INTF_CTL_RESET_ALL			0x00110000  /* bits 16,20 */

#define QAIF_AUD_INTF_CTL_MONO				1  /* Mono Mode True */
#define QAIF_AUD_INTF_CTL_STEREO			0  /* Mono Mode False */

#define QAIF_AIF_SAMPLE_WIDTH(bits)			((bits) - 1)
#define QAIF_AIF_SLOT_WIDTH(bits)			((bits) - 1)

#define QAIF_DMA_CLK_RATE_HZ				153600000

#define QAIF_DMACTL_WM_5					4
#define QAIF_DMACTL_WM_8					7
#define QAIF_DMACTL_BURSTEN					1

#define QAIF_MAX_LANES						8

/* QAIF_AUD_INTF_SYNC_CFG_REG bit masks and shifts */
#define QAIF_AUD_INTF_SYNC_CFG_INV_SYNC_MASK    BIT(12)
#define QAIF_AUD_INTF_SYNC_CFG_INV_SYNC_SHFT    12

#define QAIF_AUD_INTF_SYNC_CFG_SYNC_DELAY_MASK  GENMASK(9, 8)
#define QAIF_AUD_INTF_SYNC_CFG_SYNC_DELAY_SHFT  8

#define QAIF_AUD_INTF_SYNC_CFG_SYNC_MODE_MASK   GENMASK(5, 4)
#define QAIF_AUD_INTF_SYNC_CFG_SYNC_MODE_SHFT   4

#define QAIF_AUD_INTF_SYNC_CFG_SYNC_SRC_MASK    BIT(0)
#define QAIF_AUD_INTF_SYNC_CFG_SYNC_SRC_SHFT    0

/* QAIF_AUD_INTF_LANE_CFG_REG bit masks and shifts  */
#define QAIF_AUD_INTF_LANE_CFG_LOOPBACK_MASK    BIT(31)
#define QAIF_AUD_INTF_LANE_CFG_LOOPBACK_SHFT    31

#define QAIF_AUD_INTF_LANE_CFG_CTRL_DATA_OE_MASK    BIT(16)
#define QAIF_AUD_INTF_LANE_CFG_CTRL_DATA_OE_SHFT    16

#define QAIF_AUD_INTF_LANE_CFG_LANE_EN_MASK     GENMASK(15, 8)
#define QAIF_AUD_INTF_LANE_CFG_LANE_EN_SHFT     8

#define QAIF_AUD_INTF_LANE_CFG_LANE_DIR_MASK    GENMASK(7, 0)
#define QAIF_AUD_INTF_LANE_CFG_LANE_DIR_SHFT    0

/* ========== QAIF_AUD_INTF_BIT_WIDTH_CFG_REG bit masks and shifts ========== */
#define QAIF_AUD_INTF_BIT_WIDTH_CFG_SAMPLE_WIDTH_RX_MASK    GENMASK(28, 24)
#define QAIF_AUD_INTF_BIT_WIDTH_CFG_SAMPLE_WIDTH_RX_SHFT    24

#define QAIF_AUD_INTF_BIT_WIDTH_CFG_SAMPLE_WIDTH_TX_MASK    GENMASK(20, 16)
#define QAIF_AUD_INTF_BIT_WIDTH_CFG_SAMPLE_WIDTH_TX_SHFT    16

#define QAIF_AUD_INTF_BIT_WIDTH_CFG_SLOT_WIDTH_RX_MASK      GENMASK(12, 8)
#define QAIF_AUD_INTF_BIT_WIDTH_CFG_SLOT_WIDTH_RX_SHFT      8

#define QAIF_AUD_INTF_BIT_WIDTH_CFG_SLOT_WIDTH_TX_MASK      GENMASK(4, 0)
#define QAIF_AUD_INTF_BIT_WIDTH_CFG_SLOT_WIDTH_TX_SHFT      0

/* ========== QAIF_AUD_INTF_BIT_WIDTH_CFG_REG - Combined masks for RMW ========== */
/* RX-only fields mask (for preserving TX fields) */
#define QAIF_AUD_INTF_BIT_WIDTH_CFG_RX_FIELDS_MASK  \
	(QAIF_AUD_INTF_BIT_WIDTH_CFG_SAMPLE_WIDTH_RX_MASK | \
	 QAIF_AUD_INTF_BIT_WIDTH_CFG_SLOT_WIDTH_RX_MASK)

/* TX-only fields mask (for preserving RX fields) */
#define QAIF_AUD_INTF_BIT_WIDTH_CFG_TX_FIELDS_MASK  \
	(QAIF_AUD_INTF_BIT_WIDTH_CFG_SAMPLE_WIDTH_TX_MASK | \
	 QAIF_AUD_INTF_BIT_WIDTH_CFG_SLOT_WIDTH_TX_MASK)

/* ========== QAIF_AUD_INTF_MI2S_CFG_REG bit masks and shifts ========== */
#define QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_RX_MASK    BIT(1)
#define QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_RX_SHFT    1

#define QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_TX_MASK    BIT(0)
#define QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_TX_SHFT    0

/* Combined masks for Read-Modify-Write operations */
#define QAIF_AUD_INTF_MI2S_CFG_RX_FIELDS_MASK   \
	(QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_RX_MASK)

#define QAIF_AUD_INTF_MI2S_CFG_TX_FIELDS_MASK   \
	(QAIF_AUD_INTF_MI2S_CFG_MONO_MODE_TX_MASK)

enum qxm_sel {
	QXM0 = 0,
	QXM1 = 1,
	MAX_QXM_TYPE,
};

/* Enum list to define the interface direction */
enum aud_dma_util_direction {
	AUD_DMA_SINK   = 0,
	AUD_DMA_SOURCE = 1,
};

static inline bool is_cif_dma_port(int dai_id)
{
	switch (dai_id) {
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		return true;
	}
	return false;
}

/* Enum list to define the list of HW interfaces DMA Util is used for
 * example, Display port can be part of the below list in future
 */
enum qaif_type_t {
	QAIF_INVALID = -1,
	QAIF = 0,
	QAIF_VA,
	QAIF_MAX_TYPES
};

enum qaif_irq_type_t {
	QAIF_AIF_IRQ = 0,
	QAIF_CIF_IRQ = 1,
	QAIF_AUD_INTF_IRQ = 2,
	QAIF_IRQ_MAX = 3
};

enum qaif_dma_type {
	QAIF_AIF_DMA = 0,
	QAIF_CIF_DMA = 1,
	DMA_TYPE_MAX
};

struct qaif_dmactl {
	//AUDIO_CORE_QAIF_CODEC_xDMAa_CTL
	struct regmap_field *enable;
	struct regmap_field *reset;

	//AUDIO_CORE_QAIF_CODEC_xDMAa_CFG
	struct regmap_field *num_ot; //outstanding transaction
	struct regmap_field *dma_dyncclk;
	struct regmap_field *burst16;
	struct regmap_field *burst8;
	struct regmap_field *burst4;
	struct regmap_field *burst2;
	struct regmap_field *burst1;
	struct regmap_field *shram_wm; //SHRAM_WATERMRK

};

struct qaif_cdc_intfctl {
	//AUDIO_CORE_QAIF_CODEC_xDMAa_INTF_CFG
	struct regmap_field *active_ch_en;
	struct regmap_field *fs_sel;
	struct regmap_field *fs_delay;
	struct regmap_field *fs_out_gate;
	struct regmap_field *intf_dyncclk;
	struct regmap_field *en_16bit_unpack;
};

struct qaif_aud_intfctl {
	/* AUDIO_CORE_QAIF_AUD_INTFa_SYNC_CFG */
	struct regmap_field *inv_sync;          /* qcom,qaif-aif-invert-sync */
	struct regmap_field *sync_delay;        /* qcom,qaif-aif-sync-delay */
	struct regmap_field *sync_mode;         /* qcom,qaif-aif-sync-mode */
	struct regmap_field *sync_src;          /* qcom,qaif-aif-sync-src */

	/* AUDIO_CORE_QAIF_AUD_INTFa_BIT_WIDTH_CFG */
	struct regmap_field *slot_width_rx;     /* qcom,qaif-aif-slot-width-rx (MIC/RX Path) */
	struct regmap_field *slot_width_tx;     /* qcom,qaif-aif-slot-width-tx (SPKR/TX Path) */
	struct regmap_field *sample_width_rx;   /* qcom,qaif-aif-sample-width-rx (MIC/RX Path) */
	struct regmap_field *sample_width_tx;   /* qcom,qaif-aif-sample-width-tx (SPKR/TX Path) */

	/* AUDIO_CORE_QAIF_AUD_INTFa_MI2S_CFG */
	struct regmap_field *mono_mode_rx;      /* qcom,qaif-aif-mono-mode-rx (SPKR/TX Path) */
	struct regmap_field *mono_mode_tx;      /* qcom,qaif-aif-mono-mode-tx (MIC/RX Path) */

	/* AUDIO_CORE_QAIF_AUD_INTFa_LANE_CFG */
	struct regmap_field *lane_en;           /* Lane enable mask (bits 8-15) */
	struct regmap_field *lane_dir;          /* Lane direction mask (bits 0-7, 0=TX, 1=RX) */
	struct regmap_field *loopback_en;       /* qcom,qaif-aif-loopback-en (bit 31) */
	struct regmap_field *ctrl_data_oe;      /* qcom,qaif-aif-ctrl-data-oe (bit 16) */

	/* AUDIO_CORE_QAIF_AUD_INTFa_ACTV_SLOT_EN_RX */
	struct regmap_field *slot_en_rx_mask;   /* qcom,qaif-aif-slot-en-rx-mask (32-bit mask) */

	/* AUDIO_CORE_QAIF_AUD_INTFa_ACTV_SLOT_EN_TX */
	struct regmap_field *slot_en_tx_mask;   /* qcom,qaif-aif-slot-en-tx-mask (32-bit mask) */

	/* AUDIO_CORE_QAIF_AUD_INTFa_CFG */
	struct regmap_field *full_cycle_en;     /* qcom,qaif-aif-full-cycle-en */
	/* AUDIO_CORE_QAIF_AUD_INTFa_FRAME_CFG */
	struct regmap_field *bits_per_lane;     /* qcom,qaif-aif-bits-per-lane */
};

/* Lane configuration structure */
struct qaif_lane_config {
	u32 enable;     /* 1 = enabled, 0 = disabled */
	u32 direction;  /* 0 = TX_SPKR, 1 = RX_MIC */
};

/* QAIF Audio Interface Configuration Structure */
struct qaif_aif_config {
	/* Sync configuration */
	u32 sync_mode;          /* qcom,qaif-aif-sync-mode */
	u32 sync_src;           /* qcom,qaif-aif-sync-src */
	u32 invert_sync;        /* qcom,qaif-aif-invert-sync */
	u32 sync_delay;         /* qcom,qaif-aif-sync-delay */
	/* Slot and sample width configuration */
	u32 slot_width_rx;      /* qcom,qaif-aif-slot-width-rx (MIC/RX Path) */
	u32 slot_width_tx;      /* qcom,qaif-aif-slot-width-tx (SPKR/TX Path) */
	u32 sample_width_rx;    /* qcom,qaif-aif-sample-width-rx (MIC/RX Path) */
	u32 sample_width_tx;    /* qcom,qaif-aif-sample-width-tx (SPKR/TX Path) */
	/* Slot enable masks (32-bit masks for 32 slots) */
	u32 slot_en_rx_mask;    /* qcom,qaif-aif-slot-en-rx-mask (MIC/RX Path) */
	u32 slot_en_tx_mask;    /* qcom,qaif-aif-slot-en-tx-mask (SPKR/TX Path) */
	/* Control configuration */
	u32 loopback_en;        /* qcom,qaif-aif-loopback-en */
	u32 ctrl_data_oe;       /* qcom,qaif-aif-ctrl-data-oe */
	/* Lane configuration */
	u32 num_lanes;          /* Number of lanes configured */
	struct qaif_lane_config lane_cfg[QAIF_MAX_LANES];  /* qcom,qaif-aif-lane-config */
	u32 lane_en_mask;
	u32 lane_dir_mask;
	/* Mono/Stereo mode */
	u32 mono_mode_tx;       /* qcom,qaif-aif-mono-mode-tx (MIC/RX Path) */
	u32 mono_mode_rx;       /* qcom,qaif-aif-mono-mode-rx (SPKR/TX Path) */
	/* Frame configuration */
	u32 full_cycle_en;      /* qcom,qaif-aif-full-cycle-en */
	u32 bits_per_lane;      /* qcom,qaif-aif-bits-per-lane (FRAME_CFG) */
};

struct qaif_pcm_data {
	int stream_dma_idx;
	//int i2s_port;
};

struct qaif_dma_mem_info {
	dma_addr_t dma_addr;
	size_t alloc_size;
	void *vaddr;
};

struct qaif_dmaidx_dai_map {
	unsigned int dai_id;
};

/* Both the CPU DAI and platform drivers will access this data */
struct qaif_drv_data {
	/* MI2S system clock */
	struct clk *mi2s_osr_clk[LPASS_MAX_MI2S_PORTS];

	/* MI2S bit clock (derived from system clock by a divider */
	struct clk *mi2s_bit_clk[LPASS_MAX_MI2S_PORTS];

	/* The state of MI2S prepare dai_ops was called */
	bool mi2s_was_prepared[LPASS_MAX_MI2S_PORTS];

	/* SOC specific clock list */
	struct clk_bulk_data *clks;
	int num_clks;

	struct clk *aud_dma_clk;
	struct clk *aud_dma_mem_clk;

	/* Qualcomm audio interface (QAIF) registers */
	void __iomem *audio_qaif;

	/* regmap backed by the Qualcomm audio interface (QAIF) registers */
	struct regmap *audio_qaif_map;

	/* interrupts from the Qualcomm audio interface (QAIF) */
	int audio_qaif_irq;

	/* QAIF init config refcount*/
	unsigned int qaif_init_ref_cnt;

	/* SOC specific variations in the QAIF IP integration */
	struct qaif_variant *variant;

	/* bit map to keep track of dma idx allocations */
	unsigned long aif_dma_idx_bit_map;
	unsigned long cif_dma_idx_bit_map;

	/* used it for handling interrupt per dma channel */
	struct snd_pcm_substream *aif_substream[LPASS_MAX_AIF_DMA_IDX];
	struct snd_pcm_substream *cif_substream[LPASS_MAX_CIF_DMA_IDX];

	u64 smmu_csid_bits;
	u64 smmu_sid_bits;

	/* DMA Heap handle*/
	struct dma_heap *dma_heap;
	/* DMA Heap name*/
	const char *dma_heap_name;
	/* DMA info handle per stream/dma idx*/
	struct qaif_dma_mem_info *aif_dma_heap[LPASS_MAX_AIF_DMA_IDX];
	struct qaif_dma_mem_info *cif_dma_heap[LPASS_MAX_CIF_DMA_IDX];

};

enum qaif_summary_irq_bitmask {
	QAIF_SUMMARY_BITMASK_AIF_PERIOD_RDDMA		= BIT(0),
	QAIF_SUMMARY_BITMASK_AIF_UNDERFLOW_RDDMA	= BIT(1),
	QAIF_SUMMARY_BITMASK_AIF_ERR_RSP_RDDMA		= BIT(2),
	QAIF_SUMMARY_BITMASK_AIF_PERIOD_WRDMA		= BIT(3),
	QAIF_SUMMARY_BITMASK_AIF_OVERFLOW_WRDMA		= BIT(4),
	QAIF_SUMMARY_BITMASK_AIF_ERR_RSP_WRDMA		= BIT(5),

	QAIF_SUMMARY_BITMASK_AUD_OVERFLOW			= BIT(6),
	QAIF_SUMMARY_BITMASK_AUD_UNDERFLOW			= BIT(7),

	QAIF_SUMMARY_BITMASK_RATE_DET				= BIT(8),
	QAIF_SUMMARY_BITMASK_VFR					= BIT(9),
	QAIF_SUMMARY_BITMASK_GRP					= BIT(10),
	QAIF_SUMMARY_BITMASK_RDDMA_OVERLAP			= BIT(11),
	QAIF_SUMMARY_BITMASK_WRDMA_OVERLAP			= BIT(12),
	QAIF_SUMMARY_BITMASK_INTF_OVERLAP			= BIT(13),
	QAIF_SUMMARY_BITMASK_GRP_OVERLAP			= BIT(14),

	QAIF_SUMMARY_BITMASK_CIF_OVERLAP_RDDMA		= BIT(15),
	QAIF_SUMMARY_BITMASK_CIF_OVERLAP_WRDMA		= BIT(17),
	QAIF_SUMMARY_BITMASK_CIF_PERIOD_RDDMA		= BIT(18),
	QAIF_SUMMARY_BITMASK_CIF_UNDERFLOW_RDDMA	= BIT(19),
	QAIF_SUMMARY_BITMASK_CIF_ERR_RSP			= BIT(20),
	QAIF_SUMMARY_BITMASK_CIF_PERIOD_WRDMA		= BIT(24),
	QAIF_SUMMARY_BITMASK_CIF_OVERFLOW_WRDMA		= BIT(25),
	QAIF_SUMMARY_BITMASK_CIF_ERR_RSP_WRDMA		= BIT(26)

};

/* defines the bitmask in the status register for each of the clients */
enum qaif_client_status_register_bitmask_info {
	QAIF_BITMASK_GROUP_INF			= 0x400,
	QAIF_BITMASK_AIF_RDDMA_WRDMA	= 0x3F,
	QAIF_BITMASK_CIF_RDDMA_WRDMA	= 0x71c0000,
	QAIF_BITMASK_DP_RDDMA			= 0xe00000,
	QAIF_BITMASK_AUD_INF			= 0xC0,
};

struct qaif_irq_map {
	int client_id;
	u32 mask;
	irqreturn_t (*client_irq_handler)(struct qaif_drv_data *drvdata, u32 irq_status);
};

enum dma_type {
	DMA_TYPE_RDDMA,
	DMA_TYPE_WRDMA
};

enum qaif_irq {
	QAIF_IRQ_PERIOD,
	QAIF_IRQ_OVERFLOW,
	QAIF_IRQ_UNDERFLOW,
	QAIF_IRQ_ERROR
};

/* list of clients for IRQ Util */
enum qaif_client_info {
	QAIF_CLIENT_ID_GROUP_INF = 0,
	QAIF_CLIENT_ID_AIF_DMA	= 1,
	QAIF_CLIENT_ID_CIF_DMA	= 2,
	QAIF_CLIENT_ID_DP_DMA	 = 3,
	QAIF_CLIENT_ID_AUD_INF	= 4,
	QAIF_CLIENT_ID_MAX
};

struct qaif_variant {
	u32 ee;
	u32 qaif_type;

	u32 num_rddma;
	u32 num_wrdma;
	u32 wrdma_start;

	u32 num_codec_rddma;	//RX
	u32 num_codec_wrdma;	//TX
	u32 codec_wrdma_start;
	u32 num_intf;

	u32 rddma_reg_base;
	u32 rddma_stride;
	u32 codec_rddma_reg_base;
	u32 codec_rddma_stride;

	u32 wrdma_reg_base;
	u32 wrdma_stride;
	u32 codec_wrdma_reg_base;
	u32 codec_wrdma_stride;

	u32 rddma_irq_reg_base;
	u32 rddma_irq_stride;
	u32 codec_rddma_irq_reg_base;
	u32 codec_rddma_irq_stride;

	u32 wrdma_irq_reg_base;
	u32 wrdma_irq_stride;
	u32 codec_wrdma_irq_reg_base;
	u32 codec_wrdma_irq_stride;

	u32 qxm_type;
	u32 rd_len;
	u32 rddma_shram_len;
	u32 rddma_shram_start_addr[DMA_TYPE_MAX];
	u32 wr_len;
	u32 wrdma_shram_len;
	u32 wrdma_shram_start_addr[DMA_TYPE_MAX];

	/* AIF RDDMA register fields */
	const struct reg_field rddma_enable;
	const struct reg_field rddma_reset;
	const struct reg_field rddma_num_ot;
	const struct reg_field rddma_dma_dyncclk;
	const struct reg_field rddma_burst16;
	const struct reg_field rddma_burst8;
	const struct reg_field rddma_burst4;
	const struct reg_field rddma_burst2;
	const struct reg_field rddma_burst1;
	const struct reg_field rddma_shram_wm;

	/* AIF WRDMA register fields */
	const struct reg_field wrdma_enable;
	const struct reg_field wrdma_reset;
	const struct reg_field wrdma_num_ot;
	const struct reg_field wrdma_dma_dyncclk;
	const struct reg_field wrdma_burst16;
	const struct reg_field wrdma_burst8;
	const struct reg_field wrdma_burst4;
	const struct reg_field wrdma_burst2;
	const struct reg_field wrdma_burst1;
	const struct reg_field wrdma_shram_wm;

	/* CODEC RDDMA register fields */
	const struct reg_field cif_rddma_enable;
	const struct reg_field cif_rddma_reset;
	const struct reg_field cif_rddma_num_ot;
	const struct reg_field cif_rddma_dma_dyncclk;
	const struct reg_field cif_rddma_burst16;
	const struct reg_field cif_rddma_burst8;
	const struct reg_field cif_rddma_burst4;
	const struct reg_field cif_rddma_burst2;
	const struct reg_field cif_rddma_burst1;
	const struct reg_field cif_rddma_shram_wm;
	const struct reg_field cif_rddma_active_ch_en;
	const struct reg_field cif_rddma_fs_sel;
	const struct reg_field cif_rddma_fs_delay;
	const struct reg_field cif_rddma_fs_out_gate;
	const struct reg_field cif_rddma_intf_dyncclk;
	const struct reg_field cif_rddma_en_16bit_unpack;

	/* CODEC WRDMA register fields */
	const struct reg_field cif_wrdma_enable;
	const struct reg_field cif_wrdma_reset;
	const struct reg_field cif_wrdma_num_ot;
	const struct reg_field cif_wrdma_dma_dyncclk;
	const struct reg_field cif_wrdma_burst16;
	const struct reg_field cif_wrdma_burst8;
	const struct reg_field cif_wrdma_burst4;
	const struct reg_field cif_wrdma_burst2;
	const struct reg_field cif_wrdma_burst1;
	const struct reg_field cif_wrdma_shram_wm;
	const struct reg_field cif_wrdma_active_ch_en;
	const struct reg_field cif_wrdma_fs_sel;
	const struct reg_field cif_wrdma_fs_delay;
	const struct reg_field cif_wrdma_fs_out_gate;
	const struct reg_field cif_wrdma_intf_dyncclk;
	const struct reg_field cif_wrdma_en_16bit_unpack;

	/* Regmap fields of AIF interface registers bitfields */
	const struct reg_field aif_inv_sync;
	const struct reg_field aif_sync_delay;
	const struct reg_field aif_sync_mode;
	const struct reg_field aif_sync_src;
	const struct reg_field aif_sample_width_rx;
	const struct reg_field aif_sample_width_tx;
	const struct reg_field aif_slot_width_rx;
	const struct reg_field aif_slot_width_tx;
	const struct reg_field aif_bits_per_lane;
	const struct reg_field aif_slot_en_tx_mask;
	const struct reg_field aif_slot_en_rx_mask;
	const struct reg_field aif_loopback_en;
	const struct reg_field aif_ctrl_data_oe;
	const struct reg_field aif_lane_en;
	const struct reg_field aif_lane_dir;
	const struct reg_field aif_mono_mode_rx;
	const struct reg_field aif_mono_mode_tx;
	const struct reg_field aif_full_cycle_en;

	/* Regmap fields of DMACTL registers bitfields */
	struct qaif_dmactl *aif_rd_dmactl;
	struct qaif_dmactl *aif_wr_dmactl;

	/* Regmap fields of CODEC DMA CTRL registers */
	struct qaif_dmactl *cif_rd_dmactl;
	struct qaif_dmactl *cif_wr_dmactl;

	struct qaif_aif_config aif_intf_cfg[QAIF_MAX_AIF_CFG_CNT];
	struct qaif_aud_intfctl *aif_intfctl;

	struct qaif_cdc_intfctl *cif_rddma_intfctl;
	struct qaif_cdc_intfctl *cif_wrdma_intfctl;

	/* Platform-specific data */
	const char **clk_name;
	int num_clks;
	struct snd_soc_dai_driver *dai_driver;
	int num_dai;
	const char **dai_osr_clk_names;
	const char **dai_bit_clk_names;

	/* Platform-specific function pointers */
	int (*init)(struct platform_device *pdev);
	int (*exit)(struct platform_device *pdev);
	int (*alloc_stream_dma_idx)(struct qaif_drv_data *data, int direction, unsigned int dai_id);
	int (*free_stream_dma_idx)(struct qaif_drv_data *data, int chan, unsigned int dai_id);
	int (*get_dma_idx)(unsigned int dai_id);

};

/* External DAI ops structures defined in qaif-cpu.c */
extern const struct snd_soc_dai_ops asoc_qcom_qaif_cif_dai_ops;
extern const struct snd_soc_dai_ops asoc_qcom_qaif_aif_cpu_dai_ops;

/* Platform driver functions defined in qaif-cpu.c */
int asoc_qcom_qaif_cpu_platform_probe(struct platform_device *pdev);
int asoc_qcom_qaif_platform_register(struct platform_device *pdev);
void asoc_qcom_qaif_cpu_platform_remove(struct platform_device *pdev);
void asoc_qcom_qaif_cpu_platform_shutdown(struct platform_device *pdev);

#endif /* __QAIF_H__ */
