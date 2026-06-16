// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2025, Alibaba Group.
 */

#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/panic.h>
#include <linux/platform_device.h>
#include <linux/xarray.h>
#include <linux/genalloc.h>
#include <linux/ras.h>
#include <linux/cpu_pm.h>

#include <ras/ras_event.h>

#include "aest.h"

DEFINE_PER_CPU(struct aest_device, percpu_adev);

#undef pr_fmt
#define pr_fmt(fmt) "AEST: " fmt

static bool aest_panic_on_ue;
module_param(aest_panic_on_ue, bool, 0644);
MODULE_PARM_DESC(aest_panic_on_ue,
		 "Panic on unrecoverable error: 0=off 1=on (default: 1)");

static inline void write_errselr_el1(u64 val)
{
	asm volatile("msr s3_0_c5_c3_1, %0" : : "r" (val));
	isb();
}

static inline void set_errxctlr_el1(void)
{
	u64 val = 0x10f;

	asm volatile("msr s3_0_c5_c4_1, %0" : : "r" (val));
}

static inline void set_errxmisc_overflow(void)
{
	u64 val = 0x7F7F00000000ULL;

	asm volatile("msr s3_0_c5_c5_0, %0" : : "r" (val));
	isb();
}

static void initialize_registers(void *info)
{
	set_errxctlr_el1();
	set_errxmisc_overflow();
}

static void init_regs_on_cpu(bool all_cpus)
{
	write_errselr_el1(0);
	if (all_cpus)
		on_each_cpu(initialize_registers, NULL, 1);
	else
		initialize_registers(NULL);

	write_errselr_el1(1);
	initialize_registers(NULL);
}

#ifdef CONFIG_CPU_PM
static inline u64 read_errxstatus_el1(void)
{
	u64 val;

	asm volatile("mrs %0, s3_0_c5_c4_2" : "=r" (val));
	return val;
}

static inline void clear_errxstatus_el1(u64 val)
{
	asm volatile("msr s3_0_c5_c4_2, %0" : : "r" (val));
}

static void aest_check_and_clear_erxstatus(void)
{
	u64 status = read_errxstatus_el1();

	if (status & ERR_STATUS_V)
		clear_errxstatus_el1(status);
}

static int aest_cpu_pm_notify(struct notifier_block *self,
			      unsigned long cmd, void *v)
{
	if (cmd != CPU_PM_EXIT && cmd != CPU_PM_ENTER_FAILED)
		return NOTIFY_OK;

	init_regs_on_cpu(false);

	/* Record 1 is already selected after init_regs_on_cpu(false). */
	aest_check_and_clear_erxstatus();

	write_errselr_el1(0);
	aest_check_and_clear_erxstatus();

	return NOTIFY_OK;
}

static struct notifier_block aest_cpu_pm_nb = {
	.notifier_call = aest_cpu_pm_notify,
};

static void aest_cpu_pm_init(void)
{
	cpu_pm_register_notifier(&aest_cpu_pm_nb);
}

static void aest_cpu_pm_exit(void)
{
	cpu_pm_unregister_notifier(&aest_cpu_pm_nb);
}
#else
static inline void aest_cpu_pm_init(void) { }
static inline void aest_cpu_pm_exit(void) { }
#endif /* CONFIG_CPU_PM */

#ifdef CONFIG_DEBUG_FS
struct dentry *aest_debugfs;
#endif
/*
 * This memory pool is only to be used to save AEST node in AEST irq context.
 * There can be 500 AEST node at most.
 */
#define AEST_NODE_ALLOCED_MAX 500

#define AEST_LOG_PREFIX_BUFFER 64

BLOCKING_NOTIFIER_HEAD(aest_decoder_chain);

