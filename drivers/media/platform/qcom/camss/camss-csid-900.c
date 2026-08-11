// SPDX-License-Identifier: GPL-2.0
/*
 * camss-csid-900.c
 *
 * Qualcomm MSM Camera Subsystem - CSID (CSI Decoder) Module 900
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include "camss.h"
#include "camss-csid.h"
#include "camss-csid-gen3.h"

/* Reset and Command Registers */
#define CSID_RST_CFG				0xC
#define		RST_MODE				BIT(0)
#define		RST_LOCATION				BIT(4)

#define CSID_RST_CMD				0x10
#define		SELECT_HW_RST				BIT(0)
#define		SELECT_IRQ_RST				BIT(2)
#define CSID_IRQ_CMD				0x14
#define		IRQ_CMD_CLEAR				BIT(0)

/* Register Update Commands, RUP/AUP */
#define CSID_RUP_CMD				0x18
#define CSID_AUP_CMD				0x1C
#define		CSID_RUP_AUP_RDI(rdi)			(BIT(8) << (rdi))
#define CSID_RUP_AUP_CMD			0x20
#define		RUP_SET					BIT(0)
#define		MUP					BIT(4)

/* Top level interrupt registers */
#define CSID_TOP_IRQ_STATUS			0x84
#define CSID_TOP_IRQ_MASK			0x88
#define CSID_TOP_IRQ_CLEAR			0x8C
#define CSID_TOP_IRQ_SET			0x90
#define		INFO_RST_DONE				BIT(0)
#define		CSI2_RX_IRQ_STATUS			BIT(2)
#define		BUF_DONE_IRQ_STATUS			BIT(3)

/* Buffer done interrupt registers */
#define CSID_BUF_DONE_IRQ_STATUS		0xA4
#define		BUF_DONE_IRQ_STATUS_RDI_OFFSET		16
#define CSID_BUF_DONE_IRQ_MASK			0xA8
#define CSID_BUF_DONE_IRQ_CLEAR			0xAC
#define CSID_BUF_DONE_IRQ_SET			0xB0

/* CSI2 RX interrupt registers */
#define CSID_CSI2_RX_IRQ_STATUS			0xB4
#define CSID_CSI2_RX_IRQ_MASK			0xB8
#define CSID_CSI2_RX_IRQ_CLEAR			0xBC
#define CSID_CSI2_RX_IRQ_SET			0xC0

/* CSI2 RX Configuration */
#define CSID_CSI2_RX_CFG0			0x400
#define		CSI2_RX_CFG0_NUM_ACTIVE_LANES		0
#define		CSI2_RX_CFG0_DL0_INPUT_SEL		4
#define		CSI2_RX_CFG0_PHY_NUM_SEL		20
#define		CSI2_RX_CFG0_TPG_MUX_EN			BIT(27)
/* TPG_NUM_SEL: which TPG drives the data, programmed as n == n (TPG0 -> 0) */
#define		CSI2_RX_CFG0_TPG_MUX_SEL		GENMASK(29, 28)
#define CSID_CSI2_RX_CFG1			0x404
#define		CSI2_RX_CFG1_ECC_CORRECTION_EN		BIT(0)
#define		CSI2_RX_CFG1_VC_MODE			BIT(2)

#define MSM_CSID_MAX_SRC_STREAMS_900		(csid_is_lite(csid) ? 4 : 5)

/*
 * v900 uses a uniform register map for both CSID full and CSID lite (unlike
 * v980 where the lite RDI region and IRQ block are laid out differently). Only
 * the RDI region base differs: full at 0x1300, lite at 0x600, stride 0x200.
 */
#define CSID_RDI_BASE				(csid_is_lite(csid) ? 0x600 : 0x1300)
#define CSID_RDI_CFG0(rdi)			(CSID_RDI_BASE + 0x0 + 0x200 * (rdi))
/*
 * Silicon (nordschleife_2.0) names CFG0 bit5 RETIME_DIS = "Disable SOF/EOF
 * strobe retiming" (reset 0). Set here to match the 980, which sets the same
 * bit (as RETIME_BS).
 */
