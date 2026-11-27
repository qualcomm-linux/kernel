/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _QCOM_DCC_ELIZA_CONFIG_H
#define _QCOM_DCC_ELIZA_CONFIG_H

#include "qcom-dcc.h"

static const struct dcc_register_entry eliza_dcc_entries_ll6[] = {
};

static const struct dcc_link_config eliza_link_configs[] = {
	{
		.link_list	= 6,
		.entries	= eliza_dcc_entries_ll6,
		.num_entries	= ARRAY_SIZE(eliza_dcc_entries_ll6),
	},
};

static const struct dcc_config eliza_config = {
	.lists		= eliza_link_configs,
	.num_lists	= ARRAY_SIZE(eliza_link_configs),
};

static const struct dcc_pdata eliza_pdata = {
	.base		= 0x100ff000,
	.size		= 0x00001000,
	.ram_base	= 0x10084000,
	.ram_size	= 0x00004000,
	.dcc_offset	= 0x4000,
	.map_ver	= 0x3,
	.config		= &eliza_config,
};

#endif /* _QCOM_DCC_ELIZA_CONFIG_H */
