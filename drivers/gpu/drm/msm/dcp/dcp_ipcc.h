/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * IPCC power-notify mailbox interface for DCP firmware.
 *
 * Channel 5 carries APSS-to-DCP wake notifications; responses arrive through
 * the mailbox client's RX callback.
 */

#ifndef __DCP_IPCC_H__
#define __DCP_IPCC_H__

#include <linux/completion.h>
#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

struct device;
struct virtqueue;

struct dcp_ipcc {
	struct mbox_client client;
	struct mbox_chan *power_chan;
	struct virtqueue *resp_vq;
	struct completion power_notify_done;
	/* Serializes concurrent power-notify transactions. */
	struct mutex power_notify_lock;
	/* Protects callback state and response queue access. */
	spinlock_t state_lock;
	bool shutting_down;
	int power_notify_irq;
};

int dcp_ipcc_init(struct dcp_ipcc *ipcc, struct device *dev, unsigned int irq_index);
void dcp_ipcc_set_resp_vq(struct dcp_ipcc *ipcc, struct virtqueue *resp_vq);
void dcp_ipcc_deinit(struct dcp_ipcc *ipcc);
int dcp_ipcc_send_power_notify(struct dcp_ipcc *ipcc);
int dcp_ipcc_enable_irq_wake(struct dcp_ipcc *ipcc);
void dcp_ipcc_disable_irq_wake(struct dcp_ipcc *ipcc);

#endif /* __DCP_IPCC_H__ */
