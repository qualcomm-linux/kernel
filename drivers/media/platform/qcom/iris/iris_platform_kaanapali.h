/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_PLATFORM_KAANAPALI_H__
#define __IRIS_PLATFORM_KAANAPALI_H__

#define VIDEO_REGION_VM0_SECURE_NP_ID		1
#define VIDEO_REGION_VM0_NONSECURE_NP_ID	5

static const char *const kaanapali_clk_reset_table[] = {
	"bus0",
	"bus1",
	"core_freerun_reset",
	"vcodec0_core_freerun_reset",
};

static const char *const kaanapali_pmdomain_table[] = {
	"venus",
	"vcodec0",
	"vpp0",
	"vpp1",
	"apv",
};

static const struct platform_clk_data kaanapali_clk_table[] = {
	{ IRIS_AXI_CLK, "iface" },
	{ IRIS_CTRL_CLK, "core" },
	{ IRIS_HW_CLK, "vcodec0_core" },
	{ IRIS_AXI1_CLK, "iface1" },
	{ IRIS_CTRL_FREERUN_CLK, "core_freerun" },
	{ IRIS_HW_FREERUN_CLK, "vcodec0_core_freerun" },
	{ IRIS_BSE_HW_CLK, "vcodec_bse" },
	{ IRIS_VPP0_HW_CLK, "vcodec_vpp0" },
	{ IRIS_VPP1_HW_CLK, "vcodec_vpp1" },
	{ IRIS_APV_HW_CLK, "vcodec_apv" },
};

static const char *const kaanapali_opp_clk_table[] = {
	"vcodec0_core",
	"vcodec_apv",
	"vcodec_bse",
	"core",
	NULL,
};

static struct tz_cp_config tz_cp_config_kaanapali[] = {
	{
		.cp_start = VIDEO_REGION_VM0_SECURE_NP_ID,
		.cp_size = 0,
		.cp_nonpixel_start = 0x01000000,
		.cp_nonpixel_size = 0x24800000,
	},
	{
		.cp_start = VIDEO_REGION_VM0_NONSECURE_NP_ID,
		.cp_size = 0,
		.cp_nonpixel_start = 0x25800000,
		.cp_nonpixel_size = 0xda400000,
	},
};

#endif /* __IRIS_PLATFORM_KAANAPALI_H__ */
