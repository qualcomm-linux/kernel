// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2025, Alibaba Group.
 */

#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/panic.h>
#include <linux/platform_device.h>
#include <linux/ras.h>
#include <ras/ras_event.h>

#include "ras.h"

#undef pr_fmt
#define pr_fmt(fmt) "arm64_ras: " fmt

static bool panic_on_ue;
module_param(panic_on_ue, bool, 0600);
MODULE_PARM_DESC(aest_panic_on_ue,
		 "Panic on unrecoverable error: 0=off 1=on (default: 1)");


static DEFINE_PER_CPU(struct ras_node, percpu_ras_node);

struct dentry *arm64_ras_debugfs;

static const char *const ras_node_name[] = {
	[ACPI_AEST_PROCESSOR_ERROR_NODE] = "processor",
	[ACPI_AEST_MEMORY_ERROR_NODE] = "memory",
	[ACPI_AEST_SMMU_ERROR_NODE] = "smmu",
	[ACPI_AEST_VENDOR_ERROR_NODE] = "vendor",
	[ACPI_AEST_GIC_ERROR_NODE] = "gic",
	[ACPI_AEST_PCIE_ERROR_NODE] = "pcie",
	[ACPI_AEST_PROXY_ERROR_NODE] = "proxy",
};

const struct ras_group ras_group_config[] = {
	[ACPI_AEST_NODE_GROUP_FORMAT_4K] = {
		.errgsr_num = ERXGROUP_4K_ERRGSR_NUM,
		.size = ERXGROUP_4K_SIZE,
		.errgsr_offset = ERXGROUP_4K_OFFSET,
	},
	[ACPI_AEST_NODE_GROUP_FORMAT_16K] = {
		.errgsr_num = ERXGROUP_16K_ERRGSR_NUM,
		.size = ERXGROUP_16K_SIZE,
		.errgsr_offset = ERXGROUP_16K_OFFSET,
	},
	[ACPI_AEST_NODE_GROUP_FORMAT_64K] = {
		.errgsr_num = ERXGROUP_64K_ERRGSR_NUM,
		.size = ERXGROUP_64K_SIZE,
		.errgsr_offset = ERXGROUP_64K_OFFSET,
	},
};

static const struct ce_threshold_info ce_info[] = {
	[RAS_CE_THRESHOLD_0B] = { 0 },
	[RAS_CE_THRESHOLD_8B] = {
		.max_count = ERR_8B_CEC_MAX,
		.mask = ERR_MISC0_8B_CEC,
		.shift = ERR_MISC0_CEC_SHIFT,
	},
	[RAS_CE_THRESHOLD_16B] = {
		.max_count = ERR_16B_CEC_MAX,
		.mask = ERR_MISC0_16B_CEC,
		.shift = ERR_MISC0_CEC_SHIFT,
	},
};

#define AEST_LOG_PREFIX_BUFFER 64

