// SPDX-License-Identifier: GPL-2.0
/*
 * camss-vfe-900.c
 *
 * Qualcomm MSM Camera Subsystem - VFE (Video Front End) Module 900
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "camss.h"
#include "camss-vfe.h"

/* VFE-900 Bus Register Base Addresses */
#define BUS_REG_BASE				(vfe_is_lite(vfe) ? 0x400 : 0x700)

#define VFE_BUS_WM_CGC_OVERRIDE			(BUS_REG_BASE + 0x08)
#define		WM_CGC_OVERRIDE_ALL			(0xFFFFFFFF)
#define		WM_CGC_OVERRIDE_LITE_ALL		(0xFF)

#define VFE_BUS_WM_TEST_BUS_CTRL		(BUS_REG_BASE + 0x144)

/* Clients 32-39, VFE lite has no such register */
#define VFE_BUS_WM_CGC_OVERRIDE_1		(BUS_REG_BASE + 0x148)
#define		WM_CGC_OVERRIDE_1_ALL			(0xFF)

#define VFE_BUS_WM_CFG(n)			(BUS_REG_BASE + 0x800 + (n) * 0x100)
#define		WM_CFG_EN				BIT(0)
#define		WM_VIR_FRM_EN				BIT(1)
#define		WM_CFG_MODE				BIT(16)
#define VFE_BUS_WM_IMAGE_ADDR(n)		(BUS_REG_BASE + 0x804 + (n) * 0x100)
#define VFE_BUS_WM_FRAME_INCR(n)		(BUS_REG_BASE + 0x808 + (n) * 0x100)
#define VFE_BUS_WM_IMAGE_CFG_0(n)		(BUS_REG_BASE + 0x80C + (n) * 0x100)
#define		WM_IMAGE_CFG_0_DEFAULT_WIDTH		(0xFFFF)
#define VFE_BUS_WM_IMAGE_CFG_2(n)		(BUS_REG_BASE + 0x814 + (n) * 0x100)
#define		WM_IMAGE_CFG_2_DEFAULT_STRIDE		(0xFFFF)
#define VFE_BUS_WM_PACKER_CFG(n)		(BUS_REG_BASE + 0x818 + (n) * 0x100)

#define VFE_BUS_WM_IRQ_SUBSAMPLE_PERIOD(n)	(BUS_REG_BASE + 0x830 + (n) * 0x100)
#define VFE_BUS_WM_IRQ_SUBSAMPLE_PATTERN(n)	(BUS_REG_BASE + 0x834 + (n) * 0x100)

/* VFE lite has no such registers */
#define VFE_BUS_WM_FRAMEDROP_PERIOD(n)		(BUS_REG_BASE + 0x838 + (n) * 0x100)
#define VFE_BUS_WM_FRAMEDROP_PATTERN(n)		(BUS_REG_BASE + 0x83C + (n) * 0x100)

#define VFE_BUS_WM_MMU_PREFETCH_CFG(n)		(BUS_REG_BASE + 0x860 + (n) * 0x100)
#define VFE_BUS_WM_MMU_PREFETCH_MAX_OFFSET(n)	(BUS_REG_BASE + 0x864 + (n) * 0x100)

/*
 * IFE write master client IDs
 *
 * MAIN C0/C1/C2/UV		0-3
 * PIXEL_RAW			4
 * W_IR				5
 * AI_1 C0/C1/C2/UV		6-9
 * AI_2 C0/C1/C2/UV		10-13
 * HV_DS16 / HV_DS4		14-15
 * STATS BG_IR..BLTM		16-27
 * RDI0				28
 * RDI1				29
 * RDI2				30
 * ...
 * RDI11			39
 *
 * IFE Lite write master client IDs
 *
 * RDI0			0
 * RDI1			1
 * RDI2			2
 * RDI3			3
 * GAMMA		4
 * STATES_BE		5
 */
#define RDI_WM(n) ((vfe_is_lite(vfe) ? 0x0 : 0x1C) + (n))

