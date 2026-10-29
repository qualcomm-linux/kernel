// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * DCP VirtIO device driver.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/slab.h>

#include "dcp_drv.h"
#include "dcp_dpu_kms.h"
#include "dcp_hfi.h"
#include "dcp_ipcc.h"
#include "dcp_virtio.h"
#include "msm_drv.h"
#include "msm_kms.h"

static void dcp_virtio_hfi_cleanup(struct dcp_drv *drv);

static const unsigned int dcp_virtio_features[] = {
	VIRTIO_F_VERSION_1,
	VIRTIO_F_NOTIFICATION_DATA,
	VIRTIO_F_RING_RESET,
	VIRTIO_F_ACCESS_PLATFORM,
};

/*
 * The VirtIO core owns transport discovery. This driver owns HFI setup and
 * creates the DRM component only after firmware capabilities are available.
 */
static int dcp_virtio_probe(struct virtio_device *vdev)
{
	struct dcp_drv *drv;
	struct virtqueue *vqs[DCP_NUM_VQS];
	struct virtqueue_info vqs_info[DCP_NUM_VQS] = {
		[DCP_VQ_CMD]  = { .name = "cmd",  .callback = dcp_hfi_cmd_callback },
		[DCP_VQ_RESP] = { .name = "resp", .callback = dcp_hfi_resp_callback },
	};
	struct dcp_dpu_kms *dcp_kms;
	struct msm_drm_private *priv;
	int ret;

	dev_dbg(&vdev->dev, "DCP VirtIO probe started\n");

	drv = devm_kzalloc(&vdev->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->vdev = vdev;
	vdev->priv = drv;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev, "VIRTIO_F_VERSION_1 not negotiated\n");
		return -ENODEV;
	}

	drv->ipcc = dcp_virtio_get_ipcc(vdev);
	/* Access-platform is required for the firmware-visible IOVA mappings. */
	if (!virtio_has_feature(vdev, VIRTIO_F_ACCESS_PLATFORM)) {
		dev_err(&vdev->dev, "VIRTIO_F_ACCESS_PLATFORM not negotiated\n");
		return -ENODEV;
	}

	ret = dcp_subsystem_dev_init(drv);
	if (ret)
		return ret;

	ret = virtio_find_vqs(vdev, DCP_NUM_VQS, vqs, vqs_info, NULL);
	if (ret) {
		dev_err(&vdev->dev, "virtqueue allocation failed: %d\n", ret);
		goto err_fn_dev;
	}

	ret = dcp_hfi_init(drv, vdev, vqs[DCP_VQ_CMD], vqs[DCP_VQ_RESP],
			   vdev->dev.parent);
	if (ret) {
		dev_err(&vdev->dev, "HFI init failed: %d\n", ret);
		goto err_del_vqs;
	}

	dcp_ipcc_set_resp_vq(drv->ipcc, vqs[DCP_VQ_RESP]);

	/*
	 * Keep the device unready until HFI state and virtqueues are initialized.
	 * KMS registration below depends on synchronous firmware initialization.
	 */
	virtio_device_ready(vdev);
	virtqueue_kick(drv->hfi->resp_vq);

	ret = dcp_hfi_send_device_init(drv->hfi);
	if (ret) {
		dev_err(&vdev->dev, "HFI DEVICE_INIT failed: %d\n", ret);
		goto err_hfi_deinit;
	}

	ret = dcp_hfi_get_capabilities(drv->hfi, &drv->caps);
	if (ret) {
		dev_err(&vdev->dev, "HFI GET_CAPS failed: %d\n", ret);
		goto err_hfi_deinit;
	}

	/* Register the display resources before exposing the DRM component. */
	ret = dcp_hfi_send_resource_register(drv->hfi);
	if (ret) {
		dev_err(&vdev->dev, "HFI RESOURCE_REGISTER failed: %d\n", ret);
		goto err_hfi_deinit;
	}

	dev_dbg(&vdev->dev,
		"DCP caps: %u displays, max %ux%u, %u planes\n",
		drv->caps.num_displays, drv->caps.max_width, drv->caps.max_height,
		drv->caps.max_planes);

	dcp_kms = devm_kzalloc(&drv->dcp_subsystem_dev->dev, sizeof(*dcp_kms), GFP_KERNEL);
	if (!dcp_kms) {
		ret = -ENOMEM;
		goto err_hfi_deinit;
	}

	dcp_kms->drv       = drv;
	dcp_kms->pm_dev    = &drv->dcp_subsystem_dev->dev;
	dcp_kms->hfi       = drv->hfi;
	dcp_kms->caps      = &drv->caps;
	dcp_kms->dpus_np   = drv->dcp_subsystem_node;
	dcp_kms->iommu_dev = &drv->dcp_subsystem_dev->dev;
	drv->kms        = dcp_kms;

	ret = msm_kms_init(&dcp_kms->base, &dcp_dpu_kms_funcs);
	if (ret) {
		dev_err(&vdev->dev, "msm_kms_init failed: %d\n", ret);
		goto err_hfi_deinit;
	}

	priv = devm_kzalloc(&drv->dcp_subsystem_dev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto err_hfi_deinit;
	}

	priv->kms      = &dcp_kms->base;
	priv->kms_init = dcp_kms_init;
	dev_set_drvdata(&drv->dcp_subsystem_dev->dev, priv);
	dcp_kms->rpm_enabled = true;

	/* Component binding now sees initialized HFI, capabilities, and IOMMU state. */
	ret = dcp_component_register(drv);
	if (ret) {
		dev_err_probe(&vdev->dev, ret, "component register failed\n");
		goto err_hfi_deinit;
	}

	dcp_panic_notifier_register(drv);

	dev_dbg(&vdev->dev, "DCP VirtIO probe complete\n");
	return 0;

