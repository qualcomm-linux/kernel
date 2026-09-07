// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2024, Alibaba Group.
 */

#include "aest.h"

static struct ras_ext_regs regs_inj;

struct inj_attr {
	struct attribute attr;
	ssize_t (*show)(struct aest_node *n, struct inj_attr *a, char *b);
	ssize_t (*store)(struct aest_node *n, struct inj_attr *a, const char *b,
				size_t c);
};

struct aest_inject {
	struct aest_node *node;
	struct kobject kobj;
};

#define to_inj(k)	container_of(k, struct aest_inject, kobj)
#define to_inj_attr(a)	container_of(a, struct inj_attr, attr)

static u64 aest_sysreg_read_inject(void *__unused, u32 offset)
{
	u64 *p = (u64 *)&regs_inj;

	return p[offset/8];
}

static void aest_sysreg_write_inject(void *base, u32 offset, u64 val)
{
	u64 *p = (u64 *)&regs_inj;

	p[offset/8] = val;
}

static u64 aest_iomem_read_inject(void *base, u32 offset)
{
	u64 *p = (u64 *)&regs_inj;

	return p[offset/8];
}

static void aest_iomem_write_inject(void *base, u32 offset, u64 val)
{
	u64 *p = (u64 *)&regs_inj;

	p[offset/8] = val;
}

static struct aest_access aest_access_inject[] = {
	[ACPI_AEST_NODE_SYSTEM_REGISTER] = {
		.read = aest_sysreg_read_inject,
		.write = aest_sysreg_write_inject,
	},

	[ACPI_AEST_NODE_MEMORY_MAPPED] = {
		.read = aest_iomem_read_inject,
		.write = aest_iomem_write_inject,
	},
	[ACPI_AEST_NODE_SINGLE_RECORD_MEMORY_MAPPED] = {
		.read = aest_iomem_read_inject,
		.write = aest_iomem_write_inject,
	},
	{ }
};

static int soft_inject_store(void *data, u64 val)
{
	int count = 0;
	struct aest_record record_inj, *record = data;
	struct aest_node node_inj, *node = record->node;

	memcpy(&node_inj, node, sizeof(*node));
	node_inj.name = "AEST-injection";

	record_inj.access = &aest_access_inject[node->info->interface_hdr->type];
	record_inj.node = &node_inj;
	record_inj.index = record->index;

	regs_inj.err_status |= ERR_STATUS_V;

	aest_proc_record(&record_inj, &count, true);

	if (count != 1)
		return -EIO;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(soft_inject_ops, NULL, soft_inject_store, "%llu\n");

static int hard_inject_store(void *data, u64 val)
{
	struct aest_record *record = data;
	struct aest_node *node = record->node;

	if (!node->inj)
		return -EPERM;

	aest_select_record(node, record->index);
	record_write(record, ERXPFGCTL, val);
	record_write(record, ERXPFGCDN, 0x100);
	aest_sync(node);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(hard_inject_ops, NULL, hard_inject_store, "%llu\n");

void aest_inject_init_debugfs(struct aest_record *record)
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
	debugfs_create_file("soft_inject", 0400, inj, record, &soft_inject_ops);

	if (record->node->inj)
		debugfs_create_file("hard_inject", 0400, inj, record, &hard_inject_ops);
}
