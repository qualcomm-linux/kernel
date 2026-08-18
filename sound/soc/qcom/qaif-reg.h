/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * qaif-reg.h -- ALSA SoC CPU-Platform DAI driver register header file for QTi QAIF
 */
#ifndef __QAIF_REG_H__
#define __QAIF_REG_H__

#include "qaif.h"

#define QAIF_REG_ADDR(offset)					(offset)
/* Base for AUDIO_CORE_QAIF_HW_VERSION 0xA000000 */

#define QAIF_SUMMARY_IRQSTAT_REG(v) \
	(0x19188 + (0x1000 * ((v)->ee)))			/* 0xA019188 */
/*
 * #define QAIF_RDDMA_UNDERFLOW_REG(v)
 *	QAIF_REG_ADDR(0x19188 + (0x1000 * (ee)))
 */

/* Shikra core registers */

/* Core HW info */
#define QAIF_HW_VERSION_REG()                               (0x0000)  /* 0xA000000 */
#define QAIF_HW_INFO_REG                                    (0x0004)  /* 0xA000004 */
#define QAIF_HW_INFO2_REG                                   (0x0008)  /* 0xA000008 */

/* Interface lane and channel info */
#define QAIF_AUD_INTF_LANE_INFO_REG                         (0x0020)  /* 0xA000020 */
#define QAIF_AUD_INTF_LANE_INFO2_REG                        (0x0024)  /* 0xA000024 */
/* 0xA000028 .. 0xA000044 (n:0..7) */
#define QAIF_CODEC_TX_INTF_CH_INFO_REG(n)	(0x0028 + (0x4 * (n)))
/* 0xA000068 .. 0xA000084 (n:0..7) */
#define QAIF_CODEC_RX_INTF_CH_INFO_REG(n)	(0x0068 + (0x4 * (n)))
#define QAIF_QXM1_SHRAM_LENGTH_INFO_REG                     (0x0088)  /* 0xA000088 */
#define QAIF_QXM0_SHRAM_LENGTH_INFO_REG                     (0x008C)  /* 0xA00008C */
#define QAIF_NUM_AUD_INTF_TO_RAIL_INFO_REG                  (0x0090)  /* 0xA000090 */

/* Debug/control and status */
#define QAIF_DEBUG_CTL_REG                                  (0x0200)  /* 0xA000200 */
#define QAIF_WRDMA_LOOPBACK_EN_REG                          (0x0204)  /* 0xA000204 */
#define QAIF_WRDMA_LOOPBACK_SEL_REG                         (0x0208)  /* 0xA000208 */
#define QAIF_SHRAM_DYNAMIC_CLK_GATING_EN_REG                (0x0300)  /* 0xA000300 */
#define QAIF_AXI_STATUS_REG                                 (0x0304)  /* 0xA000304 */
#define QAIF_QSB_DYNAMIC_CLK_GATING_EN_REG                  (0x0308)  /* 0xA000308 */
#define QAIF_START_STOP_CTRL_BYPASS_EN_REG                  (0x030C)  /* 0xA00030C */
#define QAIF_QXM0_AXI_ATTR_CFG_REG                          (0x040C)  /* 0xA00040C */

/* QXM request/grant debug */
#define QAIF_QXM0_AUD_WR_REQ_GNT_DBG_STAT_REG               (0x0500)  /* 0xA000500 */
#define QAIF_QXM1_AUD_WR_REQ_GNT_DBG_STAT_REG               (0x0504)  /* 0xA000504 */
#define QAIF_QXM0_CODEC_RX_WR_REQ_DBG_STAT_REG              (0x0508)  /* 0xA000508 */
#define QAIF_QXM0_CODEC_RX_WR_GNT_DBG_STAT_REG              (0x050C)  /* 0xA00050C */
#define QAIF_QXM1_CODEC_RX_WR_REQ_DBG_STAT_REG              (0x0510)  /* 0xA000510 */
#define QAIF_QXM1_CODEC_RX_WR_GNT_DBG_STAT_REG              (0x0514)  /* 0xA000514 */
#define QAIF_QXM0_AUD_RD_REQ_GNT_DBG_STAT_REG               (0x0518)  /* 0xA000518 */
#define QAIF_QXM1_AUD_RD_REQ_GNT_DBG_STAT_REG               (0x051C)  /* 0xA00051C */
#define QAIF_QXM0_CODEC_TX_RD_REQ_DBG_STAT_REG              (0x0520)  /* 0xA000520 */
#define QAIF_QXM0_CODEC_TX_RD_GNT_DBG_STAT_REG              (0x0524)  /* 0xA000524 */
#define QAIF_QXM1_CODEC_TX_RD_REQ_DBG_STAT_REG              (0x0528)  /* 0xA000528 */
#define QAIF_QXM1_CODEC_TX_RD_GNT_DBG_STAT_REG              (0x052C)  /* 0xA00052C */
#define QAIF_QXM0_EXT_RDDMA_RD_REQ_GNT_DBG_STAT_REG         (0x0530)  /* 0xA000530 */
#define QAIF_QXM1_EXT_RDDMA_RD_REQ_GNT_DBG_STAT_REG         (0x0534)  /* 0xA000534 */

