/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __IRIS_PLATFORM_MAILI_H__
#define __IRIS_PLATFORM_MAILI_H__

static const char *const maili_clk_reset_table[] = {
	"bus0",
	"bus1",
	"core",
	"vcodec0_core",
};

static const struct iris_power_domain_data maili_ctrl_data = {
	.pd_names = (const char *[]) {
		"venus",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"iface1", "core", "core_freerun",
	},
	.clk_cnt = 3,
};

static const struct iris_power_domain_data maili_vcodec_data[] = {
	{
		.pd_names = (const char *[]) {
			"vcodec0",
		},
		.pd_cnt = 1,
		.clk_names = (const char *[]) {
			"iface", "vcodec0_core", "vcodec0_core_freerun", "vcodec_bse",
		},
		.clk_cnt = 4,
	},
};

static const struct iris_power_domain_data maili_vpp0_data = {
	.pd_names = (const char *[]) {
		"vpp0",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"vcodec_vpp0",
	},
	.clk_cnt = 1,
};

static const char *const maili_opp_clk_table[] = {
	"vcodec0_core",
	"vcodec_bse",
	"core",
	NULL,
};

static struct tz_cp_config maili_tz_cp_config[] = {
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

#endif /* __IRIS_PLATFORM_MAILI_H__ */
