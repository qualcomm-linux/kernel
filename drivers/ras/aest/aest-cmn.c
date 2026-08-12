// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table CMN700 Support
 *
 * Copyright (c) 2025, Alibaba Inc
 */

#include <asm/arm-cmn.h>

#include "aest.h"

/*
 * CMN include 5 device types, each device type has an error group register set
 * which contains a set of error records. The struct aest_cmn_700 represents
 * one CMN Instance, and the struct aest_cmn_700_child represent one CMN device.
 * The error record of CMN use memory-mapped single error record view, so one
 * record is correspond to one AEST node, it means there will be hundreds of
 * AEST node of CMN. As described in chapters 2.6.3.4 of Arm ACPI Spec[1], we
 * use vendor define data to recognize the device type of an AEST node. So AEST
 * driver can enumerate all CMN AEST node to initialize struct aest_cmn_700 and
 * aest_cmn_700_child with HID, UID and other CMN info described in AEST or CMN
 * register.
 *
 * Each CMN Instance has their own error interrupt and the struct aest_cmn_700
 * is passed to interrupt context. OS check error group register set to locate
 * record which report error. All procedure is similar with chapters 3.8 in
 * Arm CMN Spec[2].
 *
 * The CMN RAS architecture is showed as follow:
 *
 *                     +----+
 *                  -->|XP  |     ......
 *                  |  +----+
 *                  |
 *                  |  +----+     ......
 *                  |  |HNI |     +----------------+
 *                  |  +----+   ->|record/AEST node|
 *                  |           | +----------------+
 *  +------------+  |  +----+   |    .
 *  |CMN Instance|--|  |HNF |---|    .
 *  +------------+  |  +----+   |    .
 *                  |           | +----------------+
 *                  |  +----+   ->|record/AEST node|
 *                  |  |SBSX|     +----------------+
 *                  |  +----+     ......
 *                  |
 *                  |  +----+
 *                  -->|CCG |     ......
 *                     +----+
 *
 * [1]: https://developer.arm.com/documentation/den0093/latest
 * [2]: https://developer.arm.com/documentation/102308/latest
 */

#define CMN_RAS_DEV_NUM 6
#define CMN700_ERRGSR_NUM 8
#define CMN_MAX_UID 8
#define CMN_ERRDEVARCH 0x3FB8
#define CMN_ERRDEVARCH_REV GENMASK(19, 16)
#define CMN_ERRGSR_OFFSET 0x3000

struct cmn_vendor_data {
	int node_type;
	int node_id;
	int logic_id;
};

struct cmn_config {
	int errgsr_num;
	int dev_num;
	int ras_ver;
	const int *node_id_map;
	const char *const *node_name;
	int (*errgsr_mapping)(int errgsr_bit);
	u64 (*errgsr_offset)(u64 hnd_ofset, int node_idx);
};

static const char *const cmn700_node_name[] = {
	[CMN_TYPE_HNI] = "HNI",	 [CMN_TYPE_HNF] = "HNF",
	[CMN_TYPE_XP] = "XP",	 [CMN_TYPE_SBSX] = "SBSX",
	[CMN_TYPE_CXRA] = "RND", [CMN_TYPE_MTSX] = "MTSX",
};

static const int cmn700_node_id_map[] = {
	[CMN_TYPE_HNI] = 1,  [CMN_TYPE_HNF] = 2,  [CMN_TYPE_XP] = 0,
	[CMN_TYPE_SBSX] = 3, [CMN_TYPE_CXRA] = 4, [CMN_TYPE_MTSX] = 5,
};

static u64 cmn_dev_array[CMN_MAX_UID];
static struct cmn_config *cmn_config;

static u64 cmn700_errgsr_offset(u64 hnd_offset, int node_idx)
{
	return hnd_offset + CMN_ERRGSR_OFFSET +
	       (node_idx * 2) * CMN700_ERRGSR_NUM * 8;
}

static struct cmn_config cmn700_config = {
	.errgsr_num = CMN700_ERRGSR_NUM,
	.dev_num = CMN_RAS_DEV_NUM,
	.ras_ver = 1,
	.node_name = cmn700_node_name,
	.node_id_map = cmn700_node_id_map,
	.errgsr_mapping = cmn700_errgsr_mapping,
	.errgsr_offset = cmn700_errgsr_offset,
};

