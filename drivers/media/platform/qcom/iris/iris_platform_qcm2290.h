/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "iris_platform_common.h"

#define BITRATE_MAX_AR50LT		100000000
#define BITRATE_DEFAULT_AR50LT		20000000
#define MIN_QP_8BIT_AR50LT		0

static const struct platform_inst_fw_cap inst_fw_cap_qcm2290_dec[] = {
	{
		.cap_id = PROFILE_H264,
		.min = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		.max = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH),
		.value = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
		.hfi_id = HFI_PROP_PROFILE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = PROFILE_HEVC,
		.min = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.max = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE),
		.value = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.hfi_id = HFI_PROP_PROFILE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = PROFILE_VP9,
		.min = V4L2_MPEG_VIDEO_VP9_PROFILE_0,
		.max = V4L2_MPEG_VIDEO_VP9_PROFILE_0,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_VP9_PROFILE_0),
		.value = V4L2_MPEG_VIDEO_VP9_PROFILE_0,
		.hfi_id = HFI_PROP_PROFILE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = LEVEL_H264,
		.min = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.max = V4L2_MPEG_VIDEO_H264_LEVEL_4_2,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1B) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_3) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_2),
		.value = V4L2_MPEG_VIDEO_H264_LEVEL_4_2,
		.hfi_id = HFI_PROP_LEVEL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = LEVEL_HEVC,
		.min = V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
		.max = V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_2) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_3) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_4) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1),
		.value = V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1,
		.hfi_id = HFI_PROP_LEVEL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = LEVEL_VP9,
		.min = V4L2_MPEG_VIDEO_VP9_LEVEL_1_0,
		.max = V4L2_MPEG_VIDEO_VP9_LEVEL_4_1,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_1_0) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_1_1) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_2_0) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_2_1) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_3_0) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_3_1) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_4_0) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_4_1),
		.value = V4L2_MPEG_VIDEO_VP9_LEVEL_4_1,
		.hfi_id = HFI_PROP_LEVEL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = TIER,
		.min = V4L2_MPEG_VIDEO_HEVC_TIER_MAIN,
		.max = V4L2_MPEG_VIDEO_HEVC_TIER_HIGH,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) |
				BIT(V4L2_MPEG_VIDEO_HEVC_TIER_HIGH),
		.value = V4L2_MPEG_VIDEO_HEVC_TIER_HIGH,
		.hfi_id = HFI_PROP_TIER,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = INPUT_BUF_HOST_MAX_COUNT,
		.min = DEFAULT_MAX_HOST_BUF_COUNT,
		.max = DEFAULT_MAX_HOST_BURST_BUF_COUNT,
		.step_or_mask = 1,
		.value = DEFAULT_MAX_HOST_BUF_COUNT,
		.hfi_id = HFI_PROP_BUFFER_HOST_MAX_COUNT,
		.flags = CAP_FLAG_INPUT_PORT,
		.set = iris_set_u32,
	},
	{
		.cap_id = STAGE,
		.min = STAGE_1,
		.max = STAGE_2,
		.step_or_mask = 1,
		.value = STAGE_2,
		.hfi_id = HFI_PROP_STAGE,
		.set = iris_set_stage,
	},
	{
		.cap_id = PIPE,
		/* .max, .min and .value are set via platform data */
		.step_or_mask = 1,
		.hfi_id = HFI_PROP_PIPE,
		.set = iris_set_pipe,
	},
	{
		.cap_id = POC,
		.min = 0,
		.max = 2,
		.step_or_mask = 1,
		.value = 1,
		.hfi_id = HFI_PROP_PIC_ORDER_CNT_TYPE,
	},
	{
		.cap_id = CODED_FRAMES,
		.min = CODED_FRAMES_PROGRESSIVE,
		.max = CODED_FRAMES_PROGRESSIVE,
		.step_or_mask = 0,
		.value = CODED_FRAMES_PROGRESSIVE,
		.hfi_id = HFI_PROP_CODED_FRAMES,
	},
	{
		.cap_id = BIT_DEPTH,
		.min = BIT_DEPTH_8,
		.max = BIT_DEPTH_8,
		.step_or_mask = 1,
		.value = BIT_DEPTH_8,
		.hfi_id = HFI_PROP_LUMA_CHROMA_BIT_DEPTH,
	},
	{
		.cap_id = RAP_FRAME,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 1,
		.hfi_id = HFI_PROP_DEC_START_FROM_RAP_FRAME,
		.flags = CAP_FLAG_INPUT_PORT,
		.set = iris_set_u32,
	},
};

