// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm pmic BCL hardware driver for battery overcurrent and
 * battery or system under voltage monitor
 *
 * Copyright (c) 2026, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/devm-helpers.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#include "qcom-bcl-hwmon.h"

ADD_BCL_HWMON_ALARM_MAPS(in, min, lcrit);
ADD_BCL_HWMON_ALARM_MAPS(curr, max, crit);

/* Interrupt names for each alarm level */
static const char * const bcl_int_names[ALARM_MAX] = {
	[LVL0] = "bcl-max-min",
	[LVL1] = "bcl-critical",
};

static const char * const bcl_channel_label[CHANNEL_MAX] = {
	"BCL Voltage",
	"BCL Current",
};

/* Common Reg Fields */
static const struct reg_field common_reg_fields[COMMON_FIELD_MAX] = {
	[F_V_MAJOR]	= REG_FIELD(REVISION2, 0, 7),
	[F_V_MINOR]	= REG_FIELD(REVISION1, 0, 7),
	[F_CTL_EN]	= REG_FIELD(EN_CTL1, 7, 7),
	[F_LVL0_ALARM]	= REG_FIELD(STATUS, 0, 0),
	[F_LVL1_ALARM]	= REG_FIELD(STATUS, 1, 1),
};

/* BCL Version/Modes specific fields */
static const struct reg_field bcl_v1_reg_fields[] = {
	[F_IN_MON_EN]	= REG_FIELD(MODE_CTL1, 0, 2),
	[F_IN_L0_THR]	= REG_FIELD(VADC_L0_THR, 0, 7),
	[F_IN_L1_THR]	= REG_FIELD(VCMP_L1_THR, 0, 5),
	[F_IN_INPUT_EN]	= REG_FIELD(VADC_CONV_REQ, 0, 0),
	[F_IN_INPUT]	= REG_FIELD(VADC_DATA1, 0, 7),
	[F_CURR_MON_EN]	= REG_FIELD(IADC_CONV_REQ, 0, 0),
	[F_CURR_H0_THR]	= REG_FIELD(IADC_H0_THR, 0, 7),
	[F_CURR_H1_THR]	= REG_FIELD(IADC_H1_THR, 0, 7),
	[F_CURR_INPUT]	= REG_FIELD(IADC_DATA1, 0, 7),
};

static const struct reg_field bcl_v2_reg_fields[] = {
	[F_IN_MON_EN]	= REG_FIELD(VCMP_CTL, 0, 1),
	[F_IN_L0_THR]	= REG_FIELD(VADC_L0_THR, 0, 7),
	[F_IN_L1_THR]	= REG_FIELD(VCMP_L1_THR, 0, 5),
	[F_IN_INPUT_EN]	= REG_FIELD(VADC_CONV_REQ, 0, 0),
	[F_IN_INPUT]	= REG_FIELD(VADC_DATA1, 0, 7),
	[F_CURR_MON_EN]	= REG_FIELD(IADC_CONV_REQ, 0, 0),
	[F_CURR_H0_THR]	= REG_FIELD(IADC_H0_THR, 0, 7),
	[F_CURR_H1_THR]	= REG_FIELD(IADC_H1_THR, 0, 7),
	[F_CURR_INPUT]	= REG_FIELD(IADC_DATA1, 0, 7),
};

static const struct reg_field bcl_v3_bmx_reg_fields[] = {
	[F_IN_MON_EN]	= REG_FIELD(VCMP_CTL, 0, 2),
	[F_IN_L0_THR]	= REG_FIELD(VADC_L0_THR, 0, 7),
	[F_IN_L1_THR]	= REG_FIELD(VCMP_L1_THR, 0, 5),
	[F_IN_INPUT_EN]	= REG_FIELD(PARAM_1, 0, 0),
	[F_IN_INPUT]	= REG_FIELD(VADC_DATA1, 0, 7),
	[F_CURR_MON_EN]	= REG_FIELD(PARAM_1, 1, 1),
	[F_CURR_H0_THR]	= REG_FIELD(IADC_H0_THR, 0, 7),
	[F_CURR_H1_THR]	= REG_FIELD(IADC_H1_THR_GEN3, 0, 7),
	[F_CURR_INPUT]	= REG_FIELD(IADC_DATA1, 0, 7),
};