/* QSB transaction debug */
#define QAIF_QSB_AUD_WR_TXN_DBG_STAT_REG                    (0x0538)  /* 0xA000538 */
#define QAIF_QSB_CODEC_RX_WR_TXN_ERR_DBG_STAT_REG           (0x053C)  /* 0xA00053C */
#define QAIF_QSB_CODEC_RX_WR_TXN_OKAY_DBG_STAT_REG          (0x0540)  /* 0xA000540 */
#define QAIF_QSB_AUD_ADDR_SENT_DBG_STAT_REG                 (0x0544)  /* 0xA000544 */
#define QAIF_QSB_CODEC_TX_RD_ADDR_SENT_DBG_STAT_REG         (0x0548)  /* 0xA000548 */
#define QAIF_QSB_EXT_RDDMA_RD_ADDR_SENT_DBG_STAT_REG        (0x054C)  /* 0xA00054C */
#define QAIF_QSB_CODEC_RX_WR_ADDR_SENT_DBG_STAT_REG         (0x0550)  /* 0xA000550 */
#define QAIF_QSB_AUD_RD_TXN_DBG_STAT_REG                    (0x0554)  /* 0xA000554 */
#define QAIF_QSB_CODEC_TX_RD_TXN_ERR_DBG_STAT_REG           (0x0558)  /* 0xA000558 */
#define QAIF_QSB_CODEC_TX_RD_TXN_RCVD_DBG_STAT_REG          (0x055C)  /* 0xA00055C */
#define QAIF_QSB_EXT_RDDMA_RD_TXN_DBG_STAT_REG              (0x0560)  /* 0xA000560 */
#define QAIF_QSB_MISC_DBG_STATUS_REG                        (0x0564)  /* 0xA000564 */

/* Global spare and HWE */
#define QAIF_GLOBAL_SPARE_IN_REG                            (0x0B00)  /* 0xA000B00 */
#define QAIF_GLOBAL_SPARE_OUT_REG                           (0x0B04)  /* 0xA000B04 */
#define QAIF_HWE_CFG_REG                                    (0x0B08)  /* 0xA000B08 */

/* SID maps */
#define QAIF_WRDMA_SID_MAP_REG                              (0x1B00)  /* 0xA001B00 */
#define QAIF_CODEC_WRDMA_SID_MAP_REG                        (0x1B40)  /* 0xA001B40 */
#define QAIF_RDDMA_SID_MAP_REG                              (0x1C00)  /* 0xA001C00 */
#define QAIF_CODEC_RDDMA_SID_MAP_REG                        (0x1C40)  /* 0xA001C40 */

/* EE overlap interrupts */
#define QAIF_EE_OVERLAP_IRQ_EN_REG                          (0x1D00) /* 0xA001D00 */
#define QAIF_EE_OVERLAP_IRQ_RAW_STATUS_REG                  (0x1D04) /* 0xA001D04 */
#define QAIF_EE_OVERLAP_IRQ_CLEAR_REG                       (0x1D08) /* 0xA001D08 */
#define QAIF_EE_OVERLAP_IRQ_FORCE_REG                       (0x1D0C) /* 0xA001D0C */

/* EE assignments and maps */
/* 0xA019148 */
#define QAIF_EE_RDDMA_ASSIGNMENT_REG(v)	(0x19148 + (0x1000 * ((v)->ee)))
/* 0xA019150 */
#define QAIF_EE_WRDMA_ASSIGNMENT_REG(v)	(0x19150 + (0x1000 * ((v)->ee)))
/* 0xA019158 */
#define QAIF_EE_INTF_ASSIGNMENT_REG(v)	(0x19158 + (0x1000 * ((v)->ee)))
/* 0xA019308 */
#define QAIF_EE_CODEC_RDDMA_ASSIGNMENT_REG(v)	(0x19308 + (0x1000 * ((v)->ee)))
/* 0xA019318 */
#define QAIF_EE_CODEC_WRDMA_ASSIGNMENT_REG(v)	(0x19318 + (0x1000 * ((v)->ee)))
/* 0xA001920 */
#define QAIF_EE_RDDMA_MAP_REG(v)	(0x1920 + (0x1000 * ((v)->ee)))
/* 0xA001940 */
#define QAIF_EE_WRDMA_MAP_REG(v)	(0x1940 + (0x1000 * ((v)->ee)))
/* 0xA001960 */
#define QAIF_EE_INTF_MAP_REG(v)		(0x1960 + (0x1000 * ((v)->ee)))
/* 0xA001980 */
#define QAIF_EE_CODEC_RDDMA_MAP_REG(v)	(0x1980 + (0x1000 * ((v)->ee)))
/* 0xA001A00 */
#define QAIF_EE_CODEC_WRDMA_MAP_REG(v)	(0x1A00 + (0x1000 * ((v)->ee)))

