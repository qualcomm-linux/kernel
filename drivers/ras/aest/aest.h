/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2025, Alibaba Group.
 */

#include <linux/acpi_aest.h>
#include <asm/ras.h>
#include <linux/debugfs.h>
#include <linux/irqdesc.h>

#define MAX_GSI_PER_NODE 2
#define DEFAULT_CE_THRESHOLD 1

#define record_read(record, offset) \
	record->access->read(record->regs_base, offset)
#define record_write(record, offset, val) \
	record->access->write(record->regs_base, offset, val)

#define aest_dev_err(__adev, format, ...) \
	dev_err((__adev)->dev, format, ##__VA_ARGS__)
#define aest_dev_info(__adev, format, ...) \
	dev_info((__adev)->dev, format, ##__VA_ARGS__)
#define aest_dev_dbg(__adev, format, ...) \
	dev_dbg((__adev)->dev, format, ##__VA_ARGS__)

#define aest_node_err(__node, format, ...)                          \
	dev_err((__node)->adev->dev, "%s: " format, (__node)->name, \
		##__VA_ARGS__)
#define aest_node_info(__node, format, ...)                          \
	dev_info((__node)->adev->dev, "%s: " format, (__node)->name, \
		 ##__VA_ARGS__)
#define aest_node_dbg(__node, format, ...)                          \
	dev_dbg((__node)->adev->dev, "%s: " format, (__node)->name, \
		##__VA_ARGS__)

#define aest_record_err(__record, format, ...)                  \
	dev_err((__record)->node->adev->dev, "%s: %s: " format, \
		(__record)->node->name, (__record)->name, ##__VA_ARGS__)
#define aest_record_info(__record, format, ...)                  \
	dev_info((__record)->node->adev->dev, "%s: %s: " format, \
		 (__record)->node->name, (__record)->name, ##__VA_ARGS__)
#define aest_record_dbg(__record, format, ...)                  \
	dev_dbg((__record)->node->adev->dev, "%s: %s: " format, \
		(__record)->node->name, (__record)->name, ##__VA_ARGS__)

#define ERXGROUP_4K_OFFSET 0xE00
#define ERXGROUP_16K_OFFSET 0x3800
#define ERXGROUP_64K_OFFSET 0xE000
#define ERXGROUP_4K_SIZE (4 * KB)
#define ERXGROUP_16K_SIZE (16 * KB)
#define ERXGROUP_64K_SIZE (64 * KB)
#define ERXGROUP_4K_ERRGSR_NUM 1
#define ERXGROUP_16K_ERRGSR_NUM 4
#define ERXGROUP_64K_ERRGSR_NUM 14

#define ERXFR 0x0
#define ERXCTLR 0x8
#define ERXSTATUS 0x10
#define ERXADDR 0x18
#define ERXMISC0 0x20
#define ERXMISC1 0x28
#define ERXMISC2 0x30
#define ERXMISC3 0x38
#define ERXPFGF 0x800
#define ERXPFGCTL 0x808
#define ERXPFGCDN 0x810

#define GIC_ERRDEVARCH 0xFFBC

extern struct dentry *aest_debugfs;

struct aest_event {
	struct llist_node llnode;
	char *node_name;
	u32 type;
	/*
	 * Different nodes have different meanings:
	 *   - Processor node	: processor number.
	 *   - Memory node	: SRAT proximity domain.
	 *   - SMMU node	: IORT proximity domain.
	 *   - GIC node		: interface type.
	 */
	u32 id0;
	/*
	 * Different nodes have different meanings:
	 *   - Processor node	: processor resource type.
	 *   - Memory node	: Non.
	 *   - SMMU node	: subcomponent reference.
	 *   - Vendor node	: Unique ID.
	 *   - GIC node		: instance identifier.
	 */
	u32 id1;
	/* Vendor node	: hardware ID. */
	char *hid;
	u32 index;
	/* Processor node: ACPI_AEST_PROC_FLAG_* bitmask (SHARED/GLOBAL) */
	u8 proc_flags;
	u64 ce_threshold;
	int addressing_mode;
	struct ras_ext_regs regs;

	/*
	 * This field is used to store vendor specific data for decoding error
	 * record by EDAC driver.
	 */
	void *vendor_data;
	size_t vendor_data_size;
};

struct aest_access {
	u64 (*read)(void *base, u32 offset);
	void (*write)(void *base, u32 offset, u64 val);
};

struct ce_threshold_info {
	const u64 max_count;
	const u64 mask;
	const u64 shift;
};

struct ce_threshold {
	const struct ce_threshold_info *info;
	u64 count;
	u64 threshold;
	u64 reg_val;
};

struct record_count {
	u64 ce;
	u64 de;
	u64 uc;
	u64 uer;
	u64 ueo;
	u64 ueu;
};

struct aest_record {
	char *name;
	int index;
	void __iomem *regs_base;

	/*
	 * This bit specifies the addressing mode  to populate the ERR_ADDR
	 * register:
	 *   0b: Error record reports System Physical Addresses (SPA) in
	 *       the ERR_ADDR register.
	 *   1b: Error record reports error node-specific Logical Addresses(LA)
	 *       in the ERR_ADD register. OS must use other means to translate
	 *       the reported LA into SPA
	 */
	int addressing_mode;
	struct aest_node *node;
	const struct aest_access *access;

	struct ce_threshold ce;
	enum ras_ce_threshold threshold_type;
	struct record_count count;
	struct dentry *debugfs;

	void *vendor_data;
	size_t vendor_data_size;
};

struct aest_group {
	int type;
	int errgsr_num;
	size_t size;
	u64 errgsr_offset;
};

static const struct aest_group aest_group_config[] = {
	[ACPI_AEST_NODE_GROUP_FORMAT_4K] = {
		.type = ACPI_AEST_NODE_GROUP_FORMAT_4K,
		.errgsr_num = ERXGROUP_4K_ERRGSR_NUM,
		.size = ERXGROUP_4K_SIZE,
		.errgsr_offset = ERXGROUP_4K_OFFSET,
	},
	[ACPI_AEST_NODE_GROUP_FORMAT_16K] = {
		.type = ACPI_AEST_NODE_GROUP_FORMAT_16K,
		.errgsr_num = ERXGROUP_16K_ERRGSR_NUM,
		.size = ERXGROUP_16K_SIZE,
		.errgsr_offset = ERXGROUP_16K_OFFSET,
	},
	[ACPI_AEST_NODE_GROUP_FORMAT_64K] = {
		.type = ACPI_AEST_NODE_GROUP_FORMAT_64K,
		.errgsr_num = ERXGROUP_64K_ERRGSR_NUM,
		.size = ERXGROUP_64K_SIZE,
		.errgsr_offset = ERXGROUP_64K_OFFSET,
	},
};

struct aest_node {
	char *name;
	u8 type;
	void *errgsr;
	void *base;
	void *inj;
	void *irq_config;

	/*
	 * This bitmap indicates which of the error records within this error
	 * node must be polled for error status.
	 * Bit[n] of this field pertains to error record corresponding to
	 * index n in this error group.
	 * Bit[n] = 0b: Error record at index n needs to be polled.
	 * Bit[n] = 1b: Error record at index n do not needs to be polled.
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
	 *              through the ERRGSR.S bit If this error record is
	 *              implemented, then it must be polled explicitly for
	 *              error events.
	 */
	unsigned long *status_reporting;
	int version;
	/*
	 * Usually bit[n] in errgsr indicates [n]th error record within this
	 * error node report error. But some compoent may have different rules.
	 * For example, CMN700 TRM 4.3.5.12 say:
	 *	``` Error occurs when the index is even and Fault
	 *	    occurs when the index is odd. ```
	 *	Bit[n]: record[n] report ERROR.
	 *	Bit[n + 1]: record[n] report FAULT.
	 * errgsr_mapping function is used to map errgsr bit to record index
	 * for various components.
	 */
	int (*errgsr_mapping)(int errgsr_bit);
	int errgsr_num;

	const struct aest_group *group;
	struct aest_device *adev;
	struct acpi_aest_node *info;

	int record_count;
	struct aest_record *records;
	struct dentry *debugfs;
	struct aest_node __percpu *oncore_node;
};

struct aest_device {
	struct device *dev;
	u32 type;
	int node_cnt;
	struct aest_node *nodes;
	u32 id;
	int irq[MAX_GSI_PER_NODE];

	struct work_struct aest_work;
	struct gen_pool *pool;
	struct llist_head event_list;
	struct dentry *debugfs;
	struct aest_device __percpu *adev_oncore;
};

static const char *const aest_node_name[] = {
	[ACPI_AEST_PROCESSOR_ERROR_NODE] = "processor",
	[ACPI_AEST_MEMORY_ERROR_NODE] = "memory",
	[ACPI_AEST_SMMU_ERROR_NODE] = "smmu",
	[ACPI_AEST_VENDOR_ERROR_NODE] = "vendor",
	[ACPI_AEST_GIC_ERROR_NODE] = "gic",
	[ACPI_AEST_PCIE_ERROR_NODE] = "pcie",
	[ACPI_AEST_PROXY_ERROR_NODE] = "proxy",
};

struct aest_vendor_match {
	char hid[ACPI_ID_LEN];
	int (*probe)(struct aest_device *adev, struct aest_hnode *anode);
};

static inline int aest_set_name(struct aest_device *adev,
				struct aest_hnode *ahnode)
{
	adev->dev->init_name = devm_kasprintf(adev->dev, GFP_KERNEL, "%s%d",
					      aest_node_name[ahnode->type],
					      adev->id);
	if (!adev->dev->init_name)
		return -ENOMEM;

	return 0;
}

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

static inline u64 aest_sysreg_read(void *__unused, u32 offset)
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
	default :
		res = 0;
	}
	return res;
}

static inline void aest_sysreg_write(void *base, u32 offset, u64 val)
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
	default :
		return;
	}
}

static inline u64 aest_iomem_read(void *base, u32 offset)
{
	return readq_relaxed(base + offset);
}

static inline void aest_iomem_write(void *base, u32 offset, u64 val)
{
	writeq_relaxed(val, base + offset);
}

/* access type is decided by AEST interface type. */
static const struct aest_access aest_access[] = {
	[ACPI_AEST_NODE_SYSTEM_REGISTER] = {
		.read = aest_sysreg_read,
		.write = aest_sysreg_write,
	},
	[ACPI_AEST_NODE_MEMORY_MAPPED] = {
		.read = aest_iomem_read,
		.write = aest_iomem_write,
	},
	[ACPI_AEST_NODE_SINGLE_RECORD_MEMORY_MAPPED] = {
		.read = aest_iomem_read,
		.write = aest_iomem_write,
	},
	{ }
};

/*
 * Each PE may has multi error record, you must selects an error
 * record to be accessed through the Error Record System
 * registers.
 */
static inline void aest_select_record(struct aest_node *node, int index)
{
	if (node->type == ACPI_AEST_PROCESSOR_ERROR_NODE) {
		write_sysreg_s(index, SYS_ERRSELR_EL1);
		isb();
	}
}

/* Ensure all writes has taken effect. */
static inline void aest_sync(struct aest_node *node)
{
	if (node->type == ACPI_AEST_PROCESSOR_ERROR_NODE)
		isb();
}

static inline bool aest_dev_is_oncore(struct aest_device *adev)
{
	/*
	 * A processor node is "on-core" (uses PPI + cpuhp) only when its
	 * interrupt is a per-CPU PPI.  A shared processor node (e.g. cluster
	 * L3 cache, DSU) uses an SPI and must follow the non-oncore path
	 * (aest_online_dev) so that aest_config_irq and aest_online_dev are
	 * called instead of cpuhp_setup_state.
	 */
	if (adev->type != ACPI_AEST_PROCESSOR_ERROR_NODE)
		return false;
	return irq_is_percpu(adev->irq[ACPI_AEST_NODE_FAULT_HANDLING]) ||
	       irq_is_percpu(adev->irq[ACPI_AEST_NODE_ERROR_RECOVERY]);
}

static inline int default_errgsr_mapping(int errgsr_bit)
{
	return errgsr_bit;
}

static inline int cmn700_errgsr_mapping(int errgsr_bit)
{
	return errgsr_bit / 2;
}

void aest_dev_init_debugfs(struct aest_device *adev);
void aest_inject_init_debugfs(struct aest_record *record);
void aest_proc_record(struct aest_record *record, void *data, bool fake);
void aest_node_foreach_record(void (*func)(struct aest_record *, void *, bool),
			      struct aest_node *node, void *data,
			      unsigned long *bitmap);

int aest_cmn700_probe(struct aest_device *adev, struct aest_hnode *ahnode);
