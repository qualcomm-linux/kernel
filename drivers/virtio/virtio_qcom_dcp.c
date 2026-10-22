// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Qualcomm DCP VirtIO transport.
 */

#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <uapi/linux/virtio_mmio.h>

#include "virtio_mmio.h"
#include "virtio_qcom_dcp_ipcc.h"

struct dcp_virtio_transport {
	struct virtio_mmio_device mmio;
	struct virtio_config_ops config_ops;
	struct dcp_ipcc ipcc;
};

static struct dcp_virtio_transport *
to_dcp_virtio_transport(struct virtio_mmio_device *mmio)
{
	return container_of(mmio, struct dcp_virtio_transport, mmio);
}

static void dcp_virtio_synchronize_cbs(struct virtio_device *vdev)
{
	struct virtio_mmio_device *mmio =
		container_of(vdev, struct virtio_mmio_device, vdev);
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(mmio);

	virtio_mmio_config_ops.synchronize_cbs(vdev);
	dcp_ipcc_synchronize(&transport->ipcc);
}

static int dcp_virtio_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
			       struct virtqueue *vqs[],
			       struct virtqueue_info vqs_info[],
			       struct irq_affinity *desc)
{
	struct virtio_mmio_device *mmio =
		container_of(vdev, struct virtio_mmio_device, vdev);
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(mmio);
	unsigned int i;
	int ret;

	ret = virtio_mmio_config_ops.find_vqs(vdev, nvqs, vqs, vqs_info, desc);
	if (ret)
		return ret;

	for (i = 0; i < nvqs; i++) {
		if (vqs[i] && vqs_info[i].name && !strcmp(vqs_info[i].name, "resp")) {
			dcp_ipcc_set_resp_vq(&transport->ipcc, vqs[i]);
			break;
		}
	}

	return 0;
}

static void dcp_virtio_del_vqs(struct virtio_device *vdev)
{
	struct virtio_mmio_device *mmio =
		container_of(vdev, struct virtio_mmio_device, vdev);
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(mmio);

	dcp_ipcc_set_resp_vq(&transport->ipcc, NULL);
	virtio_mmio_config_ops.del_vqs(vdev);
}

static void dcp_virtio_release_dev(struct device *dev)
{
	struct virtio_device *vdev = container_of(dev, struct virtio_device, dev);
	struct virtio_mmio_device *mmio =
		container_of(vdev, struct virtio_mmio_device, vdev);
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(mmio);

	kfree(transport);
}

static int dcp_virtio_init_device(struct dcp_virtio_transport *transport,
				  struct platform_device *pdev)
{
	struct virtio_mmio_device *mmio = &transport->mmio;
	unsigned long magic;
	int ret;

	mmio->vdev.dev.parent = &pdev->dev;
	mmio->vdev.config = &transport->config_ops;
	mmio->pdev = pdev;

	mmio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mmio->base))
		return PTR_ERR(mmio->base);

	magic = readl(mmio->base + VIRTIO_MMIO_MAGIC_VALUE);
	if (magic != ('v' | 'i' << 8 | 'r' << 16 | 't' << 24)) {
		dev_warn(&pdev->dev, "Wrong magic value 0x%08lx!\n", magic);
		return -ENODEV;
	}

	mmio->version = readl(mmio->base + VIRTIO_MMIO_VERSION);
	if (mmio->version < 1 || mmio->version > 3) {
		dev_err(&pdev->dev, "Version %ld not supported!\n", mmio->version);
		return -ENXIO;
	}

	mmio->vdev.id.device = readl(mmio->base + VIRTIO_MMIO_DEVICE_ID);
	if (!mmio->vdev.id.device)
		return -ENODEV;

	mmio->vdev.id.vendor = readl(mmio->base + VIRTIO_MMIO_VENDOR_ID);

	if (mmio->version == 1) {
		writel(PAGE_SIZE, mmio->base + VIRTIO_MMIO_GUEST_PAGE_SIZE);
		ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
		if (!ret)
			dma_set_coherent_mask(&pdev->dev,
					      DMA_BIT_MASK(32 + PAGE_SHIFT));
	} else {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	}
	if (ret)
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		dev_warn(&pdev->dev,
			 "Failed to enable 64-bit or 32-bit DMA. Trying to continue, but this might not work.\n");

	return 0;
}