/* EE rate-detection and VFR interrupts */
/* 0xA0190F0 */
#define QAIF_EE_RATE_DET_IRQ_EN_REG(v)		(0x190F0 + (0x1000 * ((v)->ee)))
/* 0xA0190F4 */
#define QAIF_EE_RATE_DET_IRQ_STATUS_REG(v)	(0x190F4 + (0x1000 * ((v)->ee)))
/* 0xA0190F8 */
#define QAIF_EE_RATE_DET_IRQ_RAW_STATUS_REG(v)	(0x190F8 + (0x1000 * ((v)->ee)))
/* 0xA0190FC */
#define QAIF_EE_RATE_DET_IRQ_CLEAR_REG(v)	(0x190FC + (0x1000 * ((v)->ee)))
/* 0xA019100 */
#define QAIF_EE_RATE_DET_IRQ_FORCE_REG(v)	(0x19100 + (0x1000 * ((v)->ee)))

/* 0xA019104 */
#define QAIF_EE_VFR_IRQ_EN_REG(v)		(0x19104 + (0x1000 * ((v)->ee)))
/* 0xA019108 */
#define QAIF_EE_VFR_IRQ_STATUS_REG(v)		(0x19108 + (0x1000 * ((v)->ee)))
/* 0xA01910C */
#define QAIF_EE_VFR_IRQ_RAW_STATUS_REG(v)	(0x1910C + (0x1000 * ((v)->ee)))
/* 0xA019110 */
#define QAIF_EE_VFR_IRQ_CLEAR_REG(v)		(0x19110 + (0x1000 * ((v)->ee)))
/* 0xA019114 */
#define QAIF_EE_VFR_IRQ_FORCE_REG(v)		(0x19114 + (0x1000 * ((v)->ee)))

/* EE AUD_INTF underflow/overflow interrupts */
/* 0xA019160 */
#define QAIF_EE_AUD_INTF_UNDERFLOW_IRQ_EN_REG(v) \
	(0x19160 + (0x1000 * ((v)->ee)))
/* 0xA019164 */
#define QAIF_EE_AUD_INTF_UNDERFLOW_IRQ_STATUS_REG(v) \
	(0x19164 + (0x1000 * ((v)->ee)))
/* 0xA019168 */
#define QAIF_EE_AUD_INTF_UNDERFLOW_IRQ_RAW_STATUS_REG(v) \
	(0x19168 + (0x1000 * ((v)->ee)))
/* 0xA01916C */
#define QAIF_EE_AUD_INTF_UNDERFLOW_IRQ_CLEAR_REG(v) \
	(0x1916C + (0x1000 * ((v)->ee)))
/* 0xA019170 */
#define QAIF_EE_AUD_INTF_UNDERFLOW_IRQ_FORCE_REG(v) \
	(0x19170 + (0x1000 * ((v)->ee)))

/* 0xA019174 */
#define QAIF_EE_AUD_INTF_OVERFLOW_IRQ_EN_REG(v) \
	(0x19174 + (0x1000 * ((v)->ee)))
/* 0xA019178 */
#define QAIF_EE_AUD_INTF_OVERFLOW_IRQ_STATUS_REG(v) \
	(0x19178 + (0x1000 * ((v)->ee)))
/* 0xA01917C */
#define QAIF_EE_AUD_INTF_OVERFLOW_IRQ_RAW_STATUS_REG(v) \
	(0x1917C + (0x1000 * ((v)->ee)))
/* 0xA019180 */
#define QAIF_EE_AUD_INTF_OVERFLOW_IRQ_CLEAR_REG(v) \
	(0x19180 + (0x1000 * ((v)->ee)))
/* 0xA019184 */
#define QAIF_EE_AUD_INTF_OVERFLOW_IRQ_FORCE_REG(v) \
	(0x19184 + (0x1000 * ((v)->ee)))

/* EE L2 Period IRQ mux selection */
/* 0xA019F00 */
#define QAIF_EE_L2_PERIOD_IRQ_0_3_MUX_SEL_REG(v) \
	(0x19F00 + (0x1000 * ((v)->ee)))
/* 0xA019F04 */
#define QAIF_EE_L2_PERIOD_IRQ_4_7_MUX_SEL_REG(v) \
	(0x19F04 + (0x1000 * ((v)->ee)))

/* AUD_INTF block (per interface, stride 0x1000 starting at 0x4000) */
#define QAIF_AUD_INTF_REG_ADDR(offset, intf) \
	(0x4000 + (offset) + (0x1000 * (intf)))

