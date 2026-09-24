// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2025, Alibaba Group.
 */

#include <linux/xarray.h>
#include <linux/platform_device.h>
#include <linux/acpi_aest.h>

#include "init.h"

#undef pr_fmt
#define pr_fmt(fmt) "ACPI AEST: " fmt

static struct xarray *aest_array;

static void __init aest_init_interface(struct acpi_aest_hdr *hdr,
				       struct acpi_aest_node *node)
{
	struct acpi_aest_node_interface_header *interface;

	interface = ACPI_ADD_PTR(struct acpi_aest_node_interface_header, hdr,
				 hdr->node_interface_offset);

	node->type = hdr->type;
	node->interface_hdr = interface;

	switch (interface->group_format) {
	case ACPI_AEST_NODE_GROUP_FORMAT_4K: {
		struct acpi_aest_node_interface_4k *interface_4k =
			(struct acpi_aest_node_interface_4k *)(interface + 1);

		node->common = &interface_4k->common;
		node->record_implemented =
			(unsigned long *)&interface_4k->error_record_implemented;
		node->status_reporting =
			(unsigned long *)&interface_4k->error_status_reporting;
		node->addressing_mode =
			(unsigned long *)&interface_4k->addressing_mode;
		break;
	}
	case ACPI_AEST_NODE_GROUP_FORMAT_16K: {
		struct acpi_aest_node_interface_16k *interface_16k =
			(struct acpi_aest_node_interface_16k *)(interface + 1);

		node->common = &interface_16k->common;
		node->record_implemented =
			(unsigned long *)interface_16k->error_record_implemented;
		node->status_reporting =
			(unsigned long *)interface_16k->error_status_reporting;
		node->addressing_mode =
			(unsigned long *)interface_16k->addressing_mode;
		break;
	}
	case ACPI_AEST_NODE_GROUP_FORMAT_64K: {
		struct acpi_aest_node_interface_64k *interface_64k =
			(struct acpi_aest_node_interface_64k *)(interface + 1);

		node->common = &interface_64k->common;
		node->record_implemented =
			(unsigned long *)interface_64k->error_record_implemented;
		node->status_reporting =
			(unsigned long *)interface_64k->error_status_reporting;
		node->addressing_mode =
			(unsigned long *)interface_64k->addressing_mode;
		break;
	}
	default:
		pr_err("invalid group format: %d\n", interface->group_format);
	}

	node->interrupt = ACPI_ADD_PTR(struct acpi_aest_node_interrupt_v2, hdr,
				       hdr->node_interrupt_offset);

	node->interrupt_count = hdr->node_interrupt_count;
}

static struct aest_hnode *__init
acpi_aest_alloc_ahnode(struct acpi_aest_node *node, u64 error_device_id)
{
	struct aest_hnode *ahnode __free(kfree) = NULL;

	ahnode = kzalloc(sizeof(*ahnode), GFP_KERNEL);
	if (!ahnode)
		return NULL;

	INIT_LIST_HEAD(&ahnode->list);
	ahnode->id = error_device_id;
	ahnode->count = 0;
	ahnode->type = node->type;

	return_ptr(ahnode);
}
static int __init acpi_aest_init_node(struct acpi_aest_hdr *aest_hdr)
{
	struct aest_hnode *ahnode;
	u64 error_device_id;
	struct acpi_aest_node *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->spec_pointer =
		ACPI_ADD_PTR(void, aest_hdr, aest_hdr->node_specific_offset);
	if (aest_hdr->type == ACPI_AEST_PROCESSOR_ERROR_NODE)
		node->processor_spec_pointer =
			ACPI_ADD_PTR(void, node->spec_pointer,
				     sizeof(struct acpi_aest_processor));

	aest_init_interface(aest_hdr, node);

	if (node->interrupt_count <= 0)
		return -EINVAL;

	error_device_id = node->interrupt[0].gsiv;
	ahnode = xa_load(aest_array, error_device_id);
	if (!ahnode) {
		ahnode = acpi_aest_alloc_ahnode(node, error_device_id);
		if (!ahnode)
			return -ENOMEM;
		xa_store(aest_array, error_device_id, ahnode, GFP_KERNEL);
	}

	list_add_tail(&node->list, &ahnode->list);
	ahnode->count++;

	return 0;
}

static int __init acpi_aest_init_nodes(struct acpi_table_header *aest_table)
{
	struct acpi_aest_hdr *aest_node, *aest_end;
	struct acpi_table_aest *aest;
	int rc;

	aest = (struct acpi_table_aest *)aest_table;
	aest_node = ACPI_ADD_PTR(struct acpi_aest_hdr, aest,
				 sizeof(struct acpi_table_header));
	aest_end = ACPI_ADD_PTR(struct acpi_aest_hdr, aest, aest_table->length);

	while (aest_node < aest_end) {
		if (((u64)aest_node + aest_node->length) > (u64)aest_end) {
			pr_warn(FW_WARN
				"AEST node pointer overflow, bad table.\n");
			return -EINVAL;
		}

		rc = acpi_aest_init_node(aest_node);
		if (rc)
			return rc;

		aest_node = ACPI_ADD_PTR(struct acpi_aest_hdr, aest_node,
					 aest_node->length);
	}

	return 0;
}

