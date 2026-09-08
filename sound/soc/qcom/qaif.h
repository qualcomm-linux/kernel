/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * qaif.h -- Shared data structures and definitions for the Qualcomm Audio Interface (QAIF)
 */
#ifndef __QAIF_H__
#define __QAIF_H__

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <dt-bindings/sound/qcom,qaif.h>

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

#define QAIF_AIF_CTL_ENABLE_ON		1
#define QAIF_AIF_CTL_ENABLE_OFF		0

#define QAIF_CIF_16BIT_UNPACK_ENABLE		1
#define QAIF_CIF_16BIT_UNPACK_DISABLE		0

#define QAIF_CIF_DMA_FS_SEL_DEFAULT			0

#define QAIF_DMA_DEFAULT_BIT_WIDTH			32

#define QAIF_DMA_BYTES_TO_WORDS_SHIFT			3

#define QAIF_MAX_VARIANT_CLKS				32

#define QAIF_DMA_CLK_RATE_MULTIPLIER			100

#define QAIF_MAX_AIF_CFG_CNT (LPASS_MAX_AIF_DMA_IDX / 2)

#define QAIF_AUD_INTF_CTL_MONO				1  /* Mono Mode True */
#define QAIF_AUD_INTF_CTL_STEREO			0  /* Mono Mode False */

#define QAIF_AIF_SAMPLE_WIDTH(bits)			((bits) - 1)
#define QAIF_AIF_SLOT_WIDTH(bits)			((bits) - 1)

#define QAIF_DMACTL_WM_5					4
#define QAIF_DMACTL_WM_8					7
#define QAIF_DMACTL_BURSTEN					1

#define QAIF_MAX_LANES						8

enum qxm_sel {
	QXM0 = 0,
	QXM1 = 1,
	MAX_QXM_TYPE,
};

static inline bool is_cif_dma_port(int dai_id)
{
	switch (dai_id) {
	case QAIF_CDC_DMA_RX0 ... QAIF_CDC_DMA_RX9:
	case QAIF_CDC_DMA_TX0 ... QAIF_CDC_DMA_TX9:
	case QAIF_CDC_DMA_VA_TX0 ... QAIF_CDC_DMA_VA_TX9:
		return true;
	}
	return false;
}

enum qaif_type_t {
	QAIF_INVALID = -1,
	QAIF = 0,
	QAIF_VA,
	QAIF_MAX_TYPES
};

enum qaif_irq_type {
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
	/* AUDIO_CORE_QAIF_CODEC_xDMAa_CTL */
	struct regmap_field *enable;
	struct regmap_field *reset;

	/* AUDIO_CORE_QAIF_CODEC_xDMAa_CFG */
	struct regmap_field *num_ot; /* outstanding transactions */
	struct regmap_field *dma_dyncclk;
	struct regmap_field *burst16;
	struct regmap_field *burst8;
	struct regmap_field *burst4;
	struct regmap_field *burst2;
	struct regmap_field *burst1;
	struct regmap_field *shram_wm; /* SHRAM watermark */

};

struct qaif_cdc_intfctl {
	/* AUDIO_CORE_QAIF_CODEC_xDMAa_INTF_CFG */
	struct regmap_field *active_ch_en;
	struct regmap_field *fs_sel;
	struct regmap_field *fs_delay;
	struct regmap_field *fs_out_gate;
	struct regmap_field *intf_dyncclk;
	struct regmap_field *en_16bit_unpack;
};

struct qaif_aud_intfctl {
	/* AUDIO_CORE_QAIF_AUD_INTFa_SYNC_CFG */
	struct regmap_field *inv_sync;
	struct regmap_field *sync_delay;
	struct regmap_field *sync_mode;
	struct regmap_field *sync_src;

	/* AUDIO_CORE_QAIF_AUD_INTFa_BIT_WIDTH_CFG */
	struct regmap_field *slot_width_rx;
	struct regmap_field *slot_width_tx;
	struct regmap_field *sample_width_rx;
	struct regmap_field *sample_width_tx;

	/* AUDIO_CORE_QAIF_AUD_INTFa_MI2S_CFG */
	struct regmap_field *mono_mode_rx;
	struct regmap_field *mono_mode_tx;

	/* AUDIO_CORE_QAIF_AUD_INTFa_LANE_CFG */
	struct regmap_field *lane_en;           /* Lane enable mask (bits 8-15) */
	struct regmap_field *lane_dir;          /* Lane direction mask (bits 0-7, 0=TX, 1=RX) */
	struct regmap_field *loopback_en;
	struct regmap_field *ctrl_data_oe;

	/* AUDIO_CORE_QAIF_AUD_INTFa_ACTV_SLOT_EN_RX */
	struct regmap_field *slot_en_rx_mask;