/* 0xA004000 */
#define QAIF_AUD_INTF_CTL_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0000, (intf))
/* 0xA004004 */
#define QAIF_AUD_INTF_SYNC_CFG_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0004, (intf))
/* 0xA004008 */
#define QAIF_AUD_INTF_BIT_WIDTH_CFG_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0008, (intf))
/* 0xA00400C */
#define QAIF_AUD_INTF_FRAME_CFG_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x000C, (intf))
/* 0xA004010 */
#define QAIF_AUD_INTF_ACTV_SLOT_EN_TX_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0010, (intf))
/* 0xA004030 */
#define QAIF_AUD_INTF_ACTV_SLOT_EN_RX_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0030, (intf))
/* 0xA004050 */
#define QAIF_AUD_INTF_LANE_CFG_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0050, (intf))
/* 0xA004054 */
#define QAIF_AUD_INTF_MI2S_CFG_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0054, (intf))
/* 0xA004058 */
#define QAIF_AUD_INTF_CFG_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0058, (intf))
/* 0xA00405C */
#define QAIF_AUD_INTF_CHAR_CTL_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x005C, (intf))
/* 0xA004060 */
#define QAIF_AUD_INTF_CHAR_CFG_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0060, (intf))
/* 0xA004064 */
#define QAIF_AUD_INTF_CHAR_DATA_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0064, (intf))
/* 0xA004068 */
#define QAIF_AUD_INTF_CHAR_DATA_EXT_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0068, (intf))
/* 0xA00406C */
#define QAIF_AUD_INTF_CHAR_SYNC_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x006C, (intf))
/* 0xA004FF0 */
#define QAIF_AUD_INTF_INIT_DBG_STATUS_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0FF0, (intf))
/* 0xA004FF4 */
#define QAIF_AUD_INTF_TX_DBG_STATUS_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0FF4, (intf))
/* 0xA004FF8 */
#define QAIF_AUD_INTF_RX_DBG_STATUS_REG(intf) \
	QAIF_AUD_INTF_REG_ADDR(0x0FF8, (intf))

/* RATE_DET block (per detector, stride 0x1000 starting at 0x1E000) */
#define QAIF_RATE_DET_REG_ADDR(offset, det) \
	(0x1E000 + (offset) + (0x1000 * (det)))

/* 0xA01E000 */
#define QAIF_RATE_DET_CONFIG_REG(det) \
	QAIF_RATE_DET_REG_ADDR(0x0000, (det))
/* 0xA01E004 */
#define QAIF_RATE_DET_TARGET1_CONFIG_REG(det) \
	QAIF_RATE_DET_REG_ADDR(0x0004, (det))
/* 0xA01E008 */
#define QAIF_RATE_DET_TARGET2_CONFIG_REG(det) \
	QAIF_RATE_DET_REG_ADDR(0x0008, (det))
/* 0xA01E00C */
#define QAIF_RATE_DET_BIN_REG(det) \
	QAIF_RATE_DET_REG_ADDR(0x000C, (det))
/* 0xA01E010 */
#define QAIF_RATE_DET_STC_DIFF_REG(det) \
	QAIF_RATE_DET_REG_ADDR(0x0010, (det))
/* 0xA01E014 */
#define QAIF_RATE_DET_SEL_REG(det) \
	QAIF_RATE_DET_REG_ADDR(0x0014, (det))
/* 0xA01E018 */
#define QAIF_RATE_DET_TIMEOUT_CFG_REG(det) \
	QAIF_RATE_DET_REG_ADDR(0x0018, (det))

#define QAIF_WRDMA_MAP_QXM				(0x1000)
#define QAIF_CODEC_WRDMA_MAP_QXM			(0x1004)
#define QAIF_RDDMA_MAP_QXM				(0x1010)
#define QAIF_CODEC_RDDMA_MAP_QXM			(0x1014)
#define QAIF_RDDMA_QXM1_SHRAM_ST_ADDR(i)		(0x1100 + (0x4 * (i)))
#define QAIF_CODEC_RDDMA_QXM1_SHRAM_ST_ADDR(i)		(0x1140 + (0x4 * (i)))
#define QAIF_RDDMA_QXM0_SHRAM_ST_ADDR(i)		(0x1200 + (0x4 * (i)))
#define QAIF_CODEC_RDDMA_QXM0_SHRAM_ST_ADDR(i)		(0x1240 + (0x4 * (i)))
#define QAIF_RDDMA_QXM1_SHRAM_LEN(i)			(0x1300 + (0x4 * (i)))
#define QAIF_CODEC_RDDMA_QXM1_SHRAM_LEN(i)		(0x1340 + (0x4 * (i)))
#define QAIF_RDDMA_QXM0_SHRAM_LEN(i)			(0x1400 + (0x4 * (i)))
#define QAIF_CODEC_RDDMA_QXM0_SHRAM_LEN(i)		(0x1440 + (0x4 * (i)))
#define QAIF_WRDMA_QXM1_SHRAM_ST_ADDR(i)		(0x1500 + (0x4 * (i)))
#define QAIF_CODEC_WRDMA_QXM1_SHRAM_ST_ADDR(i)		(0x1540 + (0x4 * (i)))
#define QAIF_WRDMA_QXM0_SHRAM_ST_ADDR(i)		(0x1600 + (0x4 * (i)))
#define QAIF_CODEC_WRDMA_QXM0_SHRAM_ST_ADDR(i)		(0x1640 + (0x4 * (i)))
#define QAIF_WRDMA_QXM1_SHRAM_LEN(i)			(0x1700 + (0x4 * (i)))
#define QAIF_CODEC_WRDMA_QXM1_SHRAM_LEN(i)		(0x1740 + (0x4 * (i)))
#define QAIF_WRDMA_QXM0_SHRAM_LEN(i)			(0x1800 + (0x4 * (i)))
#define QAIF_CODEC_WRDMA_QXM0_SHRAM_LEN(i)		(0x1840 + (0x4 * (i)))