static int acpi_aest_parse_irqs(struct platform_device *pdev,
				struct acpi_aest_node *anode,
				struct resource *res, int *res_idx, int irqs[2])
{
	int i;
	struct acpi_aest_node_interrupt_v2 *interrupt;
	int trigger, irq;

	for (i = 0; i < anode->interrupt_count; i++) {
		interrupt = &anode->interrupt[i];
		if (irqs[interrupt->type])
			continue;

		trigger = (interrupt->flags & AEST_INTERRUPT_MODE) ?
				  ACPI_LEVEL_SENSITIVE :
				  ACPI_EDGE_SENSITIVE;

		irq = acpi_register_gsi(&pdev->dev, interrupt->gsiv, trigger,
					ACPI_ACTIVE_HIGH);
		if (irq <= 0) {
			pr_err("failed to map AEST GSI %d\n", interrupt->gsiv);
			return irq;
		}

		res[*res_idx].start = irq;
		res[*res_idx].end = irq;
		res[*res_idx].flags = IORESOURCE_IRQ;
		res[*res_idx].name = interrupt->type ? AEST_ERI_NAME :
						       AEST_FHI_NAME;

		(*res_idx)++;

		irqs[interrupt->type] = irq;
	}

	return 0;
}

DEFINE_FREE(res, struct resource *, if (_T) kfree(_T))
static struct platform_device *__init
acpi_aest_alloc_pdev(struct aest_hnode *ahnode, int index)
{
	struct platform_device *pdev __free(platform_device_put) =
		platform_device_alloc("AEST", index++);
	struct resource *res __free(res);
	struct acpi_aest_node *anode;
	int ret, size, j, irq[AEST_MAX_INTERRUPT_PER_NODE] = { 0 };

	if (!pdev)
		return ERR_PTR(-ENOMEM);

	res = kcalloc(ahnode->count + AEST_MAX_INTERRUPT_PER_NODE, sizeof(*res),
		      GFP_KERNEL);
	if (!res)
		return ERR_PTR(-ENOMEM);

	j = 0;
	list_for_each_entry(anode, &ahnode->list, list) {
		if (anode->interface_hdr->type !=
		    ACPI_AEST_NODE_SYSTEM_REGISTER) {
			res[j].name = AEST_NODE_NAME;
			res[j].start = anode->interface_hdr->address;
			switch (anode->interface_hdr->group_format) {
			case ACPI_AEST_NODE_GROUP_FORMAT_4K:
				size = 4 * KB;
				break;
			case ACPI_AEST_NODE_GROUP_FORMAT_16K:
				size = 16 * KB;
				break;
			case ACPI_AEST_NODE_GROUP_FORMAT_64K:
				size = 64 * KB;
				break;
			default:
				size = 4 * KB;
			}
			res[j].end = res[j].start + size - 1;
			res[j].flags = IORESOURCE_MEM;
		}

		ret = acpi_aest_parse_irqs(pdev, anode, res, &j, irq);
		if (ret)
			return ERR_PTR(ret);
	}

	ret = platform_device_add_resources(pdev, res, j);
	if (ret)
		return ERR_PTR(ret);

	ret = platform_device_add_data(pdev, &ahnode, sizeof(ahnode));
	if (ret)
		return ERR_PTR(ret);

	ret = platform_device_add(pdev);
	if (ret)
		return ERR_PTR(ret);

	return_ptr(pdev);
}
static int __init acpi_aest_alloc_pdevs(void)
{
	int ret = 0, index = 0;
	struct aest_hnode *ahnode = NULL;
	unsigned long i;

	xa_for_each(aest_array, i, ahnode) {
		struct platform_device *pdev =
			acpi_aest_alloc_pdev(ahnode, index++);

		if (IS_ERR(pdev)) {
			ret = PTR_ERR(pdev);
			break;
		}
	}

	return ret;
}

static int __init acpi_aest_init(void)
{
	int ret;

	if (acpi_disabled)
		return 0;

	struct acpi_table_header *aest_table __free(acpi_put_table) =
		acpi_get_table_pointer(ACPI_SIG_AEST, 0);
	if (IS_ERR(aest_table))
		return 0;

	aest_array = kzalloc(sizeof(struct xarray), GFP_KERNEL);
	if (!aest_array)
		return -ENOMEM;

	xa_init(aest_array);

	ret = acpi_aest_init_nodes(aest_table);
	if (ret) {
		pr_err("Failed init aest node %d\n", ret);
		return ret;
	}

	ret = acpi_aest_alloc_pdevs();
	if (ret) {
		pr_err("Failed alloc pdev %d\n", ret);
		return ret;
	}

	return 0;
}
subsys_initcall_sync(acpi_aest_init);
