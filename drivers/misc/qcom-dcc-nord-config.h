/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _QCOM_DCC_NORD_CONFIG_H
#define _QCOM_DCC_NORD_CONFIG_H

#include "qcom-dcc.h"

static const struct dcc_register_entry nord_dcc_entries_ll6[] = {
};

static const struct dcc_link_config nord_link_configs[] = {
	{
		.link_list	= 6,
		.entries	= nord_dcc_entries_ll6,
		.num_entries	= ARRAY_SIZE(nord_dcc_entries_ll6),
	},
};

static const struct dcc_config nord_config = {
	.lists		= nord_link_configs,
	.num_lists	= ARRAY_SIZE(nord_link_configs),
};

static const struct dcc_pdata nord_pdata = {
	.base		= 0x100ff000,
	.size		= 0x00001000,
	.ram_base	= 0x10081000,
	.ram_size	= 0x00007000,
	.dcc_offset	= 0x1000,
	.map_ver	= 0x3,
	.config		= &nord_config,
};

#endif /* _QCOM_DCC_NORD_CONFIG_H */