static const struct reg_field bcl_v3_wb_reg_fields[] = {
	[F_IN_MON_EN]	= REG_FIELD(VCMP_CTL, 0, 2),
	[F_IN_L0_THR]	= REG_FIELD(VADC_L0_THR, 0, 7),
	[F_IN_L1_THR]	= REG_FIELD(VCMP_L1_THR, 0, 5),
	[F_IN_INPUT_EN]	= REG_FIELD(PARAM_1, 0, 0),
	[F_IN_INPUT]	= REG_FIELD(VADC_DATA1, 0, 7),
	[F_CURR_MON_EN]	= REG_FIELD(PARAM_1, 1, 1),
	[F_CURR_H0_THR]	= REG_FIELD(IADC_H0_THR, 0, 7),
	[F_CURR_H1_THR]	= REG_FIELD(IADC_H1_THR, 0, 3),
	[F_CURR_INPUT]	= REG_FIELD(IADC_DATA1, 0, 7),
};

static const struct reg_field bcl_v3_core_reg_fields[] = {
	[F_IN_MON_EN]	= REG_FIELD(VCMP_CTL, 0, 2),
	[F_IN_L0_THR]	= REG_FIELD(VCMP_L0_THR, 0, 5),
	[F_IN_L1_THR]	= REG_FIELD(VCMP_L1_THR, 0, 5),
	[F_IN_INPUT_EN]	= REG_FIELD(PARAM_1, 0, 0),
	[F_IN_INPUT]	= REG_FIELD(VADC_DATA1, 0, 7),
	[F_CURR_MON_EN]	= REG_FIELD(PARAM_1, 1, 1),
};

static const struct reg_field bcl_v4_bmx_reg_fields[] = {
	[F_IN_MON_EN]	= REG_FIELD(VCMP_CTL, 0, 2),
	[F_IN_L0_THR]	= REG_FIELD(VADC_L0_THR, 0, 7),
	[F_IN_L1_THR]	= REG_FIELD(VCMP_L1_THR, 0, 5),
	[F_IN_INPUT_EN]	= REG_FIELD(PARAM_1, 0, 0),
	[F_IN_INPUT]	= REG_FIELD(VADC_DATA1, 0, 15),
	[F_CURR_MON_EN]	= REG_FIELD(PARAM_1, 1, 1),
	[F_CURR_H0_THR]	= REG_FIELD(IADC_H0_THR, 0, 7),
	[F_CURR_H1_THR]	= REG_FIELD(IADC_H1_THR_GEN3, 0, 7),
	[F_CURR_INPUT]	= REG_FIELD(IADC_DATA1, 0, 15),
};

static const struct reg_field bcl_v4_wb_reg_fields[] = {
	[F_IN_MON_EN]	= REG_FIELD(VCMP_CTL, 0, 2),
	[F_IN_L0_THR]	= REG_FIELD(VADC_L0_THR, 0, 7),
	[F_IN_L1_THR]	= REG_FIELD(VCMP_L1_THR, 0, 6),
	[F_IN_INPUT_EN]	= REG_FIELD(PARAM_1, 0, 0),
	[F_IN_INPUT]	= REG_FIELD(VADC_DATA1, 0, 15),
	[F_CURR_MON_EN]	= REG_FIELD(PARAM_1, 1, 1),
	[F_CURR_H0_THR]	= REG_FIELD(IADC_H0_THR, 0, 7),
	[F_CURR_H1_THR]	= REG_FIELD(IADC_H1_THR, 0, 4),
	[F_CURR_INPUT]	= REG_FIELD(IADC_DATA1, 0, 15),
};

static const struct reg_field bcl_v4_core_reg_fields[] = {
	[F_IN_MON_EN]	= REG_FIELD(VCMP_CTL, 0, 2),
	[F_IN_L0_THR]	= REG_FIELD(VCMP_L0_THR, 0, 6),
	[F_IN_L1_THR]	= REG_FIELD(VCMP_L1_THR, 0, 6),
	[F_IN_INPUT_EN]	= REG_FIELD(PARAM_1, 0, 0),
	[F_IN_INPUT]	= REG_FIELD(VADC_DATA1, 0, 15),
	[F_CURR_MON_EN]	= REG_FIELD(PARAM_1, 1, 1),
};