static void ras_print(struct ras_record *record, struct ras_ext_regs *regs)
{
	static atomic_t seqno = { 0 };
	struct ras_node *node = record->node;
	u8 *data = node->specific_data;
	unsigned int curr_seqno;
	char pfx_seq[AEST_LOG_PREFIX_BUFFER];
	int index = record->index;

	curr_seqno = atomic_inc_return(&seqno);
	snprintf(pfx_seq, sizeof(pfx_seq), "{%u}" HW_ERR, curr_seqno);
	pr_info("%sHardware error from AEST %s\n", pfx_seq, node->name);

	switch (node->type) {
	case ACPI_AEST_PROCESSOR_ERROR_NODE: {
		struct acpi_aest_processor *proc = (struct acpi_aest_processor *)data;

		if (proc->flags &
		    (ACPI_AEST_PROC_FLAG_SHARED | ACPI_AEST_PROC_FLAG_GLOBAL))
			pr_err("%s Error from shared processor resource (interrupt handled on CPU%d)\n",
			       pfx_seq, smp_processor_id());
		else
			pr_err("%s Error from CPU%d\n", pfx_seq, smp_processor_id());
		break;
	}
	case ACPI_AEST_MEMORY_ERROR_NODE:
		pr_err("%s Error from memory at SRAT proximity domain %#x\n",
		       pfx_seq,
		       ((struct acpi_aest_memory *)data)->srat_proximity_domain);
		break;
	case ACPI_AEST_SMMU_ERROR_NODE:
		pr_err("%s Error from SMMU IORT node %#x subcomponent %#x\n",
		       pfx_seq,
		       ((struct acpi_aest_smmu *)data)->iort_node_reference,
		       ((struct acpi_aest_smmu *)data)->subcomponent_reference);
		break;
	case ACPI_AEST_VENDOR_ERROR_NODE:
		pr_err("%s Error from vendor hid %8.8s uid %#x\n", pfx_seq,
		       ((struct acpi_aest_vendor_v2 *)data)->acpi_hid,
		       ((struct acpi_aest_vendor_v2 *)data)->acpi_uid);
		break;
	case ACPI_AEST_GIC_ERROR_NODE:
		pr_err("%s Error from GIC type %#x instance %#x\n", pfx_seq,
		       ((struct acpi_aest_gic *)data)->interface_type,
		       ((struct acpi_aest_gic *)data)->instance_id);
		break;
	default:
		pr_err("%s Unknown AEST node type\n", pfx_seq);
		return;
	}

	pr_err("%s  ERR%dFR: 0x%llx\n", pfx_seq, index, regs->err_fr);
	pr_err("%s  ERR%dCTRL: 0x%llx\n", pfx_seq, index, regs->err_ctlr);
	pr_err("%s  ERR%dSTATUS: 0x%llx\n", pfx_seq, index, regs->err_status);
	if (regs->err_status & ERR_STATUS_AV)
		pr_err("%s  ERR%dADDR: 0x%llx\n", pfx_seq, index,
		       regs->err_addr);

	if (regs->err_status & ERR_STATUS_MV) {
		pr_err("%s  ERR%dMISC0: 0x%llx\n", pfx_seq, index,
		       regs->err_misc[0]);
		pr_err("%s  ERR%dMISC1: 0x%llx\n", pfx_seq, index,
		       regs->err_misc[1]);
		pr_err("%s  ERR%dMISC2: 0x%llx\n", pfx_seq, index,
		       regs->err_misc[2]);
		pr_err("%s  ERR%dMISC3: 0x%llx\n", pfx_seq, index,
		       regs->err_misc[3]);
	}
}

static ATOMIC_NOTIFIER_HEAD(ras_decoder_chain);

void ras_register_decode_chain(struct notifier_block *nb)
{
	atomic_notifier_chain_register(&ras_decoder_chain, nb);
}
EXPORT_SYMBOL_GPL(ras_register_decode_chain);

void ras_unregister_decode_chain(struct notifier_block *nb)
{
	atomic_notifier_chain_unregister(&ras_decoder_chain, nb);
}
EXPORT_SYMBOL_GPL(ras_unregister_decode_chain);

static void ras_do_proc(struct ras_record *record, struct ras_ext_regs *regs)
{
	u64 status = regs->err_status, addr = regs->err_addr;

	ras_print(record, regs);
	if (regs->err_status & ERR_STATUS_CE)
		record->count.ce++;
	if (regs->err_status & ERR_STATUS_DE)
		record->count.de++;
	if (regs->err_status & ERR_STATUS_UE) {
		switch (FIELD_GET(ERR_STATUS_UET, regs->err_status)) {
		case ERR_STATUS_UET_UC:
			record->count.uc++;
			break;
		case ERR_STATUS_UET_UEU:
			record->count.ueu++;
			break;
		case ERR_STATUS_UET_UER:
			record->count.uer++;
			break;
		case ERR_STATUS_UET_UEO:
			record->count.ueo++;
			break;
		}
	}

	trace_arm_ras_ext_event(record->node->type, record->index, regs,
				record->node->specific_data, record->node->specific_data_size,
				record->vendor_data, record->vendor_data_size);

	atomic_notifier_call_chain(&ras_decoder_chain, 0, record);

	if (status & ERR_STATUS_CE)
		return;

	if (record->addressing_mode == AEST_ADDRESS_LA || (addr & ERR_ADDR_AI))
		return;

	memory_failure_queue(addr & PHYS_MASK, 0);
}

static void ras_panic(struct ras_record *record, struct ras_ext_regs *regs,
		       char *msg)
{
	ras_print(record, regs);

	panic(msg);
}

