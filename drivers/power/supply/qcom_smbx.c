// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Ltd.
 * Author: Casey Connolly <casey.connolly@linaro.org>
 *
 * This driver supports the switch-mode battery charger found in:
 *   - PMI8998 / PM660  (SMB2 IP)
 *   - PM4125           (SMBLite IP)
 */

#include <linux/bits.h>
#include <linux/devm-helpers.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/pm_wakeup.h>

/* PMIC IP types */
enum smb_type {
	SMB2,		/* PMI8998, PM660 - SMB2 IP */
	SMBLITE,	/* PM4125 - SMBLite IP */
};

/* clang-format off */
#define BATTERY_CHARGER_STATUS_1			0x06
#define BVR_INITIAL_RAMP_BIT				BIT(7)
#define CC_SOFT_TERMINATE_BIT				BIT(6)
#define STEP_CHARGING_STATUS_SHIFT			3
#define STEP_CHARGING_STATUS_MASK			GENMASK(5, 3)
#define BATTERY_CHARGER_STATUS_MASK			GENMASK(2, 0)

#define BATTERY_CHARGER_STATUS_2			0x07
#define INPUT_CURRENT_LIMITED_BIT			BIT(7)
#define CHARGER_ERROR_STATUS_SFT_EXPIRE_BIT		BIT(6)
#define CHARGER_ERROR_STATUS_BAT_OV_BIT			BIT(5)
#define CHARGER_ERROR_STATUS_BAT_TERM_MISSING_BIT	BIT(4)
#define BAT_TEMP_STATUS_MASK				GENMASK(3, 0)
#define BAT_TEMP_STATUS_SOFT_LIMIT_MASK			GENMASK(3, 2)
#define BAT_TEMP_STATUS_HOT_SOFT_LIMIT_BIT		BIT(3)
#define BAT_TEMP_STATUS_COLD_SOFT_LIMIT_BIT		BIT(2)
#define BAT_TEMP_STATUS_TOO_HOT_BIT			BIT(1)
#define BAT_TEMP_STATUS_TOO_COLD_BIT			BIT(0)

#define BATTERY_CHARGER_STATUS_4			0x0A
#define CHARGE_CURRENT_POST_JEITA_MASK			GENMASK(7, 0)

#define BATTERY_CHARGER_STATUS_7			0x0D
#define ENABLE_TRICKLE_BIT				BIT(7)
#define ENABLE_PRE_CHARGING_BIT				BIT(6)
#define ENABLE_FAST_CHARGING_BIT			BIT(5)
#define ENABLE_FULLON_MODE_BIT				BIT(4)
#define TOO_COLD_ADC_BIT				BIT(3)
#define TOO_HOT_ADC_BIT					BIT(2)
#define HOT_SL_ADC_BIT					BIT(1)
#define COLD_SL_ADC_BIT					BIT(0)

#define CHARGING_ENABLE_CMD				0x42
#define CHARGING_ENABLE_CMD_BIT				BIT(0)

#define CHGR_CFG2					0x51
#define CHG_EN_SRC_BIT					BIT(7)
#define CHG_EN_POLARITY_BIT				BIT(6)
#define PRETOFAST_TRANSITION_CFG_BIT			BIT(5)
#define BAT_OV_ECC_BIT					BIT(4)
#define I_TERM_BIT					BIT(3)
#define AUTO_RECHG_BIT					BIT(2)
#define EN_ANALOG_DROP_IN_VBATT_BIT			BIT(1)
#define CHARGER_INHIBIT_BIT				BIT(0)

#define PRE_CHARGE_CURRENT_CFG				0x60
#define PRE_CHARGE_CURRENT_SETTING_MASK			GENMASK(5, 0)

#define FAST_CHARGE_CURRENT_CFG				0x61
#define FAST_CHARGE_CURRENT_SETTING_MASK		GENMASK(7, 0)

#define FLOAT_VOLTAGE_CFG				0x70
#define FLOAT_VOLTAGE_SETTING_MASK			GENMASK(7, 0)

#define FG_UPDATE_CFG_2_SEL				0x7D
#define SOC_LT_OTG_THRESH_SEL_BIT			BIT(3)
#define SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT		BIT(2)
#define VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT		BIT(1)
#define IBT_LT_CHG_TERM_THRESH_SEL_BIT			BIT(0)

#define JEITA_EN_CFG					0x90
#define JEITA_EN_HARDLIMIT_BIT				BIT(4)
#define JEITA_EN_HOT_SL_FCV_BIT				BIT(3)
#define JEITA_EN_COLD_SL_FCV_BIT			BIT(2)
#define JEITA_EN_HOT_SL_CCC_BIT				BIT(1)
#define JEITA_EN_COLD_SL_CCC_BIT			BIT(0)

#define INT_RT_STS					0x310
#define TYPE_C_CHANGE_RT_STS_BIT			BIT(7)
#define USBIN_ICL_CHANGE_RT_STS_BIT			BIT(6)
#define USBIN_SOURCE_CHANGE_RT_STS_BIT			BIT(5)
#define USBIN_PLUGIN_RT_STS_BIT				BIT(4)
#define USBIN_OV_RT_STS_BIT				BIT(3)
#define USBIN_UV_RT_STS_BIT				BIT(2)
#define USBIN_LT_3P6V_RT_STS_BIT			BIT(1)
#define USBIN_COLLAPSE_RT_STS_BIT			BIT(0)

#define OTG_CFG						0x153
#define OTG_RESERVED_MASK				GENMASK(7, 6)
#define DIS_OTG_ON_TLIM_BIT				BIT(5)
#define QUICKSTART_OTG_FASTROLESWAP_BIT			BIT(4)
#define INCREASE_DFP_TIME_BIT				BIT(3)
#define ENABLE_OTG_IN_DEBUG_MODE_BIT			BIT(2)
#define OTG_EN_SRC_CFG_BIT				BIT(1)
#define CONCURRENT_MODE_CFG_BIT				BIT(0)

#define OTG_ENG_OTG_CFG					0x1C0
#define ENG_BUCKBOOST_HALT1_8_MODE_BIT			BIT(0)

#define APSD_STATUS					0x307
#define APSD_STATUS_7_BIT				BIT(7)
#define HVDCP_CHECK_TIMEOUT_BIT				BIT(6)
#define SLOW_PLUGIN_TIMEOUT_BIT				BIT(5)
#define ENUMERATION_DONE_BIT				BIT(4)
#define VADP_CHANGE_DONE_AFTER_AUTH_BIT			BIT(3)
#define QC_AUTH_DONE_STATUS_BIT				BIT(2)
#define QC_CHARGER_BIT					BIT(1)
#define APSD_DTC_STATUS_DONE_BIT			BIT(0)

#define APSD_RESULT_STATUS				0x308
#define ICL_OVERRIDE_LATCH_BIT				BIT(7)
#define APSD_RESULT_STATUS_MASK				GENMASK(6, 0)
#define QC_3P0_BIT					BIT(6)
#define QC_2P0_BIT					BIT(5)
#define FLOAT_CHARGER_BIT				BIT(4)
#define DCP_CHARGER_BIT					BIT(3)
#define CDP_CHARGER_BIT					BIT(2)
#define OCP_CHARGER_BIT					BIT(1)
#define SDP_CHARGER_BIT					BIT(0)

#define TYPE_C_STATUS_1					0x30B
#define UFP_TYPEC_MASK					GENMASK(7, 5)
#define UFP_TYPEC_RDSTD_BIT				BIT(7)
#define UFP_TYPEC_RD1P5_BIT				BIT(6)
#define UFP_TYPEC_RD3P0_BIT				BIT(5)
#define UFP_TYPEC_FMB_255K_BIT				BIT(4)
#define UFP_TYPEC_FMB_301K_BIT				BIT(3)
#define UFP_TYPEC_FMB_523K_BIT				BIT(2)
#define UFP_TYPEC_FMB_619K_BIT				BIT(1)
#define UFP_TYPEC_OPEN_OPEN_BIT				BIT(0)

#define TYPE_C_STATUS_2					0x30C
#define DFP_RA_OPEN_BIT					BIT(7)
#define TIMER_STAGE_BIT					BIT(6)
#define EXIT_UFP_MODE_BIT				BIT(5)
#define EXIT_DFP_MODE_BIT				BIT(4)
#define DFP_TYPEC_MASK					GENMASK(3, 0)
#define DFP_RD_OPEN_BIT					BIT(3)
#define DFP_RD_RA_VCONN_BIT				BIT(2)
#define DFP_RD_RD_BIT					BIT(1)
#define DFP_RA_RA_BIT					BIT(0)

#define TYPE_C_STATUS_3					0x30D
#define ENABLE_BANDGAP_BIT				BIT(7)
#define U_USB_GND_NOVBUS_BIT				BIT(6)
#define U_USB_FLOAT_NOVBUS_BIT				BIT(5)
#define U_USB_GND_BIT					BIT(4)
#define U_USB_FMB1_BIT					BIT(3)
#define U_USB_FLOAT1_BIT				BIT(2)
#define U_USB_FMB2_BIT					BIT(1)
#define U_USB_FLOAT2_BIT				BIT(0)

#define TYPE_C_STATUS_4					0x30E
#define UFP_DFP_MODE_STATUS_BIT				BIT(7)
#define TYPEC_VBUS_STATUS_BIT				BIT(6)
#define TYPEC_VBUS_ERROR_STATUS_BIT			BIT(5)
#define TYPEC_DEBOUNCE_DONE_STATUS_BIT			BIT(4)
#define TYPEC_UFP_AUDIO_ADAPT_STATUS_BIT		BIT(3)
#define TYPEC_VCONN_OVERCURR_STATUS_BIT			BIT(2)
#define CC_ORIENTATION_BIT				BIT(1)
#define CC_ATTACHED_BIT					BIT(0)