/* V1 BMX and core */
static const struct bcl_desc pm7250b_data = {
	.reg_fields = bcl_v1_reg_fields,
	.num_reg_fields = F_MAX_FIELDS,
	.data_field_bits_size = 8,
	.thresh_field_bits_size = 7,
	.channel[IN] = {
		.base = 2250,
		.max = 3600,
		.step = 25,
		.default_scale_nu = 194637,
		.thresh_type = {ADC, INDEX},
	},
	.channel[CURR] = {
		.max = 10000,
		.default_scale_nu = 305180,
		.thresh_type = {ADC, ADC},
	},
};

/* V2 BMX and core */
static const struct bcl_desc pm8350_data = {
	.reg_fields = bcl_v2_reg_fields,
	.num_reg_fields = F_MAX_FIELDS,
	.data_field_bits_size = 8,
	.thresh_field_bits_size = 8,
	.channel[IN] = {
		.base = 2250,
		.max = 3600,
		.step = 25,
		.default_scale_nu = 194637,
		.thresh_type = {ADC, INDEX},
	},
	.channel[CURR] = {
		.max = 10000,
		.default_scale_nu = 305180,
		.thresh_type = {ADC, ADC},
	},
};

/* V3 BMX  */
static const struct bcl_desc pm8550b_data = {
	.reg_fields = bcl_v3_bmx_reg_fields,
	.num_reg_fields = F_MAX_FIELDS,
	.data_field_bits_size = 8,
	.thresh_field_bits_size = 8,
	.channel[IN] = {
		.base = 2250,
		.max = 3600,
		.step = 25,
		.default_scale_nu = 194637,
		.thresh_type = {ADC, INDEX},
	},
	.channel[CURR] = {
		.max = 12000,
		.default_scale_nu = 366220,
		.thresh_type = {ADC, ADC},
	},
};

/* V3 WB  */
static const struct bcl_desc pmw5100_data = {
	.reg_fields = bcl_v3_wb_reg_fields,
	.num_reg_fields = F_MAX_FIELDS,
	.data_field_bits_size = 8,
	.thresh_field_bits_size = 8,
	.channel[IN] = {
		.base = 2250,
		.max = 3600,
		.step = 25,
		.default_scale_nu = 194637,
		.thresh_type = {ADC, INDEX},
	},
	.channel[CURR] = {
		.base = 800,
		.max = 2000,
		.step = 100,
		.default_scale_nu = 61035,
		.thresh_type = {ADC, INDEX},
	},
};

/* V3 CORE  */
static const struct bcl_desc pm8550_data = {
	.reg_fields = bcl_v3_core_reg_fields,
	.num_reg_fields = F_CURR_MON_EN + 1,
	.data_field_bits_size = 8,
	.thresh_field_bits_size = 8,
	.channel[IN] = {
		.base = 2250,
		.max = 3600,
		.step = 25,
		.thresh_type = {INDEX, INDEX},
	},
};

/* V4 BMX  */
static const struct bcl_desc pmih010_data = {
	.reg_fields = bcl_v4_bmx_reg_fields,
	.num_reg_fields = F_MAX_FIELDS,
	.data_field_bits_size = 16,
	.thresh_field_bits_size = 8,
	.channel[IN] = {
		.base = 2250,
		.max = 3600,
		.step = 25,
		.default_scale_nu = 194637,
		.thresh_type = {ADC, INDEX},
	},
	.channel[CURR] = {
		.max = 20000,
		.default_scale_nu = 610370,
		.thresh_type = {ADC, ADC},
	},
};

/* V4 WB  */
static const struct bcl_desc pmw6100_data = {
	.reg_fields = bcl_v4_wb_reg_fields,
	.num_reg_fields = F_MAX_FIELDS,
	.data_field_bits_size = 16,
	.thresh_field_bits_size = 8,
	.channel[IN] = {
		.base = 1500,
		.max = 4000,
		.step = 25,
		.default_scale_nu = 194637,
		.thresh_type = {ADC, INDEX},
	},
	.channel[CURR] = {
		.base = 900,
		.max = 3300,
		.step = 150,
		.default_scale_nu = 152586,
		.thresh_type = {ADC, INDEX},
	},
};

