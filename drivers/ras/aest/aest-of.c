// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/xarray.h>
#include <linux/acpi_aest.h>

#undef pr_fmt
#define pr_fmt(fmt) "DT AEST: " fmt

struct dt_aest_priv {
	struct xarray aest_array;
	u32 node_id;
};

static const struct of_device_id dt_aest_child_match[] = {
	{ .compatible = "arm,aest-processor", .data = (void *)ACPI_AEST_PROCESSOR_ERROR_NODE },
	{ .compatible = "arm,aest-memory",    .data = (void *)ACPI_AEST_MEMORY_ERROR_NODE    },
	{ .compatible = "arm,aest-smmu",      .data = (void *)ACPI_AEST_SMMU_ERROR_NODE      },
	{ .compatible = "arm,aest-vendor",    .data = (void *)ACPI_AEST_VENDOR_ERROR_NODE    },
	{ .compatible = "arm,aest-gic",       .data = (void *)ACPI_AEST_GIC_ERROR_NODE       },
	{ .compatible = "arm,aest-pcie",      .data = (void *)ACPI_AEST_PCIE_ERROR_NODE      },
	{ .compatible = "arm,aest-proxy",     .data = (void *)ACPI_AEST_PROXY_ERROR_NODE     },
	{ }
};

static int dt_aest_node_type(struct device_node *np)
{
	const struct of_device_id *match;

	match = of_match_node(dt_aest_child_match, np);
	if (!match) {
		pr_warn("unknown compatible for %pOF\n", np);
		return -EINVAL;
	}
	return (int)(uintptr_t)match->data;
}

static struct aest_hnode *dt_aest_alloc_hnode(int node_type, u32 id)
{
	struct aest_hnode *ahnode;

	ahnode = kzalloc(sizeof(*ahnode), GFP_KERNEL);
	if (!ahnode)
		return NULL;

	INIT_LIST_HEAD(&ahnode->list);
	ahnode->count = 0;
	ahnode->id    = id;
	ahnode->type  = node_type;
	return ahnode;
}

static int dt_aest_build_interface(struct device_node *np,
				   struct acpi_aest_node *anode)
{
	struct acpi_aest_node_interface_header  *hdr;
	struct acpi_aest_node_interface_common  *common;
	struct resource res;
	struct resource named_res;
	u32 gfmt = 0, flags = 0, nrec = 1;
	u32 itype;
	int ret;
	size_t body_sz;

	/*
	 * Deduce interface type from the presence and count of reg entries:
	 *   no reg  -> system-register access (type 0)
	 *   1 range -> memory-mapped access   (type 1)
	 *   2+ ranges -> single-record MMIO   (type 2)
	 */
	if (!of_property_present(np, "reg"))
		itype = ACPI_AEST_NODE_SYSTEM_REGISTER;
	else if (of_property_count_elems_of_size(np, "reg", sizeof(u32)) <=
		 (of_n_addr_cells(np) + of_n_size_cells(np)))
		itype = ACPI_AEST_NODE_MEMORY_MAPPED;
	else
		itype = ACPI_AEST_NODE_SINGLE_RECORD_MEMORY_MAPPED;

	of_property_read_u32(np, "arm,group-format",    &gfmt);
	of_property_read_u32(np, "arm,interface-flags", &flags);
	of_property_read_u32(np, "arm,num-records",     &nrec);

	switch (gfmt) {
	case ACPI_AEST_NODE_GROUP_FORMAT_16K:
		body_sz = sizeof(struct acpi_aest_node_interface_16k);
		break;
	case ACPI_AEST_NODE_GROUP_FORMAT_64K:
		body_sz = sizeof(struct acpi_aest_node_interface_64k);
		break;
	default:
		body_sz = sizeof(struct acpi_aest_node_interface_4k);
		break;
	}