void ras_proc_record(struct ras_record *record, void *data, bool fake)
{
	struct ras_ext_regs regs = { 0 };
	int *count = data;
	u64 ue;

	regs.err_status = record_read(record, ERXSTATUS);

	arm64_ras_storm_track_record(record, regs.err_status);

	if (!(regs.err_status & ERR_STATUS_V))
		return;

	(*count)++;

	if (regs.err_status & ERR_STATUS_AV)
		regs.err_addr = record_read(record, ERXADDR);

	regs.err_fr = record_read(record, ERXFR);
	regs.err_ctlr = record_read(record, ERXCTLR);

	if (regs.err_status & ERR_STATUS_MV) {
		regs.err_misc[0] = record_read(record, ERXMISC0);
		regs.err_misc[1] = record_read(record, ERXMISC1);
		if (record->node->version >= ID_AA64PFR0_EL1_RAS_V1P1) {
			regs.err_misc[2] = record_read(record, ERXMISC2);
			regs.err_misc[3] = record_read(record, ERXMISC3);
		}

		record_write(record, ERXMISC0, record->ce.reg_val);
		if (record->node->flags & AEST_XFACE_FLAG_CLEAR_MISC) {
			record_write(record, ERXMISC1, 0);
			if (record->node->version >= ID_AA64PFR0_EL1_RAS_V1P1) {
				record_write(record, ERXMISC2, 0);
				record_write(record, ERXMISC3, 0);
			}
		}
	}

	/* panic if unrecoverable and uncontainable error encountered */
	ue = FIELD_GET(ERR_STATUS_UET, regs.err_status);
	if ((regs.err_status & ERR_STATUS_UE) &&
	    (ue == ERR_STATUS_UET_UC || ue == ERR_STATUS_UET_UEU)) {
		if (fake)
			ras_record_info(record,
					"Simulated error! Skip panic due to fault injection\n");
		else if (panic_on_ue)
			ras_panic(record, &regs,
				   "AEST: unrecoverable error encountered");
		else
			ras_record_err(record, "UE detected, panic suppressed\n");
	}
	ras_do_proc(record, &regs);

	/* Write-one-to-clear the bits we've seen */
	regs.err_status &= ERR_STATUS_W1TC;

	/* Multi bit filed need to write all-ones to clear. */
	if (regs.err_status & ERR_STATUS_CE)
		regs.err_status |= ERR_STATUS_CE;

	/* Multi bit filed need to write all-ones to clear. */
	if (regs.err_status & ERR_STATUS_UET)
		regs.err_status |= ERR_STATUS_UET;

	record_write(record, ERXSTATUS, regs.err_status);
}

void ras_node_foreach_record(void (*func)(struct ras_record *, void *, bool),
			     struct ras_node *node, void *data, unsigned long *bitmap)
{
	int i;

	for_each_clear_bit(i, bitmap, node->record_count) {
		ras_select_record(node, i);

		func(&node->records[i], data, false);

		ras_sync(node);
	}
}

static void ras_node_foreach_poll_record(void (*func)(struct ras_record *, void *, bool),
					 struct ras_node *node, void *data)
{
	int i;

	/*
	 * Per AEST spec:
	 *  - record_implemented: bitmap of records that are actually
	 *    implemented (valid records on this node).
	 *  - status_reporting: bitmap of records whose error status is
	 *    reported through ERRGSR; these will be discovered via the
	 *    ERRGSR scan path below and do not need polling.
	 *
	 * The remaining records (implemented but not reported via ERRGSR)
	 * must be polled one by one to detect errors. Compute that set as:
	 *     poll_bitmap = record_implemented & ~status_reporting
	 */
	for_each_clear_bit(i, node->record_implemented, node->record_count) {
		if (!test_bit(i, node->status_reporting))
			continue;

		ras_select_record(node, i);

		func(&node->records[i], data, false);

		ras_sync(node);
	}
}

static int ras_proc(struct ras_node *node)
{
	int count = 0, i, j, size = node->record_count, record_idx;
	u64 err_group = 0;

	ras_node_foreach_poll_record(ras_proc_record, node, &count);

	if (!node->errgsr)
		return count;

	ras_node_dbg(node, "Report bitmap %*pb\n", size, node->status_reporting);
	for (i = 0; i < BITS_TO_U64(size); i++) {
		err_group = readq_relaxed((void *)node->errgsr + i * 8);

		for_each_set_bit(j, (unsigned long *)&err_group, BITS_PER_LONG) {
			record_idx = node->errgsr_mapping(i * BITS_PER_LONG + j);
			ras_node_dbg(node, "errgsr[%d]: bit %d occur error\n",
				      i, record_idx);
			/*
			 * Error group base is only valid in Memory Map node,
			 * so driver do not need to write select register and
			 * sync.
			 */
			if (test_bit(record_idx, node->status_reporting))
				continue;
			ras_proc_record(&node->records[record_idx],
					&count, false);
		}
	}

	return count;
}

irqreturn_t ras_irq_func(int irq, void *input)
{
	struct ras_node *node = input;

	ras_proc(node);

	return IRQ_HANDLED;
}