#define		RDI_CFG0_RETIME_DIS			BIT(5)
#define		RDI_CFG0_TIMESTAMP_EN			BIT(6)
#define		RDI_CFG0_TIMESTAMP_STB_SEL		BIT(8)
#define		RDI_CFG0_DECODE_FORMAT			12
#define		RDI_CFG0_DT				16
#define		RDI_CFG0_VC				22
#define		RDI_CFG0_DT_ID				27
#define		RDI_CFG0_EN				BIT(31)

/* RDI Control and Configuration */
#define CSID_RDI_CTRL(rdi)			(CSID_RDI_BASE + 0x4 + 0x200 * (rdi))
#define		RDI_CTRL_START_CMD			BIT(0)

#define CSID_RDI_CFG1(rdi)			(CSID_RDI_BASE + 0x10 + 0x200 * (rdi))
#define		RDI_CFG1_DROP_H_EN			BIT(5)
#define		RDI_CFG1_DROP_V_EN			BIT(6)
#define		RDI_CFG1_CROP_H_EN			BIT(7)
#define		RDI_CFG1_CROP_V_EN			BIT(8)
#define		RDI_CFG1_PACKING_FORMAT_MIPI		BIT(15)

/* RDI Pixel Store Configuration (full only) */
#define CSID_RDI_PIX_STORE_CFG0(rdi)		(CSID_RDI_BASE + 0x14 + 0x200 * (rdi))
#define		RDI_PIX_STORE_CFG0_EN			BIT(0)
#define		RDI_PIX_STORE_CFG0_MIN_HBI		1

/* RDI IRQ Status in wrapper */
#define CSID_CSI2_RDIN_IRQ_STATUS(rdi)		(0x114 + 0x10 * (rdi))
#define CSID_CSI2_RDIN_IRQ_CLEAR(rdi)		(0x11C + 0x10 * (rdi))
#define		INFO_RUP_DONE				BIT(23)

/*
 * Per-path domain_id (secure sub-block). The 5-bit domain_id CSID passes to the
 * IFE becomes the write master's AUSER[4:0], which the NOC translates to the
 * SMMU SID: a value of 0 yields an unmapped SID, a non-zero value selects the
 * HLOS IFE SID that the device tree maps. The register lives in a secure
 * sub-block one 4KB page below each CSID's mmio base, outside the CSID mmio
 * window, so it is mapped separately. Physical bases per full CSID (lite has
 * no such block).
 */
static const u32 csid_secure_ctrl_base[] = { 0x09b31000, 0x09b37800, 0x09b3e000 };
#define CSID_SECURE_CTRL_SIZE			0x10
#define CSID_PATH_DOMAIN_ID_CFG1		0x4
#define		PATH_DOMAIN_ID_RDI(rdi)			(0x1U << ((rdi) * 8))

static void __csid_aup_rup_trigger(struct csid_device *csid)
{
	/* trigger SET in combined register */
	writel(RUP_SET, csid->base + CSID_RUP_AUP_CMD);
}

static void __csid_aup_update(struct csid_device *csid, int port_id)
{
	csid->aup_update |= CSID_RUP_AUP_RDI(port_id);
	writel(csid->aup_update, csid->base + CSID_AUP_CMD);

	/*
	 * v900 splits AUP and RUP commands, which requires an additional SET
	 * operation to make the register modification take effect.
	 */
	__csid_aup_rup_trigger(csid);
}

static void __csid_rup_update(struct csid_device *csid, int port_id)
{
	csid->rup_update |= CSID_RUP_AUP_RDI(port_id);
	writel(csid->rup_update, csid->base + CSID_RUP_CMD);

	__csid_aup_rup_trigger(csid);
}

