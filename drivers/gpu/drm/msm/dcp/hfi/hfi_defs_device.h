/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_DEVICE_H__
#define __H_HFI_DEFS_DEVICE_H__

#include "hfi_defs_common.h"

/* Hotplug state for dynamic display interfaces. */
struct hfi_device_hotplug_config {
	u32 orientation;
	u32 port_index;
	u32 pin_config;
	u32 hpd_state;
	u32 hpd_irq;
};

/* Display resource ownership state. */
enum hfi_device_res_state {
	HFI_DEVICE_RESOURCE_ACQUIRE = 0x0,
	HFI_DEVICE_RESOURCE_RELEASE = 0x1,
	HFI_DEVICE_RESOURCE_SHARED  = 0x2,
	HFI_DEVICE_RESOURCE_PRE_RELEASE = 0x3,
};

/* Virtualized display-resource types. */
enum hfi_device_resource_type {
	HFI_DEVICE_RESOURCE_DISPLAY,
};

/* Virtualized display-resource configuration. */
struct hfi_device_resource_config {
	u32 disp_mask;
	enum hfi_device_res_state vm_state;
	u32 resource_type;
	u32 reserved[2];
};

/* Subsystem-restart event. */
enum hfi_device_ssr_event {
	HFI_DEVICE_SSR_EVENT_START  = 0,
	HFI_DEVICE_SSR_EVENT_END  = 1,
};

/* Subsystem-restart information. */
struct hfi_device_ssr_info {
	enum hfi_subsystem_type subsystem_type;
	enum hfi_device_ssr_event ssr_event;
	u32 reserved1;
	u32 reserved2;
};

/* Active layer mask for a display. */
struct hfi_device_active_layers_info {
	u32 display_id;
	u32 layers_mask;
};

#endif /* __H_HFI_DEFS_DEVICE_H__ */
