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

static int calculate_array_location(struct tgu_drvdata *drvdata,
				    int step_index, int operation_index,
				    int reg_index)
{
	switch (operation_index) {
	case TGU_PRIORITY0:
	case TGU_PRIORITY1:
	case TGU_PRIORITY2:
	case TGU_PRIORITY3:
		return operation_index * (drvdata->num_step) *
			(drvdata->num_reg) +
			step_index * (drvdata->num_reg) + reg_index;
	case TGU_CONDITION_DECODE:
		return step_index * (drvdata->num_condition_decode) +
			reg_index;
	default:
		break;
	}

	return -EINVAL;
}

static int check_array_location(struct tgu_drvdata *drvdata, int step,
				int ops, int reg)
{
	int result = calculate_array_location(drvdata, step, ops, reg);

	if (result == -EINVAL)
		dev_err(drvdata->dev, "check array location - Fail\n");

	return result;
}

static ssize_t tgu_dataset_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);
	struct tgu_attribute *tgu_attr =
			container_of(attr, struct tgu_attribute, attr);
	int index;

	index = check_array_location(drvdata, tgu_attr->step_index,
			tgu_attr->operation_index, tgu_attr->reg_num);

	if (index == -EINVAL)
		return index;

	/*
	 * Serialise against tgu_dataset_store() and tgu_enable(), which read
	 * and write the same priority array, so the value is not observed
	 * while being updated concurrently.
	 */
	guard(mutex)(&drvdata->lock);

	switch (tgu_attr->operation_index) {
	case TGU_PRIORITY0:
	case TGU_PRIORITY1:
	case TGU_PRIORITY2:
	case TGU_PRIORITY3:
		return sysfs_emit(buf, "0x%x\n",
				drvdata->value_table->priority[index]);
	case TGU_CONDITION_DECODE:
		return sysfs_emit(buf, "0x%x\n",
				drvdata->value_table->condition_decode[index]);
	default:
		break;
	}
	return -EINVAL;
}

static ssize_t tgu_dataset_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct tgu_drvdata *tgu_drvdata = dev_get_drvdata(dev);
	struct tgu_attribute *tgu_attr =
		container_of(attr, struct tgu_attribute, attr);
	unsigned long val;
	int index;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	guard(mutex)(&tgu_drvdata->lock);
	index = calculate_array_location(tgu_drvdata, tgu_attr->step_index,
					 tgu_attr->operation_index,
					 tgu_attr->reg_num);

	if (index == -EINVAL)
		return index;

	switch (tgu_attr->operation_index) {
	case TGU_PRIORITY0:
	case TGU_PRIORITY1:
	case TGU_PRIORITY2:
	case TGU_PRIORITY3:
		tgu_drvdata->value_table->priority[index] = val;
		ret = size;
		break;
	case TGU_CONDITION_DECODE:
		tgu_drvdata->value_table->condition_decode[index] = val;
		ret = size;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static umode_t tgu_node_visible(struct kobject *kobject,
				struct attribute *attr,
				int n)
{
	struct device *dev = kobj_to_dev(kobject);
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);
	struct device_attribute *dev_attr =
		container_of(attr, struct device_attribute, attr);
	struct tgu_attribute *tgu_attr =
		container_of(dev_attr, struct tgu_attribute, attr);

	if (tgu_attr->step_index >= drvdata->num_step)
		return SYSFS_GROUP_INVISIBLE;

	switch (tgu_attr->operation_index) {
	case TGU_PRIORITY0:
	case TGU_PRIORITY1:
	case TGU_PRIORITY2:
	case TGU_PRIORITY3:
		if (!drvdata->num_reg)
			return SYSFS_GROUP_INVISIBLE;
		if (tgu_attr->reg_num < drvdata->num_reg)
			return attr->mode;
		break;
	case TGU_CONDITION_DECODE:
		if (!drvdata->num_condition_decode)
			return SYSFS_GROUP_INVISIBLE;
		if (tgu_attr->reg_num < drvdata->num_condition_decode)
			return attr->mode;
		break;
	default:
		break;
	}

	return 0;
}

