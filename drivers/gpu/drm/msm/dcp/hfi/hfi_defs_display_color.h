/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_DISPLAY_COLOR_H__
#define __H_HFI_DEFS_DISPLAY_COLOR_H__

#include <linux/types.h>

#include "hfi_defs_common.h"

#define HFI_DISPLAY_COLOR_FLAGS_FEATURE_ENABLE         BIT(0)

#define HFI_DISPLAY_COLOR_FLAGS_FEATURE_BROADCAST      BIT(1)

#define HFI_DISPLAY_COLOR_HW_BLK_INDEX_0_FLAG    BIT(2)
#define HFI_DISPLAY_COLOR_HW_BLK_INDEX_1_FLAG    BIT(3)
#define HFI_DISPLAY_COLOR_HW_BLK_INDEX_2_FLAG    BIT(4)
#define HFI_DISPLAY_COLOR_HW_BLK_INDEX_3_FLAG    BIT(5)

#define HFI_DITHER_MATRIX_SZ                16
#define HFI_DITHER_LUMA_MODE                BIT(0)
#define HFI_DITHER_OFFSET_ENABLE            BIT(1)

#define HFI_DITHER_MATRIX_SZ_EXTENDED       256
#define HFI_DITHER_MATRIX_SELECT_4_4        0
#define HFI_DITHER_MATRIX_SELECT_6_6        1
#define HFI_DITHER_MATRIX_SELECT_8_8        2
#define HFI_DITHER_MATRIX_SELECT_16_16      3

/* Dither configuration. */
struct hfi_display_dither {
	u32 flags;
	u32 feature_flags;
	u32 temporal_en;
	u32 c0_bitdepth;
	u32 c1_bitdepth;
	u32 c2_bitdepth;
	u32 c3_bitdepth;
	u32 dither_matrix_select;
	s32 matrix[HFI_DITHER_MATRIX_SZ_EXTENDED];
};

/* PA dither configuration. */
struct hfi_display_pa_dither {
	u32 flags;
	u32 strength;
	u32 offset_en;
	u32 matrix[HFI_DITHER_MATRIX_SZ];
};

#define HFI_QRTC_LUT_SIZE 81        /* Each LUT is 9x9 */
#define HFI_QRTC_NUM_LUTS 3         /* Number of LPF/HPF tables */

/* QRTC subsampling modes. */
#define HFI_QRTC_SUBSAMPLE_1x1 0x0  /* Horizontal: 1, Vertical: 1 */
#define HFI_QRTC_SUBSAMPLE_2x1 0x1  /* Horizontal: 2, Vertical: 1 */
#define HFI_QRTC_SUBSAMPLE_2x2 0x2  /* Horizontal: 2, Vertical: 2 */
#define HFI_QRTC_SUBSAMPLE_3x3 0x3  /* Horizontal: 3, Vertical: 3 */

/* QRTC DMA selections. */
#define HFI_QRTC_DMA_1 0x1
#define HFI_QRTC_DMA_3 0x3

/* QRTC rectangle selections. */
#define HFI_QRTC_RECT0 0x0  /* Select Rectangle 0 */
#define HFI_QRTC_RECT1 0x1  /* Select Rectangle 1 */
#define HFI_WRITE_BACK_0 0x0
#define HFI_WRITE_BACK_1 0x1
#define HFI_WRITE_BACK_2 0x2
#define HFI_CWB_BLOCK_0 0x0
#define HFI_CWB_BLOCK_1 0x1
#define HFI_CWB_BLOCK_2 0x2
#define HFI_CWB_BLOCK_3 0x3
#define HFI_CWB_BLOCK_4 0x4
#define HFI_CWB_BLOCK_5 0x5
#define HFI_CWB_BLOCK_6 0x6
#define HFI_CWB_BLOCK_7 0x7

/* Bit 16 marks a secure QRTC buffer. */
#define HFI_QRTC_SECURE_BUFF BIT(16)

/* QRTC configuration and capture-buffer selection. */
struct hfi_qrtc_config {
	u32 flags;
	u32 lut_dma_flags;
	u32 iova_l;
	u32 iova_h;
	u32 len;
	u32 coring_en;
	u32 coring_pos;
	u32 coring_neg;
	u32 lpf_en;
	u32 subsample_mode;
	u32 dma_sel;
	u32 rect_sel;
	u32 wb_sel;
	u32 cwb_sel;
	u32 cwb_tap_point;
	u32 cwb_iova_l;
	u32 cwb_iova_h;
	u32 cwb_len;
	u32 cwb_height;
	u32 cwb_width;
	u32 format;
	u32 aligned_cwb_height;
	u32 aligned_cwb_width;
};

