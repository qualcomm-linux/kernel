// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/amba/bus.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>

#include "tgu.h"

static void tgu_write_all_hw_regs(struct tgu_drvdata *drvdata)
{
	TGU_UNLOCK(drvdata->base);
	/* Enable TGU to program the triggers */
	writel(1, drvdata->base + TGU_CONTROL);
	TGU_LOCK(drvdata->base);
}

static void tgu_enable(struct tgu_drvdata *drvdata)
{
	tgu_write_all_hw_regs(drvdata);
	drvdata->enabled = true;
}

static void tgu_disable(struct tgu_drvdata *drvdata)
{
	TGU_UNLOCK(drvdata->base);
	writel(0, drvdata->base + TGU_CONTROL);
	TGU_LOCK(drvdata->base);

	drvdata->enabled = false;
}

static ssize_t enable_tgu_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);
	bool enabled;

	scoped_guard(mutex, &drvdata->lock)
		enabled = drvdata->enabled;

	return sysfs_emit(buf, "%d\n", enabled);
}

/* enable_tgu_store - Enable or disable the Trigger Generation Unit (TGU). */
static ssize_t enable_tgu_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t size)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret || val > 1)
		return -EINVAL;

	/*
	 * Hold the mutex across the whole check-and-act sequence: the check of
	 * drvdata->enabled, the PM runtime reference counting and the state
	 * update must be atomic, otherwise concurrent writers could each
	 * imbalance the PM runtime usage counter.
	 */
	guard(mutex)(&drvdata->lock);

	if (val) {
		if (drvdata->enabled)
			return -EBUSY;

		ret = pm_runtime_resume_and_get(dev);
		if (ret)
			return ret;

		tgu_enable(drvdata);
	} else {
		if (!drvdata->enabled)
			return -EINVAL;

		tgu_disable(drvdata);
		pm_runtime_put(dev);
	}

	return size;
}
static DEVICE_ATTR_RW(enable_tgu);

static struct attribute *tgu_common_attrs[] = {
	&dev_attr_enable_tgu.attr,
	NULL,
};

static const struct attribute_group tgu_common_grp = {
	.attrs = tgu_common_attrs,
};

static const struct attribute_group *tgu_attr_groups[] = {
	&tgu_common_grp,
	NULL,
};

static int tgu_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct device *dev = &adev->dev;
	struct tgu_drvdata *drvdata;
	int ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &adev->dev;
	dev_set_drvdata(dev, drvdata);

	drvdata->base = devm_ioremap_resource(dev, &adev->res);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	ret = devm_mutex_init(dev, &drvdata->lock);
	if (ret)
		return ret;

	drvdata->enabled = false;

	ret = sysfs_create_groups(&dev->kobj, tgu_attr_groups);
	if (ret) {
		dev_err(dev, "failed to create sysfs groups: %d\n", ret);
		return ret;
	}

	pm_runtime_put(&adev->dev);

	return 0;
}

static void tgu_remove(struct amba_device *adev)
{
	struct device *dev = &adev->dev;
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);

	sysfs_remove_groups(&dev->kobj, tgu_attr_groups);

	/*
	 * If the device is still enabled through sysfs, disable the hardware
	 * and drop the PM runtime reference taken in enable_tgu_store() so the
	 * usage counter stays balanced. Use the synchronous put: the driver core
	 * disables runtime PM right after ->remove() returns, and the barrier in
	 * pm_runtime_disable() would cancel a still-queued async idle/suspend,
	 * leaving the runtime-PM state inconsistent.
	 */
	scoped_guard(mutex, &drvdata->lock) {
		if (drvdata->enabled) {
			tgu_disable(drvdata);
			pm_runtime_put_sync(dev);
		}
	}
}

static const struct amba_id tgu_ids[] = {
	{
		.id = 0x000f0e00,
		.mask = 0x000fffff,
	},
	{ 0, 0, NULL },
};

MODULE_DEVICE_TABLE(amba, tgu_ids);

static struct amba_driver tgu_driver = {
	.drv = {
		.name = "qcom-tgu",
		.suppress_bind_attrs = true,
	},
	.probe = tgu_probe,
	.remove = tgu_remove,
	.id_table = tgu_ids,
};

module_amba_driver(tgu_driver);

MODULE_AUTHOR("Songwei Chai <songwei.chai@oss.qualcomm.com>");
MODULE_AUTHOR("Jinlong Mao <jinlong.mao@oss.qualcomm.com>");
MODULE_DESCRIPTION("Qualcomm Trigger Generation Unit driver");
MODULE_LICENSE("GPL");
