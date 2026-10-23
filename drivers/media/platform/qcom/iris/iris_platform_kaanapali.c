// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_platform_common.h"

const char *const iris_kaanapali_clk_reset_table[] = {
	"bus0",
	"bus1",
	"core",
	"vcodec0_core",
};

const struct iris_power_domain_data iris_kaanapali_ctrl_data = {
	.pd_names = (const char *[]) {
		"venus",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"iface1", "core", "core_freerun",
	},
	.clk_cnt = 3,
};

const struct iris_power_domain_data iris_kaanapali_vcodec_data[] = {
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

const struct iris_power_domain_data iris_kaanapali_vpp0_data = {
	.pd_names = (const char *[]) {
		"vpp0",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"vcodec_vpp0",
	},
	.clk_cnt = 1,
};

const struct iris_power_domain_data iris_kaanapali_vpp1_data = {
	.pd_names = (const char *[]) {
		"vpp1",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"vcodec_vpp1",
	},
	.clk_cnt = 1,
};

const struct iris_power_domain_data iris_kaanapali_apv_data = {
	.pd_names = (const char *[]) {
		"apv",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"vcodec_apv",
	},
	.clk_cnt = 1,
};

const char *const iris_kaanapali_opp_clk_table[] = {
	"vcodec0_core",
	"vcodec_apv",
	"vcodec_bse",
	"core",
	NULL,
};

struct tz_cp_config iris_kaanapali_tz_cp_config[] = {
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