#define TYPE_C_STATUS_5					0x30F
#define TRY_SOURCE_FAILED_BIT				BIT(6)
#define TRY_SINK_FAILED_BIT				BIT(5)
#define TIMER_STAGE_2_BIT				BIT(4)
#define TYPEC_LEGACY_CABLE_STATUS_BIT			BIT(3)
#define TYPEC_NONCOMP_LEGACY_CABLE_STATUS_BIT		BIT(2)
#define TYPEC_TRYSOURCE_DETECT_STATUS_BIT		BIT(1)
#define TYPEC_TRYSINK_DETECT_STATUS_BIT			BIT(0)

#define CMD_APSD					0x341
#define ICL_OVERRIDE_BIT				BIT(1)
#define APSD_RERUN_BIT					BIT(0)

#define TYPE_C_CFG					0x358
#define APSD_START_ON_CC_BIT				BIT(7)
#define WAIT_FOR_APSD_BIT				BIT(6)
#define FACTORY_MODE_DETECTION_EN_BIT			BIT(5)
#define FACTORY_MODE_ICL_3A_4A_BIT			BIT(4)
#define FACTORY_MODE_DIS_CHGING_CFG_BIT			BIT(3)
#define SUSPEND_NON_COMPLIANT_CFG_BIT			BIT(2)
#define VCONN_OC_CFG_BIT				BIT(1)
#define TYPE_C_OR_U_USB_BIT				BIT(0)

#define TYPE_C_CFG_2					0x359
#define TYPE_C_DFP_CURRSRC_MODE_BIT			BIT(7)
#define DFP_CC_1P4V_OR_1P6V_BIT				BIT(6)
#define VCONN_SOFTSTART_CFG_MASK			GENMASK(5, 4)
#define EN_TRY_SOURCE_MODE_BIT				BIT(3)
#define USB_FACTORY_MODE_ENABLE_BIT			BIT(2)
#define TYPE_C_UFP_MODE_BIT				BIT(1)
#define EN_80UA_180UA_CUR_SOURCE_BIT			BIT(0)

#define TYPE_C_CFG_3					0x35A
#define TVBUS_DEBOUNCE_BIT				BIT(7)
#define TYPEC_LEGACY_CABLE_INT_EN_BIT			BIT(6)
#define TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN_B	BIT(5)
#define TYPEC_TRYSOURCE_DETECT_INT_EN_BIT		BIT(4)
#define TYPEC_TRYSINK_DETECT_INT_EN_BIT			BIT(3)
#define EN_TRYSINK_MODE_BIT				BIT(2)
#define EN_LEGACY_CABLE_DETECTION_BIT			BIT(1)
#define ALLOW_PD_DRING_UFP_TCCDB_BIT			BIT(0)

#define USBIN_OPTIONS_1_CFG				0x362
#define CABLE_R_SEL_BIT					BIT(7)
#define HVDCP_AUTH_ALG_EN_CFG_BIT			BIT(6)
#define HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT		BIT(5)
#define INPUT_PRIORITY_BIT				BIT(4)
#define AUTO_SRC_DETECT_BIT				BIT(3)
#define HVDCP_EN_BIT					BIT(2)
#define VADP_INCREMENT_VOLTAGE_LIMIT_BIT		BIT(1)
#define VADP_TAPER_TIMER_EN_BIT				BIT(0)

#define USBIN_OPTIONS_2_CFG				0x363
#define WIPWR_RST_EUD_CFG_BIT				BIT(7)
#define SWITCHER_START_CFG_BIT				BIT(6)
#define DCD_TIMEOUT_SEL_BIT				BIT(5)
#define OCD_CURRENT_SEL_BIT				BIT(4)
#define SLOW_PLUGIN_TIMER_EN_CFG_BIT			BIT(3)
#define FLOAT_OPTIONS_MASK				GENMASK(2, 0)
#define FLOAT_DIS_CHGING_CFG_BIT			BIT(2)
#define SUSPEND_FLOAT_CFG_BIT				BIT(1)
#define FORCE_FLOAT_SDP_CFG_BIT				BIT(0)

#define TAPER_TIMER_SEL_CFG				0x364
#define TYPEC_SPARE_CFG_BIT				BIT(7)
#define TYPEC_DRP_DFP_TIME_CFG_BIT			BIT(5)
#define TAPER_TIMER_SEL_MASK				GENMASK(1, 0)

#define USBIN_LOAD_CFG					0x365
#define USBIN_OV_CH_LOAD_OPTION_BIT			BIT(7)
#define ICL_OVERRIDE_AFTER_APSD_BIT			BIT(4)

#define USBIN_ICL_OPTIONS				0x366
#define CFG_USB3P0_SEL_BIT				BIT(2)
#define USB51_MODE_BIT					BIT(1)
#define USBIN_MODE_CHG_BIT				BIT(0)

#define TYPE_C_INTRPT_ENB_SOFTWARE_CTRL			0x368
#define EXIT_SNK_BASED_ON_CC_BIT			BIT(7)
#define VCONN_EN_ORIENTATION_BIT			BIT(6)
#define TYPEC_VCONN_OVERCURR_INT_EN_BIT			BIT(5)
#define VCONN_EN_SRC_BIT				BIT(4)
#define VCONN_EN_VALUE_BIT				BIT(3)
#define TYPEC_POWER_ROLE_CMD_MASK			GENMASK(2, 0)
#define UFP_EN_CMD_BIT					BIT(2)
#define DFP_EN_CMD_BIT					BIT(1)
#define TYPEC_DISABLE_CMD_BIT				BIT(0)

#define USBIN_CURRENT_LIMIT_CFG				0x370
#define USBIN_CURRENT_LIMIT_MASK			GENMASK(7, 0)

#define USBIN_AICL_OPTIONS_CFG				0x380
#define SUSPEND_ON_COLLAPSE_USBIN_BIT			BIT(7)
#define USBIN_AICL_HDC_EN_BIT				BIT(6)
#define USBIN_AICL_START_AT_MAX_BIT			BIT(5)
#define USBIN_AICL_RERUN_EN_BIT				BIT(4)
#define USBIN_AICL_ADC_EN_BIT				BIT(3)
#define USBIN_AICL_EN_BIT				BIT(2)
#define USBIN_HV_COLLAPSE_RESPONSE_BIT			BIT(1)
#define USBIN_LV_COLLAPSE_RESPONSE_BIT			BIT(0)

#define USBIN_5V_AICL_THRESHOLD_CFG			0x381
#define USBIN_5V_AICL_THRESHOLD_CFG_MASK		GENMASK(2, 0)

#define USBIN_CONT_AICL_THRESHOLD_CFG			0x384
#define USBIN_CONT_AICL_THRESHOLD_CFG_MASK		GENMASK(5, 0)

#define DC_ENG_SSUPPLY_CFG2				0x4C1
#define ENG_SSUPPLY_IVREF_OTG_SS_MASK			GENMASK(2, 0)
#define OTG_SS_SLOW					0x3

#define DCIN_AICL_REF_SEL_CFG				0x481
#define DCIN_CONT_AICL_THRESHOLD_CFG_MASK		GENMASK(5, 0)

#define WI_PWR_OPTIONS					0x495
#define CHG_OK_BIT					BIT(7)
#define WIPWR_UVLO_IRQ_OPT_BIT				BIT(6)
#define BUCK_HOLDOFF_ENABLE_BIT				BIT(5)
#define CHG_OK_HW_SW_SELECT_BIT				BIT(4)
#define WIPWR_RST_ENABLE_BIT				BIT(3)
#define DCIN_WIPWR_IRQ_SELECT_BIT			BIT(2)
#define AICL_SWITCH_ENABLE_BIT				BIT(1)
#define ZIN_ICL_ENABLE_BIT				BIT(0)

#define ICL_STATUS					0x607
#define INPUT_CURRENT_LIMIT_MASK			GENMASK(7, 0)

#define POWER_PATH_STATUS				0x60B
#define P_PATH_INPUT_SS_DONE_BIT			BIT(7)
#define P_PATH_USBIN_SUSPEND_STS_BIT			BIT(6)
#define P_PATH_DCIN_SUSPEND_STS_BIT			BIT(5)
#define P_PATH_USE_USBIN_BIT				BIT(4)
#define P_PATH_USE_DCIN_BIT				BIT(3)
#define P_PATH_POWER_PATH_MASK				GENMASK(2, 1)
#define P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT		BIT(0)

#define BARK_BITE_WDOG_PET				0x643
#define BARK_BITE_WDOG_PET_BIT				BIT(0)

#define WD_CFG						0x651
#define WATCHDOG_TRIGGER_AFP_EN_BIT			BIT(7)
#define BARK_WDOG_INT_EN_BIT				BIT(6)
#define BITE_WDOG_INT_EN_BIT				BIT(5)
#define SFT_AFTER_WDOG_IRQ_MASK				GENMASK(4, 3)
#define WDOG_IRQ_SFT_BIT				BIT(2)
#define WDOG_TIMER_EN_ON_PLUGIN_BIT			BIT(1)
#define WDOG_TIMER_EN_BIT				BIT(0)

#define SNARL_BARK_BITE_WD_CFG				0x653
#define BITE_WDOG_DISABLE_CHARGING_CFG_BIT		BIT(7)
#define SNARL_WDOG_TIMEOUT_MASK				GENMASK(6, 4)
#define BARK_WDOG_TIMEOUT_MASK				GENMASK(3, 2)
#define BITE_WDOG_TIMEOUT_MASK				GENMASK(1, 0)

#define AICL_RERUN_TIME_CFG				0x661
#define AICL_RERUN_TIME_MASK				GENMASK(1, 0)

#define STAT_CFG					0x690
#define STAT_SW_OVERRIDE_VALUE_BIT			BIT(7)
#define STAT_SW_OVERRIDE_CFG_BIT			BIT(6)
#define STAT_PARALLEL_OFF_DG_CFG_MASK			GENMASK(5, 4)
#define STAT_POLARITY_CFG_BIT				BIT(3)
#define STAT_PARALLEL_CFG_BIT				BIT(2)
#define STAT_FUNCTION_CFG_BIT				BIT(1)
#define STAT_IRQ_PULSING_EN_BIT				BIT(0)

