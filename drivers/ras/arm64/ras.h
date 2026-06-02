/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2025, Alibaba Group.
 */

#ifndef _DRIVERS_RAS_ARM64_RAS_H_
#define _DRIVERS_RAS_ARM64_RAS_H_

#include <linux/acpi_aest.h>
#include <asm/ras.h>

#define ras_node_err(__node, format, ...)                          \
	dev_err((__node)->dev, "%s: " format, (__node)->name, \
		##__VA_ARGS__)
#define ras_node_info(__node, format, ...)                          \
	dev_info((__node)->dev, "%s: " format, (__node)->name, \
		 ##__VA_ARGS__)
#define ras_node_dbg(__node, format, ...)                          \
	dev_dbg((__node)->dev, "%s: " format, (__node)->name, \
		##__VA_ARGS__)

#define ras_record_err(__record, format, ...)                  \
	dev_err((__record)->node->dev, "%s: %s: " format, \
		(__record)->node->name, (__record)->name, ##__VA_ARGS__)
#define ras_record_info(__record, format, ...)                  \
	dev_info((__record)->node->dev, "%s: %s: " format, \
		 (__record)->node->name, (__record)->name, ##__VA_ARGS__)
#define ras_record_dbg(__record, format, ...)                  \
	dev_dbg((__record)->node->dev, "%s: %s: " format, \
		(__record)->node->name, (__record)->name, ##__VA_ARGS__)

#define ERXGROUP_4K_OFFSET		0xE00
#define ERXGROUP_16K_OFFSET		0x3800
#define ERXGROUP_64K_OFFSET		0xE000
#define ERXGROUP_4K_SIZE		SZ_4K
#define ERXGROUP_16K_SIZE		SZ_16K
#define ERXGROUP_64K_SIZE		SZ_64K
#define ERXGROUP_4K_ERRGSR_NUM		1
#define ERXGROUP_16K_ERRGSR_NUM		4
#define ERXGROUP_64K_ERRGSR_NUM		14

struct ras_record {
	char *name;
	void __iomem *regs_base;
	struct ras_node *node;

	int index;
};

struct ras_group {
	int errgsr_num;
	size_t size;
	u64 errgsr_offset;
};

extern const struct ras_group ras_group_config[];

struct ras_node {
	char *name;

	struct device *dev;
	const struct ras_group *group;

	void __iomem *base;
	void __iomem *errgsr;
	phys_addr_t addr;

	u8 *specific_data;
	/*
	 * This bitmap indicates which of the error records within this error
	 * node must be polled for error status.
	 * Bit[n] of this field pertains to error record corresponding to
	 * index n in this error group.
	 * Bit[n] = 0b: Error record at index n needs to be polled.
	 * Bit[n] = 1b: Error record at index n does not need to be polled.
	 */
	unsigned long *record_implemented;
	/*
	 * This bitmap indicates which of the error records within this error
	 * node support error status reporting using ERRGSR register.
	 * Bit[n] of this field pertains to error record corresponding to
	 * index n in this error group.
	 * Bit[n] = 0b: Error record at index n supports error status reporting
	 *              through ERRGSR.S.
	 * Bit[n] = 1b: Error record at index n does not support error reporting
	 *              through the ERRGSR.S bit. If this error record is
	 *              implemented, then it must be polled explicitly for
	 *              error events.
	 */
	unsigned long *status_reporting;
	struct ras_record *records;

	u32 specific_data_size;
	u32 record_count;
	u32 record_index;
	u32 flags;

	u8 type;
	u8 group_format;
};

#endif /* _DRIVERS_RAS_ARM64_RAS_H_ */
