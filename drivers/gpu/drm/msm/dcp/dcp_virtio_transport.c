// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Qualcomm DCP VirtIO transport.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <uapi/linux/virtio_mmio.h>
#include <linux/virtio_ring.h>

#include "dcp_ipcc.h"
#include "dcp_virtio.h"

#define VIRTIO_MMIO_VRING_ALIGN		PAGE_SIZE

#define to_dcp_virtio_transport(_plat_dev) \
	container_of(_plat_dev, struct dcp_virtio_transport, vdev)

struct dcp_virtio_transport {
	struct virtio_device vdev;
	struct platform_device *pdev;

	void __iomem *base;
	unsigned long version;

	bool wake_irq_enabled;
	struct dcp_ipcc ipcc;
};

static u64 dcp_virtio_get_features(struct virtio_device *vdev)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);
	u64 features;

	writel(1, transport->base + VIRTIO_MMIO_DEVICE_FEATURES_SEL);
	features = readl(transport->base + VIRTIO_MMIO_DEVICE_FEATURES);
	features <<= 32;

	writel(0, transport->base + VIRTIO_MMIO_DEVICE_FEATURES_SEL);
	features |= readl(transport->base + VIRTIO_MMIO_DEVICE_FEATURES);

	return features;
}

static int dcp_virtio_finalize_features(struct virtio_device *vdev)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);

	vring_transport_features(vdev);

	if (transport->version >= 2 &&
	    !__virtio_test_bit(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev, "New virtio-mmio devices (version >= 2) must provide VIRTIO_F_VERSION_1 feature!\n");
		return -EINVAL;
	}

	writel(1, transport->base + VIRTIO_MMIO_DRIVER_FEATURES_SEL);
	writel((u32)(vdev->features >> 32),
	       transport->base + VIRTIO_MMIO_DRIVER_FEATURES);

	writel(0, transport->base + VIRTIO_MMIO_DRIVER_FEATURES_SEL);
	writel((u32)vdev->features,
	       transport->base + VIRTIO_MMIO_DRIVER_FEATURES);

	return 0;
}

static void dcp_virtio_get(struct virtio_device *vdev, unsigned int offset,
			   void *buf, unsigned int len)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);
	void __iomem *base = transport->base + VIRTIO_MMIO_CONFIG;
	u8 b;
	__le16 w;
	__le32 l;

	if (transport->version == 1) {
		u8 *ptr = buf;
		int i;

		for (i = 0; i < len; i++)
			ptr[i] = readb(base + offset + i);
		return;
	}

	switch (len) {
	case 1:
		b = readb(base + offset);
		memcpy(buf, &b, sizeof(b));
		break;
	case 2:
		w = cpu_to_le16(readw(base + offset));
		memcpy(buf, &w, sizeof(w));
		break;
	case 4:
		l = cpu_to_le32(readl(base + offset));
		memcpy(buf, &l, sizeof(l));
		break;
	case 8:
		l = cpu_to_le32(readl(base + offset));
		memcpy(buf, &l, sizeof(l));
		l = cpu_to_le32(ioread32(base + offset + sizeof(l)));
		memcpy(buf + sizeof(l), &l, sizeof(l));
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}
}

static void dcp_virtio_set(struct virtio_device *vdev, unsigned int offset,
			   const void *buf, unsigned int len)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);
	void __iomem *base = transport->base + VIRTIO_MMIO_CONFIG;
	u8 b;
	__le16 w;
	__le32 l;

	if (transport->version == 1) {
		const u8 *ptr = buf;
		int i;

		for (i = 0; i < len; i++)
			writeb(ptr[i], base + offset + i);

		return;
	}

	switch (len) {
	case 1:
		memcpy(&b, buf, sizeof(b));
		writeb(b, base + offset);
		break;
	case 2:
		memcpy(&w, buf, sizeof(w));
		writew(le16_to_cpu(w), base + offset);
		break;
	case 4:
		memcpy(&l, buf, sizeof(l));
		writel(le32_to_cpu(l), base + offset);
		break;
	case 8:
		memcpy(&l, buf, sizeof(l));
		writel(le32_to_cpu(l), base + offset);
		memcpy(&l, buf + sizeof(l), sizeof(l));
		writel(le32_to_cpu(l), base + offset + sizeof(l));
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}
}

static u32 dcp_virtio_generation(struct virtio_device *vdev)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);

	if (transport->version == 1)
		return 0;
	else
		return readl(transport->base + VIRTIO_MMIO_CONFIG_GENERATION);
}

static u8 dcp_virtio_get_status(struct virtio_device *vdev)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);

	return readl(transport->base + VIRTIO_MMIO_STATUS) & 0xff;
}