#define SDP_CURRENT_UA					500000
#define CDP_CURRENT_UA					1500000
#define DCP_CURRENT_UA					1500000
#define CURRENT_MAX_UA					DCP_CURRENT_UA

/* pmi8998 registers represent current in increments of 1/40th of an amp */
#define CURRENT_SCALE_FACTOR				25000
#define SMB2_FLOAT_VOLTAGE_MIN_UV			3487500
#define SMB2_FLOAT_VOLTAGE_MAX_UV			4920000
#define SMB2_FLOAT_VOLTAGE_STEP_UV			7500

/* ================================================================
 * SMBLite specific registers and base addresses
 * Source: downstream smblite-reg.h (qpnp-smblite driver)
 * ================================================================
 */
/* SMBLite peripheral base addresses (PM4125) */
#define SMBLITE_CHGR_BASE			0x1000
#define SMBLITE_DCDC_BASE			0x1100
#define SMBLITE_BATIF_BASE			0x1200
#define SMBLITE_USBIN_BASE			0x1300
/* 0x1500 (typec) belongs to the qcom,pm4125-typec peripheral, not this driver */
#define SMBLITE_MISC_BASE			0x1600

/* ---- SMBLITE CHGR registers (chg_base offsets) ---- */

#define SMBLITE_BATTERY_CHARGER_STATUS_1	0x06
#define SMBLITE_CHARGER_VBAT_STATUS		0x08
#define SMBLITE_BAT_OV_BIT			BIT(7)

#define SMBLITE_CHARGING_ENABLE_CMD		0x46
#define SMBLITE_CHARGING_ENABLE_CMD_BIT		BIT(0)

#define SMBLITE_CHGR_FAST_CHARGE_CURRENT_CFG	0x54
#define SMBLITE_CHGR_FLOAT_VOLTAGE_CFG		0x58
/*
 * chg_base + 0x60 = CHGR_TERM_CFG_REG (termination current config).
 * SMBLite has NO pre-charge current register at this offset.
 * Do NOT write pre-charge current to 0x60.
 */
#define SMBLITE_CHGR_CHG_TERM_CFG		0x60

#define SMBLITE_CHGR_RECHG_CFG			0x70
#define SMBLITE_RECHG_MASK			GENMASK(7, 6)
#define SMBLITE_CHGR_RECHG_SOC_THD_CFG		0x74
#define SMBLITE_CHGR_CHG_INHIBIT_CFG		0x78

/*
 * ---- SMBLite DCDC registers (dcdc_base offsets) ----
 * This peripheral also holds the OTG/VBUS enable registers (0x50/0x52/0x56),
 * which belong to the qcom,pm4125-vbus-reg peripheral and are intentionally
 * not touched here. The three below are status/config registers with no
 * other home, so they are read directly by this driver.
 */
#define SMBLITE_ICL_STATUS			0x09

#define SMBLITE_POWER_PATH_STATUS		0x0B
#define SMBLITE_VALID_INPUT_POWER_SOURCE_BIT	BIT(7)
#define SMBLITE_USE_USBIN_BIT			BIT(5)

#define SMBLITE_DCDC_LDO_CFG			0x70
#define SMBLITE_LDO_MODE_BIT			BIT(0)

#define SMBLITE_BATIF_BPD_CFG			0x60

#define SMBLITE_BATIF_JEITA_COMP_STATUS		0x0C
#define SMBLITE_JEITA_ADC_THOT_AFP_BIT		BIT(5)
#define SMBLITE_JEITA_ADC_THOT_BIT		BIT(4)
#define SMBLITE_JEITA_ADC_WARM_BIT		BIT(3)
#define SMBLITE_JEITA_ADC_COOL_BIT		BIT(2)
#define SMBLITE_JEITA_ADC_TCOLD_BIT		BIT(1)
#define SMBLITE_JEITA_ADC_TCOLD_AFP_BIT		BIT(0)

#define SMBLITE_JEITA_HOT_THRESHOLD_CFG		0x84
#define SMBLITE_JEITA_WARM_THRESHOLD_CFG	0x86
#define SMBLITE_JEITA_COOL_THRESHOLD_CFG	0x88
#define SMBLITE_JEITA_COLD_THRESHOLD_CFG	0x8A

#define SMBLITE_APSD_STATUS			0x0A
#define SMBLITE_APSD_RESULT_STATUS		0x0B
#define SMBLITE_CMD_APSD			0x45
#define SMBLITE_USB_APSD_EN			0x44
#define SMBLITE_USB_APSD_CFG			0x46
#define SMBLITE_USBIN_QC23_EN			0x48
#define SMBLITE_HVDCP_EN_BIT			BIT(2)

#define SMBLITE_USBIN_ICL_OPTIONS		0x50
#define SMBLITE_USBIN_MODE_CHG_BIT		BIT(2)
#define SMBLITE_USB51_MODE_BIT			BIT(1)

#define SMBLITE_USBIN_CURRENT_LIMIT_CFG		0x52
#define SMBLITE_USBIN_INPUT_SUSPEND		0x54
#define SMBLITE_SUSPEND_ON_COLLAPSE_USBIN_BIT	BIT(7)

#define SMBLITE_USBIN_AICL_OPTIONS_CFG		0x60
#define SMBLITE_USBIN_AICL_EN_BIT		BIT(7)
#define SMBLITE_USBIN_AICL_START_AT_MAX		BIT(4)

#define SMBLITE_MISC_AICL_RERUN_CFG		0x54
#define SMBLITE_USBIN_AICL_PERIODIC_RERUN_EN_BIT BIT(5)
#define SMBLITE_USBIN_AICL_RERUN_TIME_MASK	GENMASK(1, 0)
#define SMBLITE_AICL_RERUN_TIME_12S_VAL		0x01

#define SMBLITE_WD_CFG				0x58
#define SMBLITE_BITE_WDOG_DISABLE_CHARGING_CFG_BIT BIT(7)
#define SMBLITE_BARK_WDOG_INT_EN_BIT		BIT(4)
#define SMBLITE_WDOG_TIMER_EN_ON_PLUGIN_BIT	BIT(1)
#define SMBLITE_WDOG_TIMER_EN_BIT		BIT(0)
#define SMBLITE_SNARL_BARK_BITE_WD_CFG		0x59
#define SMBLITE_BARK_BITE_WDOG_PET		0x5A
#define SMBLITE_BARK_WDOG_TIMEOUT_MASK		GENMASK(5, 4)	/* 0x30 */
#define SMBLITE_BITE_WDOG_TIMEOUT_MASK		GENMASK(7, 6)	/* 0xC0 */
#define SMBLITE_BITE_WDOG_TIMEOUT_8S		0x3
#define SMBLITE_BITE_WDOG_TIMEOUT_SHIFT		6

#define SMBLITE_ICL_STEP_UA			100000
#define SMBLITE_FCC_STEP_UA			100000
#define SMBLITE_ICL_MAX_UA			2000000
#define SMBLITE_FV_STEP_UV			20000
#define SMBLITE_FV_MIN_UV			3600000
#define SMBLITE_FV_MAX_UV			4600000

/* clang-format on */

enum charger_status {
	TRICKLE_CHARGE = 0,
	PRE_CHARGE,
	FAST_CHARGE,
	FULLON_CHARGE,
	TAPER_CHARGE,
	TERMINATE_CHARGE,
	INHIBIT_CHARGE,
	DISABLE_CHARGE,
};

enum smblite_charger_status {
	SMBLITE_INHIBIT_CHARGE = 0,
	SMBLITE_TRICKLE_CHARGE,
	SMBLITE_PRE_CHARGE,
	SMBLITE_FULLON_CHARGE,
	SMBLITE_TAPER_CHARGE,
	SMBLITE_TERMINATE_CHARGE,
	SMBLITE_CHARGING_PAUSED,
	SMBLITE_CHARGING_DISABLED,
};

struct smb_init_register {
	u16 addr;
	u8 mask;
	u8 val;
};

/**
 * struct smb_chip - smb chip structure
 * @dev:		Device reference for power_supply
 * @type: PMIC IP type (SMB2 or SMBLITE)
 * @name:		The platform device name
 * @base:		Base address for smb registers
 * @regmap:		Register map
 * @batt_info:		Battery data from DT
 * @initial_charge_enable: Charging enable state before hardware setup
 * @status_change_work: Worker to handle plug/unplug events
 * @cable_irq:		USB plugin IRQ
 * @wdog_irq:		Watchdog bark IRQ
 * @wakeup_enabled:	If the cable IRQ will cause a wakeup
 * @usb_in_i_chan:	USB_IN current measurement channel
 * @usb_in_v_chan:	USB_IN voltage measurement channel
 * @chg_psy:		Charger power supply instance
 * @ldo_mode: True if operating in LDO mode (SMBLITE only)
 * @icl_step_ua: ICL step size in uA
 * @icl_max_ua: Maximum ICL in uA
 * @charge_status: Cached POWER_SUPPLY_STATUS_* value
 */
struct smb_chip {
	struct device *dev;
	enum smb_type type;
	const char *name;
	unsigned int base;
	struct regmap *regmap;
	struct power_supply_battery_info *batt_info;
	u8 initial_charge_enable;

	struct delayed_work status_change_work;
	int cable_irq;
	int wdog_irq;
	bool wakeup_enabled;

	struct iio_channel *usb_in_i_chan;
	struct iio_channel *usb_in_v_chan;

	struct power_supply *chg_psy;
	bool ldo_mode;
	unsigned int icl_step_ua;
	unsigned int icl_max_ua;
	int charge_status;
};

static enum power_supply_property smb_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static enum power_supply_property smblite_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
};

/*
 * smb_get_prop_usb_online - check whether a valid USB input is present.
 *
 * SMB2  (0x60B): P_PATH_USE_USBIN BIT(4) && VALID_INPUT BIT(0)
 * SMBLite (dcdc_base + 0x0B = 0x110B):
 *   VALID_INPUT_POWER_SOURCE_STS BIT(7) && USE_USBIN BIT(5)
 */