	hdr = kzalloc(sizeof(*hdr) + body_sz, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	/* Fill header */
	hdr->type         = (u8)itype;
	hdr->group_format = (u8)gfmt;
	hdr->flags        = flags;
	hdr->error_record_count = nrec;
	hdr->error_record_index = 0;

	if (itype != ACPI_AEST_NODE_SYSTEM_REGISTER) {
		ret = of_address_to_resource(np, 0, &res);
		if (ret) {
			pr_err("node %pOF: missing 'reg' for MMIO interface\n", np);
			kfree(hdr);
			return ret;
		}
		hdr->address = res.start;
	}

	switch (gfmt) {
	case ACPI_AEST_NODE_GROUP_FORMAT_4K: {
		struct acpi_aest_node_interface_4k *b =
			(struct acpi_aest_node_interface_4k *)(hdr + 1);
		of_property_read_u64(np, "arm,record-impl",
				     &b->error_record_implemented);
		of_property_read_u64(np, "arm,status-reporting",
				     &b->error_status_reporting);
		of_property_read_u64(np, "arm,addressing-mode",
				     &b->addressing_mode);
		common = &b->common;
		anode->record_implemented =
			(unsigned long *)&b->error_record_implemented;
		anode->status_reporting =
			(unsigned long *)&b->error_status_reporting;
		anode->addressing_mode =
			(unsigned long *)&b->addressing_mode;
		break;
	}
	case ACPI_AEST_NODE_GROUP_FORMAT_16K: {
		struct acpi_aest_node_interface_16k *b =
			(struct acpi_aest_node_interface_16k *)(hdr + 1);
		of_property_read_u64_array(np, "arm,record-impl",
					   b->error_record_implemented, 4);
		of_property_read_u64_array(np, "arm,status-reporting",
					   b->error_status_reporting, 4);
		of_property_read_u64_array(np, "arm,addressing-mode",
					   b->addressing_mode, 4);
		common = &b->common;
		anode->record_implemented =
			(unsigned long *)b->error_record_implemented;
		anode->status_reporting =
			(unsigned long *)b->error_status_reporting;
		anode->addressing_mode =
			(unsigned long *)b->addressing_mode;
		break;
	}
	case ACPI_AEST_NODE_GROUP_FORMAT_64K: {
		struct acpi_aest_node_interface_64k *b =
			(struct acpi_aest_node_interface_64k *)(hdr + 1);
		of_property_read_u64_array(np, "arm,record-impl",
					   b->error_record_implemented, 14);
		of_property_read_u64_array(np, "arm,status-reporting",
					   b->error_status_reporting, 14);
		of_property_read_u64_array(np, "arm,addressing-mode",
					   b->addressing_mode, 14);
		common = &b->common;
		anode->record_implemented =
			(unsigned long *)b->error_record_implemented;
		anode->status_reporting =
			(unsigned long *)b->error_status_reporting;
		anode->addressing_mode =
			(unsigned long *)b->addressing_mode;
		break;
	}
	default:
		pr_err("node %pOF: unsupported group-format %u\n", np, gfmt);
		kfree(hdr);
		return -EINVAL;
	}

	if (!of_address_to_resource(np, of_property_match_string
				    (np, "reg-names", "fault-inject"), &named_res))

		common->fault_inject_register_base = named_res.start;

	if (!of_address_to_resource(np, of_property_match_string
				    (np, "reg-names", "err-group"), &named_res))
		common->error_group_register_base = named_res.start;

	if (!of_address_to_resource(np, of_property_match_string
				    (np, "reg-names", "irq-config"), &named_res))
		common->interrupt_config_register_base = named_res.start;

	anode->interface_hdr = hdr;
	anode->common        = common;

	return 0;
}

static int dt_aest_build_interrupt(struct device_node *np,
				   struct acpi_aest_node *anode)
{
	struct acpi_aest_node_interrupt_v2 *irq_arr;
	int fhi_irq, eri_irq, count = 0;
	u32 fhi_flags = 0, eri_flags = 0;

	of_property_read_u32(np, "arm,fhi-flags", &fhi_flags);
	of_property_read_u32(np, "arm,eri-flags", &eri_flags);

