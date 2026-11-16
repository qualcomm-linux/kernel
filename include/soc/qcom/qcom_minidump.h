/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_MINIDUMP_H_
#define _QCOM_MINIDUMP_H_

#include <linux/types.h>

#define MAX_NAME_LENGTH		12

/**
 * struct qcom_minidump_region - APSS Minidump region information
 *
 * @name:	Entry name, Minidump will dump binary with this name.
 * @virt_addr:  Virtual address of the entry.
 * @phys_addr:	Physical address of the entry to dump.
 * @size:	Number of byte to dump from @address location,
 *		and it should be 4 byte aligned.
 */
struct qcom_minidump_region {
	char		name[MAX_NAME_LENGTH];
	void		*virt_addr;
	phys_addr_t	phys_addr;
	size_t		size;
};

int qcom_minidump_region_register(const struct qcom_minidump_region *region);
int qcom_minidump_region_unregister(const struct qcom_minidump_region *region);

#endif /* _QCOM_MINIDUMP_H_ */