static void dcp_virtio_set_status(struct virtio_device *vdev, u8 status)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);

	if (WARN_ON_ONCE(!status))
		return;

	writel(status, transport->base + VIRTIO_MMIO_STATUS);
}

static void dcp_virtio_reset(struct virtio_device *vdev)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);

	writel(0, transport->base + VIRTIO_MMIO_STATUS);

	if (transport->version >= 3) {
		while (dcp_virtio_get_status(vdev))
			fsleep(1000);
	}
}

static bool dcp_virtio_notify(struct virtqueue *vq)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vq->vdev);

	writel(vq->index, transport->base + VIRTIO_MMIO_QUEUE_NOTIFY);
	return true;
}

static bool dcp_virtio_notify_with_data(struct virtqueue *vq)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vq->vdev);
	u32 data = vring_notification_data(vq);

	writel(data, transport->base + VIRTIO_MMIO_QUEUE_NOTIFY);

	return true;
}

static irqreturn_t dcp_virtio_interrupt(int irq, void *opaque)
{
	struct dcp_virtio_transport *transport = opaque;
	struct virtqueue *vq;
	unsigned long status;
	irqreturn_t ret = IRQ_NONE;

	status = readl(transport->base + VIRTIO_MMIO_INTERRUPT_STATUS);
	writel(status, transport->base + VIRTIO_MMIO_INTERRUPT_ACK);

	if (unlikely(status & VIRTIO_MMIO_INT_CONFIG)) {
		virtio_config_changed(&transport->vdev);
		ret = IRQ_HANDLED;
	}

	if (likely(status & VIRTIO_MMIO_INT_VRING)) {
		virtio_device_for_each_vq(&transport->vdev, vq)
			ret |= vring_interrupt(irq, vq);
	}

	return ret;
}

static void dcp_virtio_del_vq(struct virtqueue *vq)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vq->vdev);
	unsigned int index = vq->index;

	writel(index, transport->base + VIRTIO_MMIO_QUEUE_SEL);
	if (transport->version == 1) {
		writel(0, transport->base + VIRTIO_MMIO_QUEUE_PFN);
	} else {
		writel(0, transport->base + VIRTIO_MMIO_QUEUE_READY);
		WARN_ON(readl(transport->base + VIRTIO_MMIO_QUEUE_READY));
	}

	vring_del_virtqueue(vq);
}

static void dcp_virtio_del_vqs(struct virtio_device *vdev)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);
	struct virtqueue *vq, *n;
	int irq = platform_get_irq(transport->pdev, 0);

	list_for_each_entry_safe(vq, n, &vdev->vqs, list)
		dcp_virtio_del_vq(vq);

	if (transport->wake_irq_enabled) {
		disable_irq_wake(irq);
		transport->wake_irq_enabled = false;
	}

	free_irq(irq, transport);
}

static void dcp_virtio_synchronize_cbs(struct virtio_device *vdev)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);

	synchronize_irq(platform_get_irq(transport->pdev, 0));
}

static struct virtqueue *dcp_virtio_setup_vq(struct virtio_device *vdev, unsigned int index,
					     void (*callback)(struct virtqueue *vq),
				  const char *name, bool ctx)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);
	bool (*notify)(struct virtqueue *vq);
	struct virtqueue *vq;
	unsigned int num;
	int err;

	if (__virtio_test_bit(vdev, VIRTIO_F_NOTIFICATION_DATA))
		notify = dcp_virtio_notify_with_data;
	else
		notify = dcp_virtio_notify;

	writel(index, transport->base + VIRTIO_MMIO_QUEUE_SEL);

	if (readl(transport->base + (transport->version == 1 ?
			VIRTIO_MMIO_QUEUE_PFN : VIRTIO_MMIO_QUEUE_READY))) {
		err = -ENOENT;
		goto error_available;
	}

	num = readl(transport->base + VIRTIO_MMIO_QUEUE_NUM_MAX);
	if (num == 0) {
		err = -ENOENT;
		goto error_new_virtqueue;
	}

	vq = vring_create_virtqueue(index, num, VIRTIO_MMIO_VRING_ALIGN, vdev,
				    true, true, ctx, notify, callback, name);
	if (!vq) {
		err = -ENOMEM;
		goto error_new_virtqueue;
	}

	vq->num_max = num;

	writel(virtqueue_get_vring_size(vq), transport->base + VIRTIO_MMIO_QUEUE_NUM);
	if (transport->version == 1) {
		u64 q_pfn = virtqueue_get_desc_addr(vq) >> PAGE_SHIFT;

		if (q_pfn >> 32) {
			dev_err(&vdev->dev,
				"platform bug: legacy virtio-mmio must not be used with RAM above 0x%llxGB\n",
				0x1ULL << (32 + PAGE_SHIFT - 30));
			err = -E2BIG;
			goto error_bad_pfn;
		}

		writel(PAGE_SIZE, transport->base + VIRTIO_MMIO_QUEUE_ALIGN);
		writel(q_pfn, transport->base + VIRTIO_MMIO_QUEUE_PFN);
	} else {
		u64 addr;

		addr = virtqueue_get_desc_addr(vq);
		writel((u32)addr, transport->base + VIRTIO_MMIO_QUEUE_DESC_LOW);
		writel((u32)(addr >> 32),
		       transport->base + VIRTIO_MMIO_QUEUE_DESC_HIGH);

		addr = virtqueue_get_avail_addr(vq);
		writel((u32)addr, transport->base + VIRTIO_MMIO_QUEUE_AVAIL_LOW);
		writel((u32)(addr >> 32),
		       transport->base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH);

		addr = virtqueue_get_used_addr(vq);
		writel((u32)addr, transport->base + VIRTIO_MMIO_QUEUE_USED_LOW);
		writel((u32)(addr >> 32),
		       transport->base + VIRTIO_MMIO_QUEUE_USED_HIGH);

		writel(1, transport->base + VIRTIO_MMIO_QUEUE_READY);
	}

	return vq;

