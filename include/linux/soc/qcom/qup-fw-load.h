/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _LINUX_QCOM_QUP_FW_LOAD
#define _LINUX_QCOM_QUP_FW_LOAD

#include <linux/kernel.h>

/*Magic numbers*/
#define MAGIC_NUM_SE			0x57464553

#define MAX_GENI_CFG_RAMn_CNT		455

#define MI_PBT_NON_PAGED_SEGMENT	0x0
#define MI_PBT_HASH_SEGMENT		0x2
#define MI_PBT_NOTUSED_SEGMENT		0x3
#define MI_PBT_SHARED_SEGMENT		0x4

#define MI_PBT_FLAG_PAGE_MODE		BIT(20)
#define MI_PBT_FLAG_SEGMENT_TYPE	GENMASK(26, 24)
#define MI_PBT_FLAG_ACCESS_TYPE		GENMASK(23, 21)

#define MI_PBT_PAGE_MODE_VALUE(x) FIELD_GET(MI_PBT_FLAG_PAGE_MODE, x)

#define MI_PBT_SEGMENT_TYPE_VALUE(x) FIELD_GET(MI_PBT_FLAG_SEGMENT_TYPE, x)

#define MI_PBT_ACCESS_TYPE_VALUE(x) FIELD_GET(MI_PBT_FLAG_ACCESS_TYPE, x)

#define M_COMMON_GENI_M_IRQ_EN	(GENMASK(6, 1) | \
				M_IO_DATA_DEASSERT_EN | \
				M_IO_DATA_ASSERT_EN | M_RX_FIFO_RD_ERR_EN | \
				M_RX_FIFO_WR_ERR_EN | M_TX_FIFO_RD_ERR_EN | \
				M_TX_FIFO_WR_ERR_EN)

/* DMA_TX/RX_IRQ_EN fields */
#define DMA_DONE_EN		BIT(0)
#define SBE_EN			BIT(2)
#define RESET_DONE_EN		BIT(3)
#define FLUSH_DONE_EN		BIT(4)

/* GENI_CLK_CTRL fields */
#define SER_CLK_SEL		BIT(0)

/* GENI_DMA_IF_EN fields */
#define DMA_IF_EN		BIT(0)

#define QUPV3_COMMON_CFG		0x120
#define FAST_SWITCH_TO_HIGH_DISABLE	BIT(0)

#define QUPV3_SE_AHB_M_CFG		0x118
#define AHB_M_CLK_CGC_ON		BIT(0)

#define QUPV3_COMMON_CGC_CTRL		0x21C
#define COMMON_CSR_SLV_CLK_CGC_ON	BIT(0)

/* access ports */
#define geni_setbits32(_addr, _v) writel_relaxed(readl_relaxed(_addr) |  (_v), _addr)
#define geni_clrbits32(_addr, _v) writel_relaxed(readl_relaxed(_addr) & ~(_v), _addr)

/**
 * struct elf_se_hdr - firmware configurations
 *
 * @magic: set to 'SEFW'
 * @version: A 32-bit value indicating the structure’s version number
 * @core_version: QUPV3_HW_VERSION
 * @serial_protocol: Programmed into GENI_FW_REVISION
 * @fw_version: Programmed into GENI_FW_REVISION
 * @cfg_version: Programmed into GENI_INIT_CFG_REVISION
 * @fw_size_in_items: Number of (uint32_t) GENI_FW_RAM words
 * @fw_offset: Byte offset of GENI_FW_RAM array
 * @cfg_items_size: Number of GENI_FW_CFG index/value pairs
 * @cfg_idx_offset: Byte offset of GENI_FW_CFG index array
 * @cfg_val_offset: Byte offset of GENI_FW_CFG values array
 */
struct elf_se_hdr {
	u32 magic;
	u32 version;
	u32 core_version;
	u16 serial_protocol;
	u16 fw_version;
	u16 cfg_version;
	u16 fw_size_in_items;
	u16 fw_offset;
	u16 cfg_items_size;
	u16 cfg_idx_offset;
	u16 cfg_val_offset;
};
#endif /* _LINUX_QCOM_QUP_FW_LOAD */
