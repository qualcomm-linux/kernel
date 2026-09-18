// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

#include <linux/acpi_aest.h>

#undef pr_fmt
#define pr_fmt(fmt) "DT RAS: " fmt

/* Maximum number of software fwnode properties per RAS node */
#define RAS_OF_MAX_PROPS	20

static const unsigned long ras_of_type_processor __initconst =
	ACPI_AEST_PROCESSOR_ERROR_NODE;
static const unsigned long ras_of_type_smmu __initconst =
	ACPI_AEST_SMMU_ERROR_NODE;
static const unsigned long ras_of_type_gic __initconst =
	ACPI_AEST_GIC_ERROR_NODE;

static const struct of_device_id ras_of_match[] __initconst = {
	{ .compatible = "arm,ras-processor", .data = &ras_of_type_processor },
	{ .compatible = "arm,ras-smmu",      .data = &ras_of_type_smmu      },
	{ .compatible = "arm,ras-gic",       .data = &ras_of_type_gic       },
	{ }
};

static const char * const __initconst ras_of_irq_res_names[] = {
	[ACPI_AEST_NODE_FAULT_HANDLING] = AEST_FHI_NAME,
	[ACPI_AEST_NODE_ERROR_RECOVERY] = AEST_ERI_NAME,
};

static const char * const __initconst ras_of_dt_irq_names[] = {
	"fhi", "eri"
};