error_bad_pfn:
	vring_del_virtqueue(vq);
error_new_virtqueue:
	if (transport->version == 1) {
		writel(0, transport->base + VIRTIO_MMIO_QUEUE_PFN);
	} else {
		writel(0, transport->base + VIRTIO_MMIO_QUEUE_READY);
		WARN_ON(readl(transport->base + VIRTIO_MMIO_QUEUE_READY));
	}
error_available:
	return ERR_PTR(err);
}

static int dcp_virtio_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
			       struct virtqueue *vqs[],
		       struct virtqueue_info vqs_info[],
		       struct irq_affinity *desc)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);
	int irq = platform_get_irq(transport->pdev, 0);
	int i, err, queue_idx = 0;

	if (irq < 0)
		return irq;

	err = request_irq(irq, dcp_virtio_interrupt, IRQF_SHARED,
			  dev_name(&vdev->dev), transport);
	if (err)
		return err;

	if (of_property_read_bool(transport->pdev->dev.of_node, "wakeup-source") &&
	    !enable_irq_wake(irq))
		transport->wake_irq_enabled = true;

	for (i = 0; i < nvqs; ++i) {
		struct virtqueue_info *vqi = &vqs_info[i];

		if (!vqi->name) {
			vqs[i] = NULL;
			continue;
		}

		vqs[i] = dcp_virtio_setup_vq(vdev, queue_idx++, vqi->callback,
					     vqi->name, vqi->ctx);
		if (IS_ERR(vqs[i])) {
			dcp_virtio_del_vqs(vdev);
			return PTR_ERR(vqs[i]);
		}
	}

	return 0;
}

static const char *dcp_virtio_bus_name(struct virtio_device *vdev)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);

	return transport->pdev->name;
}

static bool dcp_virtio_get_shm_region(struct virtio_device *vdev,
				      struct virtio_shm_region *region, u8 id)
{
	struct dcp_virtio_transport *transport = to_dcp_virtio_transport(vdev);
	u64 len, addr;

	writel(id, transport->base + VIRTIO_MMIO_SHM_SEL);

	len = (u64)readl(transport->base + VIRTIO_MMIO_SHM_LEN_LOW);
	len |= (u64)readl(transport->base + VIRTIO_MMIO_SHM_LEN_HIGH) << 32;

	region->len = len;

	if (len == ~(u64)0)
		return false;

	addr = (u64)readl(transport->base + VIRTIO_MMIO_SHM_BASE_LOW);
	addr |= (u64)readl(transport->base + VIRTIO_MMIO_SHM_BASE_HIGH) << 32;

	region->addr = addr;

	return true;
}

static const struct virtio_config_ops dcp_virtio_config_ops = {
	.get		= dcp_virtio_get,
	.set		= dcp_virtio_set,
	.generation	= dcp_virtio_generation,
	.get_status	= dcp_virtio_get_status,
	.set_status	= dcp_virtio_set_status,
	.reset		= dcp_virtio_reset,
	.find_vqs	= dcp_virtio_find_vqs,
	.del_vqs	= dcp_virtio_del_vqs,
	.get_features	= dcp_virtio_get_features,
	.finalize_features = dcp_virtio_finalize_features,
	.bus_name	= dcp_virtio_bus_name,
	.get_shm_region = dcp_virtio_get_shm_region,
	.synchronize_cbs = dcp_virtio_synchronize_cbs,
};