/* V4 CORE */
static const struct bcl_desc pmh010_data = {
	.reg_fields = bcl_v4_core_reg_fields,
	.num_reg_fields = F_CURR_MON_EN + 1,
	.thresh_field_bits_size = 8,
	.channel[IN] = {
		.base = 1500,
		.max = 4000,
		.step = 25,
		.thresh_type = {INDEX, INDEX},
	},
};

/* V4 BMX with different scale */
static const struct bcl_desc pmv010_data = {
	.reg_fields = bcl_v4_bmx_reg_fields,
	.num_reg_fields = F_MAX_FIELDS,
	.data_field_bits_size = 16,
	.thresh_field_bits_size = 8,
	.channel[IN] = {
		.base = 2250,
		.max = 3600,
		.step = 25,
		.default_scale_nu = 194637,
		.thresh_type = {ADC, INDEX},
	},
	.channel[CURR] = {
		.max = 12000,
		.default_scale_nu = 366220,
		.thresh_type = {ADC, ADC},
	},
};

/**
 * bcl_convert_raw_to_milliunit - Convert raw value to milli unit
 * @desc: BCL device descriptor
 * @raw_val: Raw ADC value from hardware
 * @type: type of the channel, in or curr
 * @field_width: bits size for data or threshold field
 *
 * Return: value in milli unit
 */
static unsigned int bcl_convert_raw_to_milliunit(const struct bcl_desc *desc, int raw_val,
						 enum bcl_channel_type type, u8 field_width)
{
	u32 def_scale = desc->channel[type].default_scale_nu;
	u32 lsb_weight = field_width > 8 ? 1 : 1 << field_width;
	u32 scaling_factor = def_scale * lsb_weight;

	return div_s64((s64)raw_val * scaling_factor, 1000000);
}

/**
 * bcl_convert_milliunit_to_raw - Convert milli unit to raw value
 * @desc: BCL device descriptor
 * @ma_val: threshold value in milli unit
 * @type: type of the channel, in or curr
 * @field_width: bits size for data or threshold field
 *
 * Return: Raw ADC value for hardware
 */
static unsigned int bcl_convert_milliunit_to_raw(const struct bcl_desc *desc, int mval,
						 enum bcl_channel_type type, u8 field_width)
{
	u32 def_scale = desc->channel[type].default_scale_nu;
	u32 lsb_weight = field_width > 8 ? 1 : 1 << field_width;
	u32 scaling_factor = def_scale * lsb_weight;

	return div_s64((s64)mval * 1000000, scaling_factor);
}

/**
 * bcl_convert_milliunit_to_index - Convert milliunit to in or curr index
 * @desc: BCL device descriptor
 * @val: in or curr value in milli unit
 * @type: type of the channel, in or curr
 *
 * Converts a value in milli unit to an index for BCL that use indexed thresholds.
 *
 * Return: Voltage index value
 */
static unsigned int bcl_convert_milliunit_to_index(const struct bcl_desc *desc, int val,
						   enum bcl_channel_type type)
{
	return div_s64((s64)val - desc->channel[type].base, desc->channel[type].step);
}

/**
 * bcl_convert_index_to_milliunit - Convert in or curr index to milli unit
 * @desc: BCL device descriptor
 * @val: index value
 * @type: type of the channel, in or curr
 *
 * Converts an index value to milli unit for BCL that use indexed thresholds.
 *
 * Return: Voltage value in millivolts
 */
static unsigned int bcl_convert_index_to_milliunit(const struct bcl_desc *desc, int val,
						   enum bcl_channel_type type)
{
	return desc->channel[type].base + val * desc->channel[type].step;
}

static int bcl_in_thresh_write(struct bcl_device *bcl, long value, enum bcl_limit_alarm lvl)
{
	const struct bcl_desc *desc = bcl->desc;
	u32 raw_val;

	int thresh = clamp_val(value, desc->channel[IN].base, desc->channel[IN].max);

	if (desc->channel[IN].thresh_type[lvl] == ADC)
		raw_val = bcl_convert_milliunit_to_raw(desc, thresh, IN,
						       desc->thresh_field_bits_size);
	else
		raw_val = bcl_convert_milliunit_to_index(desc, thresh, IN);

	return regmap_field_write(bcl->fields[F_IN_L0_THR + lvl], raw_val);
}

