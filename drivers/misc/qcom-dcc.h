/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _QCOM_DCC_H
#define _QCOM_DCC_H

#include <linux/platform_device.h>

struct dcc_register_entry {
	const char	*config;
};

struct dcc_link_config {
	int					link_list;
	const struct dcc_register_entry		*entries;
	int					num_entries;
};

struct dcc_config {
	const struct dcc_link_config	*lists;
	int				num_lists;
};

struct dcc_pdata {
	phys_addr_t	base;
	resource_size_t	size;
	phys_addr_t	ram_base;
	resource_size_t	ram_size;
	u32		dcc_offset;
	u8		map_ver;
	const struct dcc_config		*config;
};

#endif
