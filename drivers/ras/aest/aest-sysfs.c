// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2025, Alibaba Group.
 */

#include "aest.h"

static void
aest_store_threshold(struct aest_record *record, void *data)
{
	u64 err_misc0, *threshold = data;
	struct ce_threshold *ce = &record->ce;

	if (*threshold > ce->info->max_count)
		return;

	ce->threshold = *threshold;
	ce->count = ce->info->max_count - ce->threshold + 1;

	err_misc0 = record_read(record, ERXMISC0);
	ce->reg_val = (err_misc0 & ~ce->info->mask) |
			(ce->count << ce->info->shift);

	record_write(record, ERXMISC0, ce->reg_val);
}

static void
aest_error_count(struct aest_record *record, void *data)
{
	struct record_count *count = data;

	count->ce += record->count.ce;
	count->de += record->count.de;
	count->uc += record->count.uc;
	count->ueu += record->count.ueu;
	count->uer += record->count.uer;
	count->ueo += record->count.ueo;
}

/*******************************************************************************
 *
 * Debugfs for AEST node
 *
 ******************************************************************************/

static int aest_node_err_count_show(struct seq_file *m, void *data)
{
	struct aest_node *node = m->private;
	struct record_count count = { 0 };
	int i;

	for (i = 0; i < node->record_count; i++)
		if (!test_bit(i, node->record_implemented))
			aest_error_count(&node->records[i], &count);

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
DEFINE_SHOW_ATTRIBUTE(aest_node_err_count);

/*******************************************************************************
 *
 * Attribute for AEST record
 *
 ******************************************************************************/

#define DEFINE_AEST_DEBUGFS_ATTR(name, offset) \
static int name##_get(void *data, u64 *val) \
{ \
	struct aest_record *record = data; \
	*val = record_read(record, offset); \
	return 0; \
} \
static int name##_set(void *data, u64 val) \
{ \
	struct aest_record *record = data; \
	record_write(record, offset, val); \
	return 0; \
} \
DEFINE_DEBUGFS_ATTRIBUTE(name##_ops, name##_get, name##_set, "%#llx\n")

DEFINE_AEST_DEBUGFS_ATTR(err_fr, ERXFR);
DEFINE_AEST_DEBUGFS_ATTR(err_ctrl, ERXCTLR);
DEFINE_AEST_DEBUGFS_ATTR(err_status, ERXSTATUS);
DEFINE_AEST_DEBUGFS_ATTR(err_addr, ERXADDR);
DEFINE_AEST_DEBUGFS_ATTR(err_misc0, ERXMISC0);
DEFINE_AEST_DEBUGFS_ATTR(err_misc1, ERXMISC1);
DEFINE_AEST_DEBUGFS_ATTR(err_misc2, ERXMISC2);
DEFINE_AEST_DEBUGFS_ATTR(err_misc3, ERXMISC3);

static int record_ce_threshold_get(void *data, u64 *val)
{
	struct aest_record *record = data;

	*val = record->ce.threshold;
	return 0;
}

static int record_ce_threshold_set(void *data, u64 val)
{
	u64 threshold = val;
	struct aest_record *record = data;

	aest_store_threshold(record, &threshold);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(record_ce_threshold_ops, record_ce_threshold_get,
					record_ce_threshold_set, "%llu\n");

static int aest_record_err_count_show(struct seq_file *m, void *data)
{
	struct aest_record *record = m->private;
	struct record_count count = { 0 };

	aest_error_count(record, &count);

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
DEFINE_SHOW_ATTRIBUTE(aest_record_err_count);

static void aest_record_init_debugfs(struct aest_record *record)
{
	debugfs_create_file("err_fr", 0600, record->debugfs, record,
								&err_fr_ops);
	debugfs_create_file("err_ctrl", 0600, record->debugfs, record,
								&err_ctrl_ops);
	debugfs_create_file("err_status", 0600, record->debugfs, record,
								&err_status_ops);
	debugfs_create_file("err_addr", 0600, record->debugfs, record,
								&err_addr_ops);
	debugfs_create_file("err_misc0", 0600, record->debugfs, record,
								&err_misc0_ops);
	debugfs_create_file("err_misc1", 0600, record->debugfs, record,
								&err_misc1_ops);
	debugfs_create_file("err_misc2", 0600, record->debugfs, record,
								&err_misc2_ops);
	debugfs_create_file("err_misc3", 0600, record->debugfs, record,
								&err_misc3_ops);
	debugfs_create_file("err_count", 0400, record->debugfs, record,
						&aest_record_err_count_fops);
	debugfs_create_file("ce_threshold", 0600, record->debugfs, record,
						&record_ce_threshold_ops);
	aest_inject_init_debugfs(record);
}

static void
aest_node_init_debugfs(struct aest_node *node)
{
	int i;
	struct aest_record *record;

	debugfs_create_file("err_count", 0400, node->debugfs, node,
					&aest_node_err_count_fops);

	for (i = 0; i < node->record_count; i++) {
		record = &node->records[i];
		if (!record->name)
			continue;
		/* Skip records not implemented on this node. */
		if (test_bit(i, node->record_implemented))
			continue;
		record->debugfs = debugfs_create_dir(record->name,
							node->debugfs);
		aest_record_init_debugfs(record);
	}
}

static void
aest_oncore_dev_init_debugfs(struct aest_device *adev)
{
	int cpu, i;
	struct aest_node *node;
	struct aest_device *percpu_dev;
	char name[16];

	for_each_possible_cpu(cpu) {
		percpu_dev = per_cpu_ptr(adev->adev_oncore, cpu);

		snprintf(name, sizeof(name), "processor%u", cpu);
		percpu_dev->debugfs = debugfs_create_dir(name, adev->debugfs);

		for (i = 0; i < adev->node_cnt; i++) {
			node = &percpu_dev->nodes[i];

			/*
			 * Use adev->nodes[i].name (the original) rather than
			 * node->name from the per-CPU copy. The per-CPU copy
			 * receives node->name via shallow memcpy in __setup_ppi;
			 * the original is the authoritative, guaranteed-valid
			 * string.
			 */
			node->debugfs = debugfs_create_dir(adev->nodes[i].name,
							   percpu_dev->debugfs);
			aest_node_init_debugfs(node);
		}
	}
}

void aest_dev_init_debugfs(struct aest_device *adev)
{
	int i;
	struct aest_node *node;

	if (!aest_debugfs)
		dev_err(adev->dev, "debugfs not enabled\n");

	adev->debugfs = debugfs_create_dir(dev_name(adev->dev), aest_debugfs);
	if (aest_dev_is_oncore(adev)) {
		aest_oncore_dev_init_debugfs(adev);
		return;
	}

	for (i = 0; i < adev->node_cnt; i++) {
		node = &adev->nodes[i];
		if (!node->name)
			continue;
		node->debugfs = debugfs_create_dir(node->name, adev->debugfs);
		aest_node_init_debugfs(node);
	}
}