	/* AUDIO_CORE_QAIF_AUD_INTFa_ACTV_SLOT_EN_TX */
	struct regmap_field *slot_en_tx_mask;

	/* AUDIO_CORE_QAIF_AUD_INTFa_CFG */
	struct regmap_field *full_cycle_en;
	/* AUDIO_CORE_QAIF_AUD_INTFa_FRAME_CFG */
	struct regmap_field *bits_per_lane;

	/* AUDIO_CORE_QAIF_AUD_INTFa_CTL */
	struct regmap_field *enable;
	struct regmap_field *enable_tx;
	struct regmap_field *enable_rx;
	struct regmap_field *reset;
	struct regmap_field *reset_tx;
	struct regmap_field *reset_rx;
};

/* Lane configuration structure */
struct qaif_lane_config {
	u32 enable;     /* 1 = enabled, 0 = disabled */
	u32 direction;  /* 0 = TX_SPKR, 1 = RX_MIC */
};

/*
 * AIF interface protocol mode — set by the DT compatible string and
 * used to select mode-specific register defaults and property parsing.
 */
enum qaif_aif_mode {
	QAIF_AIF_MODE_PCM,
	QAIF_AIF_MODE_TDM,
	QAIF_AIF_MODE_MI2S,
};

/* QAIF Audio Interface Configuration Structure */
struct qaif_aif_config {
	/* Interface protocol mode derived from DT compatible */
	enum qaif_aif_mode mode;
	/* Sync configuration */
	u32 sync_mode;
	u32 sync_src;
	u32 invert_sync;
	u32 sync_delay;
	/* Slot and sample width configuration */
	u32 slot_width_rx;
	u32 slot_width_tx;
	u32 sample_width_rx;
	u32 sample_width_tx;
	/* Slot enable masks (32-bit masks for 32 slots) */
	u32 slot_en_rx_mask;
	u32 slot_en_tx_mask;
	/* Control configuration */
	u32 loopback_en;
	u32 ctrl_data_oe;
	/* Lane configuration */
	u32 num_lanes;          /* Number of lanes configured */
	struct qaif_lane_config lane_cfg[QAIF_MAX_LANES];
	u32 lane_en_mask;
	u32 lane_dir_mask;
	/* Frame configuration */
	u32 full_cycle_en;
	u32 bits_per_lane;
};

struct qaif_pcm_data {
	int stream_dma_idx;
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
	/* MI2S bit clock (derived from system clock by a divider) */
	struct clk *mi2s_bit_clk[LPASS_MAX_MI2S_PORTS];

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
	const struct qaif_variant *variant;

	/* Runtime-allocated regmap field handles (populated at probe) */
	struct qaif_dmactl *aif_rd_dmactl;
	struct qaif_dmactl *aif_wr_dmactl;
	struct qaif_dmactl *cif_rd_dmactl;
	struct qaif_dmactl *cif_wr_dmactl;
	struct qaif_aud_intfctl *aif_intfctl;
	struct qaif_cdc_intfctl *cif_rddma_intfctl;
	struct qaif_cdc_intfctl *cif_wrdma_intfctl;
	struct qaif_aif_config aif_intf_cfg[QAIF_MAX_AIF_CFG_CNT];

	/* bit map to keep track of dma idx allocations */
	unsigned long aif_dma_idx_bit_map;
	unsigned long cif_dma_idx_bit_map;

	/* used it for handling interrupt per dma channel */
	struct snd_pcm_substream *aif_substream[LPASS_MAX_AIF_DMA_IDX];
	struct snd_pcm_substream *cif_substream[LPASS_MAX_CIF_DMA_IDX];

	u32 smmu_csid_bits;

	/* DMA info handle per stream/dma idx */
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
	QAIF_SUMMARY_BITMASK_CIF_ERR_RSP_RDDMA		= BIT(20),
	QAIF_SUMMARY_BITMASK_CIF_PERIOD_WRDMA		= BIT(24),
	QAIF_SUMMARY_BITMASK_CIF_OVERFLOW_WRDMA		= BIT(25),
	QAIF_SUMMARY_BITMASK_CIF_ERR_RSP_WRDMA		= BIT(26)

};

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

	u32 num_codec_rddma;	/* RX */
	u32 num_codec_wrdma;	/* TX */
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

	/* AUDIO_CORE_QAIF_AUD_INTFa_CTL bitfields */
	const struct reg_field aif_enable;
	const struct reg_field aif_enable_tx;
	const struct reg_field aif_enable_rx;
	const struct reg_field aif_reset;
	const struct reg_field aif_reset_tx;
	const struct reg_field aif_reset_rx;

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
