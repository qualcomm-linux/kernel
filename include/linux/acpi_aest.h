/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ACPI_AEST_H__
#define __ACPI_AEST_H__

#include <asm/ras.h>
#include <linux/acpi.h>

/* AEST resource name */
#define AEST_NODE_NAME "AEST:NODE"
#define AEST_FHI_NAME "AEST:FHI"
#define AEST_ERI_NAME "AEST:ERI"

/* AEST component */
#define ACPI_AEST_PROC_FLAG_GLOBAL	(1<<0)
#define ACPI_AEST_PROC_FLAG_SHARED	(1<<1)

#define AEST_ADDREESS_SPA	0
#define AEST_ADDREESS_LA	1

/* AEST interrupt */
#define AEST_INTERRUPT_MODE BIT(0)

#define AEST_INTERRUPT_FHI_UE_SUPPORT		BIT(0)
#define AEST_INTERRUPT_FHI_UE_NO_SUPPORT		BIT(1)

#define AEST_MAX_INTERRUPT_PER_NODE 2

/* AEST interface */
#define AEST_XFACE_FLAG_SHARED (1 << 0)
#define AEST_XFACE_FLAG_CLEAR_MISC (1 << 1)
#define AEST_XFACE_FLAG_ERROR_DEVICE (1 << 2)
#define AEST_XFACE_FLAG_AFFINITY (1 << 3)
#define AEST_XFACE_FLAG_ERROR_GROUP (1 << 4)
#define AEST_XFACE_FLAG_FAULT_INJECT (1 << 5)
#define AEST_XFACE_FLAG_INT_CONFIG (1 << 6)

#define KB 1024
#define MB (1024 * KB)
#define GB (1024 * MB)

struct aest_hnode {
	struct list_head list;
	int count;
	u32 id;
	int type;
};

struct acpi_aest_node {
	struct list_head list;
	int type;
	struct acpi_aest_node_interface_header *interface_hdr;
	unsigned long *record_implemented;
	unsigned long *status_reporting;
	unsigned long *addressing_mode;
	struct acpi_aest_node_interface_common *common;
	union {
		struct acpi_aest_processor *processor;
		struct acpi_aest_memory *memory;
		struct acpi_aest_smmu *smmu;
		struct acpi_aest_vendor_v2 *vendor;
		struct acpi_aest_gic *gic;
		struct acpi_aest_pcie *pcie;
		struct acpi_aest_proxy *proxy;
		void *spec_pointer;
	};
	union {
		struct acpi_aest_processor_cache *cache;
		struct acpi_aest_processor_tlb *tlb;
		struct acpi_aest_processor_generic *generic;
		void *processor_spec_pointer;
	};
	struct acpi_aest_node_interrupt_v2 *interrupt;
	int interrupt_count;
};
#endif /* __ACPI_AEST_H__ */