err_hfi_deinit:
	dcp_virtio_hfi_cleanup(drv);
	goto err_fn_dev;
err_del_vqs:
	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);
err_fn_dev:
	dcp_subsystem_dev_deinit(drv);
	return ret;
}

static void dcp_virtio_hfi_cleanup(struct dcp_drv *drv)
{
	struct virtio_device *vdev = drv->vdev;

	if (!drv->hfi)
		return;

	dcp_ipcc_set_resp_vq(drv->ipcc, NULL);

	dcp_hfi_stop(drv->hfi);
	virtio_reset_device(vdev);
	virtio_synchronize_cbs(vdev);
	dcp_hfi_quiesce_queues(drv->hfi);
	vdev->config->del_vqs(vdev);
	dcp_hfi_deinit(drv->hfi);
}

/* Remove DRM users before tearing down HFI, IPCC, or the VirtIO queues. */
static void dcp_virtio_remove(struct virtio_device *vdev)
{
	struct dcp_drv *drv = vdev->priv;

	dev_dbg(&vdev->dev, "DCP VirtIO remove\n");

	dcp_component_unregister(drv);
	dcp_panic_notifier_unregister(drv);

	dcp_virtio_hfi_cleanup(drv);

	dcp_subsystem_dev_deinit(drv);
}

/* KMS shutdown must complete while the firmware transport is available. */
static void dcp_virtio_shutdown(struct virtio_device *vdev)
{
	struct dcp_drv *drv = vdev->priv;

	pm_runtime_get_sync(&drv->dcp_subsystem_dev->dev);
	msm_kms_shutdown(drv->dcp_subsystem_dev);
	pm_runtime_put_noidle(&drv->dcp_subsystem_dev->dev);
}

static const struct virtio_device_id dcp_virtio_id_table[] = {
	{ VIRTIO_ID_QPSFI, VIRTIO_VENDOR_ID_DCP_HFI },
	{ 0 },
};
MODULE_DEVICE_TABLE(virtio, dcp_virtio_id_table);

static const struct dev_pm_ops dcp_virtio_pm_ops = {
	.prepare = dcp_pm_prepare,
	.complete = dcp_pm_complete,
};

static struct virtio_driver dcp_virtio_driver = {
	.driver = {
		.name  = "qcom-dcp-hfi",
		.owner = THIS_MODULE,
		.pm    = &dcp_virtio_pm_ops,
	},
	.id_table           = dcp_virtio_id_table,
	.feature_table      = dcp_virtio_features,
	.feature_table_size = ARRAY_SIZE(dcp_virtio_features),
	.probe              = dcp_virtio_probe,
	.remove             = dcp_virtio_remove,
	.shutdown           = dcp_virtio_shutdown,
};

int dcp_virtio_register(void)
{
	int ret;

	ret = dcp_virtio_transport_register();
	if (ret)
		return ret;

	ret = register_virtio_driver(&dcp_virtio_driver);
	if (ret)
		dcp_virtio_transport_unregister();

	return ret;
}

void dcp_virtio_unregister(void)
{
	unregister_virtio_driver(&dcp_virtio_driver);
	dcp_virtio_transport_unregister();
}