#define HFI_LTM_DATA_SIZE_0        32
#define HFI_LTM_DATA_SIZE_1        128
#define HFI_LTM_DATA_SIZE_2        256
#define HFI_LTM_BLOCK_MAX          4

#define HFI_LTM_STATS_SAT          BIT(1)
#define HFI_LTM_STATS_MERGE_SAT    BIT(2)

#define HFI_LTM_HIST_DONE                   BIT(0)
#define HFI_LTM_WB_PB                       BIT(1)
#define HFI_LTM_HIST_OFF                    BIT(2)

struct hfi_display_ltm_init_param {
	u32 flags;
	u32 init_param_01;
	u32 init_param_02;
	u32 init_param_03;
	u32 init_param_04;
};

struct hfi_display_ltm_cfg_param {
	u32 flags;
	u32 cfg_param_01;
	u32 cfg_param_02;
	u32 cfg_param_03;
	u32 cfg_param_04;
	u32 cfg_param_05;
	u32 cfg_param_06;
};

/* Shared LTM statistics buffer descriptor. */
struct hfi_display_ltm_buffer {
	u32 flags;
	u32 dpu_iova_l;
	u32 dpu_iova_h;
	u32 dcp_addr_l;
	u32 dcp_addr_h;
	u32 size;
};

struct hfi_display_ltm_stats_data {
	u32 stats_01[HFI_LTM_DATA_SIZE_0][HFI_LTM_DATA_SIZE_1];
	u32 stats_02[HFI_LTM_DATA_SIZE_2];
	u32 stats_03[HFI_LTM_DATA_SIZE_0];
	u32 stats_04[HFI_LTM_DATA_SIZE_0];
	u32 stats_05[HFI_LTM_DATA_SIZE_0];
	u32 status_flag;
	u32 display_h;
	u32 display_v;
	u32 init_h[HFI_LTM_BLOCK_MAX];
	u32 init_v;
	u32 inc_h;
	u32 inc_v;
	u32 portrait_en;
	u32 merge_en;
	u32 cfg_param_01;
	u32 cfg_param_02;
	u32 cfg_param_03;
	u32 cfg_param_04;
	u32 feature_flag;
	u32 checksum;
};

struct hfi_display_ltm_event_resp {
	u32 event_type;
	u32 dcp_addr_h;
	u32 dcp_addr_l;
};

#define HFI_RGB_HISTOGRAM_BIN_COUNT 1024
#define HFI_RGB_COMPONENT_SIZE 3

#define HFI_RGB_HIST_ROI_ENABLE BIT(1)

#define HFI_RGB_HIST_DONE BIT(0)
#define HFI_RGB_HIST_WB_ERR BIT(1)
#define HFI_RGB_HIST_OFF BIT(2)

struct hfi_display_rgb_hist_ctrl {
	u32 flags;
	u32 roi_mode;
	u32 roi_x;
	u32 roi_y;
	u32 roi_width;
	u32 roi_height;
	u32 tap_point;
	u32 colorspace_mode;
};

struct hfi_display_rgb_hist_stats_data {
	u32 hist[HFI_RGB_HISTOGRAM_BIN_COUNT];
	u32 hist_min;
	u32 hist_max;
	u32 hist_sum_lsb;
	u32 hist_sum_msb;
	u32 hcount;
	u32 crc;
	u32 status_flags;
	u32 roi_mode;
	u32 roi_x;
	u32 roi_y;
	u32 roi_width;
	u32 roi_height;
	u32 tap_point;
	u32 colorspace_mode;
};

/* Shared RGB histogram buffer descriptor. */
struct hfi_display_rgb_hist_buffer {
	u32 flags;
	u32 dpu_iova_lo[HFI_RGB_COMPONENT_SIZE];
	u32 dpu_iova_hi[HFI_RGB_COMPONENT_SIZE];
	u32 dcp_addr_lo[HFI_RGB_COMPONENT_SIZE];
	u32 dcp_addr_hi[HFI_RGB_COMPONENT_SIZE];
	u32 size;
};

struct hfi_display_rgb_hist_event_resp {
	u32 event_type;
	u32 dcp_addr_lo[HFI_RGB_COMPONENT_SIZE];
	u32 dcp_addr_hi[HFI_RGB_COMPONENT_SIZE];
};

/* Shared PA histogram buffer descriptor. */
struct hfi_display_pa_hist_buffer {
	u32 flags;
	u32 dcp_addr_lo;
	u32 dcp_addr_hi;
	u32 size;
};

struct hfi_display_pa_hist_event_resp {
	u32 dcp_addr_lo;
	u32 dcp_addr_hi;
};

#endif /* __H_HFI_DEFS_DISPLAY_COLOR_H__ */