static void aest_print(struct aest_event *event)
{
	static atomic_t seqno = { 0 };
	unsigned int curr_seqno;
	char pfx_seq[AEST_LOG_PREFIX_BUFFER];
	int index;
	struct ras_ext_regs *regs;

	curr_seqno = atomic_inc_return(&seqno);
	snprintf(pfx_seq, sizeof(pfx_seq), "{%u}" HW_ERR, curr_seqno);
	pr_info("%sHardware error from AEST %s\n", pfx_seq, event->node_name);

	switch (event->type) {
	case ACPI_AEST_PROCESSOR_ERROR_NODE:
		/*
		 * For shared/global nodes (e.g. cluster L3 cache, DSU),
		 * id0 is the CPU that handled the interrupt — not the error
		 * source itself.  The node_name already identifies the resource
		 * (e.g. "processor.cache.1").  Print a distinct message so the
		 * log is not confused with a per-PE CPU error.
		 */
		if (event->proc_flags &
		    (ACPI_AEST_PROC_FLAG_SHARED | ACPI_AEST_PROC_FLAG_GLOBAL))
			pr_err("%s Error from shared processor resource (interrupt handled on CPU%d)\n",
			       pfx_seq, event->id0);
		else
			pr_err("%s Error from CPU%d\n", pfx_seq, event->id0);
		break;
	case ACPI_AEST_MEMORY_ERROR_NODE:
		pr_err("%s Error from memory at SRAT proximity domain %#x\n",
		       pfx_seq, event->id0);
		break;
	case ACPI_AEST_SMMU_ERROR_NODE:
		pr_err("%s Error from SMMU IORT node %#x subcomponent %#x\n",
		       pfx_seq, event->id0, event->id1);
		break;
	case ACPI_AEST_VENDOR_ERROR_NODE:
		pr_err("%s Error from vendor hid %8.8s uid %#x\n", pfx_seq,
		       event->hid, event->id1);
		break;
	case ACPI_AEST_GIC_ERROR_NODE:
		pr_err("%s Error from GIC type %#x instance %#x\n", pfx_seq,
		       event->id0, event->id1);
		break;
	default:
		pr_err("%s Unknown AEST node type\n", pfx_seq);
		return;
	}

	index = event->index;
	regs = &event->regs;

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

	trace_arm_ras_ext_event(event->type, event->id0, event->id1,
				event->index, event->hid, &event->regs,
				event->vendor_data, event->vendor_data_size);
}

static void aest_handle_memory_failure(u64 addr)
{
	unsigned long pfn;

	pfn = PHYS_PFN(addr);

	if (!pfn_valid(pfn)) {
		pr_warn(HW_ERR "Invalid physical address: %#llx\n", addr);
		return;
	}

#ifdef CONFIG_MEMORY_FAILURE
	memory_failure(pfn, 0);
#endif
}

static void init_aest_event(struct aest_event *event,
			    struct aest_record *record,
			    struct ras_ext_regs *regs)
{
	struct aest_node *node = record->node;
	struct acpi_aest_node *info = node->info;

	event->type = node->type;
	event->node_name = node->name;
	switch (node->type) {
	case ACPI_AEST_PROCESSOR_ERROR_NODE:
		if (info->processor->flags &
		    (ACPI_AEST_PROC_FLAG_SHARED | ACPI_AEST_PROC_FLAG_GLOBAL))
			event->id0 = smp_processor_id();
		else
			event->id0 = get_cpu_for_acpi_id(
				info->processor->processor_id);

		event->id1 = info->processor->resource_type;
		event->proc_flags = info->processor->flags;
		break;
	case ACPI_AEST_MEMORY_ERROR_NODE:
		event->id0 = info->memory->srat_proximity_domain;
		break;
	case ACPI_AEST_SMMU_ERROR_NODE:
		event->id0 = info->smmu->iort_node_reference;
		event->id1 = info->smmu->subcomponent_reference;
		break;
	case ACPI_AEST_VENDOR_ERROR_NODE:
		event->id0 = 0;
		event->id1 = info->vendor->acpi_uid;
		event->hid = info->vendor->acpi_hid;
		break;
	case ACPI_AEST_GIC_ERROR_NODE:
		event->id0 = info->gic->interface_type;
		event->id1 = info->gic->instance_id;
		break;
	default:
		event->id0 = 0;
		event->id1 = 0;
	}

	memcpy(&event->regs, regs, sizeof(*regs));
	event->index = record->index;
	event->addressing_mode = record->addressing_mode;
	event->vendor_data_size = record->vendor_data_size;
	event->vendor_data = record->vendor_data;
}

static int aest_node_gen_pool_add(struct aest_device *adev,
				  struct aest_record *record,
				  struct ras_ext_regs *regs)
{
	struct aest_event *event;

	if (!adev->pool)
		return -EINVAL;

	event = (void *)gen_pool_alloc(adev->pool, sizeof(*event));
	if (!event)
		return -ENOMEM;

	memset(event, 0, sizeof(*event));
	init_aest_event(event, record, regs);
	llist_add(&event->llnode, &adev->event_list);

