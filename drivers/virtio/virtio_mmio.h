/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _DRIVERS_VIRTIO_VIRTIO_MMIO_H
#define _DRIVERS_VIRTIO_VIRTIO_MMIO_H

#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>

struct virtio_mmio_device {
	struct virtio_device vdev;
	struct platform_device *pdev;
	void __iomem *base;
	unsigned long version;
	/* True if enable_irq_wake() succeeded for the shared IRQ. */
	bool wake_irq_enabled;
};

extern const struct virtio_config_ops virtio_mmio_config_ops;

#endif /* _DRIVERS_VIRTIO_VIRTIO_MMIO_H */
