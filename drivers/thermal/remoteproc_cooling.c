// SPDX-License-Identifier: GPL-2.0
/*
 * Remote Processor Cooling Device
 *
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/err.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#define REMOTEPROC_PREFIX		"rproc_"

struct remoteproc_cooling_ops {
	int (*get_max_level)(void *devdata, unsigned long *level);
	int (*get_cur_level)(void *devdata, unsigned long *level);
	int (*set_cur_level)(void *devdata, unsigned long level);
};

/**
 * struct remoteproc_cdev - Remote processor cooling device
 * @cdev: Thermal cooling device handle
 * @ops: Vendor-specific operation callbacks
 * @devdata: Private data for vendor implementation
 * @np: Device tree node associated with this cooling device
 * @lock: Mutex to protect cooling device operations
 */
struct remoteproc_cdev {
	struct thermal_cooling_device *cdev;
	const struct remoteproc_cooling_ops *ops;
	void *devdata;
	struct device_node *np;
	struct mutex lock;
};

/* Thermal cooling device callbacks */

static int remoteproc_get_max_state(struct thermal_cooling_device *cdev,
				    unsigned long *state)
{
	struct remoteproc_cdev *rproc_cdev = cdev->devdata;
	int ret;

	if (!rproc_cdev || !rproc_cdev->ops)
		return -EINVAL;

	mutex_lock(&rproc_cdev->lock);
	ret = rproc_cdev->ops->get_max_level(rproc_cdev->devdata, state);
	mutex_unlock(&rproc_cdev->lock);

	return ret;
}

static int remoteproc_get_cur_state(struct thermal_cooling_device *cdev,
				    unsigned long *state)
{
	struct remoteproc_cdev *rproc_cdev = cdev->devdata;
	int ret;

	if (!rproc_cdev || !rproc_cdev->ops)
		return -EINVAL;

	mutex_lock(&rproc_cdev->lock);
	ret = rproc_cdev->ops->get_cur_level(rproc_cdev->devdata, state);
	mutex_unlock(&rproc_cdev->lock);

	return ret;
}

static int remoteproc_set_cur_state(struct thermal_cooling_device *cdev,
				    unsigned long state)
{
	struct remoteproc_cdev *rproc_cdev = cdev->devdata;
	int ret;

	if (!rproc_cdev || !rproc_cdev->ops)
		return -EINVAL;

	mutex_lock(&rproc_cdev->lock);
	ret = rproc_cdev->ops->set_cur_level(rproc_cdev->devdata, state);
	mutex_unlock(&rproc_cdev->lock);

	return ret;
}

static const struct thermal_cooling_device_ops remoteproc_cooling_ops = {
	.get_max_state = remoteproc_get_max_state,
	.get_cur_state = remoteproc_get_cur_state,
	.set_cur_state = remoteproc_set_cur_state,
};

struct remoteproc_cdev *
remoteproc_cooling_register(struct device_node *np,
			    const char *name, const struct remoteproc_cooling_ops *ops,
			     void *devdata)
{
	struct remoteproc_cdev *rproc_cdev;
	struct thermal_cooling_device *cdev;
	int ret;

	if (!name || !ops) {
		return ERR_PTR(-EINVAL);
	}

	rproc_cdev = kzalloc(sizeof(*rproc_cdev), GFP_KERNEL);
	if (!rproc_cdev)
		return ERR_PTR(-ENOMEM);

	rproc_cdev->ops = ops;
	rproc_cdev->devdata = devdata;
	rproc_cdev->np = np;
	mutex_init(&rproc_cdev->lock);

	char *rproc_name __free(kfree) =
		kasprintf(GFP_KERNEL, REMOTEPROC_PREFIX "%s", name);
	/* Register with thermal framework */
	if (np) {
		cdev = thermal_of_cooling_device_register(np, rproc_name, rproc_cdev,
							  &remoteproc_cooling_ops);
	}

	if (IS_ERR(cdev)) {
		ret = PTR_ERR(cdev);
		goto free_rproc_cdev;
	}

	rproc_cdev->cdev = cdev;

	return rproc_cdev;

free_rproc_cdev:
	kfree(rproc_cdev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(remoteproc_cooling_register);

void remoteproc_cooling_unregister(struct remoteproc_cdev *rproc_cdev)
{
	if (!rproc_cdev)
		return;

	thermal_cooling_device_unregister(rproc_cdev->cdev);
	mutex_destroy(&rproc_cdev->lock);
	kfree(rproc_cdev);
}
EXPORT_SYMBOL_GPL(remoteproc_cooling_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Remote Processor Cooling Device");
