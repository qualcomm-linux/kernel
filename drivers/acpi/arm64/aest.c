// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2025, Alibaba Group.
 */

#include <linux/cleanup.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/acpi_aest.h>

#include "init.h"

#undef pr_fmt
#define pr_fmt(fmt) "ACPI AEST: " fmt

static int acpi_aest_parse_irqs(struct platform_device *pdev,
				struct acpi_aest_hdr *aest_hdr,
				struct resource *res, int *res_idx)
{
	int i;
	struct acpi_aest_node_interrupt_v2 *interrupt;
	int trigger, irq;

	interrupt = ACPI_ADD_PTR(struct acpi_aest_node_interrupt_v2, aest_hdr,
				aest_hdr->node_interrupt_offset);
	for (i = 0; i < aest_hdr->node_interrupt_count; i++, interrupt++) {
		trigger = (interrupt->flags & AEST_INTERRUPT_MODE) ?
				  ACPI_LEVEL_SENSITIVE :
				  ACPI_EDGE_SENSITIVE;

		irq = acpi_register_gsi(&pdev->dev, interrupt->gsiv, trigger,
					ACPI_ACTIVE_HIGH);
		if (irq <= 0) {
			pr_err("failed to map AEST GSI %d\n", interrupt->gsiv);
			return irq ? irq : -EINVAL;
		}

		res[*res_idx].start = irq;
		res[*res_idx].end = irq;
		res[*res_idx].flags = IORESOURCE_IRQ;
		res[*res_idx].name = interrupt->type ? AEST_ERI_NAME :
						       AEST_FHI_NAME;

		(*res_idx)++;
	}

	return 0;
}

/*
 * Fill the per-AEST-entry inner properties (node-type / interface-type /
 * group-format / record bitmaps / register bases ...).
 */
static int __init
aest_init_node_props(struct acpi_aest_hdr *hdr, struct property_entry *props,
		     int *p, struct platform_device *pdev)
{
	struct acpi_aest_node_interface_header *interface;
	struct acpi_aest_node_interface_common *common = NULL;
	struct acpi_aest_node_interrupt_v2 *interrupt;
	u64 *record_implemented = NULL;
	u64 *status_reporting = NULL;
	u64 *addressing_mode = NULL;
	u32 fhi_gsiv = 0, eri_gsiv = 0;
	int group_len = 0, i;
	size_t len;

	interface = ACPI_ADD_PTR(struct acpi_aest_node_interface_header,
				 hdr, hdr->node_interface_offset);
	switch (interface->group_format) {
	case ACPI_AEST_NODE_GROUP_FORMAT_4K: {
		struct acpi_aest_node_interface_4k *itf =
			(struct acpi_aest_node_interface_4k *)(interface + 1);

		record_implemented = &itf->error_record_implemented;
		status_reporting   = &itf->error_status_reporting;
		addressing_mode    = &itf->addressing_mode;
		group_len = 1;
		common = &itf->common;
		break;
	}
	case ACPI_AEST_NODE_GROUP_FORMAT_16K: {
		struct acpi_aest_node_interface_16k *itf =
			(struct acpi_aest_node_interface_16k *)(interface + 1);

		record_implemented = itf->error_record_implemented;
		status_reporting   = itf->error_status_reporting;
		addressing_mode    = itf->addressing_mode;
		group_len = 4;
		common = &itf->common;
		break;
	}
	case ACPI_AEST_NODE_GROUP_FORMAT_64K: {
		struct acpi_aest_node_interface_64k *itf =
			(struct acpi_aest_node_interface_64k *)(interface + 1);

		record_implemented = itf->error_record_implemented;
		status_reporting   = itf->error_status_reporting;
		addressing_mode    = itf->addressing_mode;
		group_len = 14;
		common = &itf->common;
		break;
	}
	default:
		pr_err("invalid group format: %d\n", interface->group_format);
		return -EINVAL;
	}

	interrupt = ACPI_ADD_PTR(struct acpi_aest_node_interrupt_v2, hdr,
				 hdr->node_interrupt_offset);
	for (i = 0; i < hdr->node_interrupt_count; i++, interrupt++) {
		if (interrupt->type == ACPI_AEST_NODE_FAULT_HANDLING)
			fhi_gsiv = interrupt->gsiv;
		else if (interrupt->type == ACPI_AEST_NODE_ERROR_RECOVERY)
			eri_gsiv = interrupt->gsiv;
	}

	if (interface->flags & AEST_XFACE_FLAG_ERROR_DEVICE) {
		struct acpi_device *companion;
		char uid[16];
		int n;

		n = snprintf(uid, sizeof(uid), "%u",
			     common->error_node_device);
		if (n > 0 && n < sizeof(uid)) {
			companion = acpi_dev_get_first_match_dev("ARMHE000",
								 uid, -1);
			if (companion) {
				ACPI_COMPANION_SET(&pdev->dev, companion);
				acpi_dev_put(companion);
			} else {
				pr_debug("MSC.%u: missing namespace entry\n",
					 common->error_node_device);
			}
		}
	}

	props[(*p)++] = PROPERTY_ENTRY_U8("arm,node-type", hdr->type);
	props[(*p)++] = PROPERTY_ENTRY_U8("arm,interface-type", interface->type);
	props[(*p)++] = PROPERTY_ENTRY_U8("arm,group-format",
					  interface->group_format);
	props[(*p)++] = PROPERTY_ENTRY_U32("arm,error-records-count",
					   interface->error_record_count);
	props[(*p)++] = PROPERTY_ENTRY_U32("arm,error-records-index",
					   interface->error_record_index);
	props[(*p)++] = PROPERTY_ENTRY_U32("arm,interface-flags",
					   interface->flags);
	props[(*p)++] = PROPERTY_ENTRY_U64_ARRAY_LEN("arm,record-implemented",
						     record_implemented,
						     group_len);
	props[(*p)++] = PROPERTY_ENTRY_U64_ARRAY_LEN("arm,status-reporting",
						     status_reporting,
						     group_len);
	props[(*p)++] = PROPERTY_ENTRY_U64("arm,error-group-base",
					   common->error_group_register_base);
	props[(*p)++] = PROPERTY_ENTRY_U64("arm,fault-inject-base",
					   common->fault_inject_register_base);
	props[(*p)++] = PROPERTY_ENTRY_U64("arm,interrupt-config-base",
					   common->interrupt_config_register_base);
	props[(*p)++] = PROPERTY_ENTRY_U32("arm,fhi-gsiv", fhi_gsiv);
	props[(*p)++] = PROPERTY_ENTRY_U32("arm,eri-gsiv", eri_gsiv);

	len = hdr->node_interface_offset - hdr->node_specific_offset;
	props[(*p)++] =
		PROPERTY_ENTRY_U8_ARRAY_LEN("arm,node-specific-data",
					    ACPI_ADD_PTR(u8, hdr, hdr->node_specific_offset), len);

	return 0;
}