	if (regs->err_status & ERR_STATUS_CE)
		record->count.ce++;
	if (regs->err_status & ERR_STATUS_DE)
		record->count.de++;
	if (regs->err_status & ERR_STATUS_UE) {
		switch (regs->err_status & ERR_STATUS_UET) {
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

	return 0;
}

static void aest_log(struct aest_record *record, struct ras_ext_regs *regs)
{
	struct aest_device *adev = record->node->adev;

	if (!aest_node_gen_pool_add(adev, record, regs))
		schedule_work(&adev->aest_work);
}

void aest_register_decode_chain(struct notifier_block *nb)
{
	blocking_notifier_chain_register(&aest_decoder_chain, nb);
}
EXPORT_SYMBOL_GPL(aest_register_decode_chain);

void aest_unregister_decode_chain(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&aest_decoder_chain, nb);
}
EXPORT_SYMBOL_GPL(aest_unregister_decode_chain);

static void aest_node_pool_process(struct work_struct *work)
{
	struct llist_node *head;
	struct aest_event *event;
	struct aest_device *adev =
		container_of(work, struct aest_device, aest_work);
	u64 status, addr;

	head = llist_del_all(&adev->event_list);
	if (!head)
		return;

	head = llist_reverse_order(head);
	llist_for_each_entry(event, head, llnode) {
		aest_print(event);

		status = event->regs.err_status;
		if (!(event->regs.err_addr & ERR_ADDR_AI) &&
		    (status & (ERR_STATUS_UE | ERR_STATUS_DE))) {
			if (event->addressing_mode == AEST_ADDREESS_SPA)
				addr = event->regs.err_addr & PHYS_MASK;
			aest_handle_memory_failure(addr);
		}

		blocking_notifier_call_chain(&aest_decoder_chain, 0, event);
		gen_pool_free(adev->pool, (unsigned long)event, sizeof(*event));
	}
}

static int aest_node_pool_init(struct aest_device *adev)
{
	unsigned long addr, size;

	size = ilog2(sizeof(struct aest_event));
	adev->pool =
		devm_gen_pool_create(adev->dev, size, -1, dev_name(adev->dev));
	if (!adev->pool)
		return -ENOMEM;

	size = PAGE_ALIGN(size * AEST_NODE_ALLOCED_MAX);
	addr = (unsigned long)devm_kzalloc(adev->dev, size, GFP_KERNEL);
	if (!addr)
		return -ENOMEM;

	return gen_pool_add(adev->pool, addr, size, -1);
}

static void aest_panic(struct aest_record *record, struct ras_ext_regs *regs,
		       char *msg)
{
	struct aest_event event = { 0 };

	init_aest_event(&event, record, regs);

	aest_print(&event);

	panic(msg);
}

void aest_proc_record(struct aest_record *record, void *data, bool fake)
{
	struct ras_ext_regs regs = { 0 };
	int *count = data;
	u64 ue;

	regs.err_status = record_read(record, ERXSTATUS);
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

		if (record->node->info->interface_hdr->flags &
		    AEST_XFACE_FLAG_CLEAR_MISC) {
			record_write(record, ERXMISC0, 0);
			record_write(record, ERXMISC1, 0);
			if (record->node->version >= ID_AA64PFR0_EL1_RAS_V1P1) {
				record_write(record, ERXMISC2, 0);
				record_write(record, ERXMISC3, 0);
			}
			/* ce count is 0 if record do not support ce */
		} else if (record->ce.count > 0)
			record_write(record, ERXMISC0, record->ce.reg_val);
	}

	/* panic if unrecoverable and uncontainable error encountered */
	ue = FIELD_GET(ERR_STATUS_UET, regs.err_status);
	if ((regs.err_status & ERR_STATUS_UE) &&
	    (ue == ERR_STATUS_UET_UC || ue == ERR_STATUS_UET_UEU)) {
		if (fake)
			aest_record_info(
				record,
				"Simulated error! Skip panic due to fault injection\n");
		else if (aest_panic_on_ue)
			aest_panic(record, &regs,
				   "AEST: unrecoverable error encountered");
		else
			aest_record_err(record, "UE detected, panic suppressed\n");
	}

