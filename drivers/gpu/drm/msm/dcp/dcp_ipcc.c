// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP IPCC power-notify mailbox client.
 *
 * The APSS-to-DCP channel must be signaled before a VirtIO queue kick.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/of_irq.h>
#include <linux/string.h>
#include <linux/virtio.h>

#include "dcp_ipcc.h"

#define DCP_IPCC_POWER_NOTIFY_TIMEOUT_MS	10000

static void dcp_ipcc_handle_rx(struct dcp_ipcc *ipcc)
{
	unsigned long flags;

	spin_lock_irqsave(&ipcc->state_lock, flags);
	if (ipcc->shutting_down) {
		spin_unlock_irqrestore(&ipcc->state_lock, flags);
		return;
	}

	complete(&ipcc->power_notify_done);
	if (ipcc->resp_vq && virtqueue_kick_prepare(ipcc->resp_vq))
		virtqueue_notify(ipcc->resp_vq);

	spin_unlock_irqrestore(&ipcc->state_lock, flags);
}

static irqreturn_t dcp_ipcc_power_notify_irq(int irq, void *data)
{
	struct dcp_ipcc *ipcc = data;

	dcp_ipcc_handle_rx(ipcc);

	return IRQ_HANDLED;
}

static void dcp_ipcc_rx_callback(struct mbox_client *cl, void *data)
{
	struct dcp_ipcc *ipcc = container_of(cl, struct dcp_ipcc, client);

	dcp_ipcc_handle_rx(ipcc);
}

static void dcp_ipcc_free_channel(void *data)
{
	mbox_free_channel(data);
}

int dcp_ipcc_init(struct dcp_ipcc *ipcc, struct device *dev, unsigned int irq_index)
{
	int irq, ret;

	memset(ipcc, 0, sizeof(*ipcc));
	init_completion(&ipcc->power_notify_done);
	mutex_init(&ipcc->power_notify_lock);
	spin_lock_init(&ipcc->state_lock);

	ipcc->client.dev          = dev;
	ipcc->client.tx_block     = false;
	ipcc->client.tx_tout      = 0;
	ipcc->client.knows_txdone = true;
	ipcc->client.rx_callback  = dcp_ipcc_rx_callback;

	ipcc->power_chan = mbox_request_channel(&ipcc->client, 0);
	if (IS_ERR(ipcc->power_chan)) {
		ret = PTR_ERR(ipcc->power_chan);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "IPCC: failed to get DCP power channel: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(dev, dcp_ipcc_free_channel,
				       ipcc->power_chan);
	if (ret)
		return ret;

	irq = of_irq_get(dev->of_node, irq_index);
	if (irq <= 0)
		return dev_err_probe(dev, irq ? irq : -EINVAL,
				    "failed to get required power notify IRQ\n");

	ret = devm_request_irq(dev, irq, dcp_ipcc_power_notify_irq, 0,
			       "dcp-power-notify", ipcc);
	if (ret)
		return dev_err_probe(dev, ret,
				    "failed to request power notify IRQ %d\n", irq);

	ipcc->power_notify_irq = irq;

	return 0;
}

void dcp_ipcc_set_resp_vq(struct dcp_ipcc *ipcc, struct virtqueue *resp_vq)
{
	unsigned long flags;

	spin_lock_irqsave(&ipcc->state_lock, flags);
	if (!ipcc->shutting_down)
		ipcc->resp_vq = resp_vq;
	spin_unlock_irqrestore(&ipcc->state_lock, flags);
}

void dcp_ipcc_deinit(struct dcp_ipcc *ipcc)
{
	struct device *dev = ipcc->client.dev;
	unsigned long flags;

	mutex_lock(&ipcc->power_notify_lock);

	spin_lock_irqsave(&ipcc->state_lock, flags);
	ipcc->shutting_down = true;
	ipcc->resp_vq = NULL;
	spin_unlock_irqrestore(&ipcc->state_lock, flags);

	devm_free_irq(dev, ipcc->power_notify_irq, ipcc);
	devm_release_action(dev, dcp_ipcc_free_channel, ipcc->power_chan);
	mutex_unlock(&ipcc->power_notify_lock);
}

int dcp_ipcc_enable_irq_wake(struct dcp_ipcc *ipcc)
{
	return enable_irq_wake(ipcc->power_notify_irq);
}

void dcp_ipcc_disable_irq_wake(struct dcp_ipcc *ipcc)
{
	disable_irq_wake(ipcc->power_notify_irq);
}

int dcp_ipcc_send_power_notify(struct dcp_ipcc *ipcc)
{
	unsigned long timeout;
	int ret;

	mutex_lock(&ipcc->power_notify_lock);

	reinit_completion(&ipcc->power_notify_done);

	ret = mbox_send_message(ipcc->power_chan, NULL);
	mbox_client_txdone(ipcc->power_chan, ret);
	if (ret < 0) {
		dev_err(ipcc->client.dev,
			"IPCC TX: power notify failed: %d\n", ret);
		goto out_unlock;
	}

	timeout = wait_for_completion_timeout(&ipcc->power_notify_done,
					      msecs_to_jiffies(DCP_IPCC_POWER_NOTIFY_TIMEOUT_MS));
	if (!timeout) {
		dev_err(ipcc->client.dev,
			"IPCC power notify acknowledgment timed out\n");
		ret = -ETIMEDOUT;
	} else {
		ret = 0;
	}

out_unlock:
	mutex_unlock(&ipcc->power_notify_lock);
	return ret;
}