static void ras_config_irq(struct ras_node *node)
{
	u32 fhi_gsi, eri_gsi;

	if (!node->irq_config)
		return;

	if (!device_property_read_u32(node->dev, "arm,fhi-gsiv", &fhi_gsi))
		writeq_relaxed(fhi_gsi, node->irq_config + ERRFHICR0_OFFSET);

	if (!device_property_read_u32(node->dev, "arm,eri-gsiv", &eri_gsi))
		writeq_relaxed(eri_gsi, node->irq_config + ERRERICR0_OFFSET);

	ras_node_dbg(node, "config irq fhi_gsi %u eri_gsi %u at %pK",
		     fhi_gsi, eri_gsi, node->irq_config);
}

static int ras_register_irq(struct ras_node *node)
{
	int i, irq, ret;
	char *irq_desc;

	irq_desc = devm_kasprintf(node->dev, GFP_KERNEL, "%s.%s.",
				  dev_driver_string(node->dev),
				  node->name);
	if (!irq_desc)
		return -ENOMEM;

	for (i = 0; i < AEST_MAX_INTERRUPT_PER_NODE; i++) {
		irq = node->irq[i];

		if (!irq)
			continue;

		if (irq_is_percpu_devid(irq)) {
			ret = request_percpu_irq(irq, ras_irq_func, irq_desc,
						 node->oncore_node);
			if (ret)
				goto free;
		} else {
			ret = devm_request_irq(node->dev, irq, ras_irq_func, IRQF_SHARED,
					       irq_desc, node);
			if (ret)
				return ret;
		}
	}
	return 0;

free:
	for (i = i - 1; i >= 0; i--) {
		irq = node->irq[i];

		if (irq_is_percpu_devid(irq))
			free_percpu_irq(irq, node->oncore_node);
	}

	return ret;
}

void ras_enable_irq(struct ras_record *record)
{
	struct ras_node *node = record->node;
	u64 err_ctlr;

	err_ctlr = record_read(record, ERXCTLR);

	if (node->irq[0])
		err_ctlr |= (ERR_CTLR_FI | ERR_CTLR_CFI);
	if (node->irq[1])
		err_ctlr |= ERR_CTLR_UI;

	record_write(record, ERXCTLR, err_ctlr);
}

void ras_disable_irq(struct ras_record *record)
{
	u64 err_ctlr;

	err_ctlr = record_read(record, ERXCTLR);
	err_ctlr &= ~(ERR_CTLR_FI | ERR_CTLR_CFI);
	record_write(record, ERXCTLR, err_ctlr);
}

static int ras_get_ce_threshold(struct ras_record *record)
{
	u64 err_fr, err_fr_cec, err_fr_rp;

	err_fr = record_read(record, ERXFR);
	err_fr_cec = FIELD_GET(ERR_FR_CEC, err_fr);
	err_fr_rp = FIELD_GET(ERR_FR_RP, err_fr);

	if (err_fr_cec == ERR_FR_CEC_0B_COUNTER)
		return RAS_CE_THRESHOLD_0B;
	else if (err_fr_rp == ERR_FR_RP_DOUBLE_COUNTER)
		return RAS_CE_THRESHOLD_32B;
	else if (err_fr_cec == ERR_FR_CEC_8B_COUNTER)
		return RAS_CE_THRESHOLD_8B;
	else if (err_fr_cec == ERR_FR_CEC_16B_COUNTER)
		return RAS_CE_THRESHOLD_16B;

	return RAS_CE_THRESHOLD_UNKNOWN;
}

static void ras_set_ce_threshold(struct ras_record *record)
{
	u64 err_misc0;
	struct ce_threshold *ce = &record->ce;
	const struct ce_threshold_info *info;

	record->threshold_type = ras_get_ce_threshold(record);

	switch (record->threshold_type) {
	case RAS_CE_THRESHOLD_0B:
		ras_record_dbg(record, "do not support CE threshold!\n");
		return;
	case RAS_CE_THRESHOLD_8B:
		ras_record_dbg(record, "support 8 bit CE threshold!\n");
		break;
	case RAS_CE_THRESHOLD_16B:
		ras_record_dbg(record, "support 16 bit CE threshold!\n");
		break;
	case RAS_CE_THRESHOLD_32B:
		ras_record_dbg(record, "not support 32 bit CE threshold!\n");
		return;
	default:
		ras_record_dbg(record, "Unknown misc0 ce threshold!\n");
		return;
	}

	err_misc0 = record_read(record, ERXMISC0);
	info = &ce_info[record->threshold_type];
	ce->info = info;

	/* Default CE threshold is 1 */
	ce->threshold = DEFAULT_CE_THRESHOLD;
	/*
	 * The CEC field in ERXMISC0 is a saturating up-counter; the
	 * overflow flag (ERXSTATUS.OF) is asserted only when CEC
	 * saturates at max_count. To make "threshold" mean "trigger OF
	 * after `threshold` more CEs", preset CEC to max_count - threshold.
	 */
	ce->count = info->max_count - ce->threshold + 1;
	ce->reg_val = (err_misc0 & ~info->mask) |
		      (ce->count << info->shift);

	record_write(record, ERXMISC0, ce->reg_val);
	ras_record_dbg(record, "CE threshold is %llu, controlled by Kernel",
		       ce->threshold);
}