static const struct platform_inst_fw_cap inst_fw_cap_qcm2290_enc[] = {
	{
		.cap_id = PROFILE_H264,
		.min = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		.max = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH),
		.value = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
		.hfi_id = HFI_PROP_PROFILE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile,
	},
	{
		.cap_id = PROFILE_HEVC,
		.min = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.max = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE),
		.value = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.hfi_id = HFI_PROP_PROFILE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile,
	},
	{
		.cap_id = LEVEL_H264,
		.min = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.max = V4L2_MPEG_VIDEO_H264_LEVEL_4_2,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1B) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_3) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_2),
		.value = V4L2_MPEG_VIDEO_H264_LEVEL_4_2,
		.hfi_id = HFI_PROP_LEVEL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_level,
	},
	{
		.cap_id = LEVEL_HEVC,
		.min = V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
		.max = V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_2) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_3) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_4) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1),
		.value = V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1,
		.hfi_id = HFI_PROP_LEVEL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_level,
	},
	{
		.cap_id = STAGE,
		.min = STAGE_1,
		.max = STAGE_2,
		.step_or_mask = 1,
		.value = STAGE_2,
		.hfi_id = HFI_PROP_STAGE,
		.set = iris_set_stage,
	},
	{
		.cap_id = HEADER_MODE,
		.min = V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE,
		.max = V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE) |
				BIT(V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME),
		.value = V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		.hfi_id = HFI_PROP_SEQ_HEADER_MODE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_header_mode_gen2,
	},
	{
		.cap_id = PREPEND_SPSPPS_TO_IDR,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 0,
	},
	{
		.cap_id = BITRATE,
		.min = 1,
		.max = BITRATE_MAX_AR50LT,
		.step_or_mask = 1,
		.value = BITRATE_DEFAULT_AR50LT,
		.hfi_id = HFI_PROP_TOTAL_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_bitrate,
	},
	{
		.cap_id = BITRATE_PEAK,
		.min = 1,
		.max = BITRATE_MAX_AR50LT,
		.step_or_mask = 1,
		.value = BITRATE_DEFAULT_AR50LT,
		.hfi_id = HFI_PROP_TOTAL_PEAK_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_peak_bitrate,
	},
	{
		.cap_id = BITRATE_MODE,
		.min = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
		.max = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) |
				BIT(V4L2_MPEG_VIDEO_BITRATE_MODE_CBR),
		.value = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
		.hfi_id = HFI_PROP_RATE_CONTROL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_bitrate_mode_gen2,
	},
	{
		.cap_id = FRAME_SKIP_MODE,
		.min = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED,
		.max = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED) |
				BIT(V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_LEVEL_LIMIT) |
				BIT(V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT),
		.value = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
	},
	{
		.cap_id = FRAME_RC_ENABLE,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 1,
	},
	{
		.cap_id = GOP_SIZE,
		.min = 0,
		.max = INT_MAX,
		.step_or_mask = 1,
		.value = 2 * DEFAULT_FPS - 1,
		.hfi_id = HFI_PROP_MAX_GOP_FRAMES,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_u32,
	},
	{
		.cap_id = ENTROPY_MODE,
		.min = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC,
		.max = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC) |
				BIT(V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC),
		.value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC,
		.hfi_id = HFI_PROP_CABAC_SESSION,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_entropy_mode_gen2,
	},
	{
		.cap_id = MIN_FRAME_QP_H264,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT_AR50LT,
		.hfi_id = HFI_PROP_MIN_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_min_qp,
	},
	{
		.cap_id = MIN_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT_AR50LT,
		.hfi_id = HFI_PROP_MIN_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_min_qp,
	},
	{
		.cap_id = MAX_FRAME_QP_H264,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
		.hfi_id = HFI_PROP_MAX_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_max_qp,
	},
	{
		.cap_id = MAX_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
		.hfi_id = HFI_PROP_MAX_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_max_qp,
	},
	{
		.cap_id = I_FRAME_MIN_QP_H264,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT_AR50LT,
	},
	{
		.cap_id = I_FRAME_MIN_QP_HEVC,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT_AR50LT,
	},
	{
		.cap_id = P_FRAME_MIN_QP_H264,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT_AR50LT,
	},
	{
		.cap_id = P_FRAME_MIN_QP_HEVC,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT_AR50LT,
	},
	{
		.cap_id = B_FRAME_MIN_QP_H264,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT_AR50LT,
	},
	{
		.cap_id = B_FRAME_MIN_QP_HEVC,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT_AR50LT,
	},
	{
		.cap_id = I_FRAME_MAX_QP_H264,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = I_FRAME_MAX_QP_HEVC,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = P_FRAME_MAX_QP_H264,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = P_FRAME_MAX_QP_HEVC,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = B_FRAME_MAX_QP_H264,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = B_FRAME_MAX_QP_HEVC,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = I_FRAME_QP_H264,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = I_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = P_FRAME_QP_H264,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = P_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = B_FRAME_QP_H264,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = B_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT_AR50LT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = INPUT_BUF_HOST_MAX_COUNT,
		.min = DEFAULT_MAX_HOST_BUF_COUNT,
		.max = DEFAULT_MAX_HOST_BURST_BUF_COUNT,
		.step_or_mask = 1,
		.value = DEFAULT_MAX_HOST_BUF_COUNT,
		.hfi_id = HFI_PROP_BUFFER_HOST_MAX_COUNT,
		.flags = CAP_FLAG_INPUT_PORT,
		.set = iris_set_u32,
	},
	{
		.cap_id = OUTPUT_BUF_HOST_MAX_COUNT,
		.min = DEFAULT_MAX_HOST_BUF_COUNT,
		.max = DEFAULT_MAX_HOST_BURST_BUF_COUNT,
		.step_or_mask = 1,
		.value = DEFAULT_MAX_HOST_BUF_COUNT,
		.hfi_id = HFI_PROP_BUFFER_HOST_MAX_COUNT,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_u32,
	},
	{
		.cap_id = IR_TYPE,
		.min = V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_RANDOM,
		.max = V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_RANDOM,
		.step_or_mask = BIT(V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_RANDOM),
		.value = V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_RANDOM,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
	},
	{
		.cap_id = IR_PERIOD,
		.min = 0,
		.max = INT_MAX,
		.step_or_mask = 1,
		.value = 0,
		.flags = CAP_FLAG_OUTPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_ir_period,
	},
};

static const u32 qcm2290_dec_ip_int_buf_tbl[] = {
	BUF_BIN,
	BUF_COMV,
	BUF_NON_COMV,
	BUF_LINE,
};
