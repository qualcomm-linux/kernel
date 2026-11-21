/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_PANEL_H__
#define __H_HFI_DEFS_PANEL_H__

#include "hfi_defs_common.h"

/* Panel format, link, and power properties. */
enum hfi_panel_phy_type {
	HFI_PANEL_PHY_OLED      = 0x1,
	HFI_PANEL_PHY_LCD       = 0x2,
};

/* Lower values have higher display priority. */
enum hfi_panel_display_type {
	HFI_PANEL_DISPLAY_TYPE_NONE           = 0x0,
	HFI_PANEL_DISPLAY_TYPE_BUILT_IN_0     = 0x1,
	HFI_PANEL_DISPLAY_TYPE_BUILT_IN_1     = 0x2,
	HFI_PANEL_DISPLAY_TYPE_BUILT_IN_2     = 0x3,
	HFI_PANEL_DISPLAY_TYPE_BUILT_IN_3     = 0x4,
	HFI_PANEL_DISPLAY_TYPE_BUILT_IN_4     = 0x5,
};

enum hfi_panel_bpp {
	HFI_PANEL_BPP_3      = 1,
	HFI_PANEL_BPP_8      = 2,
	HFI_PANEL_BPP_12     = 3,
	HFI_PANEL_BPP_16     = 4,
	HFI_PANEL_BPP_18     = 5,
	HFI_PANEL_BPP_24     = 6,
	HFI_PANEL_BPP_30     = 7,
};

enum hfi_panel_deinit_prop {
	HFI_PANEL_DEINIT_CONT_SPLASH = 0x00000001,
};

enum hfi_panel_lane_enable {
	HFI_PANEL_LANE_0      = 0x00000001,
	HFI_PANEL_LANE_1      = 0x00000002,
	HFI_PANEL_LANE_2      = 0x00000004,
	HFI_PANEL_LANE_3      = 0x00000008,
};

enum hfi_panel_lane_map {
	HFI_PANEL_LANE_MAP_0123 = 1,
};

enum hfi_panel_color_order_type {
	HFI_PANEL_COLOR_ORDER_RGB_SWAP_RGB   = 0x1,
	HFI_PANEL_COLOR_ORDER_RGB_SWAP_RBG   = 0x2,
	HFI_PANEL_COLOR_ORDER_RGB_SWAP_BRG   = 0x3,
	HFI_PANEL_COLOR_ORDER_RGB_SWAP_GRB   = 0x4,
	HFI_PANEL_COLOR_ORDER_RGB_SWAP_GBR   = 0x5,
};

enum hfi_panel_vsync_source {
	HFI_PANEL_VSYNC_SOURCE_GPIO_0  = 0x0,
	HFI_PANEL_VSYNC_SOURCE_GPIO_1  = 0x1,
	HFI_PANEL_VSYNC_SOURCE_GPIO_2  = 0x2,
	HFI_PANEL_VSYNC_SOURCE_GPIO_3  = 0x3,
	HFI_PANEL_VSYNC_SOURCE_GPIO_4  = 0x4,
	HFI_PANEL_VSYNC_SOURCE_GPIO_5  = 0x5,
	HFI_PANEL_VSYNC_SOURCE_WD      = 0xf,
};

enum hfi_panel_trigger_type {
	HFI_PANEL_TRIGGER_NONE      = 1,
	HFI_PANEL_TRIGGER_TE        = 2,
	HFI_PANEL_TRIGGER_SW        = 3,
	HFI_PANEL_TRIGGER_SW_TE     = 4,
	HFI_PANEL_TRIGGER_SW_SEOF   = 5,
	HFI_PANEL_TRIGGER_SEOF      = 6,
};

enum hfi_panel_fps_traffic_mode {
	HFI_PANEL_TRAFFIC_NON_BURST_SYNC_PULSE_MODE     = 1,
	HFI_PANEL_TRAFFIC_NON_BURST_SYNC_EVENT_MODE     = 2,
	HFI_PANEL_TRAFFIC_BURST_MODE                    = 3,
};

