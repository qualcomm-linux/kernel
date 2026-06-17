/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_BCL_HWMON_H__
#define __QCOM_BCL_HWMON_H__

#define BCL_DRIVER_NAME			"qcom-bcl-hwmon"

/* BCL common regmap offset */
#define REVISION1			0x0
#define REVISION2			0x1
#define STATUS				0x8
#define INT_RT_STS			0x10
#define EN_CTL1				0x46

/* BCL GEN1 regmap offsets */
#define MODE_CTL1			0x41
#define VADC_L0_THR			0x48
#define VCMP_L1_THR			0x49
#define IADC_H0_THR			0x4b
#define IADC_H1_THR			0x4c
#define VADC_CONV_REQ			0x72
#define IADC_CONV_REQ			0x82
#define VADC_DATA1			0x76
#define IADC_DATA1			0x86

/* BCL GEN3 regmap offsets */
#define VCMP_CTL			0x44
#define VCMP_L0_THR			0x47
#define PARAM_1				0x0e
#define IADC_H1_THR_GEN3		0x4d

#define BCL_IN_INC_MV			25
#define BCL_ALARM_POLLING_MS		50

/**
 * enum bcl_limit_alarm - BCL alarm threshold levels
 * @LVL0: Level 0 alarm threshold (mapped to in_min_alarm or curr_max_alarm)
 * @LVL1: Level 1 alarm threshold (mapped to in_lcrit_alarm or curr_crit_alarm)
 * @ALARM_MAX: sentinel value
 *
 * Defines the three threshold levels for BCL monitoring. Each level corresponds
 * to different severity of in or curr conditions.
 */
enum bcl_limit_alarm {
	LVL0,
	LVL1,

	ALARM_MAX,
};

/**
 * enum bcl_channel_type - BCL supported sensor channel type
 * @IN: in (voltage) channel
 * @CURR: curr (current) channel
 * @CHANNEL_MAX: sentinel value
 *
 * Defines the supported channel types for bcl.
 */
enum bcl_channel_type {
	IN,
	CURR,

	CHANNEL_MAX,
};

/**
 * enum bcl_thresh_type - voltage or current threshold representation type
 * @ADC: Raw ADC value representation
 * @INDEX: Index-based voltage or current representation
 *
 * Specifies how voltage or current thresholds are stored and interpreted in
 * registers. Some PMICs use raw ADC values while others use indexed values.
 */
enum bcl_thresh_type {
	ADC,
	INDEX,
};

/**
 * enum bcl_fields - BCL register field identifiers
 * @F_V_MAJOR: Major revision info field
 * @F_V_MINOR: Minor revision info field
 * @F_CTL_EN: Monitor enable control field
 * @F_LVL0_ALARM: Level 0 alarm status field
 * @F_LVL1_ALARM: Level 1 alarm status field
 * @COMMON_FIELD_MAX: sentinel value for common fields
 * @F_IN_MON_EN: voltage monitor enable control field
 * @F_IN_L0_THR: voltage level 0 threshold field
 * @F_IN_L1_THR: voltage level 1 threshold field
 * @F_IN_INPUT_EN: voltage input enable control field
 * @F_IN_INPUT: voltage input data field
 * @F_CURR_MON_EN: current monitor enable control field
 * @F_CURR_H0_THR: current level 0 threshold field
 * @F_CURR_H1_THR: current level 1 threshold field
 * @F_CURR_INPUT: current input data field
 * @F_MAX_FIELDS: sentinel value
 *
 * Enumeration of all register fields used by the BCL driver for accessing
 * registers through regmap fields.
 */
enum bcl_fields {
	F_V_MAJOR,
	F_V_MINOR,

	F_CTL_EN,

	/* common alarm for in and curr channel */
	F_LVL0_ALARM,
	F_LVL1_ALARM,

	COMMON_FIELD_MAX,

	F_IN_MON_EN = COMMON_FIELD_MAX,
	F_IN_L0_THR,
	F_IN_L1_THR,

	F_IN_INPUT_EN,
	F_IN_INPUT,

	F_CURR_MON_EN,
	F_CURR_H0_THR,
	F_CURR_H1_THR,

	F_CURR_INPUT,

	F_MAX_FIELDS
};

#define ADD_BCL_HWMON_ALARM_MAPS(_type, lvl0_attr, lvl1_attr)	\
								\