static int smb_get_prop_usb_online(struct smb_chip *chip, int *val)
{
	unsigned int stat;
	u16 reg;
	int rc;

	if (chip->type == SMBLITE)
		reg = SMBLITE_DCDC_BASE + SMBLITE_POWER_PATH_STATUS;
	else
		reg = chip->base + POWER_PATH_STATUS;

	rc = regmap_read(chip->regmap, reg, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read power path status: %d\n", rc);
		return rc;
	}

	if (chip->type == SMBLITE)
		*val = (stat & SMBLITE_VALID_INPUT_POWER_SOURCE_BIT) &&
		       (stat & SMBLITE_USE_USBIN_BIT);
	else
		*val = (stat & P_PATH_USE_USBIN_BIT) &&
		       (stat & P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT);

	return 0;
}

/*
 * Qualcomm "automatic power source detection" aka APSD
 * tells us what type of charger we're connected to.
 */
static int smb_apsd_get_charger_type(struct smb_chip *chip, int *val)
{
	unsigned int apsd_stat, stat;
	u16 apsd_reg, result_reg;
	int usb_online = 0;
	int rc;

	rc = smb_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		return rc;
	}

	if (chip->type == SMBLITE) {
		apsd_reg   = SMBLITE_USBIN_BASE + SMBLITE_APSD_STATUS;
		result_reg = SMBLITE_USBIN_BASE + SMBLITE_APSD_RESULT_STATUS;
	} else {
		apsd_reg   = chip->base + APSD_STATUS;
		result_reg = chip->base + APSD_RESULT_STATUS;
	}

	rc = regmap_read(chip->regmap, apsd_reg, &apsd_stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd status, rc = %d", rc);
		return rc;
	}
	if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT)) {
		dev_dbg(chip->dev, "Apsd not ready");
		return -EAGAIN;
	}

	rc = regmap_read(chip->regmap, result_reg, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd result, rc = %d", rc);
		return rc;
	}

	stat &= APSD_RESULT_STATUS_MASK;

	if (stat & CDP_CHARGER_BIT)
		*val = POWER_SUPPLY_USB_TYPE_CDP;
	else if (stat & (DCP_CHARGER_BIT | OCP_CHARGER_BIT | FLOAT_CHARGER_BIT))
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	else /* SDP_CHARGER_BIT (or others) */
		*val = POWER_SUPPLY_USB_TYPE_SDP;

	return 0;
}

static unsigned int smb_usb_type_current_ua(enum power_supply_usb_type usb_type)
{
	switch (usb_type) {
	case POWER_SUPPLY_USB_TYPE_CDP:
		return CDP_CURRENT_UA;
	case POWER_SUPPLY_USB_TYPE_DCP:
		return DCP_CURRENT_UA;
	case POWER_SUPPLY_USB_TYPE_SDP:
	default:
		return SDP_CURRENT_UA;
	}
}

/*
 * smb_get_prop_status - report POWER_SUPPLY_STATUS_* for the battery.
 *
 * BAT_OV detection:
 *   SMB2:    BATTERY_CHARGER_STATUS_2 (chip->base + 0x07), BIT(5)
 *   SMBLite: CHARGER_VBAT_STATUS_REG (chg_base + 0x08 = 0x1008), BIT(7)
 *
 * Charger status enum differs between IPs -- handled by dedicated branches.
 */
static int smb_get_prop_status(struct smb_chip *chip, int *val)
{
	unsigned int chg_status, stat_batov;
	u16 chg_status_reg, reg_batov;
	int usb_online = 0, rc;

	rc = smb_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		return rc;
	}

	if (chip->type == SMBLITE) {
		chg_status_reg = SMBLITE_CHGR_BASE + SMBLITE_BATTERY_CHARGER_STATUS_1;
		reg_batov = SMBLITE_CHGR_BASE + SMBLITE_CHARGER_VBAT_STATUS;
	} else {
		chg_status_reg = chip->base + BATTERY_CHARGER_STATUS_1;
		reg_batov = chip->base + BATTERY_CHARGER_STATUS_2;
	}

	rc = regmap_read(chip->regmap, reg_batov, &stat_batov);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read bat-ov status ret=%d\n", rc);
		return rc;
	}

	/* Check BAT_OV -- different register/bit per IP */
	if (chip->type == SMBLITE) {
		if (stat_batov & SMBLITE_BAT_OV_BIT) {
			*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
			chip->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			return 0;
		}
	} else {
		if (stat_batov & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
			*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
			chip->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			return 0;
		}
	}

	rc = regmap_read(chip->regmap, chg_status_reg, &chg_status);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read charging status ret=%d\n", rc);
		return rc;
	}

	chg_status &= BATTERY_CHARGER_STATUS_MASK;

	if (chip->type == SMBLITE) {
		switch (chg_status) {
		case SMBLITE_TRICKLE_CHARGE:
		case SMBLITE_PRE_CHARGE:
		case SMBLITE_FULLON_CHARGE:
		case SMBLITE_TAPER_CHARGE:
			*val = POWER_SUPPLY_STATUS_CHARGING;
			chip->charge_status = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case SMBLITE_TERMINATE_CHARGE:
			*val = POWER_SUPPLY_STATUS_FULL;
			chip->charge_status = POWER_SUPPLY_STATUS_FULL;
			break;
		case SMBLITE_INHIBIT_CHARGE:
		case SMBLITE_CHARGING_PAUSED:
		case SMBLITE_CHARGING_DISABLED:
			*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
			chip->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		default:
			*val = POWER_SUPPLY_STATUS_UNKNOWN;
			chip->charge_status = POWER_SUPPLY_STATUS_UNKNOWN;
			break;
		}
	} else {
		switch (chg_status) {
		case TRICKLE_CHARGE:
		case PRE_CHARGE:
		case FAST_CHARGE:
		case FULLON_CHARGE:
		case TAPER_CHARGE:
			*val = POWER_SUPPLY_STATUS_CHARGING;
			chip->charge_status = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case DISABLE_CHARGE:
			*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
			chip->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		case TERMINATE_CHARGE:
		case INHIBIT_CHARGE:
			*val = POWER_SUPPLY_STATUS_FULL;
			chip->charge_status = POWER_SUPPLY_STATUS_FULL;
			break;
		default:
			*val = POWER_SUPPLY_STATUS_UNKNOWN;
			chip->charge_status = POWER_SUPPLY_STATUS_UNKNOWN;
			break;
		}
	}

	return 0;
}

static inline int smb_get_current_limit(struct smb_chip *chip,
					 unsigned int *val)
{
	u16 reg;
	int rc;

	if (chip->type == SMBLITE)
		reg = SMBLITE_DCDC_BASE + SMBLITE_ICL_STATUS;
	else
		reg = chip->base + ICL_STATUS;

	rc = regmap_read(chip->regmap, reg, val);
	if (rc >= 0)
		*val *= chip->icl_step_ua;
	return rc;
}

static int smb_set_current_limit(struct smb_chip *chip, unsigned int val)
{
	unsigned char val_raw;
	u16 reg;

	if (val > chip->icl_max_ua) {
		dev_err(chip->dev, "Can't set current limit higher than %duA",
			chip->icl_max_ua);
		return -EINVAL;
	}

	val_raw = val / chip->icl_step_ua;

	if (chip->type == SMBLITE)
		reg = SMBLITE_USBIN_BASE + SMBLITE_USBIN_CURRENT_LIMIT_CFG;
	else
		reg = chip->base + USBIN_CURRENT_LIMIT_CFG;

	return regmap_write(chip->regmap, reg, val_raw);
}

static void smb_status_change_work(struct work_struct *work)
{
	unsigned int charger_type, current_ua;
	int usb_online = 0;
	int count, rc;
	struct smb_chip *chip;

	chip = container_of(work, struct smb_chip, status_change_work.work);

	smb_get_prop_usb_online(chip, &usb_online);
	if (!usb_online)
		return;

	for (count = 0; count < 3; count++) {
		dev_dbg(chip->dev, "get charger type retry %d\n", count);
		rc = smb_apsd_get_charger_type(chip, &charger_type);
		if (rc != -EAGAIN)
			break;
		msleep(100);
	}

	if (rc < 0 && rc != -EAGAIN) {
		dev_err(chip->dev, "get charger type failed: %d\n", rc);
		return;
	}

	if (rc < 0) {
		if (chip->type == SMBLITE)
			rc = regmap_update_bits(chip->regmap,
						SMBLITE_USBIN_BASE + SMBLITE_CMD_APSD,
						APSD_RERUN_BIT, APSD_RERUN_BIT);
		else
			rc = regmap_update_bits(chip->regmap, chip->base + CMD_APSD,
						APSD_RERUN_BIT, APSD_RERUN_BIT);
		schedule_delayed_work(&chip->status_change_work,
				      msecs_to_jiffies(1000));
		dev_dbg(chip->dev, "get charger type failed, rerun apsd\n");
		return;
	}

	current_ua = smb_usb_type_current_ua(charger_type);
	smb_set_current_limit(chip, current_ua);
	power_supply_changed(chip->chg_psy);
}

static int smb_get_iio_chan(struct smb_chip *chip, struct iio_channel *chan,
			     int *val)
{
	int rc;
	union power_supply_propval status;

	rc = power_supply_get_property(chip->chg_psy, POWER_SUPPLY_PROP_STATUS,
				       &status);
	if (rc < 0 || status.intval != POWER_SUPPLY_STATUS_CHARGING) {
		*val = 0;
		return 0;
	}

	if (IS_ERR(chan)) {
		dev_err(chip->dev, "Failed to chan, err = %li", PTR_ERR(chan));
		return PTR_ERR(chan);
	}

	return iio_read_channel_processed(chan, val);
}

/*
 * smb_get_prop_health - battery health based on temperature status.
 *
 * SMBLite: BATTERY_TEMP_STATUS_REG = batif_base + 0x0C (smblite-reg.h)
 * SMB2:    BATTERY_CHARGER_STATUS_2 = chip->base + 0x07
 */
