// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mhi.h>
#include <linux/ptp_clock_kernel.h>
#include "mhi_phc.h"

#define NSEC 1000000000ULL

/**
 * struct mhi_phc_dev - MHI PHC device
 * @ptp_clock: associated PTP clock
 * @ptp_clock_info: PTP clock information
 * @mhi_dev: associated mhi device object
 * @lock: spinlock
 * @enabled: Flag to track the state of the MHI device
 */
struct mhi_phc_dev {
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info  ptp_clock_info;
	struct mhi_device *mhi_dev;
	spinlock_t lock;
	bool enabled;
};

static int qcom_ptp_gettimex64(struct ptp_clock_info *ptp, struct timespec64 *ts,
			       struct ptp_system_timestamp *sts)
{
	struct mhi_phc_dev *phc_dev = container_of(ptp, struct mhi_phc_dev, ptp_clock_info);
	struct mhi_timesync_info time;
	ktime_t ktime_cur;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&phc_dev->lock, flags);
	if (!phc_dev->enabled) {
		ret = -ENODEV;
		goto err;
	}

	ret = mhi_get_remote_tsc_time_sync(phc_dev->mhi_dev, &time);
	if (ret)
		goto err;

	ktime_cur = time.t_dev_hi * NSEC + time.t_dev_lo;
	*ts = ktime_to_timespec64(ktime_cur);

	dev_dbg(&phc_dev->mhi_dev->dev, "TSC time stamps sec:%u nsec:%u current:%lld\n",
		time.t_dev_hi, time.t_dev_lo, ktime_cur);

	/* Update pre and post timestamps for PTP_SYS_OFFSET_EXTENDED*/
	if (sts != NULL) {
		sts->pre_ts = ktime_to_timespec64(time.t_host_pre);
		sts->post_ts = ktime_to_timespec64(time.t_host_post);
		dev_dbg(&phc_dev->mhi_dev->dev, "pre:%lld post:%lld\n",
			time.t_host_pre, time.t_host_post);
	}

err:
	spin_unlock_irqrestore(&phc_dev->lock, flags);

	return ret;
}

int mhi_phc_start(struct mhi_controller *mhi_cntrl)
{
	struct mhi_phc_dev *phc_dev = dev_get_drvdata(&mhi_cntrl->mhi_dev->dev);
	unsigned long flags;

	if (!phc_dev) {
		dev_err(&mhi_cntrl->mhi_dev->dev, "Driver data is NULL\n");
		return -ENODEV;
	}

	spin_lock_irqsave(&phc_dev->lock, flags);
	phc_dev->enabled = true;
	spin_unlock_irqrestore(&phc_dev->lock, flags);

	return 0;
}

int mhi_phc_stop(struct mhi_controller *mhi_cntrl)
{
	struct mhi_phc_dev *phc_dev = dev_get_drvdata(&mhi_cntrl->mhi_dev->dev);
	unsigned long flags;

	if (!phc_dev) {
		dev_err(&mhi_cntrl->mhi_dev->dev, "Driver data is NULL\n");
		return -ENODEV;
	}

	spin_lock_irqsave(&phc_dev->lock, flags);
	phc_dev->enabled = false;
	spin_unlock_irqrestore(&phc_dev->lock, flags);

	return 0;
}

static struct ptp_clock_info qcom_ptp_clock_info = {
	.owner    = THIS_MODULE,
	.gettimex64 =  qcom_ptp_gettimex64,
};

int mhi_phc_init(struct mhi_controller *mhi_cntrl)
{
	struct mhi_device *mhi_dev = mhi_cntrl->mhi_dev;
	struct mhi_phc_dev *phc_dev;
	int ret;

	phc_dev = devm_kzalloc(&mhi_dev->dev, sizeof(*phc_dev), GFP_KERNEL);
	if (!phc_dev)
		return -ENOMEM;

	phc_dev->mhi_dev = mhi_dev;

	phc_dev->ptp_clock_info = qcom_ptp_clock_info;
	strscpy(phc_dev->ptp_clock_info.name, mhi_dev->name, PTP_CLOCK_NAME_LEN);

	spin_lock_init(&phc_dev->lock);

	phc_dev->ptp_clock = ptp_clock_register(&phc_dev->ptp_clock_info, &mhi_dev->dev);
	if (IS_ERR(phc_dev->ptp_clock)) {
		ret = PTR_ERR(phc_dev->ptp_clock);
		dev_err(&mhi_dev->dev, "Failed to register PTP clock\n");
		phc_dev->ptp_clock = NULL;
		return ret;
	}

	dev_set_drvdata(&mhi_dev->dev, phc_dev);

	dev_dbg(&mhi_dev->dev, "probed MHI PHC dev: %s\n", mhi_dev->name);
	return 0;
};

void mhi_phc_exit(struct mhi_controller *mhi_cntrl)
{
	struct mhi_phc_dev *phc_dev = dev_get_drvdata(&mhi_cntrl->mhi_dev->dev);

	if (!phc_dev)
		return;

	/* disable the node */
	ptp_clock_unregister(phc_dev->ptp_clock);
	phc_dev->enabled = false;
}
