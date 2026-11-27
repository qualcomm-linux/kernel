// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_platform_common.h"

const struct iris_power_domain_data iris_mahua_ctrl_data = {
	.pd_names = (const char *[]) {
		"venus",
	},
	.pd_cnt = 1,
	.clk_names = (const char *[]) {
		"core_iface", "core_freerun", "core",
	},
	.clk_cnt = 3,
};

const struct iris_power_domain_data iris_mahua_vcodec_data[] = {
	{
		.pd_names = (const char *[]) {
			"vcodec0",
		},
		.pd_cnt = 1,
		.clk_names = (const char *[]) {
			"vcodec0_iface", "vcodec0_core_freerun", "vcodec0_core",
		},
		.clk_cnt = 3,
	},
};

const char * const iris_mahua_clk_reset_table[] = {
	"core_bus",
	"vcodec0_bus",
	"core",
	"vcodec0_core",
};

const char * const iris_mahua_opp_clk_table[] = {
	"vcodec0_core",
	"core",
	NULL,
};