static int __init
aest_create_node_fwnode(struct acpi_aest_hdr *hdr, struct platform_device *pdev)
{
	struct property_entry props[15] = { };
	int p = 0;
	int ret;

	ret = aest_init_node_props(hdr, props, &p, pdev);
	if (ret)
		return ret;

	return device_create_managed_software_node(&pdev->dev, props, NULL);
}

static int aest_node_mem_size(u8 group_format)
{
	switch (group_format) {
	case ACPI_AEST_NODE_GROUP_FORMAT_4K:
		return SZ_4K;
	case ACPI_AEST_NODE_GROUP_FORMAT_16K:
		return SZ_16K;
	case ACPI_AEST_NODE_GROUP_FORMAT_64K:
		return SZ_64K;
	default:
		return SZ_4K;
	}
}

DEFINE_FREE(res, struct resource *, if (_T) kfree(_T))

static struct platform_device *__init
acpi_aest_alloc_pdev(struct acpi_aest_hdr *aest_hdr)
{
	struct platform_device *pdev __free(platform_device_put) =
		platform_device_alloc("arm64_ras", PLATFORM_DEVID_AUTO);
	struct resource *res __free(res) = NULL;
	struct acpi_aest_node_interface_header *interface;
	int ret, j = 0;

	if (!pdev)
		return ERR_PTR(-ENOMEM);

	res = kcalloc(AEST_MAX_INTERRUPT_PER_NODE + 1, sizeof(*res),
		      GFP_KERNEL);
	if (!res)
		return ERR_PTR(-ENOMEM);

	interface = ACPI_ADD_PTR(struct acpi_aest_node_interface_header,
				 aest_hdr, aest_hdr->node_interface_offset);
	if (interface->type != ACPI_AEST_NODE_SYSTEM_REGISTER) {
		res[j].name = AEST_NODE_NAME;
		res[j].start = interface->address;
		res[j].end = res[j].start + aest_node_mem_size(interface->group_format) - 1;
		res[j].flags = IORESOURCE_MEM;
		j++;
	}

	ret = acpi_aest_parse_irqs(pdev, aest_hdr, res, &j);
	if (ret)
		return ERR_PTR(ret);

	ret = platform_device_add_resources(pdev, res, j);
	if (ret)
		return ERR_PTR(ret);

	return_ptr(pdev);
}

static int __init acpi_aest_init_node(struct acpi_aest_hdr *aest_hdr)
{
	struct platform_device *pdev __free(platform_device_put) = NULL;
	int ret;

	pdev = acpi_aest_alloc_pdev(aest_hdr);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	ret = aest_create_node_fwnode(aest_hdr, pdev);
	if (ret)
		return ret;

	ret = platform_device_add(pdev);
	if (ret)
		return ret;
	pr_debug("Platform device added for AEST node: %s.%d\n",
		 pdev->name, pdev->id);
	retain_and_null_ptr(pdev);

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

static int __init acpi_aest_init(void)
{
	int ret;

	if (acpi_disabled)
		return 0;

	struct acpi_table_header *aest_table __free(acpi_put_table) =
		acpi_get_table_pointer(ACPI_SIG_AEST, 0);
	if (IS_ERR(aest_table))
		return 0;

	ret = acpi_aest_init_nodes(aest_table);
	if (ret) {
		pr_err("Failed init aest node %d\n", ret);
		return ret;
	}

	return 0;
}
subsys_initcall_sync(acpi_aest_init);