	aest_log(record, &regs);

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

void aest_node_foreach_record(void (*func)(struct aest_record *, void *, bool),
			      struct aest_node *node, void *data,
			      unsigned long *bitmap)
{
	int i;

	for_each_clear_bit(i, bitmap, node->record_count) {
		aest_select_record(node, i);

		func(&node->records[i], data, false);

		aest_sync(node);
	}
}

static int aest_proc(struct aest_node *node)
{
	int count = 0, i, j, size = node->record_count, record_idx;
	u64 err_group = 0;

	aest_node_dbg(node, "Poll bitmap %*pb\n", size,
		      node->record_implemented);
	aest_node_foreach_record(aest_proc_record, node, &count,
				 node->record_implemented);

	if (!node->errgsr)
		return count;

	aest_node_dbg(node, "Report bitmap %*pb\n", size,
		      node->status_reporting);
	for (i = 0; i < BITS_TO_U64(size); i++) {
		err_group = readq_relaxed((void *)node->errgsr + i * 8);
		for_each_set_bit(j, (unsigned long *)&err_group,
				 BITS_PER_LONG) {
			record_idx =
				node->errgsr_mapping(i * BITS_PER_LONG + j);
			aest_node_dbg(node, "errgsr[%d]: bit %d occur error\n",
				      i, record_idx);
			/*
			 * Error group base is only valid in Memory Map node,
			 * so driver do not need to write select register and
			 * sync.
			 */
			if (test_bit(record_idx, node->status_reporting))
				continue;
			aest_proc_record(&node->records[record_idx], &count,
					 false);
		}
	}

	return count;
}

static irqreturn_t aest_irq_func(int irq, void *input)
{
	struct aest_device *adev = input;
	int i;

	for (i = 0; i < adev->node_cnt; i++) {
		if (!adev->nodes[i].record_count)
			continue;
		aest_proc(&adev->nodes[i]);
	}

	return IRQ_HANDLED;
}

static int aest_register_irq(struct aest_device *adev)
{
	int i, irq, ret;
	char *irq_desc;

	irq_desc = devm_kasprintf(adev->dev, GFP_KERNEL, "%s.%s.",
				  dev_driver_string(adev->dev),
				  dev_name(adev->dev));
	if (!irq_desc)
		return -ENOMEM;

	for (i = 0; i < MAX_GSI_PER_NODE; i++) {
		irq = adev->irq[i];

		if (!irq)
			continue;

		if (irq_is_percpu_devid(irq)) {
			ret = request_percpu_irq(irq, aest_irq_func, irq_desc,
						 adev->adev_oncore);
			if (ret)
				goto free;
		} else {
			ret = devm_request_irq(adev->dev, irq, aest_irq_func, 0,
					       irq_desc, adev);
			if (ret)
				return ret;
		}
	}
	return 0;

free:
	for (; i >= 0; i--) {
		irq = adev->irq[i];

		if (irq_is_percpu_devid(irq))
			free_percpu_irq(irq, adev->adev_oncore);
	}

	return ret;
}

static void aest_enable_irq(struct aest_record *record)
{
	u64 err_ctlr;
	struct aest_device *adev = record->node->adev;

	err_ctlr = record_read(record, ERXCTLR);

	if (adev->irq[ACPI_AEST_NODE_FAULT_HANDLING])
		err_ctlr |= (ERR_CTLR_FI | ERR_CTLR_CFI);
	if (adev->irq[ACPI_AEST_NODE_ERROR_RECOVERY])
		err_ctlr |= ERR_CTLR_UI;

	record_write(record, ERXCTLR, err_ctlr);
}

static void aest_config_irq(struct aest_node *node)
{
	int i;
	struct acpi_aest_node_interrupt_v2 *interrupt;

	if (!node->irq_config)
		return;

	for (i = 0; i < node->info->interrupt_count; i++) {
		interrupt = &node->info->interrupt[i];

		if (interrupt->type == ACPI_AEST_NODE_FAULT_HANDLING)
			writeq_relaxed(interrupt->gsiv, node->irq_config);

		if (interrupt->type == ACPI_AEST_NODE_ERROR_RECOVERY)
			writeq_relaxed(interrupt->gsiv, node->irq_config + 8);

		aest_node_dbg(node, "config irq type %d gsiv %d at %llx",
			      interrupt->type, interrupt->gsiv,
			      (u64)node->irq_config);
	}
}

static enum ras_ce_threshold aest_get_ce_threshold(struct aest_record *record)
{
	u64 err_fr, err_fr_cec, err_fr_rp = -1;

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
	else
		return UNKNOWN;
}

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

static void aest_set_ce_threshold(struct aest_record *record)
{
	u64 err_misc0;
	struct ce_threshold *ce = &record->ce;
	const struct ce_threshold_info *info;

	record->threshold_type = aest_get_ce_threshold(record);

	switch (record->threshold_type) {
	case RAS_CE_THRESHOLD_0B:
		aest_record_dbg(record, "do not support CE threshold!\n");
		return;
	case RAS_CE_THRESHOLD_8B:
		aest_record_dbg(record, "support 8 bit CE threshold!\n");
		break;
	case RAS_CE_THRESHOLD_16B:
		aest_record_dbg(record, "support 16 bit CE threshold!\n");
		break;
	case RAS_CE_THRESHOLD_32B:
		aest_record_dbg(record, "not support 32 bit CE threshold!\n");
		break;
	default:
		aest_record_dbg(record, "Unknown misc0 ce threshold!\n");
	}

	err_misc0 = record_read(record, ERXMISC0);
	info = &ce_info[record->threshold_type];
	ce->info = info;

	// Default CE threshold is 1.
	ce->count = info->max_count;
	ce->threshold = DEFAULT_CE_THRESHOLD;
	ce->reg_val = err_misc0 | info->mask;

	record_write(record, ERXMISC0, ce->reg_val);
	aest_record_dbg(record, "CE threshold is %llx, controlled by Kernel",
			ce->threshold);
}

static int get_aest_node_ver(struct aest_node *node)
{
	u64 reg;
	void *devarch_base;

	if (node->type == ACPI_AEST_GIC_ERROR_NODE) {
		devarch_base = ioremap(node->info->interface_hdr->address +
					       GIC_ERRDEVARCH,
				       PAGE_SIZE);
		if (!devarch_base)
			return 0;

		reg = readl_relaxed(devarch_base);
		iounmap(devarch_base);

		return FIELD_GET(ERRDEVARCH_REV, reg);
	}

	return FIELD_GET(ID_AA64PFR0_EL1_RAS_MASK, read_cpuid(ID_AA64PFR0_EL1));
}

static int aest_init_record(struct aest_record *record, int i,
			    struct aest_node *node)
{
	struct device *dev = node->adev->dev;