static ssize_t tgu_write_all_hw_regs(struct tgu_drvdata *drvdata)
{
	int i, j, k, index = 0;

	TGU_UNLOCK(drvdata->base);
	for (i = 0; i < drvdata->num_step; i++) {
		for (j = 0; j < MAX_PRIORITY; j++) {
			for (k = 0; k < drvdata->num_reg; k++) {
				index = check_array_location(
							drvdata, i, j, k);
				if (index == -EINVAL)
					goto exit;

				writel(drvdata->value_table->priority[index],
					drvdata->base +
					PRIORITY_REG_STEP(i, j, k));
			}
		}
	}

	for (i = 0; i < drvdata->num_step; i++) {
		for (j = 0; j < drvdata->num_condition_decode; j++) {
			index = check_array_location(drvdata, i,
						TGU_CONDITION_DECODE, j);
			if (index == -EINVAL)
				goto exit;

			writel(drvdata->value_table->condition_decode[index],
				drvdata->base + CONDITION_DECODE_STEP(i, j));
		}
	}
	/* Enable TGU to program the triggers */
	writel(1, drvdata->base + TGU_CONTROL);
exit:
	TGU_LOCK(drvdata->base);
	return index >= 0 ? 0 : -EINVAL;
}

static void tgu_set_reg_number(struct tgu_drvdata *drvdata)
{
	int num_sense_input;
	int num_reg;
	u32 devid;

	devid = readl(drvdata->base + TGU_DEVID);

	num_sense_input = TGU_DEVID_SENSE_INPUT(devid);
	num_reg = (num_sense_input * TGU_BITS_PER_SIGNAL) / LENGTH_REGISTER;

	if ((num_sense_input * TGU_BITS_PER_SIGNAL) % LENGTH_REGISTER)
		num_reg++;

	if (num_reg > TGU_MAX_PRIORITY_REGS) {
		dev_warn(drvdata->dev,
			"DEVID reports %d priority regs, clamping to %d\n",
			num_reg, TGU_MAX_PRIORITY_REGS);
		num_reg = TGU_MAX_PRIORITY_REGS;
	}

	drvdata->num_reg = num_reg;
}

static void tgu_set_steps(struct tgu_drvdata *drvdata)
{
	u32 devid;

	devid = readl(drvdata->base + TGU_DEVID);

	drvdata->num_step = TGU_DEVID_STEPS(devid);

	if (drvdata->num_step > TGU_MAX_STEPS) {
		dev_warn(drvdata->dev,
			"DEVID reports %d steps, clamping to %d\n",
			drvdata->num_step, TGU_MAX_STEPS);
		drvdata->num_step = TGU_MAX_STEPS;
	}
}

static int tgu_enable(struct tgu_drvdata *drvdata)
{
	int ret;

	ret = tgu_write_all_hw_regs(drvdata);
	if (ret)
		return ret;

	drvdata->enabled = true;
	return 0;
}

