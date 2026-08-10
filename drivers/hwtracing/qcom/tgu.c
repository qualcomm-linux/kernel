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
	case TGU_CONDITION_SELECT:
		return step_index * (drvdata->num_condition_select) +
			reg_index;
	case TGU_COUNTER:
		return step_index * (drvdata->num_counter) + reg_index;
	case TGU_TIMER:
		return step_index * (drvdata->num_timer) + reg_index;
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
	case TGU_CONDITION_SELECT:
		return sysfs_emit(buf, "0x%x\n",
				drvdata->value_table->condition_select[index]);
	case TGU_TIMER:
		return sysfs_emit(buf, "0x%x\n",
				drvdata->value_table->timer[index]);
	case TGU_COUNTER:
		return sysfs_emit(buf, "0x%x\n",
				drvdata->value_table->counter[index]);
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

	guard(spinlock)(&tgu_drvdata->lock);
	index = check_array_location(tgu_drvdata, tgu_attr->step_index,
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
	case TGU_CONDITION_SELECT:
		tgu_drvdata->value_table->condition_select[index] = val;
		ret = size;
		break;
	case TGU_TIMER:
		tgu_drvdata->value_table->timer[index] = val;
		ret = size;
		break;
	case TGU_COUNTER:
		tgu_drvdata->value_table->counter[index] = val;
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
		if (tgu_attr->reg_num < drvdata->num_reg)
			return attr->mode;
		break;
	case TGU_CONDITION_DECODE:
		if (tgu_attr->reg_num < drvdata->num_condition_decode)
			return attr->mode;
		break;
	case TGU_CONDITION_SELECT:
		/* 'default' register is at the end of 'select' region */
		if (tgu_attr->reg_num == drvdata->num_condition_select - 1)
			attr->name = "default";
		if (tgu_attr->reg_num < drvdata->num_condition_select)
			return attr->mode;
		break;
	case TGU_COUNTER:
		if (!drvdata->num_counter)
			break;
		if (tgu_attr->reg_num < drvdata->num_counter)
			return attr->mode;
		break;
	case TGU_TIMER:
		if (!drvdata->num_timer)
			break;
		if (tgu_attr->reg_num < drvdata->num_timer)
			return attr->mode;
		break;
	default:
		break;
	}

	return 0;
}

static ssize_t tgu_write_all_hw_regs(struct tgu_drvdata *drvdata)
{
	int i, j, k, index;

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

	for (i = 0; i < drvdata->num_step; i++) {
		for (j = 0; j < drvdata->num_condition_select; j++) {
			index = check_array_location(drvdata, i,
						TGU_CONDITION_SELECT, j);
			if (index == -EINVAL)
				goto exit;

			writel(drvdata->value_table->condition_select[index],
				drvdata->base + CONDITION_SELECT_STEP(i, j));
		}
	}

	for (i = 0; i < drvdata->num_step; i++) {
		for (j = 0; j < drvdata->num_timer; j++) {
			index = check_array_location(drvdata, i, TGU_TIMER, j);

			if (index == -EINVAL)
				goto exit;

			writel(drvdata->value_table->timer[index],
				drvdata->base + TIMER_COMPARE_STEP(i, j));
		}
	}

	for (i = 0; i < drvdata->num_step; i++) {
		for (j = 0; j < drvdata->num_counter; j++) {
			index = check_array_location(drvdata, i, TGU_COUNTER, j);

			if (index == -EINVAL)
				goto exit;

			writel(drvdata->value_table->counter[index],
				drvdata->base + COUNTER_COMPARE_STEP(i, j));
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

	drvdata->num_reg = num_reg;
}

static void tgu_set_steps(struct tgu_drvdata *drvdata)
{
	u32 devid;

	devid = readl(drvdata->base + TGU_DEVID);

	drvdata->num_step = TGU_DEVID_STEPS(devid);
}

static void tgu_set_conditions(struct tgu_drvdata *drvdata)
{
	u32 devid;

	devid = readl(drvdata->base + TGU_DEVID);
	drvdata->num_condition_decode = TGU_DEVID_CONDITIONS(devid);
	/* select region has an additional 'default' register */
	drvdata->num_condition_select = TGU_DEVID_CONDITIONS(devid) + 1;
}

static void tgu_set_timer_counter(struct tgu_drvdata *drvdata)
{
	int num_timers = 0, num_counters = 0;
	u32 devid2;

	devid2 = readl(drvdata->base + CORESIGHT_DEVID2);

	if (TGU_DEVID2_TIMER0(devid2))
		num_timers++;
	if (TGU_DEVID2_TIMER1(devid2))
		num_timers++;

	if (TGU_DEVID2_COUNTER0(devid2))
		num_counters++;
	if (TGU_DEVID2_COUNTER1(devid2))
		num_counters++;

	drvdata->num_timer = num_timers;
	drvdata->num_counter = num_counters;
}

static int tgu_enable(struct device *dev)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);
	int ret;

	guard(spinlock)(&drvdata->lock);

	ret = tgu_write_all_hw_regs(drvdata);
	if (!ret)
		drvdata->enabled = true;

	return ret;
}