static int smb_get_prop_health(struct smb_chip *chip, int *val)
{
	int rc;
	unsigned int stat;

	if (chip->type == SMBLITE) {
		rc = regmap_read(chip->regmap,
				 SMBLITE_CHGR_BASE + SMBLITE_CHARGER_VBAT_STATUS,
				 &stat);
		if (rc < 0) {
			dev_err(chip->dev,
				"Couldn't read SMBLITE vbat status rc=%d\n", rc);
			return rc;
		}
		if (stat & SMBLITE_BAT_OV_BIT) {
			*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
			return 0;
		}

		rc = regmap_read(chip->regmap,
				 SMBLITE_BATIF_BASE + SMBLITE_BATIF_JEITA_COMP_STATUS,
				 &stat);
		if (rc < 0) {
			dev_err(chip->dev,
				"Couldn't read SMBLITE JEITA status rc=%d\n", rc);
			return rc;
		}
		if (stat & (SMBLITE_JEITA_ADC_THOT_AFP_BIT |
			    SMBLITE_JEITA_ADC_THOT_BIT)) {
			*val = POWER_SUPPLY_HEALTH_OVERHEAT;
			return 0;
		}
		if (stat & (SMBLITE_JEITA_ADC_TCOLD_AFP_BIT |
			    SMBLITE_JEITA_ADC_TCOLD_BIT)) {
			*val = POWER_SUPPLY_HEALTH_COLD;
			return 0;
		}
		if (stat & SMBLITE_JEITA_ADC_WARM_BIT) {
			*val = POWER_SUPPLY_HEALTH_WARM;
			return 0;
		}
		if (stat & SMBLITE_JEITA_ADC_COOL_BIT) {
			*val = POWER_SUPPLY_HEALTH_COOL;
			return 0;
		}
		*val = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	}

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_2,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status rc=%d\n", rc);
		return rc;
	}

	if (stat & CHARGER_ERROR_STATUS_BAT_OV_BIT)
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else if (stat & BAT_TEMP_STATUS_TOO_COLD_BIT)
		*val = POWER_SUPPLY_HEALTH_COLD;
	else if (stat & BAT_TEMP_STATUS_TOO_HOT_BIT)
		*val = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (stat & BAT_TEMP_STATUS_COLD_SOFT_LIMIT_BIT)
		*val = POWER_SUPPLY_HEALTH_COOL;
	else if (stat & BAT_TEMP_STATUS_HOT_SOFT_LIMIT_BIT)
		*val = POWER_SUPPLY_HEALTH_WARM;
	else
		*val = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int smb_get_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	struct smb_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Qualcomm";
		return 0;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = chip->name;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return smb_get_current_limit(chip, &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (chip->type == SMBLITE)
			return -EOPNOTSUPP;
		return smb_get_iio_chan(chip, chip->usb_in_i_chan,
					 &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return smb_get_iio_chan(chip, chip->usb_in_v_chan,
					 &val->intval);
	case POWER_SUPPLY_PROP_ONLINE:
		return smb_get_prop_usb_online(chip, &val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		return smb_get_prop_status(chip, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return smb_get_prop_health(chip, &val->intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		return smb_apsd_get_charger_type(chip, &val->intval);
	case POWER_SUPPLY_PROP_CHARGE_TYPE: {
		int status, rc;

		if (chip->type != SMBLITE)
			return -EOPNOTSUPP;

		rc = smb_get_prop_status(chip, &status);
		if (rc < 0)
			return rc;

		switch (status) {
		case POWER_SUPPLY_STATUS_FULL:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			break;
		case POWER_SUPPLY_STATUS_CHARGING:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_STANDARD;
			break;
		default:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
			break;
		}

		return 0;
	}
	default:
		dev_err(chip->dev, "invalid property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb_set_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     const union power_supply_propval *val)
{
	struct smb_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return smb_set_current_limit(chip, val->intval);
	default:
		dev_err(chip->dev, "No setter for property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb_property_is_writable(struct power_supply *psy,
				     enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return 1;
	default:
		return 0;
	}
}

/*
 * smb_handle_batt_overvoltage - IRQ handler for battery overvoltage.
 *
 * SMB2:    chip->base + BATTERY_CHARGER_STATUS_2 (offset 0x07), BIT(5)
 * SMBLite: SMBLITE_CHGR_BASE + SMBLITE_CHARGER_VBAT_STATUS (0x1008), BIT(7)
 */
static irqreturn_t smb_handle_batt_overvoltage(int irq, void *data)
{
	struct smb_chip *chip = data;
	unsigned int status;
	u16 reg;
	u8 bit;
	int rc;

	if (chip->type == SMBLITE) {
		reg = SMBLITE_CHGR_BASE + SMBLITE_CHARGER_VBAT_STATUS;
		bit = SMBLITE_BAT_OV_BIT;
	} else {
		reg = chip->base + BATTERY_CHARGER_STATUS_2;
		bit = CHARGER_ERROR_STATUS_BAT_OV_BIT;
	}

	rc = regmap_read(chip->regmap, reg, &status);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status: %d\n", rc);
		return IRQ_HANDLED;
	}

	if (status & bit)
		dev_err(chip->dev, "battery overvoltage detected\n");

	power_supply_changed(chip->chg_psy);

	return IRQ_HANDLED;
}

/*
 * smb_handle_usb_plugin - IRQ handler for USB plug/unplug events.
 *
 * SMBLite: if the charger shows INHIBIT_CHARGE (0x00) right after plug,
 * toggle the charging enable bit (chg_base + 0x46) to kick-start charging.
 * SMB2: simply schedules the status work.
 */
static irqreturn_t smb_handle_usb_plugin(int irq, void *data)
{
	struct smb_chip *chip = data;
	int usb_online = 0;
	int rc;

	power_supply_changed(chip->chg_psy);
	smb_get_prop_usb_online(chip, &usb_online);

	if (!usb_online)
		goto schedule_work;

	if (chip->type == SMBLITE) {
		unsigned int chgr_status;
		u16 chgr_reg  = SMBLITE_CHGR_BASE + SMBLITE_BATTERY_CHARGER_STATUS_1;
		u16 chgen_reg = SMBLITE_CHGR_BASE + SMBLITE_CHARGING_ENABLE_CMD;

		rc = regmap_read(chip->regmap, chgr_reg, &chgr_status);
		if (rc < 0) {
			dev_err(chip->dev,
				"Couldn't read charger status: %d\n", rc);
			goto schedule_work;
		}

		if ((chgr_status & BATTERY_CHARGER_STATUS_MASK) ==
		    SMBLITE_INHIBIT_CHARGE) {
			rc = regmap_update_bits(chip->regmap, chgen_reg,
						SMBLITE_CHARGING_ENABLE_CMD_BIT, 0);
			if (rc < 0) {
				dev_err(chip->dev,
					"Couldn't disable charging: %d\n", rc);
				goto schedule_work;
			}

			usleep_range(10000, 10100);
			rc = regmap_update_bits(chip->regmap, chgen_reg,
						SMBLITE_CHARGING_ENABLE_CMD_BIT,
						SMBLITE_CHARGING_ENABLE_CMD_BIT);
			if (rc < 0)
				dev_err(chip->dev,
					"Couldn't re-enable charging: %d\n", rc);
		}
	}

schedule_work:
	schedule_delayed_work(&chip->status_change_work,
			      msecs_to_jiffies(1500));

	return IRQ_HANDLED;
}

static irqreturn_t smb_handle_usb_icl_change(int irq, void *data)
{
	struct smb_chip *chip = data;

	power_supply_changed(chip->chg_psy);

	return IRQ_HANDLED;
}

/*
 * smb_handle_wdog_bark - pet the watchdog after a bark interrupt.
 *
 * SMB2:    MISC base 0x600 + 0x43 = 0x643
 * SMBLite: MISC base 0x1600 + 0x5A = 0x165A  (per smblite-reg.h)
 *
 * NOTE: downstream qpnp-smblite enables the watchdog (WD_CFG bits
 * WDOG_TIMER_EN_ON_PLUGIN + BARK_WDOG_INT_EN + BITE_WDOG_DISABLE_CHARGING).
 * The wdog-bark IRQ is registered here to pet it; the watchdog config itself
 * is written in smblite_init_seq[].
 */
static irqreturn_t smb_handle_wdog_bark(int irq, void *data)
{
	struct smb_chip *chip = data;
	u16 reg;
	int rc;

	power_supply_changed(chip->chg_psy);

	if (chip->type == SMBLITE)
		reg = SMBLITE_MISC_BASE + SMBLITE_BARK_BITE_WDOG_PET;
	else
		reg = chip->base + BARK_BITE_WDOG_PET;

	rc = regmap_write(chip->regmap, reg, BARK_BITE_WDOG_PET_BIT);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't pet the dog rc=%d\n", rc);

	return IRQ_HANDLED;
}

/* ================================================================
 * SMBLite-specific initialisation helpers
 * ================================================================
 */
static int smblite_write_jeita_16(struct smb_chip *chip, u16 addr, u16 val)
{
	u8 buf[2];

	buf[0] = (val >> 8) & 0xff;
	buf[1] = val & 0xff;
	return regmap_bulk_write(chip->regmap, addr, buf, 2);
}

static int smblite_program_jeita_thresholds(struct smb_chip *chip)
{
	u32 soft[2], hard[2];
	struct device_node *np = chip->dev->of_node;
	bool soft_present, hard_present;
	int rc;

	if (chip->type != SMBLITE || !np)
		return 0;

	soft_present = of_find_property(np, "qcom,jeita-soft-thresholds", NULL);
	hard_present = of_find_property(np, "qcom,jeita-hard-thresholds", NULL);
	if (!soft_present && !hard_present)
		return 0;
	if (!soft_present || !hard_present)
		return -EINVAL;

	rc = of_property_read_u32_array(np,
					"qcom,jeita-soft-thresholds",
					soft, ARRAY_SIZE(soft));
	if (rc)
		return rc;

	rc = of_property_read_u32_array(np,
					"qcom,jeita-hard-thresholds",
					hard, ARRAY_SIZE(hard));
	if (rc)
		return rc;

	if (soft[0] > 0xffff || soft[1] > 0xffff ||
	    hard[0] > 0xffff || hard[1] > 0xffff)
		return -EINVAL;

	rc = smblite_write_jeita_16(chip,
			SMBLITE_BATIF_BASE + SMBLITE_JEITA_COOL_THRESHOLD_CFG, soft[0]);
	if (rc < 0)
		return rc;

	rc = smblite_write_jeita_16(chip,
			SMBLITE_BATIF_BASE + SMBLITE_JEITA_WARM_THRESHOLD_CFG, soft[1]);
	if (rc < 0)
		return rc;

	rc = smblite_write_jeita_16(chip,
			SMBLITE_BATIF_BASE + SMBLITE_JEITA_COLD_THRESHOLD_CFG, hard[0]);
	if (rc < 0)
		return rc;

	rc = smblite_write_jeita_16(chip,
			SMBLITE_BATIF_BASE + SMBLITE_JEITA_HOT_THRESHOLD_CFG, hard[1]);
	if (rc < 0)
		return rc;

	dev_dbg(chip->dev,
		"JEITA: COOL=0x%04x WARM=0x%04x COLD=0x%04x HOT=0x%04x\n",
		soft[0] & 0xffff, soft[1] & 0xffff,
		hard[0] & 0xffff, hard[1] & 0xffff);

	return 0;
}

/*
 * smblite_config_fast_charge_safety_timer - configure the fast-charge
 * safety timer.
 *
 * Matches downstream smblite_init_hw()'s masked write on
 * CHGR_FAST_CHARGE_SAFETY_TIMER_CFG_REG (chg_base + 0x90):
 *   mask = FAST_CHARGE_SAFETY_TIMER_EN_BIT | FAST_CHARGE_SAFETY_TIMER_MASK (0x0B)
 *   val  = FAST_CHARGE_SAFETY_TIMER_EN_BIT | FAST_CHARGE_SAFETY_TIMER_768_MIN (0x0A)
 *
 * This register has no relation to JEITA. The downstream qpnp-smblite
 * driver never programs JEITA thresholds for SMBLite at all, so there
 * is no downstream reference for an "enable JEITA" bit or register --
 * smblite_program_jeita_thresholds() only writes the threshold
 * comparator registers themselves.
 */
static int smblite_config_fast_charge_safety_timer(struct smb_chip *chip)
{
	if (chip->type != SMBLITE)
		return 0;

	return regmap_update_bits(chip->regmap,
				  SMBLITE_CHGR_BASE + 0x90,
				  0x0B, 0x0A);
}

static int smblite_write_init_reg(struct smb_chip *chip, u16 addr, u8 val,
				  const char *name)
{
	int rc;

	rc = regmap_write(chip->regmap, addr, val);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't configure %s\n", name);

	return 0;
}

static int smblite_detect_ldo_mode(struct smb_chip *chip)
{
	unsigned int val;
	int rc;

	if (chip->type != SMBLITE)
		return 0;

	rc = regmap_read(chip->regmap,
			 SMBLITE_DCDC_BASE + SMBLITE_DCDC_LDO_CFG, &val);
	if (rc < 0)
		return rc;

	chip->ldo_mode = !!(val & SMBLITE_LDO_MODE_BIT);
	dev_info(chip->dev, "Operating in %s mode\n",
		 chip->ldo_mode ? "LDO" : "switching");
	return 0;
}

static int smblite_init_hw_specific(struct smb_chip *chip)
{
	if (chip->type != SMBLITE)
		return 0;

	return smblite_detect_ldo_mode(chip);
}

static const struct power_supply_desc smb_psy_desc = {
	.name = "pmi8998_charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = smb_properties,
	.num_properties = ARRAY_SIZE(smb_properties),
	.get_property = smb_get_property,
	.set_property = smb_set_property,
	.property_is_writeable = smb_property_is_writable,
};

/* Init sequence derived from vendor downstream driver */
static const struct smb_init_register smb_init_seq[] = {
	{ .addr = AICL_RERUN_TIME_CFG, .mask = AICL_RERUN_TIME_MASK, .val = 0 },
	/*
	 * By default configure us as an upstream facing port
	 * FIXME: This will be handled by the type-c driver
	 */
	{ .addr = TYPE_C_INTRPT_ENB_SOFTWARE_CTRL,
	  .mask = TYPEC_POWER_ROLE_CMD_MASK | VCONN_EN_SRC_BIT |
		  VCONN_EN_VALUE_BIT,
	  .val = VCONN_EN_SRC_BIT },
	/*
	 * Disable Type-C factory mode and stay in Attached.SRC state when VCONN
	 * over-current happens
	 */
	{ .addr = TYPE_C_CFG,
	  .mask = FACTORY_MODE_DETECTION_EN_BIT | VCONN_OC_CFG_BIT,
	  .val = 0 },
	/* Configure VBUS for software control */
	{ .addr = OTG_CFG, .mask = OTG_EN_SRC_CFG_BIT, .val = 0 },
	/*
	 * Use VBAT to determine the recharge threshold when battery is full
	 * rather than the state of charge.
	 */
	{ .addr = FG_UPDATE_CFG_2_SEL,
	  .mask = SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT |
		  VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT,
	  .val = VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT },
	{ .addr = USBIN_OPTIONS_1_CFG, .mask = HVDCP_EN_BIT, .val = 0 },
	/*
	 * Match downstream defaults
	 * CHG_EN_SRC_BIT - charger enable is controlled by software
	 * CHG_EN_POLARITY_BIT - polarity of charge enable pin when in HW control
	 *                       pulled low on OnePlus 6 and SHIFT6mq
	 * PRETOFAST_TRANSITION_CFG_BIT -
	 * BAT_OV_ECC_BIT -
	 * I_TERM_BIT - Current termination ?? 0 = enabled
	 * AUTO_RECHG_BIT - Enable automatic recharge when battery is full
	 *                  0 = enabled
	 * EN_ANALOG_DROP_IN_VBATT_BIT
	 * CHARGER_INHIBIT_BIT - Inhibit charging based on battery voltage
	 *                       instead of ??
	 */
	{ .addr = CHGR_CFG2,
	  .mask = CHG_EN_SRC_BIT | CHG_EN_POLARITY_BIT |
		  PRETOFAST_TRANSITION_CFG_BIT | BAT_OV_ECC_BIT | I_TERM_BIT |
		  AUTO_RECHG_BIT | EN_ANALOG_DROP_IN_VBATT_BIT |
		  CHARGER_INHIBIT_BIT,
	  .val = CHARGER_INHIBIT_BIT },
	/*
	 * Set the enable command only once CHGR_CFG2 has selected software
	 * control, or a charger its watchdog has disabled stays disabled.
	 */
	{ .addr = CHARGING_ENABLE_CMD,
	  .mask = CHARGING_ENABLE_CMD_BIT,
	  .val = CHARGING_ENABLE_CMD_BIT },
	/* STAT pin software override, match downstream. Parallel charging? */
	{ .addr = STAT_CFG,
	  .mask = STAT_SW_OVERRIDE_CFG_BIT,
	  .val = STAT_SW_OVERRIDE_CFG_BIT },
	/* Set the default SDP charger type to a 500ma USB 2.0 port */
	{ .addr = USBIN_ICL_OPTIONS,
	  .mask = USB51_MODE_BIT | USBIN_MODE_CHG_BIT,
	  .val = USB51_MODE_BIT },
	/* Disable watchdog */
	{ .addr = SNARL_BARK_BITE_WD_CFG, .mask = 0xff, .val = 0 },
	{ .addr = WD_CFG,
	  .mask = WATCHDOG_TRIGGER_AFP_EN_BIT | WDOG_TIMER_EN_ON_PLUGIN_BIT |
		  BARK_WDOG_INT_EN_BIT,
	  .val = 0 },
	/* These bits aren't documented anywhere */
	{ .addr = USBIN_5V_AICL_THRESHOLD_CFG,
	  .mask = USBIN_5V_AICL_THRESHOLD_CFG_MASK,
	  .val = 0x3 },
	{ .addr = USBIN_CONT_AICL_THRESHOLD_CFG,
	  .mask = USBIN_CONT_AICL_THRESHOLD_CFG_MASK,
	  .val = 0x3 },
	/*
	 * Enable Automatic Input Current Limit, this will slowly ramp up the current
	 * When connected to a wall charger, and automatically stop when it detects
	 * the charger current limit (voltage drop?) or it reaches the programmed limit.
	 */
	{ .addr = USBIN_AICL_OPTIONS_CFG,
	  .mask = USBIN_AICL_START_AT_MAX_BIT | USBIN_AICL_ADC_EN_BIT |
		  USBIN_AICL_EN_BIT | SUSPEND_ON_COLLAPSE_USBIN_BIT |
		  USBIN_HV_COLLAPSE_RESPONSE_BIT |
		  USBIN_LV_COLLAPSE_RESPONSE_BIT,
	  .val = USBIN_HV_COLLAPSE_RESPONSE_BIT |
		 USBIN_LV_COLLAPSE_RESPONSE_BIT | USBIN_AICL_EN_BIT },
	/*
	 * Set pre charge current to default, the OnePlus 6 bootloader
	 * sets this very conservatively.
	 */
	{ .addr = PRE_CHARGE_CURRENT_CFG,
	  .mask = PRE_CHARGE_CURRENT_SETTING_MASK,
	  .val = 500000 / CURRENT_SCALE_FACTOR },
	/*
	 * This overrides all of the current limit options exposed to userspace
	 * and prevents the device from pulling more than ~1A. This is done
	 * to minimise potential fire hazard risks.
	 */
	{ .addr = FAST_CHARGE_CURRENT_CFG,
	  .mask = FAST_CHARGE_CURRENT_SETTING_MASK,
	  .val = 1000000 / CURRENT_SCALE_FACTOR },
};

/* ================================================================
 * SMBLite init sequence (PM4125)
 * All addresses are absolute (peripheral_base + offset).
 *
 * Modelled strictly after downstream smblite_init_hw() in qpnp-smblite.c.
 * Key differences from SMB2:
 *   - CHARGING_ENABLE_CMD is at chg_base + 0x46 (not 0x42)
 *   - Watchdog IS enabled (downstream enables it with specific timeout bits)
 *   - No pre-charge current register at 0x60 (that is CHGR_TERM_CFG_REG)
 *   - AICL enable bit is BIT(7) in usbin_base + 0x60
 *   - Suspend-on-collapse is in usbin_base + 0x54 BIT(7)
 * ================================================================
 */
static const struct smb_init_register smblite_init_seq[] = {
	/* AICL: periodic rerun every 12 s (misc_base + 0x54) */
	{ .addr = SMBLITE_MISC_BASE + SMBLITE_MISC_AICL_RERUN_CFG,
	  .mask = SMBLITE_USBIN_AICL_PERIODIC_RERUN_EN_BIT |
		  SMBLITE_USBIN_AICL_RERUN_TIME_MASK,
	  .val = SMBLITE_USBIN_AICL_PERIODIC_RERUN_EN_BIT |
		  SMBLITE_AICL_RERUN_TIME_12S_VAL },
	/* Enable AICL (usbin_base + 0x60) */
	{ .addr = SMBLITE_USBIN_BASE + SMBLITE_USBIN_AICL_OPTIONS_CFG,
	  .mask = SMBLITE_USBIN_AICL_EN_BIT | SMBLITE_USBIN_AICL_START_AT_MAX,
	  .val = SMBLITE_USBIN_AICL_EN_BIT },
	/* Suspend on collapse (usbin_base + 0x54 BIT(7)) */
	{ .addr = SMBLITE_USBIN_BASE + SMBLITE_USBIN_INPUT_SUSPEND,
	  .mask = SMBLITE_SUSPEND_ON_COLLAPSE_USBIN_BIT,
	  .val = SMBLITE_SUSPEND_ON_COLLAPSE_USBIN_BIT },
	/*
	 * Watchdog config (misc_base + 0x59):
	 * Bark timeout = 16 s (ilog2(16/16)=0 << shift), bite timeout = 8 s.
	 * Matches downstream DEFAULT_WD_BARK_TIME=16.
	 */
	{ .addr = SMBLITE_MISC_BASE + SMBLITE_SNARL_BARK_BITE_WD_CFG,
	  .mask = SMBLITE_BARK_WDOG_TIMEOUT_MASK | SMBLITE_BITE_WDOG_TIMEOUT_MASK, /* 0xF0 */
	  .val = (SMBLITE_BITE_WDOG_TIMEOUT_8S << SMBLITE_BITE_WDOG_TIMEOUT_SHIFT) }, /* 0xC0 */
	/*
	 * Enable watchdog: WDOG_TIMER_EN_ON_PLUGIN + BARK_WDOG_INT_EN
	 * + BITE_WDOG_DISABLE_CHARGING (misc_base + 0x58)
	 * Matches downstream WD_CFG_REG enable sequence.
	 */
	{ .addr = SMBLITE_MISC_BASE + SMBLITE_WD_CFG,
	  .mask = SMBLITE_WDOG_TIMER_EN_BIT | SMBLITE_WDOG_TIMER_EN_ON_PLUGIN_BIT |
		  SMBLITE_BARK_WDOG_INT_EN_BIT |
		  SMBLITE_BITE_WDOG_DISABLE_CHARGING_CFG_BIT,
	  .val = SMBLITE_WDOG_TIMER_EN_ON_PLUGIN_BIT |
		  SMBLITE_BARK_WDOG_INT_EN_BIT |
		  SMBLITE_BITE_WDOG_DISABLE_CHARGING_CFG_BIT },
	/* SDP: 500 mA USB 2.0 (usbin_base + 0x50) */
	{ .addr = SMBLITE_USBIN_BASE + SMBLITE_USBIN_ICL_OPTIONS,
	  .mask = SMBLITE_USB51_MODE_BIT | SMBLITE_USBIN_MODE_CHG_BIT,
	  .val = SMBLITE_USB51_MODE_BIT },
	/* Disable HVDCP (usbin_base + 0x48) */
	{ .addr = SMBLITE_USBIN_BASE + SMBLITE_USBIN_QC23_EN,
	  .mask = SMBLITE_HVDCP_EN_BIT,
	  .val = 0 },
	/* Fast-charge current 1.5 A (chg_base + 0x54) */
	{ .addr = SMBLITE_CHGR_BASE + SMBLITE_CHGR_FAST_CHARGE_CURRENT_CFG,
	  .mask = FAST_CHARGE_CURRENT_SETTING_MASK,
	  .val = 1500000 / SMBLITE_FCC_STEP_UA },
};

static int smb_init_hw(struct smb_chip *chip)
{
	const struct smb_init_register *init_seq;
	int seq_len, rc, i;

	rc = smblite_init_hw_specific(chip);
	if (rc < 0)
		return rc;

	if (chip->type == SMBLITE) {
		init_seq = smblite_init_seq;
		seq_len  = ARRAY_SIZE(smblite_init_seq);
	} else {
		init_seq = smb_init_seq;
		seq_len  = ARRAY_SIZE(smb_init_seq);
	}

	for (i = 0; i < seq_len; i++) {
		/*
		 * SMB2: addresses in smb_init_seq are relative offsets;
		 *       prepend chip->base (read from DT "reg").
		 * SMBLite: addresses in smblite_init_seq are already absolute.
		 */
		u32 addr = (chip->type == SMB2) ?
				(chip->base + init_seq[i].addr) :
				init_seq[i].addr;

		dev_dbg(chip->dev, "%d: Writing 0x%02x to 0x%04x\n", i,
			init_seq[i].val, addr);

		rc = regmap_update_bits(chip->regmap, addr,
					init_seq[i].mask,
					init_seq[i].val);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "%s: init command %d failed\n",
					     __func__, i);
	}

	/*
	 * SMBLite vendor bring-up register writes.
	 * These are undocumented initialisation values derived from the
	 * downstream qpnp-smblite driver.  They are SMBLITE-only.
	 *
	 * Float voltage is NOT written here: smb_probe() programs it from
	 * the battery-info (or leaves the hardware default untouched if no
	 * battery info is available), so an unconditional write here would
	 * clobber that "preserve hardware setting" fallback.
	 *
	 * Charging is NOT enabled here either: smb_probe() enables it only
	 * after the float voltage step above has run, matching the
	 * downstream qpnp-smblite ordering (fv_votable before clearing
	 * chg_disable_votable).
	 *
	 * IBAT_STATUS/VFLT_STATUS (chg_base + 0x0A/0x0B) are read-only
	 * hardware status registers downstream -- there is nothing to
	 * configure there, so they are not written.
	 */
	if (chip->type == SMBLITE) {
		rc = smblite_config_fast_charge_safety_timer(chip);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
				"Couldn't config fast charge safety timer\n");

		rc = smblite_write_init_reg(chip,
					    SMBLITE_BATIF_BASE +
					    SMBLITE_BATIF_BPD_CFG, 0x00,
					    "battery-present detection");
		if (rc < 0)
			return rc;

		rc = smblite_write_init_reg(chip,
					    SMBLITE_CHGR_BASE +
					    SMBLITE_CHGR_CHG_INHIBIT_CFG,
					    0x00, "charge inhibit");
		if (rc < 0)
			return rc;

		/* SOC-based recharge, matching SMBLITE_CHGR_RECHG_SOC_THD_CFG below */
		rc = regmap_update_bits(chip->regmap,
					 SMBLITE_CHGR_BASE + SMBLITE_CHGR_RECHG_CFG,
					 SMBLITE_RECHG_MASK, SMBLITE_RECHG_MASK);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
				"Couldn't configure recharge strategy\n");

		rc = smblite_write_init_reg(chip,
					    SMBLITE_CHGR_BASE +
					    SMBLITE_CHGR_RECHG_SOC_THD_CFG,
					    0xFA, "SOC recharge threshold");
		if (rc < 0)
			return rc;

		rc = smblite_write_init_reg(chip,
					    SMBLITE_CHGR_BASE +
					    SMBLITE_CHGR_CHG_TERM_CFG,
					    0x80, "charge termination");
		if (rc < 0)
			return rc;

		rc = smblite_write_init_reg(chip,
					    SMBLITE_USBIN_BASE +
					    SMBLITE_USB_APSD_EN, 0x10,
					    "APSD enable");
		if (rc < 0)
			return rc;

		rc = smblite_write_init_reg(chip,
					    SMBLITE_USBIN_BASE +
					    SMBLITE_USB_APSD_CFG, 0x00,
					    "APSD configuration");
		if (rc < 0)
			return rc;
	}

	return 0;
}

static void smb_restore_charge_state(void *data)
{
	struct smb_chip *chip = data;
	u16 reg;
	u8 charge_enable;
	int rc;

	if (chip->type == SMBLITE)
		reg = SMBLITE_CHGR_BASE + SMBLITE_CHARGING_ENABLE_CMD;
	else
		reg = chip->base + CHARGING_ENABLE_CMD;

	/* Keep SMBLite charging disabled if probe fails partway through setup. */
	charge_enable = chip->type == SMBLITE ? 0 : chip->initial_charge_enable;
	rc = regmap_update_bits(chip->regmap, reg,
				CHARGING_ENABLE_CMD_BIT,
				charge_enable);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't set probe-failure charging state: %d\n",
			rc);
}

static void smb_disable_wdog_irq_wake(void *data)
{
	struct smb_chip *chip = data;
	int rc;

	rc = disable_irq_wake(chip->wdog_irq);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't disable watchdog IRQ wake: %d\n", rc);
}

static int smb_init_irq(struct smb_chip *chip, int *irq, const char *name,
			 irqreturn_t (*handler)(int irq, void *data))
{
	int irqnum;
	int rc;

	irqnum = platform_get_irq_byname(to_platform_device(chip->dev), name);
	if (irqnum < 0)
		return irqnum;

	rc = devm_request_threaded_irq(chip->dev, irqnum, NULL, handler,
				       IRQF_ONESHOT, name, chip);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't request irq %s\n",
				     name);

	if (irq)
		*irq = irqnum;

	return 0;
}