	record->name = devm_kasprintf(dev, GFP_KERNEL, "record%d", i);
	if (!record->name)
		return -ENOMEM;

	if (node->base)
		record->regs_base =
			node->base + sizeof(struct ras_ext_regs) * i;

	record->access = &aest_access[node->info->interface_hdr->type];
	record->addressing_mode = test_bit(i, node->info->addressing_mode);
	record->index = i;
	record->node = node;

	aest_record_dbg(record, "base: %p, index: %d, address mode: %x\n",
			record->regs_base, record->index,
			record->addressing_mode);
	return 0;
}

static void aest_online_record(struct aest_record *record, void *data,
			       bool __unused)
{
	if (record_read(record, ERXFR) & ERR_FR_CE)
		aest_set_ce_threshold(record);

	aest_enable_irq(record);
}

static void aest_online_oncore_node(struct aest_node *node)
{
	int count;

	count = aest_proc(node);
	aest_node_dbg(node, "Find %d error on CPU%d before AEST probe\n", count,
		      smp_processor_id());

	aest_node_foreach_record(aest_online_record, node, NULL,
				 node->record_implemented);

	aest_node_foreach_record(aest_online_record, node, NULL,
				 node->status_reporting);
}

static void aest_online_oncore_dev(void *data)
{
	int fhi_irq, eri_irq, i;
	struct aest_device *adev = this_cpu_ptr(data);

	for (i = 0; i < adev->node_cnt; i++)
		aest_online_oncore_node(&adev->nodes[i]);

	fhi_irq = adev->irq[ACPI_AEST_NODE_FAULT_HANDLING];
	if (fhi_irq > 0)
		enable_percpu_irq(fhi_irq, IRQ_TYPE_NONE);
	eri_irq = adev->irq[ACPI_AEST_NODE_ERROR_RECOVERY];
	if (eri_irq > 0)
		enable_percpu_irq(eri_irq, IRQ_TYPE_NONE);
}

static void aest_offline_oncore_dev(void *data)
{
	int fhi_irq, eri_irq;
	struct aest_device *adev = this_cpu_ptr(data);

	fhi_irq = adev->irq[ACPI_AEST_NODE_FAULT_HANDLING];
	if (fhi_irq > 0)
		disable_percpu_irq(fhi_irq);
	eri_irq = adev->irq[ACPI_AEST_NODE_ERROR_RECOVERY];
	if (eri_irq > 0)
		disable_percpu_irq(eri_irq);
}

static void aest_online_dev(struct aest_device *adev)
{
	int count, i;
	struct aest_node *node;

	for (i = 0; i < adev->node_cnt; i++) {
		node = &adev->nodes[i];

		if (!node->name)
			continue;

		count = aest_proc(node);
		aest_node_dbg(node, "Find %d error before AEST probe\n", count);

		aest_config_irq(node);

		aest_node_foreach_record(aest_online_record, node, NULL,
					 node->record_implemented);
		aest_node_foreach_record(aest_online_record, node, NULL,
					 node->status_reporting);
	}
}

static int aest_starting_cpu(unsigned int cpu)
{
	pr_debug("CPU%d starting\n", cpu);
	aest_online_oncore_dev(&percpu_adev);

	return 0;
}

static int aest_dying_cpu(unsigned int cpu)
{
	pr_debug("CPU%d dying\n", cpu);
	aest_offline_oncore_dev(&percpu_adev);

	return 0;
}

static void aest_device_remove(struct platform_device *pdev)
{
	struct aest_device *adev = platform_get_drvdata(pdev);
	int i;

	platform_set_drvdata(pdev, NULL);

	if (adev->type != ACPI_AEST_PROCESSOR_ERROR_NODE)
		return;

	on_each_cpu(aest_offline_oncore_dev, adev->adev_oncore, 1);

	for (i = 0; i < MAX_GSI_PER_NODE; i++) {
		if (adev->irq[i])
			free_percpu_irq(adev->irq[i], adev->adev_oncore);
	}
}

static char *alloc_aest_node_name(struct aest_node *node)
{
	char *name;

	switch (node->type) {
	case ACPI_AEST_PROCESSOR_ERROR_NODE:
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
		if (node->info->processor->flags &
		    (ACPI_AEST_PROC_FLAG_SHARED | ACPI_AEST_PROC_FLAG_GLOBAL)) {
			static const char *const res_name[] = {
				[ACPI_AEST_CACHE_RESOURCE]   = "cache",
				[ACPI_AEST_TLB_RESOURCE]     = "tlb",
				[ACPI_AEST_GENERIC_RESOURCE] = "generic",
			};
			u8 rtype = node->info->processor->resource_type;
			const char *rstr = (rtype < ARRAY_SIZE(res_name) &&
				res_name[rtype]) ? res_name[rtype] : "unknown";

			name = devm_kasprintf(node->adev->dev, GFP_KERNEL,
					      "%s.%s.%d",
					      aest_node_name[node->type],
					      rstr,
					      node->adev->id);
		} else {
			name = devm_kasprintf(node->adev->dev, GFP_KERNEL,
					      "%s.%d",
					      aest_node_name[node->type],
					      node->info->processor->processor_id);
		}
		break;
	case ACPI_AEST_MEMORY_ERROR_NODE:
	case ACPI_AEST_SMMU_ERROR_NODE:
	case ACPI_AEST_VENDOR_ERROR_NODE:
	case ACPI_AEST_GIC_ERROR_NODE:
	case ACPI_AEST_PCIE_ERROR_NODE:
	case ACPI_AEST_PROXY_ERROR_NODE:
		name = devm_kasprintf(node->adev->dev, GFP_KERNEL, "%s.%llx",
				      aest_node_name[node->type],
				      node->info->interface_hdr->address);
		break;
	default:
		name = devm_kasprintf(node->adev->dev, GFP_KERNEL, "Unknown");
	}

	return name;
}

static int aest_node_set_errgsr(struct aest_device *adev,
				struct aest_node *node)
{
	struct acpi_aest_node *anode = node->info;
	u64 errgsr_base = anode->common->error_group_register_base;

