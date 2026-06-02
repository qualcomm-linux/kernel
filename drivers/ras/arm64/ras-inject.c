// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2024, Alibaba Group.
 */

#include "ras.h"

static struct ras_ext_regs regs_inj;

struct inj_attr {
	struct attribute attr;
	ssize_t (*show)(struct ras_node *n, struct inj_attr *a, char *b);
	ssize_t (*store)(struct ras_node *n, struct inj_attr *a, const char *b,
				size_t c);
};

struct ras_inject {
	struct ras_node *node;
	struct kobject kobj;
};

#define to_inj(k)	container_of(k, struct ras_inject, kobj)
#define to_inj_attr(a)	container_of(a, struct inj_attr, attr)

static u64 ras_sysreg_read_inject(void *__unused, u32 offset)
{
	u64 *p = (u64 *)&regs_inj;

	return p[offset/8];
}

static void ras_sysreg_write_inject(void *base, u32 offset, u64 val)
{
	u64 *p = (u64 *)&regs_inj;

	p[offset/8] = val;
}

static u64 ras_iomem_read_inject(void *base, u32 offset)
{
	u64 *p = (u64 *)&regs_inj;

	return p[offset/8];
}

static void ras_iomem_write_inject(void *base, u32 offset, u64 val)
{
	u64 *p = (u64 *)&regs_inj;

	p[offset/8] = val;
}

static struct ras_access ras_access_inject[] = {
	[ACPI_AEST_NODE_SYSTEM_REGISTER] = {
		.read = ras_sysreg_read_inject,
		.write = ras_sysreg_write_inject,
	},

	[ACPI_AEST_NODE_MEMORY_MAPPED] = {
		.read = ras_iomem_read_inject,
		.write = ras_iomem_write_inject,
	},
	[ACPI_AEST_NODE_SINGLE_RECORD_MEMORY_MAPPED] = {
		.read = ras_iomem_read_inject,
		.write = ras_iomem_write_inject,
	},
	{ }
};

static int soft_inject_store(void *data, u64 val)
{
	int count = 0;
	struct ras_record record_inj, *record = data;
	struct ras_node *node = record->node;

	memcpy(&record_inj, record, sizeof(*record));
	record_inj.access = &ras_access_inject[node->access_type];

	regs_inj.err_status |= ERR_STATUS_V;

	ras_proc_record(&record_inj, &count, true);

	if (count != 1)
		return -EIO;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(soft_inject_ops, NULL, soft_inject_store, "%llu\n");

static int hard_inject_store(void *data, u64 val)
{
	struct ras_record *record = data;
	struct ras_node *node = record->node;

	if (node->type != ACPI_AEST_PROCESSOR_ERROR_NODE && !node->inj)
		return -EPERM;

	ras_select_record(node, record->index);
	record_write(record, ERXPFGCTL, val);
	record_write(record, ERXPFGCDN, 0x100);
	ras_sync(node);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(hard_inject_ops, NULL, hard_inject_store, "%llu\n");

void ras_inject_init_debugfs(struct ras_record *record)
{
	struct dentry *inj;

	inj = debugfs_create_dir("inject", record->debugfs);

	debugfs_create_u64("err_fr", 0600, inj, &regs_inj.err_fr);
	debugfs_create_u64("err_ctrl", 0600, inj, &regs_inj.err_ctlr);
	debugfs_create_u64("err_status", 0600, inj, &regs_inj.err_status);
	debugfs_create_u64("err_addr", 0600, inj, &regs_inj.err_addr);
	debugfs_create_u64("err_misc0", 0600, inj, &regs_inj.err_misc[0]);
	debugfs_create_u64("err_misc1", 0600, inj, &regs_inj.err_misc[1]);
	debugfs_create_u64("err_misc2", 0600, inj, &regs_inj.err_misc[2]);
	debugfs_create_u64("err_misc3", 0600, inj, &regs_inj.err_misc[3]);
	debugfs_create_file("soft_inject", 0200, inj, record, &soft_inject_ops);

	if (record->node->type == ACPI_AEST_PROCESSOR_ERROR_NODE || record->node->inj)
		debugfs_create_file("hard_inject", 0200, inj, record, &hard_inject_ops);
}