enum hfi_panel_modes {
	HFI_PANEL_VIDEO_MODE_8_BIT      = 0x1,
	HFI_PANEL_VIDEO_MODE_10_BIT     = 0x2,
	HFI_PANEL_CMD_MODE_8_BIT        = 0x4,
	HFI_PANEL_CMD_MODE_10_BIT       = 0x8,
	HFI_PANEL_VIDEO_MODE_PSR_8_BIT	= 0x10,
	HFI_PANEL_VIDEO_MODE_PSR_10_BIT	= 0x20,
	HFI_PANEL_VIDEO_LTPO_8_BIT	= 0x40,
	HFI_PANEL_VIDEO_LTPO_10_BIT	= 0x80
};

enum hfi_panel_backlight_ctrl {
	HFI_PANEL_BACKLIGHT_CTRL_UNKNOWN    = 0x0,
	HFI_PANEL_BACKLIGHT_CTRL_PWM        = 0x1,
	HFI_PANEL_BACKLIGHT_CTRL_WLED       = 0x2,
	HFI_PANEL_BACKLIGHT_CTRL_DCS        = 0x3,
	HFI_PANEL_BACKLIGHT_CTRL_EXTERNAL   = 0x4,
};

enum hfi_panel_compression_mode {
	HFI_PANEL_COMPRESSION_NONE  = 0,
	HFI_PANEL_COMPRESSION_DSC   = 1,
};

enum hfi_panel_chroma_format_type {
	HFI_PANEL_CHROMA_FORMAT_444     = 1,
	HFI_PANEL_CHROMA_FORMAT_422     = 2,
	HFI_PANEL_CHROMA_FORMAT_420     = 3,
};

enum hfi_panel_color_space_type {
	HFI_PANEL_COLOR_SPACE_RGB     = 1,
	HFI_PANEL_COLOR_SPACE_YUV     = 2,
};

enum hfi_panel_dfps_type {
	HFI_PANEL_DFPS_NONE = 0,
	HFI_PANEL_DFPS_SUSPEND_RESUME,
	HFI_PANEL_DFPS_IMMEDIATE_CLK,
	HFI_PANEL_DFPS_IMMEDIATE_HFP,
	HFI_PANEL_DFPS_IMMEDIATE_VFP,
	HFI_PANEL_DFPS_MAX
};

/* Panel topology. */
struct hfi_panel_topology {
	u32 mixer_count;
	u32 enc_count;
	u32 display_count;
};

/* DSI command sequencing. */
enum hfi_panel_dcs_cmd_state {
	HFI_PANEL_DSI_HS_MODE   = 0x1,
	HFI_PANEL_DSI_LP_MODE   = 0x2,
};

enum hfi_panel_dcs_command_type {
	HFI_DCS_CMD_PRE_ON                      = 0x00000000,
	HFI_DCS_CMD_ON                          = 0x00000001,
	HFI_DCS_CMD_VIDEO_ON                    = 0x00000002,
	HFI_DCS_CMD_COMMAND_ON                  = 0x00000003,
	HFI_DCS_CMD_POST_PANEL_ON               = 0x00000004,
	HFI_DCS_CMD_PRE_OFF                     = 0x00000005,
	HFI_DCS_CMD_OFF                         = 0x00000006,
	HFI_DCS_CMD_POST_OFF                    = 0x00000007,
	HFI_DCS_CMD_PRE_RES_SWITCH              = 0x00000008,
	HFI_DCS_CMD_RES_SWITCH                  = 0x0000009,
	HFI_DCS_CMD_POST_RES_SWITCH             = 0x00000010,
	HFI_DCS_CMD_VIDEO_MODE_SWITCH_INS       = 0x00000011,
	HFI_DCS_CMD_VIDEO_MODE_SWITCH_OUTS      = 0x00000012,
	HFI_DCS_CMD_COMMAND_MODE_SWITCH_INS     = 0x00000013,
	HFI_DCS_CMD_COMMAND_MODE_SWITCH_OUTS    = 0x00000014,
	HFI_DCS_CMD_PANEL_STATUS                = 0x00000015,
	HFI_DCS_CMD_LP1                         = 0x00000016,
	HFI_DCS_CMD_LP2                         = 0x00000017,
	HFI_DCS_CMD_NOLP                        = 0x00000018,
	HFI_DCS_CMD_PPS                         = 0x00000019,
	HFI_DCS_CMD_ROI                         = 0x00000020,
	HFI_DCS_CMD_TIMING_SWITCH               = 0x00000021,
	HFI_DCS_CMD_POST_MODE_SWITCH_ON         = 0x00000022,
	HFI_DCS_CMD_QSYNC_ONS                   = 0x00000023,
	HFI_DCS_CMD_QSYNC_OFFS                  = 0x00000024,
	HFI_DCS_CMD_MAX                         = 0x00000025
};