static void __csid_aup_rup_clear(struct csid_device *csid, int port_id)
{
	/* Hardware clears the registers upon consuming the settings */
	csid->aup_update &= ~CSID_RUP_AUP_RDI(port_id);
	csid->rup_update &= ~CSID_RUP_AUP_RDI(port_id);
}

static void __csid_configure_rx(struct csid_device *csid,
				struct csid_phy_config *phy)
{
	struct camss *camss = csid->camss;
	int val;

	val = (phy->lane_cnt - 1) << CSI2_RX_CFG0_NUM_ACTIVE_LANES;
	val |= phy->lane_assign << CSI2_RX_CFG0_DL0_INPUT_SEL;

	if (camss->tpg && csid->tpg_linked &&
	    camss->tpg[phy->csiphy_id].testgen.mode != TPG_PAYLOAD_MODE_DISABLED) {
		val |= FIELD_PREP(CSI2_RX_CFG0_TPG_MUX_SEL, phy->csiphy_id + 1);
		val |= CSI2_RX_CFG0_TPG_MUX_EN;
	} else {
		val |= (phy->csiphy_id + CSI2_RX_CFG0_PHY_SEL_BASE_IDX)
		       << CSI2_RX_CFG0_PHY_NUM_SEL;
	}
	writel(val, csid->base + CSID_CSI2_RX_CFG0);

	val = CSI2_RX_CFG1_ECC_CORRECTION_EN;
	writel(val, csid->base + CSID_CSI2_RX_CFG1);
}

static void __csid_configure_rx_vc(struct csid_device *csid, int vc)
{
	int val;

	if (vc > 3) {
		val = readl(csid->base + CSID_CSI2_RX_CFG1);
		val |= CSI2_RX_CFG1_VC_MODE;
		writel(val, csid->base + CSID_CSI2_RX_CFG1);
	}
}

static void __csid_ctrl_rdi(struct csid_device *csid, int enable, u8 rdi)
{
	int val = 0;
	u32 rdi_ctrl_offset = CSID_RDI_CTRL(rdi);

	if (enable)
		val = RDI_CTRL_START_CMD;

	writel(val, csid->base + rdi_ctrl_offset);
}

static void __csid_configure_rdi_pix_store(struct csid_device *csid, u8 rdi)
{
	u32 val;

	/*
	 * Configure pixel store to allow absorption of hblanking or idle time.
	 * This helps with horizontal crop and prevents line buffer conflicts.
	 * Reset state is 0x8 which has MIN_HBI=4, we keep the default MIN_HBI
	 * and just enable the pixel store functionality.
	 */
	val = (4 << RDI_PIX_STORE_CFG0_MIN_HBI) | RDI_PIX_STORE_CFG0_EN;
	writel(val, csid->base + CSID_RDI_PIX_STORE_CFG0(rdi));
}