static int get_ras_node_ver(struct ras_node *node)
{
	u32 reg;

	if (node->type == ACPI_AEST_GIC_ERROR_NODE) {
		if (!node->base)
			return 0;

		reg = readl_relaxed(node->base + GIC_ERRDEVARCH);
		return FIELD_GET(ERRDEVARCH_REV, reg);
	}

	return FIELD_GET(ID_AA64PFR0_EL1_RAS_MASK, read_cpuid(ID_AA64PFR0_EL1));
}


static int ras_init_record(struct ras_record *record, int i, struct ras_node *node)
{
	record->name = devm_kasprintf(node->dev, GFP_KERNEL, "record%d", i);
	if (!record->name)
		return -ENOMEM;

	if (node->base)
		record->regs_base = node->base + sizeof(struct ras_ext_regs) * i;

	record->access = &ras_access[node->access_type];
	record->index = i;
	record->node = node;
	record->addressing_mode = test_bit(i, node->addressing_mode);

	ras_record_dbg(record, "record initialized, addressing mode: %s\n",
		       record->addressing_mode ? "LA" : "SPA");
	return 0;
}

static void ras_online_record(struct ras_record *record, void *data, bool __unused)
{
	ras_set_ce_threshold(record);
	ras_enable_irq(record);
}

static void ras_offline_record(struct ras_record *record, void *data, bool __unused)
{
	ras_disable_irq(record);
}

void ras_online_node(struct ras_node *node)
{
	int count = 0;

	if (!node->name)
		return;

	ras_node_foreach_record(ras_proc_record, node, &count,
				node->record_implemented);

	ras_node_dbg(node, "%d errors found before enabled\n", count);

	ras_config_irq(node);

	arm64_ras_storm_init(node);

	ras_node_foreach_record(ras_online_record, node, NULL,
				node->record_implemented);
}

static void ras_offline_node(struct ras_node *node)
{
	if (!node->name)
		return;

	arm64_ras_storm_reset_node(node);

	ras_node_foreach_record(ras_offline_record, node, NULL,
				node->record_implemented);
}

static void ras_online_oncore_dev(void *data)
{
	int fhi_irq, eri_irq;
	struct ras_node *node = this_cpu_ptr(data);

	ras_online_node(node);

	fhi_irq = node->irq[ACPI_AEST_NODE_FAULT_HANDLING];
	if (fhi_irq > 0)
		enable_percpu_irq(fhi_irq, IRQ_TYPE_NONE);
	eri_irq = node->irq[ACPI_AEST_NODE_ERROR_RECOVERY];
	if (eri_irq > 0)
		enable_percpu_irq(eri_irq, IRQ_TYPE_NONE);
}

static void ras_offline_oncore_dev(void *data)
{
	int fhi_irq, eri_irq;
	struct ras_node *node = this_cpu_ptr(data);

	ras_offline_node(node);

	fhi_irq = node->irq[ACPI_AEST_NODE_FAULT_HANDLING];
	if (fhi_irq > 0)
		disable_percpu_irq(fhi_irq);
	eri_irq = node->irq[ACPI_AEST_NODE_ERROR_RECOVERY];
	if (eri_irq > 0)
		disable_percpu_irq(eri_irq);
}

static int ras_starting_cpu(unsigned int cpu)
{
	pr_debug("CPU%d starting\n", cpu);
	ras_online_oncore_dev(&percpu_ras_node);

	return 0;
}

static int ras_dying_cpu(unsigned int cpu)
{
	pr_debug("CPU%d dying\n", cpu);
	ras_offline_oncore_dev(&percpu_ras_node);

	return 0;
}