static int smb_probe(struct platform_device *pdev)
{
	struct power_supply_config supply_config = {};
	struct power_supply_desc *desc;
	struct smb_chip *chip;
	unsigned int charge_enable;
	unsigned int float_voltage_sel;
	int float_voltage_uv;
	const char *pmic_name;
	int rc, irq;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;

	pmic_name = device_get_match_data(&pdev->dev);
	if (!pmic_name)
		return -EINVAL;
	chip->name = pmic_name;

	if (strcmp(pmic_name, "pm4125") == 0) {
		chip->type        = SMBLITE;
		chip->icl_step_ua = SMBLITE_ICL_STEP_UA;
		chip->icl_max_ua  = SMBLITE_ICL_MAX_UA;
		/* Use the charger peripheral as the base for SMBLite accesses. */
		chip->base = SMBLITE_CHGR_BASE;
	} else {
		/* SMB2: PMI8998, PM660 */
		chip->type        = SMB2;
		chip->icl_step_ua = CURRENT_SCALE_FACTOR;
		chip->icl_max_ua  = 4800000;
		rc = device_property_read_u32(chip->dev, "reg", &chip->base);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "Couldn't read base address\n");
	}

	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap)
		return dev_err_probe(chip->dev, -ENODEV,
				     "failed to locate the regmap\n");

	/*
	 * IIO channels:
	 *   usbin_v  -- voltage, supported by both SMB2 and SMBLite
	 *   usbin_i  -- current, SMB2 only (not in downstream smblite)
	 */
	chip->usb_in_v_chan = devm_iio_channel_get(chip->dev, "usbin_v");
	if (IS_ERR(chip->usb_in_v_chan))
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_in_v_chan),
				     "Couldn't get usbin_v IIO channel\n");

	if (chip->type != SMBLITE) {
		chip->usb_in_i_chan = devm_iio_channel_get(chip->dev, "usbin_i");
		if (IS_ERR(chip->usb_in_i_chan))
			return dev_err_probe(chip->dev,
					     PTR_ERR(chip->usb_in_i_chan),
					     "Couldn't get usbin_i IIO channel\n");
	}

	if (chip->type == SMBLITE)
		rc = regmap_read(chip->regmap,
				 SMBLITE_CHGR_BASE + SMBLITE_CHARGING_ENABLE_CMD,
				 &charge_enable);
	else
		rc = regmap_read(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
				 &charge_enable);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't read charging state\n");

	chip->initial_charge_enable = charge_enable & CHARGING_ENABLE_CMD_BIT;
	rc = devm_add_action_or_reset(chip->dev, smb_restore_charge_state, chip);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't register charging state rollback\n");

	if (chip->type == SMBLITE) {
		rc = regmap_update_bits(chip->regmap,
					 SMBLITE_CHGR_BASE + SMBLITE_CHARGING_ENABLE_CMD,
					 SMBLITE_CHARGING_ENABLE_CMD_BIT, 0);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "Couldn't disable charging for setup\n");
	}

	rc = smb_init_hw(chip);
	if (rc < 0)
		return rc;

	if (chip->type == SMBLITE) {
		rc = smblite_program_jeita_thresholds(chip);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "Couldn't program JEITA thresholds\n");
	}

	supply_config.drv_data = chip;
	supply_config.fwnode = dev_fwnode(&pdev->dev);

	desc = devm_kzalloc(chip->dev, sizeof(smb_psy_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &smb_psy_desc, sizeof(smb_psy_desc));

	if (chip->type == SMBLITE) {
		desc->properties     = smblite_properties;
		desc->num_properties = ARRAY_SIZE(smblite_properties);
	}

	desc->name =
		devm_kasprintf(chip->dev, GFP_KERNEL, "%s-charger",
			       (const char *)device_get_match_data(chip->dev));
	if (!desc->name)
		return -ENOMEM;

	chip->chg_psy =
		devm_power_supply_register(chip->dev, desc, &supply_config);
	if (IS_ERR(chip->chg_psy))
		return dev_err_probe(chip->dev, PTR_ERR(chip->chg_psy),
				     "failed to register power supply\n");

	rc = power_supply_get_battery_info(chip->chg_psy, &chip->batt_info);
	if (rc && (chip->type == SMB2 || rc != -ENOENT))
		return dev_err_probe(chip->dev, rc,
				     "Failed to get battery info\n");
	if (!chip->batt_info) {
		if (chip->type == SMB2)
			return dev_err_probe(chip->dev, -ENODATA,
					     "Failed to get battery info\n");

		dev_warn(chip->dev,
			 "No battery info, skipping float voltage configuration\n");
		chip->batt_info = NULL;
	}

	rc = devm_delayed_work_autocancel(chip->dev, &chip->status_change_work,
					  smb_status_change_work);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "Failed to init status change work\n");

	if (!chip->batt_info)
		goto init_irqs;

	if (power_supply_battery_info_has_prop(chip->batt_info,
					       POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX)) {
		float_voltage_uv =
			chip->batt_info->constant_charge_voltage_max_uv;
	} else if (power_supply_battery_info_has_prop(chip->batt_info,
					      POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN)) {
		float_voltage_uv = chip->batt_info->voltage_max_design_uv;
	} else {
		dev_warn(chip->dev,
			 "No battery float voltage; preserving hardware setting\n");
		goto init_irqs;
	}

	if (chip->type == SMBLITE) {
		if (float_voltage_uv < SMBLITE_FV_MIN_UV ||
		    float_voltage_uv > SMBLITE_FV_MAX_UV)
			return dev_err_probe(chip->dev, -EINVAL,
					     "float voltage %d uV outside %d-%d uV\n",
					     float_voltage_uv, SMBLITE_FV_MIN_UV,
					     SMBLITE_FV_MAX_UV);

		float_voltage_sel = (float_voltage_uv - SMBLITE_FV_MIN_UV) /
				     SMBLITE_FV_STEP_UV;

		rc = regmap_write(chip->regmap,
				  SMBLITE_CHGR_BASE +
				  SMBLITE_CHGR_FLOAT_VOLTAGE_CFG,
				  float_voltage_sel);
	} else {
		if (float_voltage_uv < SMB2_FLOAT_VOLTAGE_MIN_UV ||
		    float_voltage_uv > SMB2_FLOAT_VOLTAGE_MAX_UV)
			return dev_err_probe(chip->dev, -EINVAL,
					     "float voltage %d uV outside %d-%d uV\n",
					     float_voltage_uv,
					     SMB2_FLOAT_VOLTAGE_MIN_UV,
					     SMB2_FLOAT_VOLTAGE_MAX_UV);

		float_voltage_sel =
			(float_voltage_uv - SMB2_FLOAT_VOLTAGE_MIN_UV) /
			SMB2_FLOAT_VOLTAGE_STEP_UV;

		rc = regmap_update_bits(chip->regmap,
					chip->base + FLOAT_VOLTAGE_CFG,
					FLOAT_VOLTAGE_SETTING_MASK,
					float_voltage_sel);
	}

	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't set vbat max\n");

