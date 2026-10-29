/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __DCP_VIRTIO_H__
#define __DCP_VIRTIO_H__

#include <linux/device.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>

struct dcp_drv;
struct dcp_ipcc;
#define VIRTIO_VENDOR_ID_DCP_HFI		1

struct dcp_ipcc *dcp_virtio_get_ipcc(struct virtio_device *vdev);

int dcp_virtio_transport_register(void);
void dcp_virtio_transport_unregister(void);

int dcp_virtio_register(void);
void dcp_virtio_unregister(void);

#endif /* __DCP_VIRTIO_H__ */
