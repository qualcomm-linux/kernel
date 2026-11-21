/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __MEDIA_IRIS_PLATFORM_HAWI_H__
#define __MEDIA_IRIS_PLATFORM_HAWI_H__

static const char * const hawi_clk_reset_table[] = {
	"bus0",
	"core",
};

static const struct iris_power_domain_data hawi_ctrl_data = {
	.pd_names = (const char *[]) {
		"venus",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"ifacec", "core", "core_ctl_freerun", "core_debug",
	},
	.clk_cnt = 4,
};

static const struct iris_power_domain_data hawi_vcodec_data[] = {
	{
		.pd_names = (const char *[]) {
			"vcodec0",
		},
		.pd_cnt = 1,
		.clk_names = (const char *[]) {
			"iface", "vcodec0_core", "vcodec_bse",
			"vcodec_vpp0_vpp1_gating",
		},
		.clk_cnt = 4,
	},
};

static const struct iris_power_domain_data hawi_vcodec_vpp0_data = {
	.pd_names = (const char *[]) {
		"vpp0",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"vcodec_vpp0",
	},
	.clk_cnt = 1,
};

static const struct iris_power_domain_data hawi_vcodec_vpp1_data = {
	.pd_names = (const char *[]) {
		"vpp1",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"vcodec_vpp1",
	},
	.clk_cnt = 1,
};

static const struct iris_power_domain_data hawi_apv_data = {
	.pd_names = (const char *[]) {
		"apv",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"vcodec_apv",
	},
	.clk_cnt = 1,
};

static const struct iris_power_domain_data hawi_mm_int_data = {
	.pd_names = (const char *[]) {
		"mm-int",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"core_freerun",
	},
	.clk_cnt = 1,
};

static const struct iris_power_domain_data hawi_cx_int_data = {
	.pd_names = (const char *[]) {
		"cx-int",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"cx_iface",
	},
	.clk_cnt = 1,
};

static const char *const hawi_opp_clk_table[] = {
	"vcodec0_core",
	"vcodec_apv",
	"vcodec_bse",
	"core",
	NULL,
};

#endif