static int bcl_curr_thresh_write(struct bcl_device *bcl, long value, enum bcl_limit_alarm lvl)
{
	const struct bcl_desc *desc = bcl->desc;
	u32 raw_val;

	/* Clamp only to curr max */
	int thresh = clamp_val(value, value, desc->channel[CURR].max);

	if (desc->channel[CURR].thresh_type[lvl] == ADC)
		raw_val = bcl_convert_milliunit_to_raw(desc, thresh, CURR,
						       desc->thresh_field_bits_size);
	else
		raw_val = bcl_convert_milliunit_to_index(desc, thresh, CURR);

	return regmap_field_write(bcl->fields[F_CURR_H0_THR + lvl], raw_val);
}

static int bcl_in_thresh_read(struct bcl_device *bcl, enum bcl_limit_alarm lvl, long *out)
{
	int ret, thresh;
	u32 raw_val = 0;
	const struct bcl_desc *desc = bcl->desc;

	ret = bcl_read_field_value(bcl, F_IN_L0_THR + lvl, &raw_val);
	if (ret)
		return ret;

	if (desc->channel[IN].thresh_type[lvl] == ADC)
		thresh = bcl_convert_raw_to_milliunit(desc, raw_val, IN,
						      desc->thresh_field_bits_size);
	else
		thresh = bcl_convert_index_to_milliunit(desc, raw_val, IN);

	*out = thresh;

	return 0;
}

static int bcl_curr_thresh_read(struct bcl_device *bcl, enum bcl_limit_alarm lvl, long *out)
{
	int ret, thresh;
	u32 raw_val = 0;
	const struct bcl_desc *desc = bcl->desc;

	ret = bcl_read_field_value(bcl, F_CURR_H0_THR + lvl, &raw_val);
	if (ret)
		return ret;

	if (desc->channel[CURR].thresh_type[lvl] == ADC)
		thresh = bcl_convert_raw_to_milliunit(desc, raw_val, CURR,
						      desc->thresh_field_bits_size);
	else
		thresh = bcl_convert_index_to_milliunit(desc, raw_val, CURR);

	*out = thresh;

	return 0;
}

static int bcl_curr_input_read(struct bcl_device *bcl, long *out)
{
	int ret;
	u32 raw_val = 0;
	const struct bcl_desc *desc = bcl->desc;

	ret = bcl_read_field_value(bcl, F_CURR_INPUT, &raw_val);
	if (ret)
		return ret;

	/*
	 * The sensor sometime can read a value 0 if there are
	 * consecutive reads
	 */
	if (raw_val != 0)
		bcl->last_curr_input =
			bcl_convert_raw_to_milliunit(desc, raw_val, CURR,
						     desc->data_field_bits_size);

	*out = bcl->last_curr_input;

	return 0;
}

static int bcl_in_input_read(struct bcl_device *bcl, long *out)
{
	int ret;
	u32 raw_val = 0;
	const struct bcl_desc *desc = bcl->desc;

	ret = bcl_read_field_value(bcl, F_IN_INPUT, &raw_val);
	if (ret)
		return ret;

	if (raw_val < GENMASK(desc->data_field_bits_size - 1, 0))
		bcl->last_in_input =
			bcl_convert_raw_to_milliunit(desc, raw_val, IN,
						     desc->data_field_bits_size);

	*out = bcl->last_in_input;

	return 0;
}

static int bcl_read_alarm_status(struct bcl_device *bcl,
				 enum bcl_limit_alarm lvl, long *status)
{
	int ret;
	u32 raw_val = 0;

	ret = bcl_read_field_value(bcl, F_LVL0_ALARM + lvl, &raw_val);
	if (ret)
		return ret;

	*status = raw_val;

	return 0;
}

static unsigned int bcl_get_version_major(const struct bcl_device *bcl)
{
	u32 raw_val = 0;

	bcl_read_field_value(bcl, F_V_MAJOR, &raw_val);

	return raw_val;
}

static unsigned int bcl_get_version_minor(const struct bcl_device *bcl)
{
	u32 raw_val = 0;

	bcl_read_field_value(bcl, F_V_MINOR, &raw_val);

	return raw_val;
}

