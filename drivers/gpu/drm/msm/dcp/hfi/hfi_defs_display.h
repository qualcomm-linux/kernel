/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_DISPLAY_H__
#define __H_HFI_DEFS_DISPLAY_H__

#include "hfi_defs_common.h"

/* Display blending and power controls. */
enum hfi_display_blend_ops {
	HFI_BLEND_OP_NOT_DEFINED        = 0x0,
	HFI_BLEND_OP_OPAQUE             = 0x1,
	HFI_BLEND_OP_PREMULTIPLIED      = 0x2,
	HFI_BLEND_OP_COVERAGE           = 0x3,
	HFI_BLEND_OP_SKIP               = 0x4,
	HFI_BLEND_OP_MAX                = 0x5,
};

enum hfi_display_power_mode {
	HFI_MODE_DPMS_INVALID   = 0,
	HFI_MODE_DPMS_OFF       = 0x1,
	HFI_MODE_DPMS_ON        = 0x2,
	HFI_MODE_DPMS_LP1       = 0x3,
	HFI_MODE_DPMS_LP2       = 0x4,
	HFI_MODE_DPMS_NOLP      = 0x5,
};

enum hfi_display_power_control {
	HFI_PANEL_POWER      = 0x1,
	HFI_CTRL_POWER       = 0x2,
	HFI_PHY_POWER        = 0x4,
};

enum hfi_display_commit_flag {
	HFI_VALIDATE           = 0x1,
	HFI_COMMIT             = 0x2,
};

enum hfi_display_res_type {
	HFI_RESOURCE_LM = 0x1,
};

/* Firmware resource configuration. */
struct hfi_resource_cfg {
	u32 res_type;
	u32 resource_idx;
	u32 width;
	u32 height;
	u32 reserved;
};

/* Display region of interest. */
struct hfi_display_roi {
	u32 x_pos;
	u32 y_pos;
	u32 width;
	u32 height;
};

enum hfi_display_roi_type {
	PANEL_ROI = 0x0,
};

/* VSync event payload. */
struct hfi_display_vsync_data {
	u32 timestamp_lo;
	u32 timestamp_hi;
	u32 vsync_index;
};

/* Frame-scan event payload. */
struct hfi_display_frame_event_data {
	u32 timestamp_lo;
	u32 timestamp_hi;
	u32 bufferflip_index;
};

struct hfi_display_color {
	u16 color_0;
	u16 color_1;
	u16 color_2;
	u16 color_3;
};

enum hfi_display_dim_layer_flag {
	HFI_DIM_LAYER_INCLUSIVE = 0x1,
	HFI_DIM_LAYER_EXCLUSIVE = 0x2,
};

/* Dim-layer configuration. */
struct hfi_display_dim_layer {
	enum hfi_display_dim_layer_flag flags;
	u32 stage;
	struct hfi_display_color color_fill;
	struct hfi_display_roi rect;
};

enum hfi_layer_cache_state {
	HFI_CACHE_STATE_DISABLE = 0x0,
	HFI_CACHE_STATE_READ = 0x1,
	HFI_CACHE_STATE_WRITE = 0x2,
};

enum hfi_layer_cache_op_type {
	HFI_CACHE_OP_TYPE_NONE = 0x0,
	HFI_CACHE_NORMAL_CACHEABLE_READ = 0x1,
	HFI_CACHE_READ_INVALIDATE = 0x2,
	HFI_CACHE_READ_EVICT = 0x3,
	HFI_CACHE_PREFETCH_READ = 0x4,
};

/* Autorefresh configuration. */
struct hfi_display_autorefresh_cfg {
	u32 enable;
	u32 frame_count;
};

struct hfi_display_idle_event_data {
	u32 timestamp_lo;
	u32 timestamp_hi;
	u32 idle_index;
};

struct hfi_display_power_event_data {
	u32 timestamp_lo;
	u32 timestamp_hi;
	enum hfi_display_power_mode power_state;
};