/*
 * RDDMA
 * v : ptr to qaif_variant
 */
static inline u32 QAIF_RDDMA_REG_ADDR(const struct qaif_variant *v, u32 offset, u32 chan)
{
	return (v)->rddma_reg_base + offset + (v)->rddma_stride * chan;
}

#define QAIF_RDDMA_CTL_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x00, (chan))
#define QAIF_RDDMA_CFG_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x04, (chan))
#define QAIF_RDDMA_BASE_ADDR_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x08, (chan))
#define QAIF_RDDMA_BUFF_LEN_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x10, (chan))
#define QAIF_RDDMA_CURR_ADDR_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x14, (chan))
#define QAIF_RDDMA_PERIOD_LEN_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x1C, (chan))
#define QAIF_RDDMA_PERIOD_CNT_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x20, (chan))
#define QAIF_RDDMA_SHRAM_WORDCNT_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x24, (chan))
#define QAIF_RDDMA_FRAME_STATUS_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x28, (chan))
#define QAIF_RDDMA_FRAME_STATUS_EXTN_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x2C, (chan))
#define QAIF_RDDMA_FRAME_STATUS_CLR_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x30, (chan))
#define QAIF_RDDMA_SET_BUFF_CNT_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x34, (chan))
#define QAIF_RDDMA_SET_PERIOD_CNT_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x38, (chan))
#define QAIF_RDDMA_STC_LSB_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x3C, (chan))
#define QAIF_RDDMA_STC_MSB_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x40, (chan))
#define QAIF_RDDMA_PERIOD_DET_STAT_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x44, (chan))
#define QAIF_RDDMA_PERIOD_DET_CLR_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x48, (chan))
#define QAIF_RDDMA_FORMAT_ERR_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x4C, (chan))
#define QAIF_RDDMA_AHB_BYPASS_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x50, (chan))
#define QAIF_RDDMA_SHUTDOWN_STAT_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x54, (chan))
#define QAIF_RDDMA_PADDING_CFG_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x58, (chan))
#define QAIF_RDDMA_STATUS_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0x60, (chan))
#define QAIF_RDDMA_DBG_STATUS_REG(v, chan) \
	QAIF_RDDMA_REG_ADDR(v, 0xFF0, (chan))

static inline u32 QAIF_CODEC_RDDMA_REG_ADDR(const struct qaif_variant *v, u32 offset, u32 chan)
{
	return (v)->codec_rddma_reg_base + offset + (v)->codec_rddma_stride * chan;
}

#define QAIF_CODEC_RDDMA_CTL_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x00, (chan))
#define QAIF_CODEC_RDDMA_CFG_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x04, (chan))
#define QAIF_CODEC_RDDMA_BASE_ADDR_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x08, (chan))
#define QAIF_CODEC_RDDMA_BUFF_LEN_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x10, (chan))
#define QAIF_CODEC_RDDMA_CURR_ADDR_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x14, (chan))
#define QAIF_CODEC_RDDMA_PERIOD_LEN_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x1C, (chan))
#define QAIF_CODEC_RDDMA_PERIOD_CNT_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x20, (chan))
#define QAIF_CODEC_RDDMA_SHRAM_WORDCNT_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x24, (chan))
#define QAIF_CODEC_RDDMA_FRAME_STATUS_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x28, (chan))
#define QAIF_CODEC_RDDMA_FRAME_STATUS_EXTN_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x2C, (chan))
#define QAIF_CODEC_RDDMA_FRAME_STATUS_CLR_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x30, (chan))
#define QAIF_CODEC_RDDMA_SET_BUFF_CNT_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x34, (chan))
#define QAIF_CODEC_RDDMA_SET_PERIOD_CNT_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x38, (chan))
#define QAIF_CODEC_RDDMA_STC_LSB_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x3C, (chan))
#define QAIF_CODEC_RDDMA_STC_MSB_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x40, (chan))
#define QAIF_CODEC_RDDMA_PERIOD_DET_STAT_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x44, (chan))
#define QAIF_CODEC_RDDMA_PERIOD_DET_CLR_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x48, (chan))
#define QAIF_CODEC_RDDMA_FORMAT_ERR_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x4C, (chan))
#define QAIF_CODEC_RDDMA_AHB_BYPASS_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x50, (chan))
#define QAIF_CODEC_RDDMA_SHUTDOWN_STAT_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x54, (chan))
#define QAIF_CODEC_RDDMA_PADDING_CFG_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x58, (chan))
#define QAIF_CODEC_RDDMA_INTF_CFG_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x5C, (chan))
#define QAIF_CODEC_RDDMA_STATUS_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0x60, (chan))
#define QAIF_CODEC_RDDMA_DBG_STATUS_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0xFF0, (chan))
#define QAIF_CODEC_RDDMA_INTF_DBG_STATUS_REG(v, chan) \
	QAIF_CODEC_RDDMA_REG_ADDR(v, 0xFF4, (chan))