static void bcl_hwmon_notify_event(struct bcl_device *bcl, enum bcl_limit_alarm alarm)
{
	if (bcl->in_mon_enabled)
		hwmon_notify_event(bcl->hwmon_dev, hwmon_in,
				   in_lvl_to_attr_map[alarm], 0);
	if (bcl->curr_mon_enabled)
		hwmon_notify_event(bcl->hwmon_dev, hwmon_curr,
				   curr_lvl_to_attr_map[alarm], 0);
}

static void bcl_alarm_enable_poll(struct work_struct *work)
{
	struct bcl_alarm_data *alarm = container_of(work, struct bcl_alarm_data,
							 alarm_poll_work.work);
	struct bcl_device *bcl = alarm->device;
	long status;

	guard(mutex)(&bcl->lock);

	if (bcl_read_alarm_status(bcl, alarm->type, &status))
		goto re_schedule;

	if (!status & !alarm->irq_enabled) {
		bcl_enable_irq(alarm);
		bcl_hwmon_notify_event(bcl, alarm->type);
		return;
	}

re_schedule:
	schedule_delayed_work(&alarm->alarm_poll_work,
			      msecs_to_jiffies(BCL_ALARM_POLLING_MS));
}

static irqreturn_t bcl_handle_alarm(int irq, void *data)
{
	struct bcl_alarm_data *alarm = data;
	struct bcl_device *bcl = alarm->device;
	long status;

	guard(mutex)(&bcl->lock);

	if (bcl_read_alarm_status(bcl, alarm->type, &status) || !status)
		return IRQ_HANDLED;

	if (!bcl->hwmon_dev)
		return IRQ_HANDLED;

	bcl_hwmon_notify_event(bcl, alarm->type);

	bcl_disable_irq(alarm);
	schedule_delayed_work(&alarm->alarm_poll_work,
			      msecs_to_jiffies(BCL_ALARM_POLLING_MS));

	dev_dbg(bcl->dev, "Irq:%d triggered for bcl type:%d\n",
		irq, alarm->type);

	return IRQ_HANDLED;
}

static umode_t bcl_hwmon_is_visible(const void *data,
				    enum hwmon_sensor_types type,
				    u32 attr, int channel)
{
	const struct bcl_device *bcl = data;

	switch (type) {
	case hwmon_in:
		if (!bcl->in_mon_enabled)
			return 0;
		switch (attr) {
		case hwmon_in_input:
			return bcl->in_input_enabled ? 0444 : 0;
		case hwmon_in_label:
		case hwmon_in_min_alarm:
		case hwmon_in_lcrit_alarm:
			return 0444;
		case hwmon_in_min:
		case hwmon_in_lcrit:
			return 0644;
		default:
			return 0;
		}
	case hwmon_curr:
		if (!bcl->curr_mon_enabled)
			return 0;
		switch (attr) {
		case hwmon_curr_input:
		case hwmon_curr_label:
		case hwmon_curr_max_alarm:
		case hwmon_curr_crit_alarm:
			return 0444;
		case hwmon_curr_max:
		case hwmon_curr_crit:
			return 0644;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

static int bcl_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long val)
{
	struct bcl_device *bcl = dev_get_drvdata(dev);
	int ret = -EOPNOTSUPP;

	guard(mutex)(&bcl->lock);

	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_min:
		case hwmon_in_lcrit:
			ret = bcl_in_thresh_write(bcl, val, in_attr_to_lvl_map[attr]);
			break;
		default:
			ret = -EOPNOTSUPP;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_max:
		case hwmon_curr_crit:
			ret = bcl_curr_thresh_write(bcl, val, curr_attr_to_lvl_map[attr]);
			break;
		default:
			ret = -EOPNOTSUPP;
		}
		break;
	default:
		break;
	}

	return ret;
}