struct hfi_display_prog_line_event_data {
	u32 timestamp_lo;
	u32 timestamp_hi;
	u32 prog_line_index;
};

enum hfi_display_idle_timer_control {
	HFI_DEFAULT          = 0x0,
	HFI_WAKEUP           = 0x1,
	HFI_BLOCK_TIMER      = 0x2,
	HFI_UNBLOCK_TIMER    = 0x3,
};

/* Display event identifiers. */
enum hfi_display_event_id {
	HFI_EVENT_VSYNC               = 0x1,
	HFI_EVENT_FRAME_SCAN_START    = 0x2,
	HFI_EVENT_FRAME_SCAN_COMPLETE = 0x3,
	HFI_EVENT_FRAME_IDLE          = 0x4,
	HFI_EVENT_DISPLAY_POWER       = 0x5,
	HFI_EVENT_HW_RECOVERY         = 0x6,
	HFI_EVENT_FRAME_CAPTURE_COMPLETE = 0x7,
	HFI_EVENT_PANEL_DEAD          = 0x8,
	HFI_EVENT_LTM                 = 0x9,
	HFI_EVENT_RGB_HIST            = 0xa,
	HFI_EVENT_PA_HIST             = 0xb,
	HFI_EVENT_HPD_STATUS          = 0xc,
	HFI_EVENT_DISPLAY_EDID_INFO   = 0xd,
	HFI_EVENT_SPR_OPR             = 0xe,
	HFI_EVENT_INTF_MISR           = 0xf,
	HFI_EVENT_HDCP1X_START        = 0x10,
	HFI_EVENT_HDCP1X_STOP         = 0x11,
	HFI_EVENT_HDCP1X_ENC          = 0x12,
	HFI_EVENT_HDCP1X_TOPOLOGY_UPDATE = 0x13,
	HFI_EVENT_HDCP2X_START        = 0x14,
	HFI_EVENT_HDCP2X_PROCESS_MSG  = 0x15,
	HFI_EVENT_HDCP2X_TIMEOUT      = 0x16,
	HFI_EVENT_HDCP_FEATURE_SUPPORTED = 0x17,
	HFI_EVENT_AIQE_COPR           = 0x18,
	HFI_EVENT_PROG_LINE_INTR      = 0x19,
};

enum hfi_display_dp_event {
	HFI_DP_EVENT_NONE         = 0x0,
	HFI_DP_EVENT_HPD_PLUGGED  = 0x1,
	HFI_DP_EVENT_IRQ_HPD      = 0x2,
	HFI_DP_EVENT_SET_MODE     = 0x3,
	HFI_DP_EVENT_HPD_UNPLUGGED = 0x4,
};

enum hfi_display_dp_state {
	HFI_DP_STATE_DISCONNECTED = 0x0,
	HFI_DP_STATE_HPD_IN       = 0x1,
	HFI_DP_STATE_CONNECTED    = 0x2,
	HFI_DP_STATE_HPD_OUT      = 0x3,
};

struct hfi_display_hpd_status {
	enum hfi_display_dp_event dp_evt;
	enum hfi_display_dp_state dp_state;
};

/* Display mode sent to firmware. */
struct hfi_display_mode_info {
	u32 size;
	u32 h_active;
	u32 h_back_porch;
	u32 h_sync_width;
	u32 h_front_porch;
	u32 h_skew;
	u32 h_sync_polarity;
	u32 v_active;
	u32 v_back_porch;
	u32 v_sync_width;
	u32 v_front_porch;
	u32 v_sync_polarity;
	u32 refresh_rate;
	u32 clk_rate_hz_lo;
	u32 clk_rate_hz_hi;
	u32 flags_lo;
	u32 flags_hi;
	u32 reserved1;
	u32 reserved2;
};

/* DSI command descriptor. */
struct hfi_dsi_cmd_desc {
	u32 size;