static int dcp_virtio_transport_probe(struct platform_device *pdev)
{
	struct dcp_virtio_transport *transport;
	struct virtio_mmio_device *mmio;
	int ret;

	transport = kzalloc_obj(*transport);
	if (!transport)
		return -ENOMEM;

	mmio = &transport->mmio;
	mmio->vdev.dev.release = dcp_virtio_release_dev;
	transport->config_ops = virtio_mmio_config_ops;
	transport->config_ops.find_vqs = dcp_virtio_find_vqs;
	transport->config_ops.del_vqs = dcp_virtio_del_vqs;
	transport->config_ops.synchronize_cbs = dcp_virtio_synchronize_cbs;

	ret = dcp_ipcc_init(&transport->ipcc, &pdev->dev, 1);
	if (ret) {
		kfree(transport);
		return ret;
	}

	/* Wake DCP before the first access to the VirtIO MMIO registers. */
	ret = dcp_ipcc_send_power_notify(&transport->ipcc);
	if (ret)
		goto err_ipcc;

	ret = dcp_virtio_init_device(transport, pdev);
	if (ret)
		goto err_ipcc;

	platform_set_drvdata(pdev, transport);

	ret = register_virtio_device(&mmio->vdev);
	if (ret) {
		platform_set_drvdata(pdev, NULL);
		dcp_ipcc_deinit(&transport->ipcc);
		put_device(&mmio->vdev.dev);
		return ret;
	}

	return 0;

err_ipcc:
	dcp_ipcc_deinit(&transport->ipcc);
	kfree(transport);
	return ret;
}

static void dcp_virtio_transport_remove(struct platform_device *pdev)
{
	struct dcp_virtio_transport *transport = platform_get_drvdata(pdev);

	/* Keep the embedded virtio_device alive until IPCC teardown is complete. */
	get_device(&transport->mmio.vdev.dev);
	unregister_virtio_device(&transport->mmio.vdev);
	platform_set_drvdata(pdev, NULL);
	dcp_ipcc_deinit(&transport->ipcc);
	put_device(&transport->mmio.vdev.dev);
}

#ifdef CONFIG_PM_SLEEP
static int dcp_virtio_transport_suspend(struct device *dev)
{
	struct dcp_virtio_transport *transport = dev_get_drvdata(dev);
	int ret;

	ret = virtio_device_freeze(&transport->mmio.vdev);
	if (ret)
		return ret;

	ret = dcp_ipcc_enable_irq_wake(&transport->ipcc);
	if (ret)
		goto err_restore;

	ret = dcp_ipcc_send_power_notify(&transport->ipcc);
	if (ret) {
		dcp_ipcc_disable_irq_wake(&transport->ipcc);
		goto err_restore;
	}

	return 0;

err_restore:
	virtio_device_restore(&transport->mmio.vdev);
	return ret;
}

static int dcp_virtio_transport_resume(struct device *dev)
{
	struct dcp_virtio_transport *transport = dev_get_drvdata(dev);
	struct virtio_mmio_device *mmio = &transport->mmio;
	int ret;

	ret = dcp_ipcc_send_power_notify(&transport->ipcc);
	if (ret) {
		dcp_ipcc_disable_irq_wake(&transport->ipcc);
		return ret;
	}

	dcp_ipcc_disable_irq_wake(&transport->ipcc);
	if (mmio->version == 1)
		writel(PAGE_SIZE, mmio->base + VIRTIO_MMIO_GUEST_PAGE_SIZE);

	return virtio_device_restore(&mmio->vdev);
}

#endif
static const struct dev_pm_ops dcp_virtio_transport_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dcp_virtio_transport_suspend,
				dcp_virtio_transport_resume)
};

static const struct of_device_id dcp_virtio_transport_of_match[] = {
	{ .compatible = "qcom,dcp-virtio" },
	{ }
};
MODULE_DEVICE_TABLE(of, dcp_virtio_transport_of_match);

static struct platform_driver dcp_virtio_transport_driver = {
	.probe = dcp_virtio_transport_probe,
	.remove = dcp_virtio_transport_remove,
	.driver = {
		.name = "qcom-dcp-virtio",
		.of_match_table = dcp_virtio_transport_of_match,
		.pm = pm_sleep_ptr(&dcp_virtio_transport_pm_ops),
	},
};
module_platform_driver(dcp_virtio_transport_driver);

MODULE_DESCRIPTION("Qualcomm DCP VirtIO transport");
MODULE_LICENSE("GPL");