static void dcp_virtio_release_dev(struct device *dev)
{
}

struct dcp_ipcc *dcp_virtio_get_ipcc(struct virtio_device *vdev)
{
	return &to_dcp_virtio_transport(vdev)->ipcc;
}

static int dcp_virtio_transport_probe(struct platform_device *pdev)
{
	struct dcp_virtio_transport *transport;
	unsigned long magic;
	int ret;

	transport = devm_kzalloc(&pdev->dev, sizeof(*transport), GFP_KERNEL);
	if (!transport)
		return -ENOMEM;

	transport->pdev = pdev;
	transport->vdev.dev.parent = &pdev->dev;
	transport->vdev.dev.release = dcp_virtio_release_dev;
	transport->vdev.config = &dcp_virtio_config_ops;
	platform_set_drvdata(pdev, transport);

	ret = dcp_ipcc_init(&transport->ipcc, &pdev->dev, 1);
	if (ret)
		goto err_free;

	ret = dcp_ipcc_send_power_notify(&transport->ipcc);
	if (ret)
		goto err_ipcc;

	transport->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(transport->base)) {
		ret = PTR_ERR(transport->base);
		goto err_node;
	}

	magic = readl(transport->base + VIRTIO_MMIO_MAGIC_VALUE);
	if (magic != ('v' | 'i' << 8 | 'r' << 16 | 't' << 24)) {
		dev_warn(&pdev->dev, "Wrong magic value 0x%08lx!\n", magic);
		ret = -ENODEV;
		goto err_node;
	}

	transport->version = readl(transport->base + VIRTIO_MMIO_VERSION);
	if (transport->version < 1 || transport->version > 3) {
		dev_err(&pdev->dev, "Version %ld not supported!\n",
			transport->version);
		ret = -ENXIO;
		goto err_node;
	}

	transport->vdev.id.device =
		readl(transport->base + VIRTIO_MMIO_DEVICE_ID);
	if (!transport->vdev.id.device) {
		ret = -ENODEV;
		goto err_node;
	}
	transport->vdev.id.vendor =
		readl(transport->base + VIRTIO_MMIO_VENDOR_ID);

	if (transport->version == 1) {
		writel(PAGE_SIZE,
		       transport->base + VIRTIO_MMIO_GUEST_PAGE_SIZE);
		ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
		if (!ret)
			ret = dma_set_coherent_mask(&pdev->dev,
						    DMA_BIT_MASK(32 + PAGE_SHIFT));
	} else {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	}
	if (ret)
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		dev_warn(&pdev->dev,
			 "Failed to enable 64-bit or 32-bit DMA. Trying to continue, but this might not work.\n");

	ret = register_virtio_device(&transport->vdev);
	if (ret) {
		put_device(&transport->vdev.dev);
		dcp_ipcc_deinit(&transport->ipcc);
		platform_set_drvdata(pdev, NULL);
		return ret;
	}

	return 0;

err_node:
err_ipcc:
	dcp_ipcc_deinit(&transport->ipcc);
err_free:
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static void dcp_virtio_transport_remove(struct platform_device *pdev)
{
	struct dcp_virtio_transport *transport = platform_get_drvdata(pdev);

	unregister_virtio_device(&transport->vdev);
	platform_set_drvdata(pdev, NULL);
	dcp_ipcc_deinit(&transport->ipcc);
}

static int dcp_virtio_transport_suspend(struct device *dev)
{
	struct dcp_virtio_transport *transport = dev_get_drvdata(dev);
	int ret;

	ret = virtio_device_freeze(&transport->vdev);
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
	virtio_device_restore(&transport->vdev);
	return ret;
}

static int dcp_virtio_transport_resume(struct device *dev)
{
	struct dcp_virtio_transport *transport = dev_get_drvdata(dev);
	int ret;

	ret = dcp_ipcc_send_power_notify(&transport->ipcc);
	if (ret)
		return ret;

	dcp_ipcc_disable_irq_wake(&transport->ipcc);
	if (transport->version == 1)
		writel(PAGE_SIZE,
		       transport->base + VIRTIO_MMIO_GUEST_PAGE_SIZE);

	return virtio_device_restore(&transport->vdev);
}

static const struct dev_pm_ops dcp_virtio_transport_pm_ops = {
	.suspend = dcp_virtio_transport_suspend,
	.resume = dcp_virtio_transport_resume,
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
		.pm = &dcp_virtio_transport_pm_ops,
	},
};

int dcp_virtio_transport_register(void)
{
	return platform_driver_register(&dcp_virtio_transport_driver);
}

void dcp_virtio_transport_unregister(void)
{
	platform_driver_unregister(&dcp_virtio_transport_driver);
}