static void __csid_configure_rdi_stream(struct csid_device *csid, u8 enable, u8 port, u8 vc)
{
	u32 val;
	u8 lane_cnt = csid->phy.lane_cnt;

	/* Source pads matching RDI channels on hardware.
	 * E.g. Pad 1 -> RDI0, Pad 2 -> RDI1, etc.
	 */
	struct v4l2_mbus_framefmt *input_format = &csid->fmt[MSM_CSID_PAD_FIRST_SRC + port];
	const struct csid_format_info *format = csid_get_fmt_entry(csid->res->formats->formats,
								   csid->res->formats->nformats,
								   input_format->code);

	if (!lane_cnt)
		lane_cnt = 4;

	/*
	 * DT_ID is a two bit bitfield that is concatenated with
	 * the four least significant bits of the five bit VC
	 * bitfield to generate an internal CID value.
	 *
	 * CSID_RDI_CFG0(vc)
	 * DT_ID : 28:27
	 * VC    : 26:22
	 * DT    : 21:16
	 *
	 * CID   : VC 3:0 << 2 | DT_ID 1:0
	 */
	u8 dt_id = vc & 0x03;
	u32 rdi_cfg0_offset = CSID_RDI_CFG0(port);
	u32 rdi_cfg1_offset = CSID_RDI_CFG1(port);
	u32 rdi_ctrl_offset = CSID_RDI_CTRL(port);

	val = RDI_CFG0_TIMESTAMP_EN;
	val |= RDI_CFG0_TIMESTAMP_STB_SEL;
	/* bit5 set to match the 980 */
	val |= RDI_CFG0_RETIME_DIS;

	/* note: for non-RDI path, this should be format->decode_format */
	val |= DECODE_FORMAT_PAYLOAD_ONLY << RDI_CFG0_DECODE_FORMAT;
	val |= vc << RDI_CFG0_VC;
	val |= format->data_type << RDI_CFG0_DT;
	val |= dt_id << RDI_CFG0_DT_ID;
	writel(val, csid->base + rdi_cfg0_offset);

	val = RDI_CFG1_PACKING_FORMAT_MIPI;
	writel(val, csid->base + rdi_cfg1_offset);

	/* Pixel store is only present on CSID full */
	if (!csid_is_lite(csid))
		__csid_configure_rdi_pix_store(csid, port);

	val = 0;
	writel(val, csid->base + rdi_ctrl_offset);

	val = readl(csid->base + rdi_cfg0_offset);

	if (enable)
		val |= RDI_CFG0_EN;

	writel(val, csid->base + rdi_cfg0_offset);
}

/* Program a non-zero per-path domain_id (see CSID_PATH_DOMAIN_ID_CFG1). */
static void __csid_configure_domain_id(struct csid_device *csid)
{
	void __iomem *sec;
	u32 val;
	int i;

	/* Only full CSIDs (0..2) have the secure domain_id block */
	if (csid_is_lite(csid) || csid->id >= ARRAY_SIZE(csid_secure_ctrl_base))
		return;

	sec = ioremap(csid_secure_ctrl_base[csid->id], CSID_SECURE_CTRL_SIZE);
	if (!sec)
		return;

	/* CFG1: one 8-bit domain_id field per RDI (RDI0..3) */
	val = readl(sec + CSID_PATH_DOMAIN_ID_CFG1);
	for (i = 0; i < 4; i++)
		if (csid->phy.en_vc & BIT(i))
			val |= PATH_DOMAIN_ID_RDI(i);
	writel(val, sec + CSID_PATH_DOMAIN_ID_CFG1);

	iounmap(sec);
}

static void csid_configure_stream(struct csid_device *csid, u8 enable)
{
	u8 i, k;

	__csid_configure_rx(csid, &csid->phy);

	for (i = 0; i < MSM_CSID_MAX_SRC_STREAMS_900; i++) {
		if (csid->phy.en_vc & BIT(i)) {
			__csid_configure_rdi_stream(csid, enable, i, 0);
			__csid_configure_rx_vc(csid, 0);

			for (k = 0; k < CAMSS_INIT_BUF_COUNT; k++)
				__csid_aup_update(csid, i);

			/*
			 * Latch the configuration into the active bank with a
			 * single register update after the address updates, then
			 * start.
			 */
			__csid_rup_update(csid, i);

			/* Program domain_id before START (see __csid_configure_domain_id) */
			if (enable)
				__csid_configure_domain_id(csid);

			__csid_ctrl_rdi(csid, enable, i);
		}
	}
}

static int csid_configure_testgen_pattern(struct csid_device *csid, s32 val)
{
	return 0;
}

static void csid_subdev_reg_update(struct csid_device *csid, int port_id,
				   bool clear)
{
	if (clear)
		__csid_aup_rup_clear(csid, port_id);
	else
		__csid_aup_update(csid, port_id);
}