	u8 channel;
	u8 type;
	u16 flags;

		u32 tx_len;
	u32 tx_buff_addr_lsb;
	u32 tx_buff_addr_msb;

		u32 rx_len;
	u32 rx_buff_addr_lsb;
	u32 rx_buff_addr_msb;

		u32 ctrl_idx;
	u32 ctrl_flags;
	u32 last_command;
	u32 post_wait_ms;

	u32 seq_no;

		u32 reserved2;
};

/* DSI command descriptor. */
struct hfi_dsi_cmd_desc_set {
	u32 size;
	u32 type;
	u32 count;
	u32 state;
	u32 seq_no;
	u32 reserved2;
	struct hfi_dsi_cmd_desc cmds[];
};

enum hfi_display_blend_stage {
	HFI_BLEND_STAGE_BASE,
	HFI_BLEND_STAGE_0,
	HFI_BLEND_STAGE_1,
	HFI_BLEND_STAGE_2,
	HFI_BLEND_STAGE_3,
	HFI_BLEND_STAGE_4,
	HFI_BLEND_STAGE_5,
	HFI_BLEND_STAGE_6,
	HFI_BLEND_STAGE_7,
	HFI_BLEND_STAGE_8,
	HFI_BLEND_STAGE_9,
	HFI_BLEND_STAGE_10,
};

enum hfi_layer_fetch_mode {
	HFI_PARALLEL_FETCH        = 0x0,
	HFI_TIME_MULTIPLEX_FETCH  = 0x1,
};

enum hfi_layer_security_policy {
	HFI_LAYER_SECURITY_POLICY_NON_SECURE                  = 0x0,
	HFI_LAYER_SECURITY_POLICY_SECURE                      = 0x1,
	HFI_LAYER_SECURITY_POLICY_SECURE_DIR_TRANSLATION      = 0x2,
	HFI_LAYER_SECURITY_POLICY_MAX
};

#define HFI_DISPLAY_ROTATION_0   BIT(0)

#define HFI_DISPLAY_ROTATION_90   BIT(1)

#define HFI_DISPLAY_ROTATION_180     BIT(2)

#define HFI_DISPLAY_ROTATION_270   BIT(3)

#define HFI_DISPLAY_REFLECT_X   BIT(4)

#define HFI_DISPLAY_REFLECT_Y   BIT(5)

enum hfi_cwb_tap_points {
	HFI_TAP_POINT_NONE,
	HFI_TAP_POINT_LM,
	HFI_TAP_POINT_DSPP,
	HFI_TAP_POINT_DEMURA,
	HFI_TAP_POINT_MAX,
};

enum hfi_fence_type {
	HFI_FENCE_SCAN_START,
	HFI_FENCE_SCAN_COMPLETE,
};

enum hfi_dp_sink_capability_flags {
	HFI_DP_SINK_CAP_MST,
	HFI_DP_SINK_CAP_DSC,
	HFI_DP_SINK_CAP_FEC,
};

/* EDID and mode information. */
struct hfi_display_event_edid_info {
	u32 controller_id;
	u32 stream_id;
	u32 link_rate;
	u32 lane_count;
	u32 bits_per_pixel;
	u32 flags;
	struct hfi_buff edid_modes_buf;
};

struct hfi_display_dp_tu {
	u32 dp_tu;
	u32 valid_boundary;
	u32 valid_boundary2;
};

/* Display audio configuration. */
struct hfi_audio_config {
	u32 sample_rate;
	u32 num_of_channels;
	u32 channel_allocation;
	u32 level_shift;
	u32 down_mix;
	u32 sample_present;
	u32 stream_id;
};

struct hfi_hdr_metadata {
		u32 hdr_state;
	u32 eotf;
	u32 hdr_supported;
	u32 display_primaries_x[HFI_MAX_COLOR_COMPONENTS];
	u32 display_primaries_y[HFI_MAX_COLOR_COMPONENTS];
	u32 white_point_x;
	u32 white_point_y;
	u32 max_luminance;
	u32 min_luminance;
	u32 max_content_light_level;
	u32 max_average_light_level;
};

