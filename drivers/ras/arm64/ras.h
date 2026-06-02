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

#define record_read(record, offset) \
	((record)->access->read((record)->regs_base, (offset)))
#define record_write(record, offset, val) \
	((record)->access->write((record)->regs_base, (offset), (val)))

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

#define ERXFR			0x0
#define ERXCTLR			0x8
#define ERXSTATUS		0x10
#define ERXADDR			0x18
#define ERXMISC0		0x20
#define ERXMISC1		0x28
#define ERXMISC2		0x30
#define ERXMISC3		0x38
#define ERXPFGF			0x800
#define ERXPFGCTL		0x808
#define ERXPFGCDN		0x810

struct ras_access {
	u64 (*read)(void __iomem *base, u32 offset);
	void (*write)(void __iomem *base, u32 offset, u64 val);
};

struct ras_record {
	char *name;
	void __iomem *regs_base;
	struct ras_node *node;
	const struct ras_access *access;

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
	struct ras_node __percpu *oncore_node;

	void __iomem *base;
	void __iomem *errgsr;
	void __iomem *inj;
	void __iomem *irq_config;
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
	u8 access_type;
	u8 group_format;
	u32 irq[AEST_MAX_INTERRUPT_PER_NODE];
	u32 gsi[AEST_MAX_INTERRUPT_PER_NODE];
};

#define CASE_READ(res, x)                           \
	case (x): {                                 \
		res = read_sysreg_s(SYS_##x##_EL1); \
		break;                              \
	}

#define CASE_WRITE(val, x)                            \
	case (x): {                                   \
		write_sysreg_s((val), SYS_##x##_EL1); \
		break;                                \
	}

static inline u64 ras_sysreg_read(void __iomem *base __always_unused, u32 offset)
{
	u64 res;

	switch (offset) {
	CASE_READ(res, ERXFR)
	CASE_READ(res, ERXCTLR)
	CASE_READ(res, ERXSTATUS)
	CASE_READ(res, ERXADDR)
	CASE_READ(res, ERXMISC0)
	CASE_READ(res, ERXMISC1)
	CASE_READ(res, ERXMISC2)
	CASE_READ(res, ERXMISC3)
	CASE_READ(res, ERXPFGF)
	CASE_READ(res, ERXPFGCTL)
	CASE_READ(res, ERXPFGCDN)
	default:
		res = 0;
	}
	return res;
}

static inline void ras_sysreg_write(void __iomem *base __always_unused, u32 offset, u64 val)
{
	switch (offset) {
	CASE_WRITE(val, ERXFR)
	CASE_WRITE(val, ERXCTLR)
	CASE_WRITE(val, ERXSTATUS)
	CASE_WRITE(val, ERXADDR)
	CASE_WRITE(val, ERXMISC0)
	CASE_WRITE(val, ERXMISC1)
	CASE_WRITE(val, ERXMISC2)
	CASE_WRITE(val, ERXMISC3)
	CASE_WRITE(val, ERXPFGF)
	CASE_WRITE(val, ERXPFGCTL)
	CASE_WRITE(val, ERXPFGCDN)
	default:
		return;
	}
}

static inline u64 ras_iomem_read(void __iomem *base, u32 offset)
{
	return readq_relaxed(base + offset);
}

static inline void ras_iomem_write(void __iomem *base, u32 offset, u64 val)
{
	writeq_relaxed(val, base + offset);
}

/* access type is decided by AEST interface type. */
static const struct ras_access ras_access[] = {
	[ACPI_AEST_NODE_SYSTEM_REGISTER] = {
		.read = ras_sysreg_read,
		.write = ras_sysreg_write,
	},
	[ACPI_AEST_NODE_MEMORY_MAPPED] = {
		.read = ras_iomem_read,
		.write = ras_iomem_write,
	},
	[ACPI_AEST_NODE_SINGLE_RECORD_MEMORY_MAPPED] = {
		.read = ras_iomem_read,
		.write = ras_iomem_write,
	},
};

static inline bool ras_node_is_oncore(struct ras_node *node)
{
	/*
	 * A processor node is "on-core" (uses PPI + cpuhp) only when its
	 * interrupt is a per-CPU PPI.  A shared processor node (e.g. cluster
	 * L3 cache, DSU) uses an SPI and must follow the non-oncore path
	 * (aest_online_dev) so that aest_config_irq and aest_online_dev are
	 * called instead of cpuhp_setup_state.
	 */
	if (node->type != ACPI_AEST_PROCESSOR_ERROR_NODE)
		return false;
	return irq_is_percpu(node->irq[ACPI_AEST_NODE_FAULT_HANDLING]) ||
	       irq_is_percpu(node->irq[ACPI_AEST_NODE_ERROR_RECOVERY]);
}

static inline void ras_select_record(struct ras_node *node, int index)
{
	if (node->type == ACPI_AEST_PROCESSOR_ERROR_NODE) {
		write_sysreg_s(index, SYS_ERRSELR_EL1);
		isb();
	}
}

/* Ensure all writes has taken effect. */
static inline void ras_sync(struct ras_node *node)
{
	if (node->type == ACPI_AEST_PROCESSOR_ERROR_NODE)
		isb();
}

#endif /* _DRIVERS_RAS_ARM64_RAS_H_ */