/* Panel ESD status modes. */
enum hfi_panel_esd_status_mode {
	HFI_PANEL_ESD_STATUS_MODE_REG_READ       = 0x1,
	HFI_PANEL_ESD_STATUS_MODE_SW_BTA         = 0x2,
	HFI_PANEL_ESD_STATUS_MODE_PANEL_TE       = 0x3,
	HFI_PANEL_ESD_STATUS_MODE_PANEL_RW       = 0x4,
	HFI_PANEL_ESD_STATUS_MODE_SW_SIM_SUCCESS = 0x5,
	HFI_PANEL_ESD_STATUS_MODE_SW_SIM_FAILURE = 0x6
};

/* Panel timing parameters. */
struct hfi_panel_res_data {
	u32 active_width;
	u32 active_height;
	u32 h_front_porch;
	u32 h_back_porch;
	u32 h_left_border;
	u32 h_right_border;
	u32 h_pulse_width;
	u32 h_sync_skew;
	u32 h_sync_pulse;
	u32 v_front_porch;
	u32 v_back_porch;
	u32 v_top_border;
	u32 v_bottom_border;
	u32 v_pulse_width;
};

/* Panel compression parameters. */
struct hfi_panel_compression_params {
	enum hfi_panel_compression_mode	mode;
	u32 version;
	u32 scr_version;
	u32 slice_height;
	u32 slice_width;
	u32 slices_per_pkt;
	u32 bits_per_component;
	u32 pps_delay_ms;
	u32 bits_per_pixel;
	enum hfi_panel_chroma_format_type chroma_format;
	enum hfi_panel_color_space_type color_space;
	u32 block_prediction_enable;
};

/* DCS command buffers. */
struct hfi_panel_dcs_cmd_buffer_info {
	u32 offset_l;
	u32 offset_h;
	u32 size;
	u32 delay;
	u32 ctrl_flags;
	enum hfi_panel_dcs_cmd_state mode;
	u32 reserve1;
	u32 reserve2;
};

struct hfi_panel_compression_rc_override {
	u8 min_qp[15];
	u8 padding1;
	u8 max_qp[15];
	u8 padding2;
	u8 offsets[15];
	u8 padding3;
};

/* Panel ESD configuration. */
/* Register-read fields are valid only in REG_READ mode. */
struct hfi_panel_esd_config {
	u32 size;
	enum hfi_panel_esd_status_mode status_mode;

		u32 count;
	u32 groups;

	u32 hfi_dsi_cmd_desc_lsb;
	u32 hfi_dsi_cmd_desc_msb;

	u32 valid_params_lsb;
	u32 valid_params_msb;

	u32 status_values_lsb;
	u32 status_values_msb;
};

struct hfi_qsync_params {
	u32 avr_step_fps;
	u32 qsync_min_fps;
};

struct hfi_panel_esync_caps {
	u32 esync_support;
	u32 esync_milli_skew;
	u32 hsync_milli_pulse_width;
	u32 emsync_milli_pulse_width;
	u32 emsync_fps;
};

struct hfi_panel_dfps_caps {
	u32 dfps_support;
	u32 min_refresh_rate;
	u32 max_refresh_rate;
	enum hfi_panel_dfps_type type;
};

struct hfi_display_vrr_frame_params {
	u32 frame_interval_ns_lo;
	u32 frame_interval_ns_hi;
	u32 usecase_idx;
};

/* Panel operating-mode capabilities. */
struct hfi_panel_operating_mode_caps {
	u32 panel_mode_switch_enabled;
	u32 vsync_aligned_switch;
};

#endif /* __H_HFI_DEFS_PANEL_H__ */