static void tgu_do_disable(struct tgu_drvdata *drvdata)
{
	TGU_UNLOCK(drvdata->base);
	writel(0, drvdata->base + TGU_CONTROL);
	TGU_LOCK(drvdata->base);

	drvdata->enabled = false;
}

static void tgu_disable(struct device *dev)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);

	guard(spinlock)(&drvdata->lock);
	if (!drvdata->enabled)
		return;

	tgu_do_disable(drvdata);
}

static ssize_t enable_tgu_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);
	bool enabled;

	guard(spinlock)(&drvdata->lock);
	enabled = drvdata->enabled;

	return sysfs_emit(buf, "%d\n", !!enabled);
}

/* enable_tgu_store - Configure Trace and Gating Unit (TGU) triggers. */
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

	if (val) {
		scoped_guard(spinlock, &drvdata->lock) {
			if (drvdata->enabled)
				return -EBUSY;
		}

		ret = pm_runtime_resume_and_get(dev);
		if (ret)
			return ret;

		ret = tgu_enable(dev);
		if (ret) {
			pm_runtime_put(dev);
			return ret;
		}
	} else {
		scoped_guard(spinlock, &drvdata->lock) {
			if (!drvdata->enabled)
				return -EINVAL;
		}

		tgu_disable(dev);
		pm_runtime_put(dev);
	}

	return size;
}
static DEVICE_ATTR_RW(enable_tgu);

/* reset_tgu_store - Reset Trace and Gating Unit (TGU) configuration. */
static ssize_t reset_tgu_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t size)
{
	struct tgu_drvdata *drvdata = dev_get_drvdata(dev);
	struct value_table *vt = drvdata->value_table;
	u32 *cond_decode = drvdata->value_table->condition_decode;
	unsigned long value;
	int i, j, ret;

	if (kstrtoul(buf, 0, &value) || value != 1)
		return -EINVAL;

	spin_lock(&drvdata->lock);
	if (!drvdata->enabled) {
		spin_unlock(&drvdata->lock);
		ret = pm_runtime_resume_and_get(drvdata->dev);
		if (ret)
			return ret;
		spin_lock(&drvdata->lock);
	}

	tgu_do_disable(drvdata);

	if (vt->priority) {
		size_t size = MAX_PRIORITY * drvdata->num_step *
				drvdata->num_reg * sizeof(unsigned int);
		memset(vt->priority, 0, size);
	}

	if (vt->condition_decode) {
		size_t size = drvdata->num_condition_decode *
			      drvdata->num_step * sizeof(unsigned int);
		memset(vt->condition_decode, 0, size);
	}

	/* Initialize all condition registers to NOT(value=0x1000000) */
	for (i = 0; i < drvdata->num_step; i++) {
		for (j = 0; j < drvdata->num_condition_decode; j++) {
			cond_decode[calculate_array_location(drvdata, i,
			TGU_CONDITION_DECODE, j)] = 0x1000000;
		}
	}

	if (vt->condition_select) {
		size_t size = drvdata->num_condition_select *
			      drvdata->num_step * sizeof(unsigned int);
		memset(vt->condition_select, 0, size);
	}

	if (vt->timer) {
		size_t size = (drvdata->num_step) * (drvdata->num_timer) *
				sizeof(unsigned int);
		memset(vt->timer, 0, size);
	}

	if (vt->counter) {
		size_t size = (drvdata->num_step) * (drvdata->num_counter) *
			      sizeof(unsigned int);
		memset(vt->counter, 0, size);
	}

	spin_unlock(&drvdata->lock);

	dev_dbg(dev, "Qualcomm-TGU reset complete\n");

	pm_runtime_put(drvdata->dev);

	return size;
}
static DEVICE_ATTR_WO(reset_tgu);