static int bcl_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long *value)
{
	struct bcl_device *bcl = dev_get_drvdata(dev);
	int ret;

	guard(mutex)(&bcl->lock);

	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			ret = bcl_in_input_read(bcl, value);
			break;
		case hwmon_in_min:
		case hwmon_in_lcrit:
			ret = bcl_in_thresh_read(bcl, in_attr_to_lvl_map[attr], value);
			break;
		case hwmon_in_min_alarm:
		case hwmon_in_lcrit_alarm:
			ret = bcl_read_alarm_status(bcl, in_attr_to_lvl_map[attr], value);
			break;
		default:
			ret = -EOPNOTSUPP;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			ret = bcl_curr_input_read(bcl, value);
			break;
		case hwmon_curr_max:
		case hwmon_curr_crit:
			ret = bcl_curr_thresh_read(bcl, curr_attr_to_lvl_map[attr], value);
			break;
		case hwmon_curr_max_alarm:
		case hwmon_curr_crit_alarm:
			ret = bcl_read_alarm_status(bcl, curr_attr_to_lvl_map[attr], value);
			break;
		default:
			ret = -EOPNOTSUPP;
		}
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int bcl_hwmon_read_string(struct device *dev,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_in:
		if (attr != hwmon_in_label)
			break;
		*str = bcl_channel_label[IN];
		return 0;
	case hwmon_curr:
		if (attr != hwmon_curr_label)
			break;
		*str = bcl_channel_label[CURR];
		return 0;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops bcl_hwmon_ops = {
	.is_visible	= bcl_hwmon_is_visible,
	.read		= bcl_hwmon_read,
	.read_string	= bcl_hwmon_read_string,
	.write		= bcl_hwmon_write,
};

static const struct hwmon_channel_info *bcl_hwmon_info[] = {
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL | HWMON_I_MIN |
			   HWMON_I_LCRIT | HWMON_I_MIN_ALARM |
			   HWMON_I_LCRIT_ALARM),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL | HWMON_C_MAX |
			   HWMON_C_CRIT | HWMON_C_MAX_ALARM |
			   HWMON_C_CRIT_ALARM),
	NULL,
};

static const struct hwmon_chip_info bcl_hwmon_chip_info = {
	.ops	= &bcl_hwmon_ops,
	.info	= bcl_hwmon_info,
};

