// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table CMN-700 Support
 *
 * Copyright (c) 2025, Alibaba Inc
 *
 * CMN-700 exposes 6 RAS-relevant device types (HN-I, HN-F, XP, SBSX,
 * RN-D/CXRA, MTSX). Each device type owns an error group register set
 * holding a set of error records.
 *
 * CMN uses the memory-mapped single-record view, so every AEST node
 * corresponds to exactly one CMN error record - a single mesh can
 * yield hundreds of AEST entries. Per Arm ACPI Spec[1] §2.6.3.4 the
 * device type is recovered from the AEST vendor-specific data. This
 * driver enumerates every CMN AEST entry, reads the CMN node-info
 * register and stitches all entries of the same type into one
 * aggregate ras_node carrying many ras_records (one per logic_id).
 *
 * Each CMN instance owns its own error interrupt. The shared FHI/ERI
 * lines are registered per ras_node with IRQF_SHARED, so every
 * per-type handler runs and locates the offending record by walking
 * the error group registers - mirroring CMN Spec[2] §3.8.
 *
 * The CMN RAS topology is:
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
 *                  -->|RND |     ......   (also MTSX)
 *                     +----+
 *
 * All addressing needed to reach the CMN RAS register block, the CMN
 * node-info register and the CMN ERRGSR is taken from AEST.
 *
 *   PERIPHBASE = ERRFR_addr - ERRFR_offset_in_register_block
 *                           - register_block_offset_within_CMN
 *              = record_base - 0x3100 - cmn_node_offset
 *
 * where the CMN-700 record register block places ERRFR at offset 0x3100
 * (CMN-700 TRM[2]). The AEST "arm,node-specific-data" payload carries
 * two u64s used by this driver: [0..7] = hnd_offset (locates the
 * per-type ERRGSR via cmn_config->errgsr_offset()), [8..15] =
 * cmn_node_offset (offset of this node's register block within CMN).
 *
 * Per CMN-700 erratum #2732981, ERRGSR for HN-I / HN-S / SBSX is
 * broken; for those types the per-record status_reporting bit is left
 * set so the core polls the records instead of reading ERRGSR.
 *
 * AEST topology consumed by this driver (see drivers/acpi/arm64/aest.c):
 *
 *   pdev (arm64_ras, dev_name = "cmn700")
 *   ├── primary fwnode  :
 *   └── child swnode x N: per-AEST-entry properties:
 *                            arm,interface-type
 *                            arm,record-base
 *                            arm,node-specific-data[]  (vendor data)
 *
 * Each child swnode corresponds to one AEST node, i.e. one CMN error
 * record identified by (node_type, logic_id).
 *
 * [1] Arm ACPI for Armv8/Armv9: https://developer.arm.com/documentation/den0093/latest
 * [2] CMN-700 TRM (Arm 102308): https://developer.arm.com/documentation/102308/latest
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/unaligned.h>

#include "ras.h"


#define CMN_NODE_INFO			0x0000
#define CMN_NI_NODE_TYPE		GENMASK_ULL(15, 0)
#define CMN_NI_NODE_ID			GENMASK_ULL(31, 16)
#define CMN_NI_LOGICAL_ID		GENMASK_ULL(47, 32)

/* Subset of CMN node types relevant to RAS */
enum cmn_ras_node_type {
	CMN_TYPE_HNI	= 0x4,
	CMN_TYPE_HNF	= 0x5,
	CMN_TYPE_XP	= 0x6,
	CMN_TYPE_SBSX	= 0x7,
	CMN_TYPE_MTSX	= 0x10,
	CMN_TYPE_CXRA	= 0x100,
	CMN_TYPE_CXHA	= 0x101,
	CMN_TYPE_CCHA	= 0x104,
	CMN_TYPE_HNS	= 0x200,
};

/*
 * Offset of ERRFR within the CMN-700 RAS register block.
 * AEST's interface->address points at ERRFR; subtracting this plus the
 * cmn_node_offset (vendor-specific-data[8..15]) yields PERIPHBASE.
 */
#define CMN_ERRFR_OFFSET_IN_REGBLK	0x3100

#define CMN_RAS_DEV_NUM			6
#define CMN700_ERRGSR_NUM		8
#define CMN_ERRGSR_OFFSET		0x3000

struct cmn_vendor_data {
	struct acpi_aest_vendor_v2 vendor;
	int node_type;
	int node_id;
	int logic_id;
};

struct cmn_config {
	int errgsr_num;
	int dev_num;
	const int *node_id_map;
	const char *const *node_name;
	int (*errgsr_mapping)(int errgsr_bit);
	u64 (*errgsr_offset)(u64 hnd_offset, int node_idx);
};

static const char *const cmn700_node_name[] = {
	[CMN_TYPE_HNI]	= "HNI",
	[CMN_TYPE_HNF]	= "HNF",
	[CMN_TYPE_XP]	= "XP",
	[CMN_TYPE_SBSX]	= "SBSX",
	[CMN_TYPE_CXRA]	= "RND",
	[CMN_TYPE_MTSX]	= "MTSX",
};

static const int cmn700_node_id_map[] = {
	[CMN_TYPE_HNI]	= 1,
	[CMN_TYPE_HNF]	= 2,
	[CMN_TYPE_XP]	= 0,
	[CMN_TYPE_SBSX]	= 3,
	[CMN_TYPE_CXRA]	= 4,
	[CMN_TYPE_MTSX]	= 5,
};

static u64 cmn700_errgsr_offset(u64 hnd_offset, int node_idx)
{
	return hnd_offset + CMN_ERRGSR_OFFSET +
	       (node_idx * 2) * CMN700_ERRGSR_NUM * 8;
}

static int cmn700_errgsr_mapping(int errgsr_bit)
{
	return errgsr_bit / 2;
}

static struct cmn_config cmn700_config = {
	.errgsr_num	= CMN700_ERRGSR_NUM,
	.dev_num	= CMN_RAS_DEV_NUM,
	.node_name	= cmn700_node_name,
	.node_id_map	= cmn700_node_id_map,
	.errgsr_mapping	= cmn700_errgsr_mapping,
	.errgsr_offset	= cmn700_errgsr_offset,
};

static struct cmn_config *cmn_config;


static int cmn_init_vendor_data(struct device *dev, struct cmn_vendor_data *vendor_data,
				u64 *errgsr_addr, u64 record_base)
{
	struct acpi_aest_vendor_v2 vendor;
	u64 cmn_node_offset, reg, logic_id, type, node_id;
	u64 hnd_offset, periphbase;
	void __iomem *cmn_node_base;
	struct fwnode_handle *child = dev_fwnode(dev);

	fwnode_property_read_u8_array(child, "arm,node-specific-data",
				       (u8 *)&vendor, sizeof(vendor));

	hnd_offset = get_unaligned_le64(&vendor.vendor_specific_data[0]);
	cmn_node_offset = get_unaligned_le64(&vendor.vendor_specific_data[8]);

	periphbase = record_base - CMN_ERRFR_OFFSET_IN_REGBLK - cmn_node_offset;

	cmn_node_base = devm_ioremap(dev, periphbase + cmn_node_offset +
			    CMN_NODE_INFO, SZ_4K);
	if (!cmn_node_base)
		return -ENOMEM;

	reg = readq_relaxed(cmn_node_base);
	logic_id = FIELD_GET(CMN_NI_LOGICAL_ID, reg);
	type = FIELD_GET(CMN_NI_NODE_TYPE, reg);
	node_id = FIELD_GET(CMN_NI_NODE_ID, reg);

	if (type >= ARRAY_SIZE(cmn700_node_id_map) ||
	    !cmn_config->node_name[type]) {
		dev_dbg(dev, "Skipping unsupported CMN node type %llx\n", type);
		return -ENODEV;
	}

	*errgsr_addr = periphbase + cmn_config->errgsr_offset(hnd_offset,
							      cmn_config->node_id_map[type]);

	vendor_data->vendor = vendor;
	vendor_data->node_type = type;
	vendor_data->node_id = node_id;
	vendor_data->logic_id = logic_id;

	devm_iounmap(dev, cmn_node_base);

	dev_dbg(dev, "periphbase %llx, node_offset %llx, logic_id %llx, type %llx, node_id %llx\n",
		periphbase, cmn_node_offset, logic_id, type, node_id);

	return 0;
}

/*
 * Initialise one ras_node (representing one CMN node *type*, e.g. HN-F).
 * Per CMN-700 erratum #2732981, ERRGSR for HN-I / HN-S / SBSX is broken;
 * AEST conveys this via the per-record "Error group-based status reporting
 * supported" flag (bit0 of arm,status-reporting). When that bit is 0 we
 * leave node->errgsr NULL so the core polls instead of reading ERRGSR.
 */
static int cmn_init_node(struct platform_device *pdev,
			 struct ras_node *cmn_node, u64 type, u64 errgsr_addr)
{
	struct device *dev = &pdev->dev;
	int ret;

	cmn_node->dev = dev;
	cmn_node->type = ACPI_AEST_VENDOR_ERROR_NODE;
	cmn_node->name = devm_kasprintf(dev, GFP_KERNEL, "%s.%llx",
					cmn_config->node_name[type], errgsr_addr);
	if (!cmn_node->name)
		return -ENOMEM;

	/* CMN700 just support version 1 */
	cmn_node->version = 1;
	cmn_node->errgsr = devm_ioremap(dev, errgsr_addr, cmn_config->errgsr_num * 8);
	if (!cmn_node->errgsr)
		return -ENOMEM;

	cmn_node->errgsr_num = cmn_config->errgsr_num;
	cmn_node->errgsr_mapping = cmn_config->errgsr_mapping;
	cmn_node->record_count = cmn_config->errgsr_num * BITS_PER_LONG / 2;
	cmn_node->record_implemented = devm_bitmap_zalloc(
		dev, cmn_node->record_count, GFP_KERNEL);
	if (!cmn_node->record_implemented)
		return -ENOMEM;
	bitmap_set(cmn_node->record_implemented, 0, cmn_node->record_count);

	cmn_node->status_reporting = devm_bitmap_zalloc(
		dev, cmn_node->record_count, GFP_KERNEL);
	if (!cmn_node->status_reporting)
		return -ENOMEM;
	bitmap_set(cmn_node->status_reporting, 0, cmn_node->record_count);
	/* If !errgsr_supported leave bitmap zero so all records are polled. */

	cmn_node->records = devm_kcalloc(dev, cmn_node->record_count,
					 sizeof(struct ras_record), GFP_KERNEL);
	if (!cmn_node->records)
		return -ENOMEM;

	cmn_node->specific_data_size = device_property_count_u8(dev,
								"arm,node-specific-data");
	if (cmn_node->specific_data_size > 0) {
		cmn_node->specific_data = devm_kzalloc(dev, cmn_node->specific_data_size,
						       GFP_KERNEL);
		if (!cmn_node->specific_data)
			return -ENOMEM;
		ret = device_property_read_u8_array(dev, "arm,node-specific-data",
						    cmn_node->specific_data,
						    cmn_node->specific_data_size);
		if (ret)
			return ret;
	}

	ras_node_dbg(cmn_node, "Init with errgsr %llx\n", errgsr_addr);
	return 0;
}

/*
 * Process one AEST record (one child fwnode) and stitch it into the
 * appropriate per-type ras_node. The ras_node is initialised lazily on the
 * first record observed for that type.
 */
static int cmn_init_record(struct platform_device *pdev, struct ras_node *nodes,
			   struct fwnode_handle *child)
{
	struct device *dev = &pdev->dev;
	u64 errgsr_addr, record_base;
	struct cmn_vendor_data *vendor_data;
	struct ras_node *cmn_node;
	struct ras_record *record;
	int ret, node_index;
	u8 interface_type;


	ret = fwnode_property_read_u8(child, "arm,interface-type",
				      &interface_type);
	if (ret)
		return ret;
	if (interface_type != ACPI_AEST_NODE_SINGLE_RECORD_MEMORY_MAPPED) {
		dev_err(dev, "CMN only supports single-record memory mapped\n");
		return -ENODEV;
	}

	ret = fwnode_property_read_u64(child, "arm,record-base",
				       &record_base);
	if (ret)
		return ret;

	vendor_data = devm_kzalloc(dev, sizeof(*vendor_data), GFP_KERNEL);
	if (!vendor_data)
		return -ENOMEM;

	ret = cmn_init_vendor_data(dev, vendor_data, &errgsr_addr, record_base);
	if (ret)
		return ret;

	node_index = cmn_config->node_id_map[vendor_data->node_type];

	cmn_node = &nodes[node_index];
	if (!cmn_node->name) {
		ret = cmn_init_node(pdev, cmn_node, vendor_data->node_type, errgsr_addr);
		if (ret)
			return ret;
	}

	if (vendor_data->logic_id >= cmn_node->record_count) {
		dev_warn(dev, "logic_id %u exceeds record_count %u\n",
			 vendor_data->logic_id, cmn_node->record_count);
		return 0;
	}

	/*
	 * CMN-700 stitches several single-mapping AEST nodes into one
	 * aggregate ras_node, so the record_implemented / status_reporting
	 * bitmaps that ACPI normally provides per group are absent here
	 * and must be populated by the driver: clear the bit at this
	 * record's logic_id slot to mark it implemented (and reporting).
	 */
	clear_bit(vendor_data->logic_id, cmn_node->record_implemented);
	/* CMN-700 erratum #2732981, ERRGSR for HN-I / HN-S / SBSX is broken */
	if (vendor_data->node_type != CMN_TYPE_HNI &&
	    vendor_data->node_type != CMN_TYPE_HNS &&
	    vendor_data->node_type != CMN_TYPE_SBSX)
		clear_bit(vendor_data->logic_id, cmn_node->status_reporting);

	record = &cmn_node->records[vendor_data->logic_id];
	record->name = devm_kasprintf(dev, GFP_KERNEL, "record%d", vendor_data->logic_id);
	if (!record->name)
		return -ENOMEM;
	record->regs_base = devm_ioremap(dev,
					 (resource_size_t)record_base,
					 sizeof(struct ras_ext_regs));
	if (!record->regs_base)
		return -ENOMEM;
	record->node = cmn_node;
	record->index = vendor_data->logic_id;
	record->access = &ras_access[interface_type];

	record->vendor_data = vendor_data;
	record->vendor_data_size = sizeof(*vendor_data);

	ras_record_dbg(record, "base %llx\n", record_base);
	return 0;
}

/*
 * Vendor pdev (CMN) carries one shared fhi/eri pair. Register it on each
 * populated ras_node with IRQF_SHARED so all per-type handlers run, and
 * enable per-record FI/CFI/UI in ERXCTLR via the shared ras_enable_irq.
 */
static int cmn_register_record_irq(struct platform_device *pdev,
				   struct ras_node *nodes)
{
	struct device *dev = &pdev->dev;
	int fhi_irq, eri_irq, i, ret;

	fhi_irq = platform_get_irq_byname_optional(pdev, AEST_FHI_NAME);
	eri_irq = platform_get_irq_byname_optional(pdev, AEST_ERI_NAME);
	if (fhi_irq <= 0 && eri_irq <= 0)
		return 0;

	for (i = 0; i < cmn_config->dev_num; i++) {
		struct ras_node *n = &nodes[i];
		char *desc;

		if (!n->name)		/* slot not used by this CMN */
			continue;

		desc = devm_kasprintf(dev, GFP_KERNEL, "arm64_ras.%s.%s",
				      dev_name(dev), n->name);
		if (!desc)
			return -ENOMEM;

		if (fhi_irq > 0) {
			ret = devm_request_irq(dev, fhi_irq, ras_irq_func,
					       IRQF_SHARED, desc, n);
			if (ret)
				return ret;
			n->irq[0] = fhi_irq;
		}
		if (eri_irq > 0) {
			ret = devm_request_irq(dev, eri_irq, ras_irq_func,
					       IRQF_SHARED, desc, n);
			if (ret)
				return ret;
			n->irq[1] = eri_irq;
		}
	}
	return 0;
}

/* Common entry point: walk every child swnode under @pdev. */
static int cmn_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *child;
	struct ras_node *nodes;
	int ret;

	nodes = devm_kcalloc(dev, cmn_config->dev_num, sizeof(*nodes),
				GFP_KERNEL);
	if (!nodes)
		return -ENOMEM;

	/*
	 * In CMN-700, each AEST node is a single mapping record, so
	 * treat every child fwnode as one record rather than a node
	 * with multiple records underneath.
	 */
	device_for_each_child_node(dev, child) {
		ret = cmn_init_record(pdev, nodes, child);
		if (ret) {
			fwnode_handle_put(child);
			return ret;
		}
	}

	ret = cmn_register_record_irq(pdev, nodes);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, nodes);

	for (int i = 0; i < cmn_config->dev_num; i++) {
		ras_online_node(&nodes[i]);
		ras_node_init_debugfs(&nodes[i]);
	}

	return 0;
}

int ras_cmn700_probe(struct platform_device *pdev)
{
	cmn_config = &cmn700_config;

	dev_set_name(&pdev->dev, "cmn700");

	return cmn_probe(pdev);
}