/**
 * csid_isr - CSID module interrupt service routine
 * @irq: Interrupt line
 * @dev: CSID device
 *
 * Return IRQ_HANDLED on success
 */
static irqreturn_t csid_isr(int irq, void *dev)
{
	struct csid_device *csid = dev;
	u32 val, buf_done_val;
	u8 reset_done;
	int i;

	val = readl(csid->base + CSID_TOP_IRQ_STATUS);
	writel(val, csid->base + CSID_TOP_IRQ_CLEAR);

	reset_done = val & INFO_RST_DONE;

	buf_done_val = readl(csid->base + CSID_BUF_DONE_IRQ_STATUS);
	writel(buf_done_val, csid->base + CSID_BUF_DONE_IRQ_CLEAR);

	for (i = 0; i < MSM_CSID_MAX_SRC_STREAMS_900; i++) {
		if (csid->phy.en_vc & BIT(i)) {
			val = readl(csid->base + CSID_CSI2_RDIN_IRQ_STATUS(i));
			writel(val, csid->base + CSID_CSI2_RDIN_IRQ_CLEAR(i));

			if (val & INFO_RUP_DONE)
				csid_subdev_reg_update(csid, i, true);

			if (buf_done_val & BIT(BUF_DONE_IRQ_STATUS_RDI_OFFSET + i))
				camss_buf_done(csid->camss, csid->id, i);
		}
	}

	val = IRQ_CMD_CLEAR;
	writel(val, csid->base + CSID_IRQ_CMD);

	if (reset_done)
		complete(&csid->reset_complete);

	return IRQ_HANDLED;
}

/**
 * csid_reset - Trigger reset on CSID module and wait to complete
 * @csid: CSID device
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csid_reset(struct csid_device *csid)
{
	unsigned long time;
	u32 val;
	int i;

	reinit_completion(&csid->reset_complete);

	val = INFO_RST_DONE | BUF_DONE_IRQ_STATUS;
	writel(val, csid->base + CSID_TOP_IRQ_CLEAR);
	writel(val, csid->base + CSID_TOP_IRQ_MASK);

	val = 0;
	for (i = 0; i < MSM_CSID_MAX_SRC_STREAMS_900; i++) {
		if (csid->phy.en_vc & BIT(i)) {
			/*
			 * Only need to clear buf done IRQ status here,
			 * RUP done IRQ status will be cleared once isr
			 * strobe generated by CSID_RST_CMD
			 */
			val |= BIT(BUF_DONE_IRQ_STATUS_RDI_OFFSET + i);
		}
	}
	writel(val, csid->base + CSID_BUF_DONE_IRQ_CLEAR);
	writel(val, csid->base + CSID_BUF_DONE_IRQ_MASK);

	/* Clear all IRQ status with CLEAR bits set */
	val = IRQ_CMD_CLEAR;
	writel(val, csid->base + CSID_IRQ_CMD);

	val = RST_LOCATION | RST_MODE;
	writel(val, csid->base + CSID_RST_CFG);

	val = SELECT_HW_RST | SELECT_IRQ_RST;
	writel(val, csid->base + CSID_RST_CMD);

	time = wait_for_completion_timeout(&csid->reset_complete,
					   msecs_to_jiffies(CSID_RESET_TIMEOUT_MS));

	if (!time) {
		dev_err(csid->camss->dev, "CSID reset timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void csid_subdev_init(struct csid_device *csid)
{
	csid->testgen.nmodes = CSID_PAYLOAD_MODE_DISABLED;
}

const struct csid_hw_ops csid_ops_900 = {
	.configure_stream = csid_configure_stream,
	.configure_testgen_pattern = csid_configure_testgen_pattern,
	.hw_version = csid_hw_version,
	.isr = csid_isr,
	.reset = csid_reset,
	.src_pad_code = csid_src_pad_code,
	.subdev_init = csid_subdev_init,
	.reg_update = csid_subdev_reg_update,
};