static void arm64_ras_remove(struct platform_device *pdev)
{
	struct ras_node *node = platform_get_drvdata(pdev);
	int i;

	platform_set_drvdata(pdev, NULL);

	if (node->type != ACPI_AEST_PROCESSOR_ERROR_NODE)
		return;

	cpuhp_remove_state(CPUHP_AP_ARM_RAS_STARTING);
	on_each_cpu(ras_offline_oncore_dev, node->oncore_node, 1);

	for (i = 0; i < AEST_MAX_INTERRUPT_PER_NODE; i++) {
		if (node->irq[i])
			free_percpu_irq(node->irq[i], node->oncore_node);
	}
}

static char *alloc_ras_node_name(struct ras_node *node)
{
	char *name;
	struct acpi_aest_processor *processor = NULL;

	switch (node->type) {
	case ACPI_AEST_PROCESSOR_ERROR_NODE:
		processor = (struct acpi_aest_processor *)node->specific_data;

		/*
		 * Shared/global processor nodes (e.g. cluster L3 cache, DSU)
		 * have processor_id=0 and use smp_processor_id() at error-log
		 * time — using processor_id in the name would produce the same
		 * "processor.0" string for every shared node and every CPU0
		 * per-PE node, making logs ambiguous.
		 *
		 * For shared/global nodes, build the name from the resource
		 * type and the device id so each node gets a unique, meaningful
		 * name (e.g. "processor.cache.1", "processor.tlb.2").
		 *
		 * For per-PE nodes, keep the original "processor.<mpidr>" form.
		 */
		if (processor->flags &
		    (ACPI_AEST_PROC_FLAG_SHARED | ACPI_AEST_PROC_FLAG_GLOBAL)) {
			static const char *const res_name[] = {
				[ACPI_AEST_CACHE_RESOURCE]   = "cache",
				[ACPI_AEST_TLB_RESOURCE]     = "tlb",
				[ACPI_AEST_GENERIC_RESOURCE] = "generic",
			};
			u8 rtype = processor->resource_type;
			const char *rstr = (rtype < ARRAY_SIZE(res_name) &&
				res_name[rtype]) ? res_name[rtype] : "unknown";

			name = devm_kasprintf(node->dev, GFP_KERNEL,
					      "%s.%s.%x",
					      ras_node_name[node->type],
					      rstr,
					      *(u32 *)(processor + 1));
		} else {
			name = devm_kasprintf(node->dev, GFP_KERNEL,
					      "%s.%d",
					      ras_node_name[node->type],
					      processor->processor_id);
		}
		break;
	case ACPI_AEST_MEMORY_ERROR_NODE:
	case ACPI_AEST_SMMU_ERROR_NODE:
	case ACPI_AEST_VENDOR_ERROR_NODE:
	case ACPI_AEST_GIC_ERROR_NODE:
	case ACPI_AEST_PCIE_ERROR_NODE:
	case ACPI_AEST_PROXY_ERROR_NODE:
		name = devm_kasprintf(node->dev, GFP_KERNEL, "%s.%llx",
				      ras_node_name[node->type], node->addr);
		break;
	default:
		dev_warn(node->dev, "unknown AEST node type %u\n", node->type);
		return NULL;
	}

	return name;
}

static int ras_node_set_errgsr(struct ras_node *node, phys_addr_t base)
{
	phys_addr_t errgsr_base;
	int ret;

	if (!(node->flags & AEST_XFACE_FLAG_ERROR_GROUP)) {
		node->errgsr = node->base + node->group->errgsr_offset;
		return 0;
	}

	ret = device_property_read_u64(node->dev, "arm,error-group-base",
				       &errgsr_base);
	if (ret || !errgsr_base)
		return -EINVAL;

	node->errgsr = errgsr_base - base + node->base;
	return 0;
}

static int ras_node_set_inj_base(struct ras_node *node, phys_addr_t base)
{
	phys_addr_t inj_base = 0;
	int ret = 0;

	if (!(node->flags & AEST_XFACE_FLAG_FAULT_INJECT))
		return 0;

	ret = device_property_read_u64(node->dev, "arm,fault-inject-base",
				       &inj_base);
	if (ret || !inj_base)
		return -EINVAL;

	node->inj = inj_base - base + node->base;
	return 0;
}

static int ras_node_set_irq_base(struct ras_node *node, phys_addr_t base)
{
	phys_addr_t irq_base;
	int ret;

	if (!(node->flags & AEST_XFACE_FLAG_INT_CONFIG))
		return 0;

	ret = device_property_read_u64(node->dev, "arm,interrupt-config-base",
				       &irq_base);
	if (ret || !irq_base)
		return 0;

	node->irq_config = irq_base - base + node->base;
	return 0;
}