	if (anode->interface_hdr->type != ACPI_AEST_NODE_MEMORY_MAPPED)
		return 0;

	if (!node->base)
		return 0;

	if (!(anode->interface_hdr->flags & AEST_XFACE_FLAG_ERROR_GROUP)) {
		node->errgsr = node->base + node->group->errgsr_offset;
		return 0;
	}

	if (!errgsr_base)
		return -EINVAL;

	node->errgsr = devm_ioremap(adev->dev, errgsr_base, PAGE_SIZE);
	if (!node->errgsr)
		return -ENOMEM;

	return 0;
}

static int aest_init_node(struct aest_device *adev, struct aest_node *node,
			  struct acpi_aest_node *anode)
{
	int i, ret;
	u64 address, flags;

	node->adev = adev;
	node->info = anode;
	node->type = anode->type;
	node->version = get_aest_node_ver(node);
	node->errgsr_mapping = default_errgsr_mapping;
	node->name = alloc_aest_node_name(node);
	if (!node->name)
		return -ENOMEM;
	node->record_implemented = anode->record_implemented;
	node->status_reporting = anode->status_reporting;
	node->group = &aest_group_config[anode->interface_hdr->group_format];

	address = anode->interface_hdr->address;
	if (address) {
		node->base =
			devm_ioremap(adev->dev, address, node->group->size);
		if (!node->base)
			return -ENOMEM;
	}