static void tgu_set_conditions(struct tgu_drvdata *drvdata)
{
	u32 devid;

	devid = readl(drvdata->base + TGU_DEVID);
	drvdata->num_condition_decode = TGU_DEVID_CONDITIONS(devid);

	if (drvdata->num_condition_decode > TGU_MAX_CONDITION_DECODE) {
		dev_warn(drvdata->dev,
			"DEVID reports %d condition decode regs, clamping to %d\n",
			drvdata->num_condition_decode, TGU_MAX_CONDITION_DECODE);
		drvdata->num_condition_decode = TGU_MAX_CONDITION_DECODE;
	}
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

		ret = tgu_enable(drvdata);
		if (ret) {
			pm_runtime_put(dev);
			return ret;
		}
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
	PRIORITY_ATTRIBUTE_GROUP_INIT(0, 0),
	PRIORITY_ATTRIBUTE_GROUP_INIT(0, 1),
	PRIORITY_ATTRIBUTE_GROUP_INIT(0, 2),
	PRIORITY_ATTRIBUTE_GROUP_INIT(0, 3),
	PRIORITY_ATTRIBUTE_GROUP_INIT(1, 0),
	PRIORITY_ATTRIBUTE_GROUP_INIT(1, 1),
	PRIORITY_ATTRIBUTE_GROUP_INIT(1, 2),
	PRIORITY_ATTRIBUTE_GROUP_INIT(1, 3),
	PRIORITY_ATTRIBUTE_GROUP_INIT(2, 0),
	PRIORITY_ATTRIBUTE_GROUP_INIT(2, 1),
	PRIORITY_ATTRIBUTE_GROUP_INIT(2, 2),
	PRIORITY_ATTRIBUTE_GROUP_INIT(2, 3),
	PRIORITY_ATTRIBUTE_GROUP_INIT(3, 0),
	PRIORITY_ATTRIBUTE_GROUP_INIT(3, 1),
	PRIORITY_ATTRIBUTE_GROUP_INIT(3, 2),
	PRIORITY_ATTRIBUTE_GROUP_INIT(3, 3),
	PRIORITY_ATTRIBUTE_GROUP_INIT(4, 0),
	PRIORITY_ATTRIBUTE_GROUP_INIT(4, 1),
	PRIORITY_ATTRIBUTE_GROUP_INIT(4, 2),
	PRIORITY_ATTRIBUTE_GROUP_INIT(4, 3),
	PRIORITY_ATTRIBUTE_GROUP_INIT(5, 0),
	PRIORITY_ATTRIBUTE_GROUP_INIT(5, 1),
	PRIORITY_ATTRIBUTE_GROUP_INIT(5, 2),
	PRIORITY_ATTRIBUTE_GROUP_INIT(5, 3),
	PRIORITY_ATTRIBUTE_GROUP_INIT(6, 0),
	PRIORITY_ATTRIBUTE_GROUP_INIT(6, 1),
	PRIORITY_ATTRIBUTE_GROUP_INIT(6, 2),
	PRIORITY_ATTRIBUTE_GROUP_INIT(6, 3),
	PRIORITY_ATTRIBUTE_GROUP_INIT(7, 0),
	PRIORITY_ATTRIBUTE_GROUP_INIT(7, 1),
	PRIORITY_ATTRIBUTE_GROUP_INIT(7, 2),
	PRIORITY_ATTRIBUTE_GROUP_INIT(7, 3),
	CONDITION_DECODE_ATTRIBUTE_GROUP_INIT(0),
	CONDITION_DECODE_ATTRIBUTE_GROUP_INIT(1),
	CONDITION_DECODE_ATTRIBUTE_GROUP_INIT(2),
	CONDITION_DECODE_ATTRIBUTE_GROUP_INIT(3),
	CONDITION_DECODE_ATTRIBUTE_GROUP_INIT(4),
	CONDITION_DECODE_ATTRIBUTE_GROUP_INIT(5),
	CONDITION_DECODE_ATTRIBUTE_GROUP_INIT(6),
	CONDITION_DECODE_ATTRIBUTE_GROUP_INIT(7),
	NULL,
};

static int tgu_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct device *dev = &adev->dev;
	struct tgu_drvdata *drvdata;
	unsigned int *priority, *condition;
	size_t priority_size, condition_size;
	int i, j;
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

	tgu_set_reg_number(drvdata);
	tgu_set_steps(drvdata);
	tgu_set_conditions(drvdata);

	drvdata->value_table =
		devm_kzalloc(dev, sizeof(*drvdata->value_table), GFP_KERNEL);
	if (!drvdata->value_table)
		return -ENOMEM;

	priority_size = MAX_PRIORITY * drvdata->num_reg * drvdata->num_step;

	priority = devm_kcalloc(dev, priority_size,
				sizeof(*drvdata->value_table->priority),
				GFP_KERNEL);
	if (!priority)
		return -ENOMEM;

	drvdata->value_table->priority = priority;

	condition_size = drvdata->num_condition_decode * drvdata->num_step;

	condition = devm_kcalloc(dev, condition_size,
				sizeof(*(drvdata->value_table->condition_decode)),
				GFP_KERNEL);
	if (!condition)
		return -ENOMEM;

	drvdata->value_table->condition_decode = condition;

	/* Initialize all condition decode registers to NOT */
	for (i = 0; i < drvdata->num_step; i++)
		for (j = 0; j < drvdata->num_condition_decode; j++)
			condition[calculate_array_location(
				drvdata, i, TGU_CONDITION_DECODE, j)] =
				TGU_CONDITION_DECODE_NOT;

	drvdata->enabled = false;
	/*
	 * Create the sysfs groups only after the backing storage is fully
	 * allocated, otherwise userspace could open a priority attribute and
	 * have tgu_dataset_show() dereference an uninitialised value_table.
	 * This also keeps the error paths above free of sysfs teardown.
	 */
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