/* HDR display configuration. */
struct hfi_display_hdr_cfg {
	struct hfi_hdr_metadata hdr_meta;
	u32 dynamic_hdr_payload_size;
	u8 dynamic_hdr_payload[HFI_DHDR_PAYLOAD_MAX_SIZE];
};

enum hfi_colorimetry {
	HFI_COLORIMETRY_DEFAULT            = 0,
	HFI_COLORIMETRY_SMPTE_170M_YCC     = 1,
	HFI_COLORIMETRY_BT709_YCC          = 2,
	HFI_COLORIMETRY_XVYCC_601          = 3,
	HFI_COLORIMETRY_XVYCC_709          = 4,
	HFI_COLORIMETRY_SYCC_601           = 5,
	HFI_COLORIMETRY_OPYCC_601          = 6,
	HFI_COLORIMETRY_OPRGB              = 7,
	HFI_COLORIMETRY_BT2020_CYCC        = 8,
	HFI_COLORIMETRY_BT2020_RGB         = 9,
	HFI_COLORIMETRY_BT2020_YCC         = 10,
	HFI_COLORIMETRY_DCI_P3_RGB_D65     = 11,
	HFI_COLORIMETRY_DCI_P3_RGB_THEATER = 12,
};

enum hfi_misr_block {
	HFI_MISR_DSI   = 0x0,
	HFI_MISR_MIXER = 0x1,
	HFI_MISR_INTF  = 0x2,
};

enum hfi_batch_mode {
	HFI_BATCH_MODE_NONE   = 0x0,
	HFI_BATCH_MODE_START  = 0x1,
	HFI_BATCH_MODE_END    = 0x2,
	HFI_BATCH_MODE_CANCEL = 0x3,
};

enum hfi_batch_usecase_id {
	HFI_BATCH_USECASE_NONE = 0,
	HFI_BATCH_USECASE_GMU_REPROJ,
};

struct hfi_misr_config {
	u32 enable;
	u32 frame_count;
	enum hfi_misr_block block;
};

/* HDCP 1.x topology. */
struct hfi_hdcp1x_topology_data {
	u32 depth;
	u32 device_count;
	u32 max_devices_exceeded;
	u32 max_cascade_exceeded;
};

/* HDCP 2.x message payload. */
struct hfi_hdcp2_message {
	u32 timeout_ms;
	u32 repeater_flag;
	struct hfi_buff request;
	struct hfi_buff response;
};

struct hfi_display_mode_extended_info {
	u32 size;
	struct hfi_display_mode_info base;
	u32 mixer_count;
	u32 cmpr_count;
	u32 cmpr_enabled;
	u32 cmpr_bpp;
	u32 cmpr_slice_count;
	u8  test_pattern;
	u8  reserved[7];
};

struct hfi_cmd_set_remap {
	u32 cmd_type;
	u32 custom_cmd_type;
	u32 flag;
	u32 reserved;
};

struct hfi_dsi_dcs_cmd_set_replace_entry {
	u32 dpu_buff_type_offset;
	u32 cmd_type;
	u32 count_cmds;
	u32 hfi_buff_struct_offset;
	u32 reserved;
};

struct hfi_custom_wd_te_params {
	u32 wd_te_enabled;
	u32 custom_fps;
};

/* Display batch-mode configuration. */
struct hfi_batch_mode_info {
	enum hfi_batch_mode mode;
	enum hfi_batch_usecase_id usecase_id;
	u32 reserved[2];
};

#define HFI_WB_DNSC_CFG_DISABLE	BIT(0)

struct hfi_dnsc_cfg {
	u32 flags;
	u32 dst_width;
	u32 dst_height;
};

#endif /* __H_HFI_DEFS_DISPLAY_H__ */