static const u8 _type##_attr_to_lvl_map[] = {			\
	[hwmon_##_type##_##lvl0_attr] = LVL0,			\
	[hwmon_##_type##_##lvl1_attr] = LVL1,			\
	[hwmon_##_type##_##lvl0_attr##_alarm] = LVL0,		\
	[hwmon_##_type##_##lvl1_attr##_alarm] = LVL1,		\
};								\
								\
static const u8 _type##_lvl_to_attr_map[ALARM_MAX] = {		\
	[LVL0] = hwmon_##_type##_##lvl0_attr##_alarm,		\
	[LVL1] = hwmon_##_type##_##lvl1_attr##_alarm,		\
}

/**
 * struct bcl_channel_cfg - BCL channel related configuration
 * @default_scale_nu:  Default scaling factor in nano unit
 * @base: Base threshold value in milli unit
 * @max: Maximum threshold value in milli unit
 * @step: step increment value between two indexed threshold value
 * @thresh_type: Array specifying threshold representation type for each alarm level
 *
 * Contains hardware-specific configuration and scaling parameters for different
 * channel(voltage and current)..
 */

struct bcl_channel_cfg {
	u32 default_scale_nu;
	u32 base;
	u32 max;
	u32 step;
	u8 thresh_type[ALARM_MAX];
};

/**
 * struct bcl_desc - BCL device descriptor
 * @reg_fields: Array of register field definitions for this device variant
 * @channel: Each channel specific(voltage or current) configuration
 * @num_reg_fields: Number of register field definitions for this device variant
 * @data_field_bits_size: data read register bit size
 * @thresh_field_bits_size: lsb bit size those are not included in threshold register
 *
 * Contains hardware-specific configuration and scaling parameters for different
 * BCL variants. Each PMIC model may have different register layouts and
 * conversion factors.
 */

struct bcl_desc {
	const struct reg_field *reg_fields;
	struct bcl_channel_cfg channel[CHANNEL_MAX];
	u8 num_reg_fields;
	u8 data_field_bits_size;
	u8 thresh_field_bits_size;
};

struct bcl_device;

/**
 * struct bcl_alarm_data - BCL alarm interrupt data
 * @irq: IRQ number assigned to this alarm
 * @irq_enabled: Flag indicating if IRQ is enabled
 * @type: Alarm level type (LVL0, or LVL1)
 * @device: Pointer to parent BCL device structure
 * @alarm_poll_work: delayed_work to poll alarm status
 *
 * Stores interrupt-related information for each alarm threshold level.
 * Used by the IRQ handler to identify which alarm triggered.
 */
struct bcl_alarm_data {
	int			irq;
	bool			irq_enabled;
	enum bcl_limit_alarm	type;
	struct bcl_device	*device;
	struct delayed_work	alarm_poll_work;
};

/**
 * struct bcl_device - Main BCL device structure
 * @dev: Pointer to device structure
 * @regmap: Regmap for accessing PMIC registers
 * @fields: Array of regmap fields for register access
 * @bcl_alarms: Array of alarm data structures for each threshold level
 * @lock: Mutex for protecting concurrent hardware access
 * @in_mon_enabled: Flag indicating if voltage monitoring is enabled
 * @curr_mon_enabled: Flag indicating if current monitoring is enabled
 * @curr_thresholds: Current threshold values in milliamps from dt-binding(LVL0 and LVL1)
 * @base: the BCL regbase offset from regmap
 * @in_input_enabled: Flag indicating if voltage input reading is enabled
 * @last_in_input: Last valid voltage input reading in millivolts
 * @last_curr_input: Last valid current input reading in milliamps
 * @desc: Pointer to device descriptor with hardware-specific parameters
 * @hwmon_dev: Pointer to registered hwmon device
 * @hwmon_name: Sanitized name for hwmon device
 *
 * Main driver structure containing all state and configuration for a BCL
 * monitoring instance. Manages voltage and current monitoring, thresholds,
 * and alarm handling.
 */
struct bcl_device {
	struct device		*dev;
	struct regmap		*regmap;
	u16			base;
	struct regmap_field	*fields[F_MAX_FIELDS];
	struct bcl_alarm_data	bcl_alarms[ALARM_MAX];
	struct mutex		lock;
	u32			curr_thresholds[ALARM_MAX];
	u32			last_in_input;
	u32			last_curr_input;
	bool			in_mon_enabled;
	bool			curr_mon_enabled;
	bool			in_input_enabled;
	const struct bcl_desc	*desc;
	struct device		*hwmon_dev;
	char			*hwmon_name;
};

/**
 * bcl_read_field_value - Read alarm status for a given level
 * @bcl: BCL device structure
 * @id: Index in bcl->fields[]
 * @val: Pointer to store val
 *
 * Return: 0 on success or regmap error code
 */
static inline int bcl_read_field_value(const struct bcl_device *bcl, enum bcl_fields id, u32 *val)
{
	return regmap_field_read(bcl->fields[id], val);
}

/**
 * bcl_field_enabled - Generic helper to check if a regmap field is enabled
 * @bcl: BCL device structure
 * @field: Index in bcl->fields[]
 *
 * Return: true if field is non-zero, false otherwise
 */
static inline bool bcl_field_enabled(const struct bcl_device *bcl, enum bcl_fields id)
{
	int ret;
	u32 val = 0;

	ret = regmap_field_read(bcl->fields[id], &val);
	if (ret)
		return false;

	return !!val;
}

#define bcl_in_input_enabled(bcl)	bcl_field_enabled(bcl, F_IN_INPUT_EN)
#define bcl_curr_monitor_enabled(bcl)	bcl_field_enabled(bcl, F_CURR_MON_EN)
#define bcl_in_monitor_enabled(bcl)	bcl_field_enabled(bcl, F_IN_MON_EN)
#define bcl_hw_is_enabled(bcl)		bcl_field_enabled(bcl, F_CTL_EN)

/**
 * bcl_enable_irq - Generic helper to enable alarm irq
 * @alarm: BCL level alarm data
 */
static inline void bcl_enable_irq(struct bcl_alarm_data *alarm)
{
	if (alarm->irq_enabled)
		return;
	alarm->irq_enabled = true;
	enable_irq(alarm->irq);
	enable_irq_wake(alarm->irq);
}

/**
 * bcl_disable_irq - Generic helper to disable alarm irq
 * @alarm: BCL level alarm data
 */
static inline void bcl_disable_irq(struct bcl_alarm_data *alarm)
{
	if (!alarm->irq_enabled)
		return;
	alarm->irq_enabled = false;
	disable_irq_nosync(alarm->irq);
	disable_irq_wake(alarm->irq);
}

#endif /* __QCOM_BCL_HWMON_H__ */
