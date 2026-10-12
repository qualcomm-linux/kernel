/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides constants for the Arm Error Source Table (AEST)
 * DT binding (Documentation/devicetree/bindings/arm/arm,aest.yaml).
 */

#ifndef _DT_BINDINGS_ARM_AEST_H
#define _DT_BINDINGS_ARM_AEST_H

/* arm,interface-flags - AEST node interface flags field */
#define AEST_XFACE_SHARED		1
#define AEST_XFACE_CLEAR_MISC		2
#define AEST_XFACE_ERROR_DEVICE		4
#define AEST_XFACE_AFFINITY		8
#define AEST_XFACE_ERROR_GROUP		16
#define AEST_XFACE_FAULT_INJECT		32
#define AEST_XFACE_INT_CONFIG		64

/* arm,fhi-flags / arm,eri-flags - AEST node interrupt flags field */
#define AEST_IRQ_MODE_LEVEL		0
#define AEST_IRQ_MODE_EDGE		1

/* arm,processor-flags - AEST processor node flags field */
#define AEST_PROC_GLOBAL		1
#define AEST_PROC_SHARED		2

/* arm,group-format - error record group register window page size */
#define AEST_GROUP_FORMAT_4K		0
#define AEST_GROUP_FORMAT_16K		1
#define AEST_GROUP_FORMAT_64K		2

/* arm,resource-type - processor resource type */
#define AEST_RESOURCE_CACHE		0
#define AEST_RESOURCE_TLB		1
#define AEST_RESOURCE_GENERIC		2

/* arm,gic-type - GIC component type */
#define AEST_GIC_CPU			0
#define AEST_GIC_DISTRIBUTOR		1
#define AEST_GIC_REDISTRIBUTOR		2
#define AEST_GIC_ITS			3

#endif /* _DT_BINDINGS_ARM_AEST_H */
