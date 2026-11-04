// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * DCP VirtIO driver.
 */

#include <linux/component.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/panic_notifier.h>
#include <linux/pm_runtime.h>

#define DCP_DPU_AUTOSUSPEND_DELAY_MS	5000

#include "dcp_drv.h"
#include "dcp_dpu_kms.h"
#include "dcp_hfi.h"
#include "dcp_ipcc.h"
#include "dcp_virtio.h"
#include "msm_drv.h"
#include "msm_kms.h"

int dcp_subsystem_dev_init(struct dcp_drv *drv)
{
	struct virtio_device *vdev = drv->vdev;
	struct device_node *dcp_subsystem_node;
	struct platform_device *pdev;
	struct device *dev;
	char busid[32];
	bool iommu_mapped;
	int ret;

	dcp_subsystem_node = of_node_get(vdev->dev.of_node);
	if (!dcp_subsystem_node) {
		dev_err(&vdev->dev, "no DCP subsystem DT node\n");
		return -ENODEV;
	}

	snprintf(busid, sizeof(busid), "dcp-subsystem.%s", dev_name(vdev->dev.parent));

	/*
	 * DCP is discovered through VirtIO, but display/HFI buffers must be
	 * mapped through the dcp-subsystem node's DMA/IOMMU identity. Create a
	 * platform device for that DT node so DMA API and msm_kms_init_vm() use
	 * the display function's IOMMU domain.
	 */
	pdev = of_platform_device_create(dcp_subsystem_node, busid, vdev->dev.parent);
	if (!pdev) {
		dev_err(&vdev->dev, "dcp_subsystem_dev creation failed\n");
		of_node_put(dcp_subsystem_node);
		return -ENODEV;
	}
	dev = &pdev->dev;

	dev->dma_parms = &drv->dcp_subsystem_dma_parms;
	dma_set_max_seg_size(dev, UINT_MAX);

	ret = of_dma_configure_id(dev, dcp_subsystem_node, true, NULL);
	if (ret) {
		dev_err(dev, "of_dma_configure_id failed: %d\n", ret);
		goto err_destroy;
	}

	iommu_mapped = device_iommu_mapped(dev);
	if (!iommu_mapped) {
		dev_err(dev, "no IOMMU group attached\n");
		ret = -ENODEV;
		goto err_destroy;
	}

	dev_dbg(dev, "dcp-subsystem: iommu_mapped=%d iommu_group=%d\n",
		iommu_mapped,
		dev->iommu_group ? iommu_group_id(dev->iommu_group) : -1);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, DCP_DPU_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);

	drv->dcp_subsystem_dev = pdev;
	drv->dcp_subsystem_node = dcp_subsystem_node;

	return 0;

err_destroy:
	of_platform_device_destroy(dev, NULL);
	of_node_put(dcp_subsystem_node);
	return ret;
}

void dcp_subsystem_dev_deinit(struct dcp_drv *drv)
{
	if (!drv->dcp_subsystem_dev)
		return;

	pm_runtime_disable(&drv->dcp_subsystem_dev->dev);
	of_platform_device_destroy(&drv->dcp_subsystem_dev->dev, NULL);
	of_node_put(drv->dcp_subsystem_node);
	drv->dcp_subsystem_dev = NULL;
}

int dcp_pm_prepare(struct device *vdev_dev)
{
	struct dcp_drv *drv = vdev_dev ?
		dev_to_virtio(vdev_dev)->priv : NULL;

	if (!drv || !drv->dcp_subsystem_dev)
		return 0;

	return msm_kms_pm_prepare(&drv->dcp_subsystem_dev->dev);
}

void dcp_pm_complete(struct device *vdev_dev)
{
	struct dcp_drv *drv = vdev_dev ?
		dev_to_virtio(vdev_dev)->priv : NULL;

	if (!drv || !drv->dcp_subsystem_dev)
		return;

	msm_kms_pm_complete(&drv->dcp_subsystem_dev->dev);
}

int dcp_component_register(struct dcp_drv *drv)
{
	struct device *dev = &drv->dcp_subsystem_dev->dev;
	struct component_match *match = NULL;
	int ret;

	ret = dcp_dpu_kms_init_dsi_bridges(drv->kms, &match);
	if (ret) {
		dev_err(dev, "DSI host initialization failed: %d\n", ret);
		dcp_dpu_kms_cleanup_bridges(drv->kms);
		return ret;
	}

	/* A GPU is optional for the DCP display-only DRM device. */
	msm_add_gpu_components(dev, &match);

	ret = component_master_add_with_match(dev, &msm_drm_ops, match);
	if (ret) {
		dev_err(dev, "component_master_add_with_match failed: %d\n",
			ret);
		dcp_dpu_kms_cleanup_bridges(drv->kms);
		return ret;
	}

	return 0;
}

void dcp_component_unregister(struct dcp_drv *drv)
{
	struct device *dev = &drv->dcp_subsystem_dev->dev;

	component_master_del(dev, &msm_drm_ops);
	dcp_dpu_kms_cleanup_bridges(drv->kms);
}

static int dcp_panic_notifier_cb(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct dcp_drv *drv = container_of(nb, struct dcp_drv, panic_notifier);

	if (drv->hfi)
		atomic_set(&drv->hfi->shutdown_in_progress, 1);

	return NOTIFY_OK;
}

void dcp_panic_notifier_register(struct dcp_drv *drv)
{
	drv->panic_notifier.notifier_call = dcp_panic_notifier_cb;
	atomic_notifier_chain_register(&panic_notifier_list, &drv->panic_notifier);
}

void dcp_panic_notifier_unregister(struct dcp_drv *drv)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &drv->panic_notifier);
}

void __init msm_dcp_register(void)
{
	dcp_virtio_register();
}

void __exit msm_dcp_unregister(void)
{
	dcp_virtio_unregister();
}