	flags = anode->interface_hdr->flags;
	address = node->info->common->fault_inject_register_base;
	if ((flags & AEST_XFACE_FLAG_FAULT_INJECT) && address) {
		if (address - anode->interface_hdr->address < node->group->size)
			node->inj = node->base +
				    (address - anode->interface_hdr->address);
		else {
			node->inj = devm_ioremap(adev->dev, address, PAGE_SIZE);
			if (!node->inj)
				return -ENOMEM;
		}
	}

	address = node->info->common->interrupt_config_register_base;
	if ((flags & AEST_XFACE_FLAG_INT_CONFIG) && address) {
		if (address - anode->interface_hdr->address < node->group->size)
			node->irq_config =
				node->base +
				(address - anode->interface_hdr->address);
		else {
			node->irq_config =
				devm_ioremap(adev->dev, address, PAGE_SIZE);
			if (!node->irq_config)
				return -ENOMEM;
		}
	}

	ret = aest_node_set_errgsr(adev, node);
	if (ret)
		return ret;

	node->record_count = anode->interface_hdr->error_record_count;
	node->records = devm_kcalloc(adev->dev, node->record_count,
				     sizeof(struct aest_record), GFP_KERNEL);
	if (!node->records)
		return -ENOMEM;

	node->errgsr_num = DIV_ROUND_UP(node->record_count, BITS_PER_LONG);
	for (i = 0; i < node->record_count; i++) {
		ret = aest_init_record(&node->records[i], i, node);
		if (ret)
			return ret;
	}
	aest_node_dbg(node, "%d records, base: %llx, errgsr: %llx\n",
		      node->record_count, (u64)node->base, (u64)node->errgsr);
	return 0;
}

static int aest_init_nodes(struct aest_device *adev, struct aest_hnode *ahnode)
{
	struct acpi_aest_node *anode;
	struct aest_node *node;
	int ret, i = 0;

	adev->node_cnt = ahnode->count;
	adev->nodes = devm_kcalloc(adev->dev, adev->node_cnt,
				   sizeof(struct aest_node), GFP_KERNEL);
	if (!adev->nodes)
		return -ENOMEM;

	list_for_each_entry(anode, &ahnode->list, list) {
		adev->type = anode->type;

		node = &adev->nodes[i++];
		ret = aest_init_node(adev, node, anode);
		if (ret)
			return ret;
	}

	return 0;
}

static int __setup_ppi(struct aest_device *adev)
{
	int cpu, i;
	struct aest_device *oncore_adev;
	struct aest_node *oncore_node;
	size_t size;

	adev->adev_oncore = &percpu_adev;
	for_each_possible_cpu(cpu) {
		oncore_adev = per_cpu_ptr(&percpu_adev, cpu);
		memcpy(oncore_adev, adev, sizeof(struct aest_device));

		oncore_adev->nodes =
			devm_kcalloc(adev->dev, oncore_adev->node_cnt,
				     sizeof(struct aest_node), GFP_KERNEL);
		if (!oncore_adev->nodes)
			return -ENOMEM;

		size = adev->node_cnt * sizeof(struct aest_node);
		memcpy(oncore_adev->nodes, adev->nodes, size);
		for (i = 0; i < oncore_adev->node_cnt; i++) {
			oncore_node = &oncore_adev->nodes[i];
			oncore_node->records = devm_kcalloc(
				adev->dev, oncore_node->record_count,
				sizeof(struct aest_record), GFP_KERNEL);
			if (!oncore_node->records)
				return -ENOMEM;

			size = oncore_node->record_count *
			       sizeof(struct aest_record);
			memcpy(oncore_node->records, adev->nodes[i].records,
			       size);
		}

		aest_dev_dbg(adev, "Init device on CPU%d.\n", cpu);
	}

	return 0;
}

static int aest_setup_irq(struct platform_device *pdev,
			  struct aest_device *adev)
{
	int fhi_irq, eri_irq;