static acpi_status aest_cmn_700_resource_ioremap(struct acpi_resource *res,
						 void *data)
{
	struct acpi_resource_address64 addr64;
	u32 *uid = data;
	acpi_status status;

	status = acpi_resource_to_address64(res, &addr64);
	if (ACPI_FAILURE(status) || (addr64.resource_type != ACPI_MEMORY_RANGE))
		return AE_OK;

	cmn_dev_array[*uid] = (u64)ioremap(addr64.address.minimum,
					   addr64.address.address_length);

	pr_debug("CMN device resource [%llx-%llx] ioremap to %llx\n",
		 addr64.address.minimum, addr64.address.maximum,
		 cmn_dev_array[*uid]);

	return AE_CTRL_TERMINATE;
}

static acpi_status aest_cmn_get_dev_by_uid(acpi_handle handle, u32 level,
					   void *data, void **return_value)
{
	u32 *match_uid = data;
	acpi_status status;
	unsigned long long uid;

	status = acpi_evaluate_integer(handle, METHOD_NAME__UID, NULL, &uid);
	if (ACPI_FAILURE(status)) {
		pr_err("Do not find devive\n");
		return_ACPI_STATUS(status);
	}

	if (uid != *match_uid)
		return AE_OK;

	pr_debug("CMN device instance %llx, walk through resource\n", uid);

	status = acpi_walk_resources(handle, METHOD_NAME__CRS,
				     aest_cmn_700_resource_ioremap, data);

	if (ACPI_FAILURE(status)) {
		pr_err("Device do not have resource\n");
		return_ACPI_STATUS(status);
	}

	return AE_CTRL_TERMINATE;
}

static inline int aest_cmn_node_ver(void *base)
{
	return FIELD_GET(CMN_ERRDEVARCH_REV,
			 readl_relaxed(base + CMN_ERRDEVARCH));
}

static int aest_cmn_init_node(struct aest_device *adev,
			      struct aest_node *cmn_node,
			      struct acpi_aest_node *anode, u64 type,
			      u64 errgsr_addr)
{
	cmn_node->info = anode;
	cmn_node->name = devm_kasprintf(adev->dev, GFP_KERNEL, "%s",
					cmn_config->node_name[type]);
	if (!cmn_node->name)
		return -ENOMEM;
	cmn_node->errgsr = (void *)errgsr_addr;
	cmn_node->type = anode->type;
	cmn_node->adev = adev;
	cmn_node->version = cmn_config->ras_ver;
	cmn_node->errgsr_num = cmn_config->errgsr_num;
	cmn_node->errgsr_mapping = cmn_config->errgsr_mapping;
	cmn_node->record_count = cmn_node->errgsr_num * BITS_PER_LONG / 2;
	cmn_node->record_implemented = devm_bitmap_zalloc(
		adev->dev, cmn_node->record_count, GFP_KERNEL);
	if (!cmn_node->record_implemented)
		return -ENOMEM;
	bitmap_set(cmn_node->record_implemented, 0, cmn_node->record_count);

	cmn_node->status_reporting = devm_bitmap_zalloc(
		adev->dev, cmn_node->record_count, GFP_KERNEL);
	if (!cmn_node->status_reporting)
		return -ENOMEM;
	bitmap_set(cmn_node->status_reporting, 0, cmn_node->record_count);

	cmn_node->records = devm_kcalloc(adev->dev, cmn_node->record_count,
					 sizeof(struct aest_record),
					 GFP_KERNEL);
	if (!cmn_node->records)
		return -ENOMEM;

	aest_node_dbg(cmn_node, "Node init with errgsr %llx\n", errgsr_addr);

	return 0;
}

static int aest_cmn_reorgnize_node(struct aest_device *adev,
				   struct acpi_aest_node *anode, u64 base)
{
	struct aest_node *cmn_node;
	u64 hnd_offset, cmn_node_offset, reg, logic_id, type, node_id;
	u64 errgsr_addr, hnd_base;
	struct aest_record *record;
	int ret, node_index;
	struct cmn_vendor_data *vendor_data;

	if (anode->interface_hdr->type !=
	    ACPI_AEST_NODE_SINGLE_RECORD_MEMORY_MAPPED) {
		aest_dev_err(adev, "CMN just use single memory mapping\n");
		return -ENODEV;
	}