static struct attribute *tgu_common_attrs[] = {
	&dev_attr_enable_tgu.attr,
	&dev_attr_reset_tgu.attr,
	NULL,
};

static const struct attribute_group tgu_common_grp = {
	.attrs = tgu_common_attrs,
	NULL,
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
	CONDITION_SELECT_ATTRIBUTE_GROUP_INIT(0),
	CONDITION_SELECT_ATTRIBUTE_GROUP_INIT(1),
	CONDITION_SELECT_ATTRIBUTE_GROUP_INIT(2),
	CONDITION_SELECT_ATTRIBUTE_GROUP_INIT(3),
	CONDITION_SELECT_ATTRIBUTE_GROUP_INIT(4),
	CONDITION_SELECT_ATTRIBUTE_GROUP_INIT(5),
	CONDITION_SELECT_ATTRIBUTE_GROUP_INIT(6),
	CONDITION_SELECT_ATTRIBUTE_GROUP_INIT(7),
	TIMER_ATTRIBUTE_GROUP_INIT(0),
	TIMER_ATTRIBUTE_GROUP_INIT(1),
	TIMER_ATTRIBUTE_GROUP_INIT(2),
	TIMER_ATTRIBUTE_GROUP_INIT(3),
	TIMER_ATTRIBUTE_GROUP_INIT(4),
	TIMER_ATTRIBUTE_GROUP_INIT(5),
	TIMER_ATTRIBUTE_GROUP_INIT(6),
	TIMER_ATTRIBUTE_GROUP_INIT(7),
	COUNTER_ATTRIBUTE_GROUP_INIT(0),
	COUNTER_ATTRIBUTE_GROUP_INIT(1),
	COUNTER_ATTRIBUTE_GROUP_INIT(2),
	COUNTER_ATTRIBUTE_GROUP_INIT(3),
	COUNTER_ATTRIBUTE_GROUP_INIT(4),
	COUNTER_ATTRIBUTE_GROUP_INIT(5),
	COUNTER_ATTRIBUTE_GROUP_INIT(6),
	COUNTER_ATTRIBUTE_GROUP_INIT(7),
	NULL,
};

static int tgu_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct device *dev = &adev->dev;
	struct tgu_drvdata *drvdata;
	unsigned int *priority, *condition, *select, *timer, *counter;
	size_t priority_size, condition_size, select_size, timer_size, counter_size;
	int ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &adev->dev;
	dev_set_drvdata(dev, drvdata);

	drvdata->base = devm_ioremap_resource(dev, &adev->res);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	spin_lock_init(&drvdata->lock);

	tgu_set_reg_number(drvdata);
	tgu_set_steps(drvdata);
	tgu_set_conditions(drvdata);
	tgu_set_timer_counter(drvdata);

	ret = sysfs_create_groups(&dev->kobj, tgu_attr_groups);
	if (ret) {
		dev_err(dev, "failed to create sysfs groups: %d\n", ret);
		return ret;
	}

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

	select_size = drvdata->num_condition_select * drvdata->num_step;

	select = devm_kcalloc(dev, select_size,
			     sizeof(*(drvdata->value_table->condition_select)),
			     GFP_KERNEL);
	if (!select)
		return -ENOMEM;

	drvdata->value_table->condition_select = select;

	timer_size = drvdata->num_step * drvdata->num_timer;

	timer = devm_kcalloc(dev, timer_size,
			    sizeof(*(drvdata->value_table->timer)),
			    GFP_KERNEL);
	if (!timer)
		return -ENOMEM;

	drvdata->value_table->timer = timer;

	counter_size = drvdata->num_step * drvdata->num_counter;

	counter = devm_kcalloc(dev, counter_size,
			      sizeof(*(drvdata->value_table->counter)),
			      GFP_KERNEL);
	if (!counter)
		return -ENOMEM;

	drvdata->value_table->counter = counter;

	drvdata->enabled = false;

	pm_runtime_put(&adev->dev);

	return 0;
}

static void tgu_remove(struct amba_device *adev)
{
	struct device *dev = &adev->dev;

	sysfs_remove_groups(&dev->kobj, tgu_attr_groups);

	tgu_disable(dev);
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