static int __init ras_of_build_node_data(struct device_node *np,
					 u8 node_type, int index,
					 int fhi_virq,
					 u8 **data_out, u32 *size_out)
{
	*data_out = NULL;
	*size_out = 0;

	switch (node_type) {
	case ACPI_AEST_PROCESSOR_ERROR_NODE: {
		/*
		 * Allocate the processor header plus the cache sub-structure.
		 * resource_type is inferred from the presence of the 'cache'
		 * phandle: if present the resource is a cache, otherwise it
		 * is treated as a generic processor resource.
		 * flags (SHARED/GLOBAL) are inferred from the interrupt type:
		 * a PPI is per-PE (global); an SPI is shared across a cluster.
		 */
		size_t total = sizeof(struct acpi_aest_processor) +
			       sizeof(struct acpi_aest_processor_cache);
		struct acpi_aest_processor_cache *c;
		struct acpi_aest_processor *proc;
		struct device_node *cache_np;
		u8 pflags;

		proc = kzalloc(total, GFP_KERNEL);
		if (!proc)
			return -ENOMEM;

		cache_np = of_parse_phandle(np, "arm,cache-ref", 0);
		if (cache_np) {
			proc->resource_type = ACPI_AEST_CACHE_RESOURCE;
			c = (struct acpi_aest_processor_cache *)(proc + 1);
			/*
			 * alloc_ras_node_name() reads c->cache_reference as
			 * the name disambiguator for shared/global nodes.
			 */
			c->cache_reference = cache_np->phandle;
			of_node_put(cache_np);
		} else {
			proc->resource_type = ACPI_AEST_GENERIC_RESOURCE;
		}

		/*
		 * Infer scope from the FHI interrupt type: a PPI is
		 * per-PE (set GLOBAL); an SPI is cluster-shared (set SHARED).
		 * alloc_ras_node_name() uses flags to choose the name format.
		 */
		pflags = irq_is_percpu(fhi_virq) ?
			 ACPI_AEST_PROC_FLAG_GLOBAL :
			 ACPI_AEST_PROC_FLAG_SHARED;
		proc->flags        = pflags;
		proc->processor_id = (u32)index;

		*data_out = (u8 *)proc;
		*size_out = (u32)total;
		break;
	}
	case ACPI_AEST_SMMU_ERROR_NODE: {
		struct acpi_aest_smmu *smmu;
		struct device_node *smmu_np;

		smmu = kzalloc_obj(*smmu, GFP_KERNEL);
		if (!smmu)
			return -ENOMEM;

		smmu_np = of_parse_phandle(np, "arm,smmu-ref", 0);
		if (smmu_np) {
			smmu->iort_node_reference = smmu_np->phandle;
			of_node_put(smmu_np);
		}

		*data_out = (u8 *)smmu;
		*size_out = sizeof(*smmu);
		break;
	}
	case ACPI_AEST_GIC_ERROR_NODE: {
		struct acpi_aest_gic *gic;
		struct device_node *gic_np;

		gic = kzalloc_obj(*gic, GFP_KERNEL);
		if (!gic)
			return -ENOMEM;

		gic_np = of_parse_phandle(np, "arm,gic-ref", 0);
		if (gic_np) {
			gic->instance_id = gic_np->phandle;
			of_node_put(gic_np);
		}
		*data_out = (u8 *)gic;
		*size_out = sizeof(*gic);
		break;
	}
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Determine the register access type from the DT node:
 *   no reg  -> system-register access (ERRSELR_EL1 + ERX*_EL1)
 *   1 range -> memory-mapped access
 *   2+ ranges -> single-record memory-mapped access
 */
static u8 __init ras_of_interface_type(struct device_node *np)
{
	int addr_cells, size_cells, reg_len;

	if (!of_property_present(np, "reg"))
		return ACPI_AEST_NODE_SYSTEM_REGISTER;

	addr_cells = of_n_addr_cells(np);
	size_cells = of_n_size_cells(np);
	if (addr_cells <= 0 || size_cells <= 0)
		return ACPI_AEST_NODE_SYSTEM_REGISTER;

	reg_len = of_property_count_elems_of_size(np, "reg", sizeof(u32));
	if (reg_len <= 0)
		return ACPI_AEST_NODE_SYSTEM_REGISTER;

	if (reg_len <= addr_cells + size_cells)
		return ACPI_AEST_NODE_MEMORY_MAPPED;

	return ACPI_AEST_NODE_SINGLE_RECORD_MEMORY_MAPPED;
}

static resource_size_t __init ras_of_mem_size(u32 gfmt)
{
	switch (gfmt) {
	case ACPI_AEST_NODE_GROUP_FORMAT_16K:
		return SZ_16K;
	case ACPI_AEST_NODE_GROUP_FORMAT_64K:
		return SZ_64K;
	default:
		return SZ_4K;
	}
}

static u32 __init ras_of_virq_to_gsiv(int virq)
{
	struct irq_data *irqd = irq_get_irq_data(virq);

	return irqd ? (u32)irqd->hwirq : 0;
}

static int __init ras_of_attach_fwnode(struct device_node *np,
				       struct platform_device *pdev,
				       u8 node_type, u8 itype, u32 gfmt,
				       int index, int fhi_virq,
				       u32 fhi_gsiv, u32 eri_gsiv)
{
	u64 rec_impl[14] = { };
	u64 stat_rep[14] = { };
	u64 addr_mode[14] = { };
	u64 err_group_base = 0, fault_inject_base = 0, irq_cfg_base = 0;
	struct property_entry props[RAS_OF_MAX_PROPS] = { };
	struct resource res;
	u8 *node_data = NULL;
	u32 node_data_size = 0;
	u32 nrec = 1;
	int group_len, p = 0, i, ret;

	of_property_read_u32(np, "arm,num-records", &nrec);

	switch (gfmt) {
	case ACPI_AEST_NODE_GROUP_FORMAT_16K:
		group_len = 4;
		break;
	case ACPI_AEST_NODE_GROUP_FORMAT_64K:
		group_len = 14;
		break;
	default:
		group_len = 1;
		break;
	}

	of_property_read_u64_array(np, "arm,record-impl",      rec_impl,  group_len);
	of_property_read_u64_array(np, "arm,status-reporting", stat_rep,  group_len);
	of_property_read_u64_array(np, "arm,addressing-mode",  addr_mode, group_len);

	/*
	 * DT binding: bit=1 means record IS implemented.
	 * ras-core.c uses for_each_clear_bit() on record_implemented, so
	 * bit=0 means implemented internally.  Invert before storing.
	 */
	for (i = 0; i < group_len; i++)
		rec_impl[i] = ~rec_impl[i];

	/* Named MMIO windows — only present on memory-mapped nodes */
	if (itype != ACPI_AEST_NODE_SYSTEM_REGISTER) {
		int idx;

		idx = of_property_match_string(np, "reg-names", "err-group");
		if (idx >= 0 && !of_address_to_resource(np, idx, &res))
			err_group_base = res.start;

		idx = of_property_match_string(np, "reg-names", "fault-inject");
		if (idx >= 0 && !of_address_to_resource(np, idx, &res))
			fault_inject_base = res.start;

		idx = of_property_match_string(np, "reg-names", "irq-config");
		if (idx >= 0 && !of_address_to_resource(np, idx, &res))
			irq_cfg_base = res.start;
	}

	ret = ras_of_build_node_data(np, node_type, index, fhi_virq,
				     &node_data, &node_data_size);
	if (ret)
		return ret;

	props[p++] = PROPERTY_ENTRY_U8("arm,node-type",      node_type);
	props[p++] = PROPERTY_ENTRY_U8("arm,interface-type", itype);
	props[p++] = PROPERTY_ENTRY_U8("arm,group-format",   (u8)gfmt);
	props[p++] = PROPERTY_ENTRY_U32("arm,error-records-count", nrec);
	props[p++] = PROPERTY_ENTRY_U32("arm,error-records-index", 0);
	props[p++] = PROPERTY_ENTRY_U32("arm,interface-flags",     0);
	props[p++] = PROPERTY_ENTRY_U64_ARRAY_LEN("arm,record-implemented",
						  rec_impl,  group_len);
	props[p++] = PROPERTY_ENTRY_U64_ARRAY_LEN("arm,status-reporting",
						  stat_rep,  group_len);
	props[p++] = PROPERTY_ENTRY_U64_ARRAY_LEN("arm,addressing-mode",
						  addr_mode, group_len);
	props[p++] = PROPERTY_ENTRY_U64("arm,error-group-base",      err_group_base);
	props[p++] = PROPERTY_ENTRY_U64("arm,fault-inject-base",     fault_inject_base);
	props[p++] = PROPERTY_ENTRY_U64("arm,interrupt-config-base", irq_cfg_base);
	props[p++] = PROPERTY_ENTRY_U32("arm,fhi-gsiv", fhi_gsiv);
	props[p++] = PROPERTY_ENTRY_U32("arm,eri-gsiv", eri_gsiv);

	if (node_data && node_data_size)
		props[p++] = PROPERTY_ENTRY_U8_ARRAY_LEN("arm,node-specific-data",
							 node_data, node_data_size);

	ret = device_create_managed_software_node(&pdev->dev, props, NULL);

	kfree(node_data);
	return ret;
}

static int __init ras_of_init_one_node(struct device_node *np, u8 node_type,
				       int index)
{
	struct resource res[AEST_MAX_INTERRUPT_PER_NODE + 1] = { };
	struct platform_device *pdev;
	u32 gfmt = ACPI_AEST_NODE_GROUP_FORMAT_4K;
	u32 gsiv[AEST_MAX_INTERRUPT_PER_NODE] = { };
	int virq[AEST_MAX_INTERRUPT_PER_NODE] = { };
	int nres = 0, ret, i;
	u8 itype;

	itype = ras_of_interface_type(np);
	of_property_read_u32(np, "arm,group-format", &gfmt);

	pdev = platform_device_alloc("arm64_ras", PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;

	if (itype != ACPI_AEST_NODE_SYSTEM_REGISTER) {
		struct resource mmio_res;

		ret = of_address_to_resource(np, 0, &mmio_res);
		if (ret) {
			pr_err("node %pOF: missing 'reg' for MMIO interface\n", np);
			goto err_put;
		}
		res[nres].name  = AEST_NODE_NAME;
		res[nres].start = mmio_res.start;
		res[nres].end   = mmio_res.start + ras_of_mem_size(gfmt) - 1;
		res[nres].flags = IORESOURCE_MEM;
		nres++;
	}

	for (i = 0; i < AEST_MAX_INTERRUPT_PER_NODE; i++) {
		int irq_num = of_irq_get_byname(np, ras_of_dt_irq_names[i]);

		if (irq_num <= 0)
			continue;

		gsiv[i] = ras_of_virq_to_gsiv(irq_num);
		virq[i] = irq_num;

		res[nres].name  = ras_of_irq_res_names[i];
		res[nres].start = irq_num;
		res[nres].end   = irq_num;
		res[nres].flags = IORESOURCE_IRQ;
		nres++;
	}

	ret = platform_device_add_resources(pdev, res, nres);
	if (ret)
		goto err_put;

	ret = ras_of_attach_fwnode(np, pdev, node_type, itype, gfmt, index,
				   virq[ACPI_AEST_NODE_FAULT_HANDLING],
				   gsiv[ACPI_AEST_NODE_FAULT_HANDLING],
				   gsiv[ACPI_AEST_NODE_ERROR_RECOVERY]);
	if (ret)
		goto err_put;

	ret = platform_device_add(pdev);
	if (ret)
		goto err_put;

	pr_debug("registered RAS node %pOF as arm64_ras.%d\n", np, pdev->id);
	return 0;

err_put:
	platform_device_put(pdev);
	return ret;
}

static int __init ras_of_init(void)
{
	const struct of_device_id *match;
	struct device_node *np;
	int index = 0, ret;

	if (!acpi_disabled)
		return 0;

	for_each_matching_node_and_match(np, ras_of_match, &match) {
		u8 node_type = *(const unsigned long *)match->data;

		ret = ras_of_init_one_node(np, node_type, index++);
		if (ret) {
			pr_err("failed to register RAS node %pOF: %d\n",
			       np, ret);
			of_node_put(np);
			return ret;
		}
	}

	if (index)
		pr_info("registered %d RAS error source(s) from DT\n", index);

	return 0;
}
subsys_initcall_sync(ras_of_init);