	hnd_offset = *((u64 *)anode->vendor->vendor_specific_data);
	cmn_node_offset = *((u64 *)&anode->vendor->vendor_specific_data[8]);

	reg = readq_relaxed((void *)base + cmn_node_offset + CMN_NODE_INFO);

	logic_id = FIELD_GET(CMN_NI_LOGICAL_ID, reg);
	type = FIELD_GET(CMN_NI_NODE_TYPE, reg);
	node_id = FIELD_GET(CMN_NI_NODE_ID, reg);

	hnd_base = base + hnd_offset;
	node_index = cmn_config->node_id_map[type];
	errgsr_addr = base + cmn_config->errgsr_offset(hnd_offset, node_index);

	// node not register, create it
	cmn_node = &adev->nodes[node_index];
	if (!cmn_node->errgsr) {
		ret = aest_cmn_init_node(adev, cmn_node, anode, type,
					 errgsr_addr);
		if (ret)
			return -ENOMEM;
	}

	aest_dev_dbg(adev, "node type %llx, id %llx, offset %llx\n", type,
		     logic_id, cmn_node_offset);

	if (!test_bit(0, anode->record_implemented))
		clear_bit(logic_id, cmn_node->record_implemented);

	if (!test_bit(0, anode->status_reporting))
		clear_bit(logic_id, cmn_node->status_reporting);

	record = &cmn_node->records[logic_id];
	record->name =
		devm_kasprintf(adev->dev, GFP_KERNEL, "record%lld", logic_id);
	if (!record->name)
		return -ENOMEM;
	record->regs_base = devm_ioremap(
		adev->dev, (resource_size_t)anode->interface_hdr->address,
		sizeof(struct ras_ext_regs));
	if (!record->regs_base)
		return -ENOMEM;
	record->addressing_mode = test_bit(0, anode->addressing_mode);
	record->node = cmn_node;
	record->index = logic_id;
	record->access = &aest_access[anode->interface_hdr->type];

	vendor_data = devm_kzalloc(adev->dev, sizeof(struct cmn_vendor_data),
				   GFP_KERNEL);
	vendor_data->node_type = type;
	vendor_data->node_id = node_id;
	vendor_data->logic_id = logic_id;

	record->vendor_data = vendor_data;
	record->vendor_data_size = sizeof(struct cmn_vendor_data);

	aest_record_dbg(record, "base %llx\n", anode->interface_hdr->address);

	return 0;
}

// reorgnize cmn node
static int aest_cmn_probe(struct aest_device *adev, struct aest_hnode *ahnode)
{
	acpi_status status;
	u64 base;
	int ret = 0;
	struct acpi_aest_node *anode;
	char name[9];

	anode = list_first_entry(&ahnode->list, struct acpi_aest_node, list);
	if (!anode)
		return -ENODEV;

	if (!cmn_dev_array[anode->vendor->acpi_uid]) {
		snprintf(name, 9, "%s", anode->vendor->acpi_hid);
		status = acpi_get_devices(name, aest_cmn_get_dev_by_uid,
					  &anode->vendor->acpi_uid, NULL);
		if (ACPI_FAILURE(status)) {
			aest_dev_err(adev, "Can not find base\n");
			return_ACPI_STATUS(status);
		}
	}
	base = cmn_dev_array[anode->vendor->acpi_uid];
	if (!base) {
		aest_dev_err(adev, "Device base invalid\n");
		return -ENODEV;
	}

	adev->type = anode->type;
	adev->node_cnt = cmn_config->dev_num;
	adev->nodes = devm_kcalloc(adev->dev, adev->node_cnt,
				   sizeof(struct aest_node), GFP_KERNEL);
	if (!adev->nodes)
		return -ENOMEM;
	aest_set_name(adev, ahnode);

	list_for_each_entry(anode, &ahnode->list, list) {
		ret = aest_cmn_reorgnize_node(adev, anode, base);
		if (ret)
			return ret;
	}

	return 0;
}

int aest_cmn700_probe(struct aest_device *adev, struct aest_hnode *ahnode)
{
	cmn_config = &cmn700_config;

	return aest_cmn_probe(adev, ahnode);
}
