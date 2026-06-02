// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2025, Alibaba Group.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ras.h>

#include "ras.h"

#undef pr_fmt
#define pr_fmt(fmt) "arm64_ras: " fmt

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

static int ras_init_record(struct ras_record *record, int i, struct ras_node *node)
{
	record->name = devm_kasprintf(node->dev, GFP_KERNEL, "record%d", i);
	if (!record->name)
		return -ENOMEM;

	if (node->base)
		record->regs_base = node->base + sizeof(struct ras_ext_regs) * i;

	record->index = i;
	record->node = node;

	return 0;
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

	ret = device_property_read_u64_array(dev, "arm,record-implemented",
					     (u64 *)node->record_implemented,
					     node->group->errgsr_num);
	ret = ret ?: device_property_read_u64_array(dev, "arm,status-reporting",
						    (u64 *)node->status_reporting,
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
	}

	node->name = alloc_ras_node_name(node);
	if (!node->name)
		return ERR_PTR(-ENOMEM);

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
	ras_node_dbg(node, "base: %llx\n", node->addr);
	return node;
}

static int arm64_ras_probe(struct platform_device *pdev)
{
	int ret;
	struct ras_node *node;

	node = ras_init_node(pdev);
	if (IS_ERR(node))
		return PTR_ERR(node);

	ret = dev_set_name(&pdev->dev, "%s%d", ras_node_name[node->type],
			   pdev->id);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, node);

	return 0;
}

static struct platform_driver arm64_ras_driver = {
	.driver	= {
		.name	= "arm64_ras",
	},
	.probe	= arm64_ras_probe,
};

static int __init arm64_ras_init(void)
{
	return platform_driver_register(&arm64_ras_driver);
}
module_init(arm64_ras_init);

static void __exit arm64_ras_exit(void)
{
	platform_driver_unregister(&arm64_ras_driver);
}
module_exit(arm64_ras_exit);

MODULE_DESCRIPTION("ARM RAS Driver");
MODULE_AUTHOR("Ruidong Tian <tianruidong@linux.alibaba.com>");
MODULE_LICENSE("GPL");