	fhi_irq = platform_get_irq_byname_optional(pdev, AEST_FHI_NAME);
	if (fhi_irq > 0)
		adev->irq[0] = fhi_irq;

	eri_irq = platform_get_irq_byname_optional(pdev, AEST_ERI_NAME);
	if (eri_irq > 0)
		adev->irq[1] = eri_irq;

	/* Allocate and initialise the percpu device pointer for PPI */
	if (irq_is_percpu(fhi_irq) || irq_is_percpu(eri_irq))
		return __setup_ppi(adev);

	return 0;
}

static struct aest_vendor_match vendor_match[] = {
	{ "ARMHC700", &aest_cmn700_probe },
	{},
};

static int aest_vendor_probe(struct aest_device *adev,
			     struct aest_hnode *ahnode)
{
	int i;
	struct acpi_aest_node *anode;

	anode = list_first_entry(&ahnode->list, struct acpi_aest_node, list);
	if (!anode)
		return -ENODEV;

	aest_dev_dbg(adev, "Try to probe vendor node %s\n",
		     anode->vendor->acpi_hid);
	for (i = 0; i < ARRAY_SIZE(vendor_match); i++) {
		if (!strncmp(vendor_match[i].hid, anode->vendor->acpi_hid, 8))
			return vendor_match[i].probe(adev, ahnode);
	}

	return 0;
}

static int aest_device_probe(struct platform_device *pdev)
{
	int ret;
	struct aest_device *adev;
	struct aest_hnode *ahnode;

	init_regs_on_cpu(true);

	ahnode = *((struct aest_hnode **)pdev->dev.platform_data);
	if (!ahnode)
		return -ENODEV;

	adev = devm_kzalloc(&pdev->dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;
	adev->dev = &pdev->dev;
	adev->id = pdev->id;
	aest_set_name(adev, ahnode);

	INIT_WORK(&adev->aest_work, aest_node_pool_process);
	ret = aest_node_pool_init(adev);
	if (ret) {
		aest_dev_err(adev, "Failed init aest node pool.\n");
		return ret;
	}
	init_llist_head(&adev->event_list);

	if (ahnode->type == ACPI_AEST_VENDOR_ERROR_NODE)
		ret = aest_vendor_probe(adev, ahnode);
	else
		ret = aest_init_nodes(adev, ahnode);
	if (ret)
		return ret;

	ret = aest_setup_irq(pdev, adev);
	if (ret)
		return ret;

	ret = aest_register_irq(adev);
	if (ret) {
		aest_dev_err(adev, "register irq failed\n");
		return ret;
	}

	if (aest_dev_is_oncore(adev))
		ret = cpuhp_setup_state(CPUHP_AP_ARM_AEST_STARTING,
					"drivers/acpi/arm64/aest:starting",
					aest_starting_cpu, aest_dying_cpu);
	else
		aest_online_dev(adev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, adev);

	aest_cpu_pm_init();

	aest_dev_init_debugfs(adev);

	aest_dev_dbg(adev, "Node cnt: %x, id: %x\n", adev->node_cnt, adev->id);

	return 0;
}

static struct platform_driver aest_driver = {
	.driver	= {
		.name	= "AEST",
	},
	.probe	= aest_device_probe,
	.remove = aest_device_remove,
};

static int __init aest_init(void)
{
#ifdef CONFIG_DEBUG_FS
	aest_debugfs = debugfs_create_dir("aest", NULL);
#endif
	return platform_driver_register(&aest_driver);
}
module_init(aest_init);

static void __exit aest_exit(void)
{
#ifdef CONFIG_DEBUG_FS
	debugfs_remove(aest_debugfs);
#endif
	aest_cpu_pm_exit();
	platform_driver_unregister(&aest_driver);
}
module_exit(aest_exit);

MODULE_DESCRIPTION("ARM AEST Driver");
MODULE_AUTHOR("Ruidong Tian <tianruidong@linux.alibaba.com>");
MODULE_LICENSE("GPL");