/*
 * WRDMA
 * v : ptr to qaif_variant
 */
static inline u32 QAIF_WRDMA_REG_ADDR(const struct qaif_variant *v, u32 offset, u32 chan)
{
	return (v)->wrdma_reg_base + offset + (v)->wrdma_stride * chan;
}

#define QAIF_WRDMA_CTL_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x00, (chan))
#define QAIF_WRDMA_CFG_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x04, (chan))
#define QAIF_WRDMA_BASE_ADDR_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x08, (chan))
#define QAIF_WRDMA_BUFF_LEN_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x10, (chan))
#define QAIF_WRDMA_CURR_ADDR_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x14, (chan))
#define QAIF_WRDMA_PERIOD_LEN_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x1C, (chan))
#define QAIF_WRDMA_PERIOD_CNT_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x20, (chan))
#define QAIF_WRDMA_SHRAM_WORDCNT_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x24, (chan))
#define QAIF_WRDMA_FRAME_STATUS_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x28, (chan))
#define QAIF_WRDMA_FRAME_STATUS_EXTN_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x2C, (chan))
#define QAIF_WRDMA_FRAME_STATUS_CLR_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x30, (chan))
#define QAIF_WRDMA_SET_BUFF_CNT_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x34, (chan))
#define QAIF_WRDMA_SET_PERIOD_CNT_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x38, (chan))
#define QAIF_WRDMA_STC_LSB_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x3C, (chan))
#define QAIF_WRDMA_STC_MSB_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x40, (chan))
#define QAIF_WRDMA_PERIOD_DET_STAT_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x44, (chan))
#define QAIF_WRDMA_PERIOD_DET_CLR_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x48, (chan))
#define QAIF_WRDMA_FORMAT_ERR_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x4C, (chan))
#define QAIF_WRDMA_AHB_BYPASS_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x50, (chan))
#define QAIF_WRDMA_SHUTDOWN_STAT_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0x54, (chan))
#define QAIF_WRDMA_DBG_STATUS_REG(v, chan) \
	QAIF_WRDMA_REG_ADDR(v, 0xFF0, (chan))

static inline u32 QAIF_CODEC_WRDMA_REG_ADDR(const struct qaif_variant *v, u32 offset, u32 chan)
{
	return (v)->codec_wrdma_reg_base + offset + (v)->codec_wrdma_stride * chan;
}

#define QAIF_CODEC_WRDMA_CTL_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x00, (chan))
#define QAIF_CODEC_WRDMA_CFG_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x04, (chan))
#define QAIF_CODEC_WRDMA_BASE_ADDR_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x08, (chan))
#define QAIF_CODEC_WRDMA_BUFF_LEN_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x10, (chan))
#define QAIF_CODEC_WRDMA_CURR_ADDR_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x14, (chan))
#define QAIF_CODEC_WRDMA_PERIOD_LEN_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x1C, (chan))
#define QAIF_CODEC_WRDMA_PERIOD_CNT_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x20, (chan))
#define QAIF_CODEC_WRDMA_SHRAM_WORDCNT_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x24, (chan))
#define QAIF_CODEC_WRDMA_FRAME_STATUS_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x28, (chan))
#define QAIF_CODEC_WRDMA_FRAME_STATUS_EXTN_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x2C, (chan))
#define QAIF_CODEC_WRDMA_FRAME_STATUS_CLR_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x30, (chan))
#define QAIF_CODEC_WRDMA_SET_BUFF_CNT_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x34, (chan))
#define QAIF_CODEC_WRDMA_SET_PERIOD_CNT_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x38, (chan))
#define QAIF_CODEC_WRDMA_STC_LSB_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x3C, (chan))
#define QAIF_CODEC_WRDMA_STC_MSB_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x40, (chan))
#define QAIF_CODEC_WRDMA_PERIOD_DET_STAT_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x44, (chan))
#define QAIF_CODEC_WRDMA_PERIOD_DET_CLR_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x48, (chan))
#define QAIF_CODEC_WRDMA_FORMAT_ERR_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x4C, (chan))
#define QAIF_CODEC_WRDMA_AHB_BYPASS_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x50, (chan))
#define QAIF_CODEC_WRDMA_SHUTDOWN_STAT_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x54, (chan))
#define QAIF_CODEC_WRDMA_INTF_CFG_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0x58, (chan))
#define QAIF_CODEC_WRDMA_DBG_STATUS_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0xFF0, (chan))
#define QAIF_CODEC_WRDMA_INTF_DBG_STATUS_REG(v, chan) \
	QAIF_CODEC_WRDMA_REG_ADDR(v, 0xFF4, (chan))

