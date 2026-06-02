// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2025, Alibaba Group.
 */

#include "ras.h"

static int ras_store_threshold(struct ras_record *record, u64 threshold)
{
	struct ce_threshold *ce = &record->ce;
	u64 err_misc0;

	if (!ce->info)
		return -EOPNOTSUPP;

	if (threshold > ce->info->max_count)
		return -EINVAL;

	ce->threshold = threshold;
	ce->count = ce->info->max_count - threshold + 1;

	err_misc0 = record_read(record, ERXMISC0);
	ce->reg_val = (err_misc0 & ~ce->info->mask) |
		      (ce->count << ce->info->shift);

	record_write(record, ERXMISC0, ce->reg_val);
	return 0;
}

static void ras_error_count(struct ras_record *record, struct record_count *count)
{
	count->ce += record->count.ce;
	count->de += record->count.de;
	count->uc += record->count.uc;
	count->ueu += record->count.ueu;
	count->uer += record->count.uer;
	count->ueo += record->count.ueo;
}

/* Debugfs for RAS node */

static int ras_node_err_count_show(struct seq_file *m, void *data)
{
	struct ras_node *node = m->private;
	struct record_count count = { 0 };
	int i;

	for (i = 0; i < node->record_count; i++)
		if (!test_bit(i, node->record_implemented))
			ras_error_count(&node->records[i], &count);

	seq_printf(m, "CE: %llu\n"
		   "DE: %llu\n"
		   "UC: %llu\n"
		   "UEU: %llu\n"
		   "UEO: %llu\n"
		   "UER: %llu\n",
		   count.ce, count.de, count.uc, count.ueu,
		   count.uer, count.ueo);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ras_node_err_count);

/* Attribute for RAS record */

#define DEFINE_RAS_DEBUGFS_ATTR(name, offset) \
static int name##_get(void *data, u64 *val) \
{ \
	struct ras_record *record = data; \
	*val = record_read(record, offset); \
	return 0; \
} \
static int name##_set(void *data, u64 val) \
{ \
	struct ras_record *record = data; \
	record_write(record, offset, val); \
	return 0; \
} \
DEFINE_DEBUGFS_ATTRIBUTE(name##_ops, name##_get, name##_set, "%#llx\n")

DEFINE_RAS_DEBUGFS_ATTR(err_fr, ERXFR);
DEFINE_RAS_DEBUGFS_ATTR(err_ctrl, ERXCTLR);

static int record_ce_threshold_get(void *data, u64 *val)
{
	struct ras_record *record = data;

	*val = record->ce.threshold;
	return 0;
}

static int record_ce_threshold_set(void *data, u64 val)
{
	struct ras_record *record = data;

	return ras_store_threshold(record, val);
}

DEFINE_DEBUGFS_ATTRIBUTE(record_ce_threshold_ops, record_ce_threshold_get,
			 record_ce_threshold_set, "%llu\n");

/* Node-level ce_threshold: write threshold to all records of this node */

static int node_ce_threshold_set(void *data, u64 val)
{
	struct ras_node *node = data;
	int i, ret, last_err = -EOPNOTSUPP;

	for (i = 0; i < node->record_count; i++) {
		ret = ras_store_threshold(&node->records[i], val);
		if (ret == 0)
			last_err = 0;
		else if (ret == -EINVAL)
			return ret;
	}

	return last_err;
}

DEFINE_DEBUGFS_ATTRIBUTE(node_ce_threshold_ops, NULL,
			 node_ce_threshold_set, "%llu\n");

static int ras_record_err_count_show(struct seq_file *m, void *data)
{
	struct ras_record *record = m->private;
	struct record_count count = { 0 };

	ras_error_count(record, &count);

	seq_printf(m, "CE: %llu\n"
		   "DE: %llu\n"
		   "UC: %llu\n"
		   "UEU: %llu\n"
		   "UEO: %llu\n"
		   "UER: %llu\n",
		   count.ce, count.de, count.uc, count.ueu,
		   count.uer, count.ueo);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ras_record_err_count);

static void ras_record_init_debugfs(struct ras_record *record)
{
	debugfs_create_file("err_fr", 0600, record->debugfs,
			    record, &err_fr_ops);
	debugfs_create_file("err_ctrl", 0600, record->debugfs,
			    record, &err_ctrl_ops);
	debugfs_create_file("err_count", 0400, record->debugfs,
			    record, &ras_record_err_count_fops);
	debugfs_create_file("ce_threshold", 0600, record->debugfs,
			    record, &record_ce_threshold_ops);
	ras_inject_init_debugfs(record);
}

static void ras_init_records_debugfs(struct ras_node *node)
{
	struct ras_record *record;
	int i;

	for (i = 0; i < node->record_count; i++) {
		record = &node->records[i];
		if (!record->name || test_bit(i, node->record_implemented))
			continue;
		record->debugfs = debugfs_create_dir(record->name,
						     node->debugfs);

		ras_record_init_debugfs(record);
	}
}

static void ras_oncore_node_init_debugfs(struct ras_node *node)
{
	int cpu;
	struct ras_node *percpu_node;
	char name[16];

	for_each_possible_cpu(cpu) {
		percpu_node = per_cpu_ptr(node->oncore_node, cpu);

		snprintf(name, sizeof(name), "processor%u", cpu);
		percpu_node->debugfs = debugfs_create_dir(name, arm64_ras_debugfs);

		debugfs_create_file("err_count", 0400, percpu_node->debugfs,
				    percpu_node, &ras_node_err_count_fops);
		debugfs_create_file("ce_threshold", 0200, percpu_node->debugfs,
				    percpu_node, &node_ce_threshold_ops);
		ras_init_records_debugfs(percpu_node);
	}
}

void ras_node_init_debugfs(struct ras_node *node)
{
	if (!node->name)
		return;

	if (ras_node_is_oncore(node)) {
		ras_oncore_node_init_debugfs(node);
		return;
	}

	node->debugfs = debugfs_create_dir(node->name, arm64_ras_debugfs);
	if (IS_ERR_OR_NULL(node->debugfs))
		return;

	debugfs_create_file("err_count", 0400, node->debugfs,
			    node, &ras_node_err_count_fops);
	debugfs_create_file("ce_threshold", 0200, node->debugfs,
			    node, &node_ce_threshold_ops);
	ras_init_records_debugfs(node);
}