	fhi_irq = of_irq_get_byname(np, "fhi");
	if (fhi_irq == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (fhi_irq < 0 && fhi_irq != -EINVAL) {
		const char *name = NULL;

		of_property_read_string(np, "interrupt-names", &name);

		pr_warn("node %pOF: failed to map FHI IRQ: %d (interrupt-names[0]=\"%s\", want \"%s\")\n",
			np, fhi_irq, name ?: "<missing>", "fhi");
	}
	eri_irq = of_irq_get_byname(np, "eri");
	if (eri_irq == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (eri_irq < 0 && eri_irq != -EINVAL) {
		const char *name = NULL;

		of_property_read_string_index(np, "interrupt-names", 1, &name);

		pr_warn("node %pOF: failed to map ERI IRQ: %d (interrupt-names[1]=\"%s\", want \"%s\")\n",
			np, eri_irq, name ?: "<missing>", "eri");
	}

	if (fhi_irq > 0)
		count++;
	if (eri_irq > 0)
		count++;

	if (!count) {
		anode->interrupt       = NULL;
		anode->interrupt_count = 0;
		return 0;
	}

	irq_arr = kcalloc(count, sizeof(*irq_arr), GFP_KERNEL);
	if (!irq_arr)
		return -ENOMEM;

	count = 0;
	if (fhi_irq > 0) {
		irq_arr[count].gsiv  = fhi_irq;
		irq_arr[count].flags = AEST_INTERRUPT_MODE | fhi_flags;
		irq_arr[count].type  = ACPI_AEST_NODE_FAULT_HANDLING;
		count++;
	}
	if (eri_irq > 0) {
		irq_arr[count].gsiv  = eri_irq;
		irq_arr[count].flags = eri_flags;
		irq_arr[count].type  = ACPI_AEST_NODE_ERROR_RECOVERY;
		count++;
	}

	anode->interrupt       = irq_arr;
	anode->interrupt_count = count;
	return 0;
}

static int dt_aest_build_node_specific(struct device_node *np,
				       struct acpi_aest_node *anode,
				       int node_type)
{
	switch (node_type) {
	case ACPI_AEST_PROCESSOR_ERROR_NODE: {
		struct acpi_aest_processor *proc;
		u32 rtype = 0, pflags = 0;

		proc = kzalloc(sizeof(*proc), GFP_KERNEL);
		if (!proc)
			return -ENOMEM;

		of_property_read_u32(np, "arm,resource-type", &rtype);
		of_property_read_u32(np, "arm,processor-flags", &pflags);

		proc->resource_type   = (u8)rtype;
		proc->flags           = (u8)pflags;

		/* Processor cache/TLB/generic sub-structure */
		switch (rtype) {
		case ACPI_AEST_CACHE_RESOURCE: {
			struct acpi_aest_processor_cache *c;
			struct device_node *cache_np;

			c = kzalloc(sizeof(*c), GFP_KERNEL);
			if (!c) {
				kfree(proc);
				return -ENOMEM;
			}

			cache_np = of_parse_phandle(np, "arm,cache-ref", 0);
			if (cache_np) {
				c->cache_reference = cache_np->phandle;
				of_node_put(cache_np);
			}
			anode->cache = c;
			break;
		}
		case ACPI_AEST_TLB_RESOURCE: {
			struct acpi_aest_processor_tlb *t;

			t = kzalloc(sizeof(*t), GFP_KERNEL);
			if (!t) {
				kfree(proc);
				return -ENOMEM;
			}
			of_property_read_u32(np, "arm,tlb-level",
					     &t->tlb_level);
			anode->tlb = t;
			break;
		}
		default: {
			struct acpi_aest_processor_generic *g;

			g = kzalloc(sizeof(*g), GFP_KERNEL);
			if (!g) {
				kfree(proc);
				return -ENOMEM;
			}
			of_property_read_u32(np, "arm,resource-ref",
					     &g->resource);
			anode->generic = g;
			break;
		}
		}
		anode->processor = proc;
		break;
	}

	case ACPI_AEST_MEMORY_ERROR_NODE: {
		struct acpi_aest_memory *mem;

		mem = kzalloc(sizeof(*mem), GFP_KERNEL);

		if (!mem)
			return -ENOMEM;
		of_property_read_u32(np, "arm,proximity-domain",
				     &mem->srat_proximity_domain);
		anode->memory = mem;
		break;
	}

	case ACPI_AEST_SMMU_ERROR_NODE: {
		struct acpi_aest_smmu *smmu;
		struct device_node *smmu_np;

		smmu = kzalloc(sizeof(*smmu), GFP_KERNEL);

		if (!smmu)
			return -ENOMEM;
		smmu_np = of_parse_phandle(np, "arm,smmu-ref", 0);
		if (smmu_np) {
			/* Use the DT node offset as the IORT reference */
			smmu->iort_node_reference = smmu_np->phandle;
			of_node_put(smmu_np);
		}
		of_property_read_u32(np, "arm,smmu-subcomponent",
				     &smmu->subcomponent_reference);
		anode->smmu = smmu;
		break;
	}

	case ACPI_AEST_VENDOR_ERROR_NODE: {
		struct acpi_aest_vendor_v2 *vendor;
		const char *hid = "ARMHC000";

		vendor = kzalloc(sizeof(*vendor), GFP_KERNEL);

		if (!vendor)
			return -ENOMEM;
		of_property_read_string(np, "arm,vendor-hid", &hid);
		strscpy(vendor->acpi_hid, hid, sizeof(vendor->acpi_hid));
		of_property_read_u32(np, "arm,vendor-uid",
				     &vendor->acpi_uid);
		anode->vendor = vendor;
		break;
	}

	case ACPI_AEST_GIC_ERROR_NODE: {
		struct acpi_aest_gic *gic;

		gic = kzalloc(sizeof(*gic), GFP_KERNEL);

		if (!gic)
			return -ENOMEM;
		of_property_read_u32(np, "arm,gic-type",
				     &gic->interface_type);
		of_property_read_u32(np, "arm,gic-instance",
				     &gic->instance_id);
		anode->gic = gic;
		break;
	}

	case ACPI_AEST_PCIE_ERROR_NODE: {
		struct acpi_aest_pcie *pcie;

		pcie = kzalloc(sizeof(*pcie), GFP_KERNEL);

		if (!pcie)
			return -ENOMEM;
		of_property_read_u32(np, "arm,pcie-segment",
				     &pcie->iort_node_reference);
		anode->pcie = pcie;
		break;
	}

	case ACPI_AEST_PROXY_ERROR_NODE:
		/* No node-specific data for proxy nodes */
		anode->spec_pointer = NULL;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static struct acpi_aest_node *
dt_aest_alloc_anode(struct device_node *np, int node_type)
{
	struct acpi_aest_node *anode;
	int ret;

	anode = kzalloc(sizeof(*anode), GFP_KERNEL);
	if (!anode)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&anode->list);
	anode->type = node_type;

	ret = dt_aest_build_interface(np, anode);
	if (ret)
		goto err_free;

	ret = dt_aest_build_node_specific(np, anode, node_type);
	if (ret)
		goto err_free;

	ret = dt_aest_build_interrupt(np, anode);
	if (ret)
		goto err_free;

	return anode;

err_free:
	kfree(anode->interface_hdr);
	kfree(anode->spec_pointer);
	kfree(anode->processor_spec_pointer);
	kfree(anode);
	return ERR_PTR(ret);
}

static int dt_aest_init_one_node(struct device_node *np,
				 struct dt_aest_priv *priv)
{
	int node_type;
	struct aest_hnode *ahnode;
	struct acpi_aest_node *anode;

	node_type = dt_aest_node_type(np);
	if (node_type < 0) {
		pr_warn("unknown node type for %pOF, skipping\n", np);
		return 0;
	}

	ahnode = dt_aest_alloc_hnode(node_type, priv->node_id);
	if (!ahnode)
		return -ENOMEM;

	anode = dt_aest_alloc_anode(np, node_type);
	if (IS_ERR(anode)) {
		kfree(ahnode);
		return PTR_ERR(anode);
	}

	list_add_tail(&anode->list, &ahnode->list);
	ahnode->count = 1;

	if (xa_err(xa_store(&priv->aest_array, priv->node_id,
			    ahnode, GFP_KERNEL))) {
		kfree(anode);
		kfree(ahnode);
		return -ENOMEM;
	}
	priv->node_id++;
	return 0;
}

static int dt_aest_init_nodes(struct device_node *aest_root,
			      struct dt_aest_priv *priv)
{
	struct device_node *np;
	int ret;

	for_each_available_child_of_node(aest_root, np) {
		ret = dt_aest_init_one_node(np, priv);
		if (ret) {
			pr_err("failed to init node %pOF: %d\n", np, ret);
			of_node_put(np);
			return ret;
		}
	}
	return 0;
}

static struct platform_device *dt_aest_alloc_pdev(struct aest_hnode *ahnode,
						  int index)
{
	struct platform_device *pdev;
	struct resource *res;
	struct acpi_aest_node *anode;
	int ret, size, j;
	int irq[AEST_MAX_INTERRUPT_PER_NODE] = { 0 };

	pdev = platform_device_alloc("AEST", index);
	if (!pdev)
		return ERR_PTR(-ENOMEM);

	res = kcalloc(ahnode->count + AEST_MAX_INTERRUPT_PER_NODE,
		      sizeof(*res), GFP_KERNEL);
	if (!res) {
		platform_device_put(pdev);
		return ERR_PTR(-ENOMEM);
	}

	j = 0;
	list_for_each_entry(anode, &ahnode->list, list) {
		if (anode->interface_hdr->type !=
		    ACPI_AEST_NODE_SYSTEM_REGISTER) {
			res[j].name  = AEST_NODE_NAME;
			res[j].start = anode->interface_hdr->address;

			switch (anode->interface_hdr->group_format) {
			case ACPI_AEST_NODE_GROUP_FORMAT_4K:
				size = 4 * KB; break;
			case ACPI_AEST_NODE_GROUP_FORMAT_16K:
				size = 16 * KB; break;
			case ACPI_AEST_NODE_GROUP_FORMAT_64K:
				size = 64 * KB; break;
			default:
				size = 4 * KB;
			}
			res[j].end   = res[j].start + size - 1;
			res[j].flags = IORESOURCE_MEM;
			j++;
		}

		if (anode->interrupt && anode->interrupt_count > 0) {
			int k;

			for (k = 0; k < anode->interrupt_count &&
			     k < AEST_MAX_INTERRUPT_PER_NODE; k++) {
				struct acpi_aest_node_interrupt_v2 *intr =
					&anode->interrupt[k];
				int itype = intr->type;
				int virq  = intr->gsiv;
				struct irq_data *irqd;

				if (!virq)
					continue;
				if (itype >= AEST_MAX_INTERRUPT_PER_NODE)
					continue;
				if (irq[itype] == virq)
					continue;
				irq[itype] = virq;
				/*
				 * aest_config_irq() writes intr->gsiv directly
				 * to the hardware IRQ-config register, so it
				 * must hold the GIC hardware SPI number, not the
				 * Linux virtual IRQ.  Convert here now that we
				 * have the virq in hand; the resource still gets
				 * the virq so devm_request_irq() works correctly.
				 */
				irqd = irq_get_irq_data(virq);
				if (irqd)
					intr->gsiv = irqd->hwirq;

				res[j].name  = (itype == ACPI_AEST_NODE_FAULT_HANDLING)
						? AEST_FHI_NAME : AEST_ERI_NAME;
				res[j].start = virq;
				res[j].end   = virq;
				res[j].flags = IORESOURCE_IRQ;
				j++;
			}
		}
	}

	ret = platform_device_add_resources(pdev, res, j);
	kfree(res);
	if (ret) {
		platform_device_put(pdev);
		return ERR_PTR(ret);
	}

	ret = platform_device_add_data(pdev, &ahnode, sizeof(ahnode));
	if (ret) {
		platform_device_put(pdev);
		return ERR_PTR(ret);
	}

	ret = platform_device_add(pdev);
	if (ret) {
		platform_device_put(pdev);
		return ERR_PTR(ret);
	}

	return pdev;
}

static int dt_aest_alloc_pdevs(struct dt_aest_priv *priv)
{
	struct aest_hnode *ahnode;
	unsigned long i;
	int ret = 0, index = 0;

	xa_for_each(&priv->aest_array, i, ahnode) {
		struct platform_device *pdev =
			dt_aest_alloc_pdev(ahnode, index++);
		if (IS_ERR(pdev)) {
			ret = PTR_ERR(pdev);
			pr_err("failed to alloc pdev for node %u: %d\n",
			       ahnode->id, ret);
			break;
		}
	}
	return ret;
}

static int __init dt_aest_init(void)
{
	struct device_node *aest_root;
	struct dt_aest_priv priv = {};
	int ret;

	if (!acpi_disabled)
		return 0;

	aest_root = of_find_compatible_node(NULL, NULL, "arm,aest");
	if (!aest_root)
		return 0;

	xa_init(&priv.aest_array);

	ret = dt_aest_init_nodes(aest_root, &priv);
	of_node_put(aest_root);
	if (ret) {
		pr_err("failed to init AEST nodes: %d\n", ret);
		return ret;
	}

	ret = dt_aest_alloc_pdevs(&priv);
	if (ret) {
		pr_err("failed to alloc AEST pdevs: %d\n", ret);
		return ret;
	}

	pr_info("registered %u AEST error source(s) from DT\n", priv.node_id);

	return 0;
}
subsys_initcall_sync(dt_aest_init);