static struct ras_node *ras_init_node(struct platform_device *pdev)
{
	int i, ret = 0;
	struct device *dev = &pdev->dev;
	struct resource *mem;
	struct ras_node *node;

	node = devm_kzalloc(&pdev->dev, sizeof(*node), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);

	node->dev = &pdev->dev;

	ret = ret ?: device_property_read_u8(dev, "arm,node-type", &node->type);
	ret = ret ?: device_property_read_u8(dev, "arm,interface-type", &node->access_type);
	ret = ret ?: device_property_read_u8(dev, "arm,group-format", &node->group_format);
	ret = ret ?: device_property_read_u32(dev, "arm,interface-flags", &node->flags);
	ret = ret ?: device_property_read_u32(dev, "arm,error-records-count", &node->record_count);
	ret = ret ?: device_property_read_u32(dev, "arm,error-records-index", &node->record_index);
	if (ret)
		return ERR_PTR(ret);
	node->group = &ras_group_config[node->group_format];

	node->record_implemented = devm_bitmap_zalloc(dev,
					node->group->errgsr_num * BITS_PER_TYPE(u64),
					GFP_KERNEL);
	if (!node->record_implemented)
		return ERR_PTR(-ENOMEM);
	node->status_reporting = devm_bitmap_zalloc(dev,
					node->group->errgsr_num * BITS_PER_TYPE(u64),
					GFP_KERNEL);
	if (!node->status_reporting)
		return ERR_PTR(-ENOMEM);
	node->addressing_mode = devm_bitmap_zalloc(dev,
					node->group->errgsr_num * BITS_PER_TYPE(u64),
					GFP_KERNEL);
	if (!node->addressing_mode)
		return ERR_PTR(-ENOMEM);

	ret = device_property_read_u64_array(dev, "arm,record-implemented",
					     (u64 *)node->record_implemented,
					     node->group->errgsr_num);
	ret = ret ?: device_property_read_u64_array(dev, "arm,status-reporting",
						    (u64 *)node->status_reporting,
						    node->group->errgsr_num);
	ret = ret ?: device_property_read_u64_array(dev, "arm,addressing-mode",
						    (u64 *)node->addressing_mode,
						    node->group->errgsr_num);
	if (ret)
		return ERR_PTR(ret);

	node->specific_data_size = device_property_count_u8(dev, "arm,node-specific-data");
	if (node->specific_data_size > 0) {
		node->specific_data = devm_kzalloc(dev, node->specific_data_size, GFP_KERNEL);
		if (!node->specific_data)
			return ERR_PTR(-ENOMEM);
		ret = device_property_read_u8_array(dev, "arm,node-specific-data",
						    node->specific_data,
						    node->specific_data_size);
		if (ret)
			return ERR_PTR(ret);
	}

	mem = platform_get_resource(to_platform_device(dev), IORESOURCE_MEM, 0);
	if (mem) {
		node->addr = mem->start;
		node->base = devm_ioremap(node->dev, mem->start, resource_size(mem));
		if (!node->base)
			return ERR_PTR(-ENOMEM);

		ret = ras_node_set_errgsr(node, mem->start);
		if (ret)
			return ERR_PTR(ret);
		ret = ras_node_set_inj_base(node, mem->start);
		if (ret)
			return ERR_PTR(ret);
		ret = ras_node_set_irq_base(node, mem->start);
		if (ret)
			return ERR_PTR(ret);
	} else if (node->access_type == ACPI_AEST_NODE_MEMORY_MAPPED) {
		return ERR_PTR(-EINVAL);
	}

	node->errgsr_mapping = default_errgsr_mapping;
	node->name = alloc_ras_node_name(node);
	if (!node->name)
		return ERR_PTR(-ENOMEM);

	node->version = get_ras_node_ver(node);
	node->records = devm_kcalloc(node->dev, node->record_count,
				     sizeof(struct ras_record), GFP_KERNEL);
	if (!node->records)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < node->record_count; i++) {
		ret = ras_init_record(&node->records[i],
				      i + node->record_index, node);
		if (ret)
			return ERR_PTR(ret);
	}
	ras_node_dbg(node, "base: %llx, access_type: %s, %s inject\n",
		     node->addr, node->access_type ? "MMIO" : "Register",
		     node->flags & AEST_XFACE_FLAG_FAULT_INJECT ? "with" : "without");
	return node;
}


