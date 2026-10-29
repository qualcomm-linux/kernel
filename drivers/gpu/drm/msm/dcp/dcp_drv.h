/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __DCP_DRV_H__
#define __DCP_DRV_H__

#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>

#include "dcp_hfi.h"
#include "dcp_ipcc.h"

struct dcp_dpu_kms;
struct component_match;

struct dcp_drv {
	struct virtio_device	*vdev;
	struct platform_device	*dcp_subsystem_dev;
	struct device_dma_parameters dcp_subsystem_dma_parms;
	struct device_node	*dcp_subsystem_node;
	struct dcp_hfi		*hfi;
	struct dcp_dpu_kms	*kms;
	struct dcp_caps		 caps;
	struct notifier_block	 panic_notifier;
	struct dcp_ipcc		*ipcc;
};

int dcp_subsystem_dev_init(struct dcp_drv *drv);
void dcp_subsystem_dev_deinit(struct dcp_drv *drv);

int dcp_component_register(struct dcp_drv *drv);
void dcp_component_unregister(struct dcp_drv *drv);

int dcp_pm_prepare(struct device *vdev_dev);
void dcp_pm_complete(struct device *vdev_dev);

void dcp_panic_notifier_register(struct dcp_drv *drv);
void dcp_panic_notifier_unregister(struct dcp_drv *drv);

void __init msm_dcp_register(void);
void __exit msm_dcp_unregister(void);

#endif /* __DCP_DRV_H__ */
