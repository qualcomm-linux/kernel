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

struct dcp_dpu_kms;

struct dcp_drv {
	struct virtio_device	*vdev;
	struct platform_device	*dcp_subsystem_dev;
	struct device_dma_parameters dcp_subsystem_dma_parms;
	struct device_node	*dcp_subsystem_node;
	struct dcp_hfi		*hfi;
	struct dcp_dpu_kms	*kms;
	struct dcp_caps		 caps;
	struct notifier_block	 panic_notifier;
};

#endif /* __DCP_DRV_H__ */