static int __setup_ppi(struct ras_node *node)
{
	int cpu;
	struct ras_node *oncore_node;
	size_t size;

	node->oncore_node = &percpu_ras_node;
	for_each_possible_cpu(cpu) {
		oncore_node = per_cpu_ptr(&percpu_ras_node, cpu);
		memcpy(oncore_node, node, sizeof(struct ras_node));

		oncore_node->records = devm_kcalloc(
			node->dev, oncore_node->record_count,
			sizeof(struct ras_record), GFP_KERNEL);
		if (!oncore_node->records)
			return -ENOMEM;

		size = oncore_node->record_count *
			sizeof(struct ras_record);
		memcpy(oncore_node->records, node->records, size);

		ras_node_dbg(node, "Init node on CPU%d.\n", cpu);
	}

	return 0;
}

static int ras_setup_irq(struct platform_device *pdev, struct ras_node *node)
{
	int fhi_irq, eri_irq;

	fhi_irq = platform_get_irq_byname_optional(pdev, AEST_FHI_NAME);
	if (fhi_irq > 0)
		node->irq[ACPI_AEST_NODE_FAULT_HANDLING] = fhi_irq;

	eri_irq = platform_get_irq_byname_optional(pdev, AEST_ERI_NAME);
	if (eri_irq > 0)
		node->irq[ACPI_AEST_NODE_ERROR_RECOVERY] = eri_irq;

	/* Allocate and initialise the percpu device pointer for PPI */
	if (irq_is_percpu(fhi_irq) || irq_is_percpu(eri_irq))
		return __setup_ppi(node);

	return 0;
}

static struct ras_vendor_match vendor_match[] = {
	{ "ARMHC701", &ras_cmn700_probe },
	{  },
};

static int
ras_vendor_probe(struct platform_device *pdev)
{
	int i;
	struct acpi_aest_vendor_v2 vendor;

	device_property_read_u8_array(&pdev->dev, "arm,node-specific-data",
				      (u8 *)&vendor, sizeof(vendor));

	dev_dbg(&pdev->dev, "Try to probe vendor node %s\n", vendor.acpi_hid);
	for (i = 0; i < ARRAY_SIZE(vendor_match); i++) {
		if (!strncmp(vendor_match[i].hid, vendor.acpi_hid, 8))
			return vendor_match[i].probe(pdev);
	}

	return -ENODEV;
}

static int arm64_ras_probe(struct platform_device *pdev)
{
	int ret;
	struct ras_node *node;
	u8 type;

	ret = device_property_read_u8(&pdev->dev, "arm,node-type", &type);
	if (ret)
		return ret;

	if (type == ACPI_AEST_VENDOR_ERROR_NODE)
		return ras_vendor_probe(pdev);

	node = ras_init_node(pdev);
	if (IS_ERR(node))
		return PTR_ERR(node);

	ret = dev_set_name(&pdev->dev, "%s%d", ras_node_name[node->type],
			   pdev->id);
	if (ret)
		return ret;

	ret = ras_setup_irq(pdev, node);
	if (ret)
		return ret;

	ret = ras_register_irq(node);
	if (ret) {
		ras_node_err(node, "register irq failed\n");
		return ret;
	}

	ret = arm64_ras_storm_init(node);
	if (ret) {
		ras_node_err(node, "init storm mitigation failed\n");
		return ret;
	}

	if (ras_node_is_oncore(node))
		ret = cpuhp_setup_state(CPUHP_AP_ARM_RAS_STARTING,
					"drivers/ras/arm64/ras:starting",
					ras_starting_cpu, ras_dying_cpu);
	else
		ras_online_node(node);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, node);

	ras_node_init_debugfs(node);

	return 0;
}

static struct platform_driver arm64_ras_driver = {
	.driver	= {
		.name	= "arm64_ras",
	},
	.probe		= arm64_ras_probe,
	.remove		= arm64_ras_remove,
};

static int __init arm64_ras_init(void)
{
#ifdef CONFIG_DEBUG_FS
	arm64_ras_debugfs = debugfs_create_dir("arm64", ras_debugfs_dir);
#endif
	return platform_driver_register(&arm64_ras_driver);
}
module_init(arm64_ras_init);

static void __exit arm64_ras_exit(void)
{
#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(arm64_ras_debugfs);
#endif
	platform_driver_unregister(&arm64_ras_driver);
}
module_exit(arm64_ras_exit);

MODULE_DESCRIPTION("ARM RAS Driver");
MODULE_AUTHOR("Ruidong Tian <tianruidong@linux.alibaba.com>");
MODULE_LICENSE("GPL");