static int bcl_curr_thresh_update(struct bcl_device *bcl)
{
	int ret, i;

	if (!bcl->curr_thresholds[0])
		return 0;

	for (i = 0; i < ALARM_MAX; i++) {
		ret = bcl_curr_thresh_write(bcl, bcl->curr_thresholds[i], i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void bcl_hw_channel_mon_init(struct bcl_device *bcl)
{
	bcl->in_mon_enabled = bcl_in_monitor_enabled(bcl);
	bcl->in_input_enabled = bcl_in_input_enabled(bcl);
	bcl->curr_mon_enabled = bcl_curr_monitor_enabled(bcl);
}

static int bcl_alarm_irq_init(struct platform_device *pdev,
			      struct bcl_device *bcl)
{
	int ret = 0, irq_num = 0, i = 0;
	struct bcl_alarm_data *alarm;

	for (i = LVL0; i < ALARM_MAX; i++) {
		alarm = &bcl->bcl_alarms[i];
		alarm->type = i;
		alarm->device = bcl;

		ret = devm_delayed_work_autocancel(bcl->dev, &alarm->alarm_poll_work,
						   bcl_alarm_enable_poll);
		if (ret)
			return ret;

		irq_num = platform_get_irq_byname(pdev, bcl_int_names[i]);
		if (irq_num <= 0)
			continue;

		ret = devm_request_threaded_irq(&pdev->dev, irq_num, NULL,
						bcl_handle_alarm, IRQF_ONESHOT,
						bcl_int_names[i], alarm);
		if (ret) {
			dev_err(&pdev->dev, "Error requesting irq(%s).err:%d\n",
				bcl_int_names[i], ret);
			return ret;
		}
		alarm->irq = irq_num;
		enable_irq_wake(alarm->irq);
		alarm->irq_enabled = true;
	}

	return 0;
}

static int bcl_regmap_field_init(struct device *dev, struct bcl_device *bcl,
				 const struct bcl_desc *data)
{
	int i;
	struct reg_field fields[F_MAX_FIELDS];

	BUILD_BUG_ON(ARRAY_SIZE(common_reg_fields) != COMMON_FIELD_MAX);

	for (i = 0; i < data->num_reg_fields; i++) {
		if (i < COMMON_FIELD_MAX)
			fields[i] = common_reg_fields[i];
		else
			fields[i] = data->reg_fields[i];

		/* Need to adjust BCL base from regmap dynamically */
		fields[i].reg += bcl->base;
	}

	return devm_regmap_field_bulk_alloc(dev, bcl->regmap, bcl->fields,
					   fields, data->num_reg_fields);
}

static int bcl_get_device_property_data(struct platform_device *pdev,
					struct bcl_device *bcl)
{
	struct device *dev = &pdev->dev;
	int ret;
	u32 reg;

	ret = device_property_read_u32(dev, "reg", &reg);
	if (ret < 0)
		return ret;

	bcl->base = reg;

	device_property_read_u32_array(dev, "overcurrent-thresholds-milliamp",
				       bcl->curr_thresholds, 2);
	return 0;
}

static int bcl_probe(struct platform_device *pdev)
{
	struct bcl_device *bcl;
	int ret;

	bcl = devm_kzalloc(&pdev->dev, sizeof(*bcl), GFP_KERNEL);
	if (!bcl)
		return -ENOMEM;

	bcl->dev = &pdev->dev;
	bcl->desc = device_get_match_data(&pdev->dev);
	if (!bcl->desc)
		return -EINVAL;

	ret = devm_mutex_init(bcl->dev, &bcl->lock);
	if (ret)
		return ret;

	bcl->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!bcl->regmap) {
		dev_err(&pdev->dev, "Couldn't get parent's regmap\n");
		return -EINVAL;
	}

	ret = bcl_get_device_property_data(pdev, bcl);
	if (ret < 0)
		return ret;

	ret = bcl_regmap_field_init(bcl->dev, bcl, bcl->desc);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to allocate regmap fields, err:%d\n", ret);
		return ret;
	}

	if (!bcl_hw_is_enabled(bcl))
		return -ENODEV;

	ret = bcl_curr_thresh_update(bcl);
	if (ret < 0)
		return ret;

	ret = bcl_alarm_irq_init(pdev, bcl);
	if (ret < 0)
		return ret;

	bcl_hw_channel_mon_init(bcl);

	dev_set_drvdata(&pdev->dev, bcl);

	bcl->hwmon_name = devm_hwmon_sanitize_name(&pdev->dev,
						   dev_name(bcl->dev));
	if (IS_ERR(bcl->hwmon_name)) {
		dev_err(&pdev->dev, "Failed to sanitize hwmon name\n");
		return PTR_ERR(bcl->hwmon_name);
	}

	bcl->hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
							      bcl->hwmon_name,
							      bcl,
							      &bcl_hwmon_chip_info,
							      NULL);
	if (IS_ERR(bcl->hwmon_dev)) {
		dev_err(&pdev->dev, "Failed to register hwmon device: %ld\n",
			PTR_ERR(bcl->hwmon_dev));
		return PTR_ERR(bcl->hwmon_dev);
	}

	dev_dbg(&pdev->dev, "BCL hwmon device with version: %u.%u registered\n",
		bcl_get_version_major(bcl), bcl_get_version_minor(bcl));

	return 0;
}

static const struct of_device_id bcl_match[] = {
	{
		.compatible = "qcom,bcl-v1",
		.data = &pm7250b_data,
	}, {
		.compatible = "qcom,bcl-v2",
		.data = &pm8350_data,
	}, {
		.compatible = "qcom,bcl-v3-bmx",
		.data = &pm8550b_data,
	}, {
		.compatible = "qcom,bcl-v3-wb",
		.data = &pmw5100_data,
	}, {
		.compatible = "qcom,bcl-v3-core",
		.data = &pm8550_data,
	}, {
		.compatible = "qcom,bcl-v4-bmx",
		.data = &pmih010_data,
	}, {
		.compatible = "qcom,bcl-v4-wb",
		.data = &pmw6100_data,
	}, {
		.compatible = "qcom,bcl-v4-core",
		.data = &pmh010_data,
	}, {
		.compatible = "qcom,bcl-v4-pmv010",
		.data = &pmv010_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, bcl_match);

static struct platform_driver bcl_driver = {
	.probe	= bcl_probe,
	.driver	= {
		.name		= BCL_DRIVER_NAME,
		.of_match_table	= bcl_match,
	},
};

MODULE_AUTHOR("Manaf Meethalavalappu Pallikunhi <manaf.pallikunhi@oss.qualcomm.com>");
MODULE_DESCRIPTION("QCOM BCL HWMON driver");
module_platform_driver(bcl_driver);
MODULE_LICENSE("GPL");