static void vfe_wm_start(struct vfe_device *vfe, u8 wm, struct vfe_line *line)
{
	struct v4l2_pix_format_mplane *pix =
		&line->video_out.active_fmt.fmt.pix_mp;

	wm = RDI_WM(wm);

	/* no clock gating at bus input */
	if (vfe_is_lite(vfe)) {
		writel(WM_CGC_OVERRIDE_LITE_ALL,
		       vfe->base + VFE_BUS_WM_CGC_OVERRIDE);
	} else {
		writel(WM_CGC_OVERRIDE_ALL, vfe->base + VFE_BUS_WM_CGC_OVERRIDE);
		writel(WM_CGC_OVERRIDE_1_ALL,
		       vfe->base + VFE_BUS_WM_CGC_OVERRIDE_1);
	}

	writel(0x0, vfe->base + VFE_BUS_WM_TEST_BUS_CTRL);

	/* The address registers take a byte address on VFE 900 */
	writel(ALIGN(pix->plane_fmt[0].bytesperline, 16) * pix->height,
	       vfe->base + VFE_BUS_WM_FRAME_INCR(wm));
	writel((WM_IMAGE_CFG_0_DEFAULT_WIDTH & 0xFFFF),
	       vfe->base + VFE_BUS_WM_IMAGE_CFG_0(wm));
	writel(WM_IMAGE_CFG_2_DEFAULT_STRIDE,
	       vfe->base + VFE_BUS_WM_IMAGE_CFG_2(wm));
	writel(0, vfe->base + VFE_BUS_WM_PACKER_CFG(wm));

	/* no dropped frames, one irq per frame */
	if (!vfe_is_lite(vfe)) {
		writel(0, vfe->base + VFE_BUS_WM_FRAMEDROP_PERIOD(wm));
		writel(1, vfe->base + VFE_BUS_WM_FRAMEDROP_PATTERN(wm));
	}

	writel(0, vfe->base + VFE_BUS_WM_IRQ_SUBSAMPLE_PERIOD(wm));
	writel(1, vfe->base + VFE_BUS_WM_IRQ_SUBSAMPLE_PATTERN(wm));

	writel(1, vfe->base + VFE_BUS_WM_MMU_PREFETCH_CFG(wm));
	writel(0xFFFFFFFF, vfe->base + VFE_BUS_WM_MMU_PREFETCH_MAX_OFFSET(wm));

	writel(WM_CFG_EN | WM_CFG_MODE, vfe->base + VFE_BUS_WM_CFG(wm));
}

static void vfe_wm_stop(struct vfe_device *vfe, u8 wm)
{
	wm = RDI_WM(wm);
	writel(0, vfe->base + VFE_BUS_WM_CFG(wm));
}

static void vfe_wm_update(struct vfe_device *vfe, u8 wm, u32 addr,
			  struct vfe_line *line)
{
	wm = RDI_WM(wm);
	writel(addr, vfe->base + VFE_BUS_WM_IMAGE_ADDR(wm));

	dev_dbg(vfe->camss->dev, "wm:%d, image buf addr:0x%x\n", wm, addr);
}

static void vfe_reg_update(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	int port_id = line_id;

	camss_reg_update(vfe->camss, vfe->id, port_id, false);
}

static void vfe_reg_update_clear(struct vfe_device *vfe,
				 enum vfe_line_id line_id)
{
	int port_id = line_id;

	camss_reg_update(vfe->camss, vfe->id, port_id, true);
}

static void vfe_addr_update(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	int port_id = line_id;

	camss_addr_update(vfe->camss, vfe->id, port_id, false);
}

static void vfe_addr_update_clear(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	int port_id = line_id;

	camss_addr_update(vfe->camss, vfe->id, port_id, true);
}

static const struct camss_video_ops vfe_video_ops_900 = {
	.queue_buffer = vfe_queue_buffer_v2,
	.flush_buffers = vfe_flush_buffers,
};

static void vfe_subdev_init(struct device *dev, struct vfe_device *vfe)
{
	vfe->video_ops = vfe_video_ops_900;
}

static void vfe_global_reset(struct vfe_device *vfe)
{
	vfe_isr_reset_ack(vfe);
}

static irqreturn_t vfe_isr(int irq, void *dev)
{
	/* nop */
	return IRQ_HANDLED;
}

static int vfe_halt(struct vfe_device *vfe)
{
	/* rely on vfe_disable_output() to stop the VFE */
	return 0;
}

const struct vfe_hw_ops vfe_ops_900 = {
	.addr_update = vfe_addr_update,
	.addr_update_clear = vfe_addr_update_clear,
	.global_reset = vfe_global_reset,
	.hw_version = vfe_hw_version,
	.isr = vfe_isr,
	.pm_domain_off = vfe_pm_domain_off,
	.pm_domain_on = vfe_pm_domain_on,
	.reg_update = vfe_reg_update,
	.reg_update_clear = vfe_reg_update_clear,
	.subdev_init = vfe_subdev_init,
	.vfe_disable = vfe_disable,
	.vfe_enable = vfe_enable_v2,
	.vfe_halt = vfe_halt,
	.vfe_wm_start = vfe_wm_start,
	.vfe_wm_stop = vfe_wm_stop,
	.vfe_buf_done = vfe_buf_done,
	.vfe_wm_update = vfe_wm_update,
};