static inline u32 QAIF_EE_RDDMA_IRQ_REG_ADDR(const struct qaif_variant *v,
					     enum qaif_irq_type_t dma_type,
					     u32 offset)
{
	if (dma_type == QAIF_AIF_IRQ)
		return (v)->rddma_irq_reg_base + offset +
			(v)->rddma_irq_stride * (v)->ee;
	return (v)->codec_rddma_irq_reg_base + offset +
		(v)->codec_rddma_irq_stride * (v)->ee;
}

/* RDDMA Period Interrupts */
#define QAIF_EE_RDDMA_PERIOD_IRQ_EN_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x00)
#define QAIF_EE_RDDMA_PERIOD_IRQ_STAT_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x08)
#define QAIF_EE_RDDMA_PERIOD_IRQ_RAW_STAT_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x10)
#define QAIF_EE_RDDMA_PERIOD_IRQ_CLR_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x18)
#define QAIF_EE_RDDMA_PERIOD_IRQ_FORCE_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x20)
/* RDDMA Underflow Interrupts */
#define QAIF_EE_RDDMA_UNDERFLOW_IRQ_EN_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x28)
#define QAIF_EE_RDDMA_UNDERFLOW_IRQ_STAT_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x30)
#define QAIF_EE_RDDMA_UNDERFLOW_IRQ_RAW_STAT_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x38)
#define QAIF_EE_RDDMA_UNDERFLOW_IRQ_CLR_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x40)
#define QAIF_EE_RDDMA_UNDERFLOW_IRQ_FORCE_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x48)
/* RDDMA Error Response Interrupts */
#define QAIF_EE_RDDMA_ERR_RSP_IRQ_EN_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x50)
#define QAIF_EE_RDDMA_ERR_RSP_IRQ_STAT_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x58)
#define QAIF_EE_RDDMA_ERR_RSP_IRQ_RAW_STAT_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x60)
#define QAIF_EE_RDDMA_ERR_RSP_IRQ_CLR_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x68)
#define QAIF_EE_RDDMA_ERR_RSP_IRQ_FORCE_REG(v, i) \
	QAIF_EE_RDDMA_IRQ_REG_ADDR(v, i, 0x70)

static inline u32 QAIF_EE_WRDMA_IRQ_REG_ADDR(const struct qaif_variant *v,
					     enum qaif_irq_type_t dma_type,
					     u32 offset)
{
	if (dma_type == QAIF_AIF_IRQ)
		return (v)->wrdma_irq_reg_base + offset +
			(v)->wrdma_irq_stride * (v)->ee;
	return (v)->codec_wrdma_irq_reg_base + offset +
		(v)->codec_wrdma_irq_stride * (v)->ee;
}

/* WRDMA Period Interrupts */
#define QAIF_EE_WRDMA_PERIOD_IRQ_EN_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x00)
#define QAIF_EE_WRDMA_PERIOD_IRQ_STAT_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x08)
#define QAIF_EE_WRDMA_PERIOD_IRQ_RAW_STAT_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x10)
#define QAIF_EE_WRDMA_PERIOD_IRQ_CLR_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x18)
#define QAIF_EE_WRDMA_PERIOD_IRQ_FORCE_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x20)
/* WRDMA Overflow Interrupts */
#define QAIF_EE_WRDMA_OVERFLOW_IRQ_EN_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x28)
#define QAIF_EE_WRDMA_OVERFLOW_IRQ_STAT_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x30)
#define QAIF_EE_WRDMA_OVERFLOW_IRQ_RAW_STAT_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x38)
#define QAIF_EE_WRDMA_OVERFLOW_IRQ_CLR_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x40)
#define QAIF_EE_WRDMA_OVERFLOW_IRQ_FORCE_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x48)
/* WRDMA Error Response Interrupts */
#define QAIF_EE_WRDMA_ERR_RSP_IRQ_EN_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x50)
#define QAIF_EE_WRDMA_ERR_RSP_IRQ_STAT_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x58)
#define QAIF_EE_WRDMA_ERR_RSP_IRQ_RAW_STAT_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x60)
#define QAIF_EE_WRDMA_ERR_RSP_IRQ_CLR_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x68)
#define QAIF_EE_WRDMA_ERR_RSP_IRQ_FORCE_REG(v, i) \
	QAIF_EE_WRDMA_IRQ_REG_ADDR(v, i, 0x70)