init_irqs:

	/*
	 * SMBLite: enable charging only after the float voltage has been
	 * configured (or deliberately left at its hardware default, above),
	 * matching the downstream qpnp-smblite ordering of applying fv
	 * before clearing chg_disable_votable. SMB2 keeps its pre-existing
	 * behaviour of enabling charging as part of smb_init_seq.
	 */
	if (chip->type == SMBLITE) {
		rc = regmap_update_bits(chip->regmap,
					 SMBLITE_CHGR_BASE + SMBLITE_CHARGING_ENABLE_CMD,
					 SMBLITE_CHARGING_ENABLE_CMD_BIT,
					 SMBLITE_CHARGING_ENABLE_CMD_BIT);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "Couldn't enable charging\n");
	}

	rc = smb_init_irq(chip, &irq, "bat-ov", smb_handle_batt_overvoltage);
	if (rc < 0)
		return rc;

	rc = smb_init_irq(chip, &chip->cable_irq, "usb-plugin",
			   smb_handle_usb_plugin);
	if (rc < 0)
		return rc;

	rc = smb_init_irq(chip, &irq, "usbin-icl-change",
			   smb_handle_usb_icl_change);
	if (rc < 0)
		return rc;
	rc = smb_init_irq(chip, &chip->wdog_irq, "wdog-bark",
			  smb_handle_wdog_bark);
	if (rc < 0)
		return rc;

	devm_device_init_wakeup(chip->dev);

	rc = devm_pm_set_wake_irq(chip->dev, chip->cable_irq);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't set wake irq\n");

	if (chip->type == SMBLITE) {
		rc = enable_irq_wake(chip->wdog_irq);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "Couldn't enable watchdog IRQ wake\n");

		rc = devm_add_action_or_reset(chip->dev,
					      smb_disable_wdog_irq_wake, chip);
		if (rc)
			return dev_err_probe(chip->dev, rc,
					     "Couldn't register watchdog IRQ wake cleanup\n");
	}

	devm_remove_action(chip->dev, smb_restore_charge_state, chip);

	platform_set_drvdata(pdev, chip);

	/* Initialise charger state */
	schedule_delayed_work(&chip->status_change_work, 0);

	return 0;
}

static const struct of_device_id smb_match_id_table[] = {
	{ .compatible = "qcom,pmi8998-charger", .data = "pmi8998" },
	{ .compatible = "qcom,pm660-charger", .data = "pm660" },
	{ .compatible = "qcom,pm4125-charger", .data = "pm4125" },
	{ /* sentinal */ }
};
MODULE_DEVICE_TABLE(of, smb_match_id_table);

static struct platform_driver qcom_spmi_smb = {
	.probe = smb_probe,
	.driver = {
		.name = "qcom-smbx-charger",
		.of_match_table = smb_match_id_table,
		},
};

module_platform_driver(qcom_spmi_smb);

MODULE_AUTHOR("Casey Connolly <casey.connolly@linaro.org>");
MODULE_DESCRIPTION("Qualcomm SMB2/SMBLite charger driver");
MODULE_LICENSE("GPL");
