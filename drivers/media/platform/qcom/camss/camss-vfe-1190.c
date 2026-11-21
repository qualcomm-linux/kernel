// SPDX-License-Identifier: GPL-2.0
/*
 * camss-vfe-1190.c
 *
 * Qualcomm MSM Camera Subsystem - VFE (Video Front End) Module 1190
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "camss.h"
#include "camss-vfe.h"

/* VFE-1190 Bus Register Base Addresses */
#define BUS_REG_BASE				(0x800)
#define BUS_REG_OFFSET(n)			(0x1000 + (n) * 0x200)

#define VFE_BUS_WM_CGC_OVERRIDE			(BUS_REG_BASE + 0x4)
#define		WM_CGC_OVERRIDE_ALL			(0x7FFFFFF)

#define VFE_BUS_WM_TEST_BUS_CTRL		(BUS_REG_BASE + 0x144)

#define VFE_BUS_WM_CFG(n)			(BUS_REG_BASE + BUS_REG_OFFSET(n))
#define		WM_CFG_EN				BIT(0)
#define		WM_VIR_FRM_EN				BIT(1)
#define		WM_CFG_MODE				BIT(16)
#define VFE_BUS_WM_IMAGE_ADDR(n)		(BUS_REG_BASE + BUS_REG_OFFSET(n) + 0x4)
#define VFE_BUS_WM_FRAME_INCR(n)		(BUS_REG_BASE + BUS_REG_OFFSET(n) + 0x8)
#define VFE_BUS_WM_IMAGE_CFG_0(n)		(BUS_REG_BASE + BUS_REG_OFFSET(n) + 0xC)
#define		WM_IMAGE_CFG_0_DEFAULT_WIDTH		(0xFFFF)
#define VFE_BUS_WM_IMAGE_CFG_2(n)		(BUS_REG_BASE + BUS_REG_OFFSET(n) + 0x14)
#define		WM_IMAGE_CFG_2_DEFAULT_STRIDE		(0xFFFF)
#define VFE_BUS_WM_PACKER_CFG(n)		(BUS_REG_BASE + BUS_REG_OFFSET(n) + 0x18)

#define VFE_BUS_WM_IRQ_SUBSAMPLE_PERIOD(n)	(BUS_REG_BASE + BUS_REG_OFFSET(n) + 0x30)
#define VFE_BUS_WM_IRQ_SUBSAMPLE_PATTERN(n)	(BUS_REG_BASE + BUS_REG_OFFSET(n) + 0x34)

/* VFE lite has no such registers */
#define VFE_BUS_WM_FRAMEDROP_PERIOD(n)		(BUS_REG_BASE + BUS_REG_OFFSET(n) + 0x38)
#define VFE_BUS_WM_FRAMEDROP_PATTERN(n)		(BUS_REG_BASE + BUS_REG_OFFSET(n) + 0x3C)

#define VFE_BUS_WM_MMU_PREFETCH_CFG(n)		(BUS_REG_BASE + BUS_REG_OFFSET(n) + 0x64)
#define VFE_BUS_WM_MMU_PREFETCH_MAX_OFFSET(n)	(BUS_REG_BASE + BUS_REG_OFFSET(n) + 0x68)

/*
 * VFE 1190 write master client IDs
 *
 * VIDEO_FULL_Y			0
 * VIDEO_FULL_UV		1
 * VIDEO_DC4_Y			2
 * VIDEO_DC4_C			3
 * VIDEO_DC16_Y			4
 * VIDEO_DC16_C			5
 * FD_Y				6
 * FD_C				7
 * RAW_OUT			8
 * STATS_AEC_BG			9
 * STATS_AEC_BHIST		10
 * STATS_TINTLESS_BG		11
 * STATS_AWB_BG			12
 * STATS_AWB_BFW		13
 * STATS_AF_BHIST		14
 * STATS_ALSC_BG		15
 * STATS_FLICKER_BAYERRS	16
 * STATS_TMC_BHIST		17
 * PDAF_0			18
 * PDAF_1			19
 * PDAF_2			20
 * PDAF_3			21
 * PDAF_4			22
 * RDI0				23
 * RDI1				24
 * RDI2				25
 * RDI3				26
 * RDI4				27
 * C2PA Y			28
 * C2PA C			29
 *
 * VFE Lite 1190 write master client IDs
 *
 * RDI0			0
 * RDI1			1
 * RDI2			2
 * RDI3			3
 * GAMMA		4
 * STATES_BG		5
 */
#define RDI_WM(n) ((vfe_is_lite(vfe) ? 0x0 : 0x17) + (n))

static void vfe_wm_start(struct vfe_device *vfe, u8 wm, struct vfe_line *line)
{
	struct v4l2_pix_format_mplane *pix =
		&line->video_out.active_fmt.fmt.pix_mp;

	wm = RDI_WM(wm);

	/* no clock gating at bus input */
	writel(WM_CGC_OVERRIDE_ALL, vfe->base + VFE_BUS_WM_CGC_OVERRIDE);

	writel(0x0, vfe->base + VFE_BUS_WM_TEST_BUS_CTRL);

	writel(ALIGN(pix->plane_fmt[0].bytesperline, 16) * pix->height >> 8,
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
	writel(addr >> 8, vfe->base + VFE_BUS_WM_IMAGE_ADDR(wm));

	dev_dbg(vfe->camss->dev, "wm:%d, image buf addr:0x%x\n", wm, addr);
}

static void vfe_reg_update(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	int port_id = line_id;

	camss_reg_update(vfe->camss, vfe->id, port_id, false);
}

static inline void vfe_reg_update_clear(struct vfe_device *vfe,
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

static inline void vfe_addr_update_clear(struct vfe_device *vfe,
					 enum vfe_line_id line_id)
{
	int port_id = line_id;

	camss_addr_update(vfe->camss, vfe->id, port_id, true);
}

static const struct camss_video_ops vfe_video_ops_1190 = {
	.queue_buffer = vfe_queue_buffer_v2,
	.flush_buffers = vfe_flush_buffers,
};

static void vfe_subdev_init(struct device *dev, struct vfe_device *vfe)
{
	vfe->video_ops = vfe_video_ops_1190;
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

const struct vfe_hw_ops vfe_ops_1190 = {
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