static inline u32 QAIF_DMACFG_REG(const struct qaif_variant *v, u32 chan, int dir, int dai_id)
{
	if (is_cif_dma_port(dai_id)) {
		if (dir == SNDRV_PCM_STREAM_PLAYBACK)
			return QAIF_CODEC_RDDMA_CFG_REG(v, chan);
		return QAIF_CODEC_WRDMA_CFG_REG(v, chan);
	}
	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		return QAIF_RDDMA_CFG_REG(v, chan);
	return QAIF_WRDMA_CFG_REG(v, chan);
}

static inline u32 QAIF_DMACTL_REG(const struct qaif_variant *v, u32 chan, int dir, int dai_id)
{
	if (is_cif_dma_port(dai_id)) {
		if (dir == SNDRV_PCM_STREAM_PLAYBACK)
			return QAIF_CODEC_RDDMA_CTL_REG(v, chan);
		return QAIF_CODEC_WRDMA_CTL_REG(v, chan);
	}
	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		return QAIF_RDDMA_CTL_REG(v, chan);
	return QAIF_WRDMA_CTL_REG(v, chan);
}

static inline u32 QAIF_DMABUFF_REG(const struct qaif_variant *v, u32 chan, int dir, int dai_id)
{
	if (is_cif_dma_port(dai_id)) {
		if (dir == SNDRV_PCM_STREAM_PLAYBACK)
			return QAIF_CODEC_RDDMA_BUFF_LEN_REG(v, chan);
		return QAIF_CODEC_WRDMA_BUFF_LEN_REG(v, chan);
	}
	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		return QAIF_RDDMA_BUFF_LEN_REG(v, chan);
	return QAIF_WRDMA_BUFF_LEN_REG(v, chan);
}

static inline u32 QAIF_DMACURR_REG(const struct qaif_variant *v, u32 chan, int dir, int dai_id)
{
	if (is_cif_dma_port(dai_id)) {
		if (dir == SNDRV_PCM_STREAM_PLAYBACK)
			return QAIF_CODEC_RDDMA_CURR_ADDR_REG(v, chan);
		return QAIF_CODEC_WRDMA_CURR_ADDR_REG(v, chan);
	}
	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		return QAIF_RDDMA_CURR_ADDR_REG(v, chan);
	return QAIF_WRDMA_CURR_ADDR_REG(v, chan);
}

static inline u32 QAIF_DMAPER_REG(const struct qaif_variant *v, u32 chan, int dir, int dai_id)
{
	if (is_cif_dma_port(dai_id)) {
		if (dir == SNDRV_PCM_STREAM_PLAYBACK)
			return QAIF_CODEC_RDDMA_PERIOD_CNT_REG(v, chan);
		return QAIF_CODEC_WRDMA_PERIOD_CNT_REG(v, chan);
	}
	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		return QAIF_RDDMA_PERIOD_CNT_REG(v, chan);
	return QAIF_WRDMA_PERIOD_CNT_REG(v, chan);
}

static inline u32 QAIF_DMAPER_LEN_REG(const struct qaif_variant *v, u32 chan, int dir, int dai_id)
{
	if (is_cif_dma_port(dai_id)) {
		if (dir == SNDRV_PCM_STREAM_PLAYBACK)
			return QAIF_CODEC_RDDMA_PERIOD_LEN_REG(v, chan);
		return QAIF_CODEC_WRDMA_PERIOD_LEN_REG(v, chan);
	}
	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		return QAIF_RDDMA_PERIOD_LEN_REG(v, chan);
	return QAIF_WRDMA_PERIOD_LEN_REG(v, chan);
}

static inline u32 QAIF_DMABASE_REG(const struct qaif_variant *v, u32 chan, int dir, int dai_id)
{
	if (is_cif_dma_port(dai_id)) {
		if (dir == SNDRV_PCM_STREAM_PLAYBACK)
			return QAIF_CODEC_RDDMA_BASE_ADDR_REG(v, chan);
		return QAIF_CODEC_WRDMA_BASE_ADDR_REG(v, chan);
	}
	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		return QAIF_RDDMA_BASE_ADDR_REG(v, chan);
	return QAIF_WRDMA_BASE_ADDR_REG(v, chan);
}

static inline u32 QAIF_SID_MAP_REG(int dir, int dai_id)
{
	if (is_cif_dma_port(dai_id)) {
		if (dir == SNDRV_PCM_STREAM_PLAYBACK)
			return QAIF_CODEC_RDDMA_SID_MAP_REG;
		return QAIF_CODEC_WRDMA_SID_MAP_REG;
	}
	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		return QAIF_RDDMA_SID_MAP_REG;
	return QAIF_WRDMA_SID_MAP_REG;
}

#endif /* __QAIF_REG_H__ */
