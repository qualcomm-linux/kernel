// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_platform_common.h"
#include "iris_platform_glymur.h"

const struct platform_clk_data iris_glymur_clk_table[] = {
	{IRIS_AXI_VCODEC_CLK,		"iface"			},
	{IRIS_CTRL_CLK,			"core"			},
	{IRIS_VCODEC_CLK,		"vcodec0_core"		},
	{IRIS_AXI_CTRL_CLK,		"iface1"		},
	{IRIS_CTRL_FREERUN_CLK,		"core_freerun"		},
	{IRIS_VCODEC_FREERUN_CLK,	"vcodec0_core_freerun"	},
	{IRIS_AXI_VCODEC1_CLK,		"iface2"		},
	{IRIS_VCODEC1_CLK,		"vcodec1_core"		},
	{IRIS_VCODEC1_FREERUN_CLK,	"vcodec1_core_freerun"	},
};

const char * const iris_glymur_clk_reset_table[] = {
	"bus0",
	"bus1",
	"core",
	"vcodec0_core",
	"bus2",
	"vcodec1_core",
};

const char * const iris_glymur_opp_clk_table[] = {
	"vcodec0_core",
	"vcodec1_core",
	"core",
	NULL,
};

const struct platform_pd_data iris_glymur_pmdomain_table = {
	.pd_types = (enum platform_pm_domain_type []) {
		IRIS_CTRL_POWER_DOMAIN,
		IRIS_VCODEC_POWER_DOMAIN,
		IRIS_VCODEC1_POWER_DOMAIN,
	},
	.pd_names = (const char *[]) {
		"venus",
		"vcodec0",
		"vcodec1",
	},
	.pd_count = 3,
};

const struct tz_cp_config iris_glymur_tz_cp_config[] = {
	{
		.cp_start = VIDEO_REGION_SECURE_FW_REGION_ID,
		.cp_size = 0,
		.cp_nonpixel_start = 0,
		.cp_nonpixel_size = 0x1000000,
	},
	{
		.cp_start = VIDEO_REGION_VM0_SECURE_NP_ID,
		.cp_size = 0,
		.cp_nonpixel_start = 0x1000000,
		.cp_nonpixel_size = 0x24800000,
	},
	{
		.cp_start = VIDEO_REGION_VM0_NONSECURE_NP_ID,
		.cp_size = 0,
		.cp_nonpixel_start = 0x25800000,
		.cp_nonpixel_size = 0xda600000,
	},
};
