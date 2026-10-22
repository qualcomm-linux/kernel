/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_COMMON_H__
#define __H_HFI_DEFS_COMMON_H__

#include <linux/bits.h>

/* Boolean property values. */
#define HFI_FALSE		0
#define HFI_TRUE		1

#define HFI_MAX_PLANES  4 /* Maximum number of planes supported */

#define HFI_MAX_COLOR_COMPONENTS  3 /* Maximum number of color components supported */
#define HFI_DHDR_PAYLOAD_MAX_SIZE 32 /* Maximum dynamic HDR payload size */

/* Color-format class is encoded in bits 31:24. */
enum hfi_color_formats {
	/* Interleaved RGB */
	HFI_COLOR_FORMAT_INTERLEAVED_RGB_MIN        = 0x01000000,
	HFI_COLOR_FORMAT_RGB565                     = 0x01000001,
	HFI_COLOR_FORMAT_RGB888                     = 0x01000002,
	HFI_COLOR_FORMAT_ARGB8888                   = 0x01000003,
	HFI_COLOR_FORMAT_RGBA8888                   = 0x01000004,
	HFI_COLOR_FORMAT_XRGB8888                   = 0x01000005,
	HFI_COLOR_FORMAT_RGBX8888                   = 0x01000006,
	HFI_COLOR_FORMAT_ARGB1555                   = 0x01000007,
	HFI_COLOR_FORMAT_RGBA5551                   = 0x01000008,
	HFI_COLOR_FORMAT_XRGB1555                   = 0x01000009,
	HFI_COLOR_FORMAT_RGBX5551                   = 0x0100000a,
	HFI_COLOR_FORMAT_ARGB4444                   = 0x0100000b,
	HFI_COLOR_FORMAT_RGBA4444                   = 0x0100000c,
	HFI_COLOR_FORMAT_RGBX4444                   = 0x0100000d,
	HFI_COLOR_FORMAT_XRGB4444                   = 0x0100000e,
	HFI_COLOR_FORMAT_ARGB2_10_10_10             = 0x0100000f,
	HFI_COLOR_FORMAT_XRGB2_10_10_10             = 0x01000010,
	HFI_COLOR_FORMAT_RGBA10_10_10_2             = 0x01000011,
	HFI_COLOR_FORMAT_RGBX10_10_10_2             = 0x01000012,
	HFI_COLOR_FORMAT_ARGB_FP_16                 = 0x01000013,
	HFI_COLOR_FORMAT_RGBA_FP_16                 = 0x01000014,
	HFI_COLOR_FORMAT_RGBA8888_TILED             = 0x01000015,
	HFI_COLOR_FORMAT_RGBX8888_TILED             = 0x01000016,
	HFI_COLOR_FORMAT_RGBA10_10_10_2_TILED       = 0x01000017,
	HFI_COLOR_FORMAT_RGBX10_10_10_2_TILED       = 0x01000018,
	HFI_COLOR_FORMAT_RGB888_BYPASS              = 0x01000019,
	HFI_COLOR_FORMAT_INTERLEAVED_RGB_MAX        = 0x01ffffff,
	/* Interleaved BGR */
	HFI_COLOR_FORMAT_INTERLEAVED_BGR_MIN        = 0x02000000,
	HFI_COLOR_FORMAT_BGR565                     = 0x02000001,
	HFI_COLOR_FORMAT_BGR888                     = 0x02000002,
	HFI_COLOR_FORMAT_ABGR8888                   = 0x02000003,
	HFI_COLOR_FORMAT_BGRA8888                   = 0x02000004,
	HFI_COLOR_FORMAT_BGRX8888                   = 0x02000005,
	HFI_COLOR_FORMAT_XBGR8888                   = 0x02000006,
	HFI_COLOR_FORMAT_ABGR1555                   = 0x02000007,
	HFI_COLOR_FORMAT_BGRA5551                   = 0x02000008,
	HFI_COLOR_FORMAT_XBGR1555                   = 0x02000009,
	HFI_COLOR_FORMAT_BGRX5551                   = 0x0200000a,
	HFI_COLOR_FORMAT_ABGR4444                   = 0x0200000b,
	HFI_COLOR_FORMAT_BGRA4444                   = 0x0200000c,
	HFI_COLOR_FORMAT_BGRX4444                   = 0x0200000d,
	HFI_COLOR_FORMAT_XBGR4444                   = 0x0200000e,
	HFI_COLOR_FORMAT_ABGR2_10_10_10             = 0x0200000f,
	HFI_COLOR_FORMAT_XBGR2_10_10_10             = 0x02000010,
	HFI_COLOR_FORMAT_BGRA10_10_10_2             = 0x02000011,
	HFI_COLOR_FORMAT_BGRX10_10_10_2             = 0x02000012,
	HFI_COLOR_FORMAT_ABGR16_FP_16               = 0x02000013,
	HFI_COLOR_FORMAT_BGRA16_FP_16               = 0x02000014,
	HFI_COLOR_FORMAT_INTERLEAVED_BGR_MAX        = 0x02ffffff,
	/* Compressed RGBA UBWC 3.0/ 4.x / 5.0 Lossless */
	HFI_COLOR_FORMAT_RGBA_UBWC_LOSSLESS_MIN     = 0x03000000,
	HFI_COLOR_FORMAT_UBWC_RGBA8888              = 0x03000001,
	HFI_COLOR_FORMAT_UBWC_RGBA10_10_10_2        = 0x03000002,
	HFI_COLOR_FORMAT_UBWC_RGBA16_16_16_16_F     = 0x03000003,
	HFI_COLOR_FORMAT_UBWC_RGB565                = 0x03000004,
	HFI_COLOR_FORMAT_UBWC_RGBX8888              = 0x03000005,
	HFI_COLOR_FORMAT_UBWC_RGBX10_10_10_2        = 0x03000006,
	HFI_COLOR_FORMAT_RGBA_UBWC_LOSSLESS_MAX     = 0x03ffffff,
	/* Compressed RGBA UBWC 5.0 Lossy */
	HFI_COLOR_FORMAT_RGBA_UBWC_LOSSY_MIN        = 0x04000000,
	HFI_COLOR_FORMAT_UBWC_RGBA8888_L_8_5        = 0x04000001,
	HFI_COLOR_FORMAT_UBWC_RGBA8888_L_2_1        = 0x04000002,
	HFI_COLOR_FORMAT_RGBA_UBWC_LOSSY_MAX        = 0x04ffffff,
	/* Linear / Uncompressed YUV420 / FSC */
	HFI_COLOR_FORMAT_LINEAR_MIN                 = 0x05000000,
	HFI_COLOR_FORMAT_YU12                       = 0x05000001,
	HFI_COLOR_FORMAT_YV12                       = 0x05000002,
	HFI_COLOR_FORMAT_NV12                       = 0x05000003,
	HFI_COLOR_FORMAT_NV21                       = 0x05000004,
	HFI_COLOR_FORMAT_P010                       = 0x05000005,
	HFI_COLOR_FORMAT_YUYV                       = 0x05000006,
	HFI_COLOR_FORMAT_YVYU                       = 0x05000007,
	HFI_COLOR_FORMAT_UYVY                       = 0x05000008,
	HFI_COLOR_FORMAT_VYUY                       = 0x05000009,
	HFI_COLOR_FORMAT_FSC_R                      = 0x0500000a,
	HFI_COLOR_FORMAT_FSC_G                      = 0x0500000b,
	HFI_COLOR_FORMAT_FSC_B                      = 0x0500000c,
	HFI_COLOR_FORMAT_FSC_ALPHA                  = 0x0500000d,
	HFI_COLOR_FORMAT_P210                       = 0x0500000e,
	HFI_COLOR_FORMAT_Y10                        = 0x0500000f,
	HFI_COLOR_FORMAT_A10                        = 0x05000010,
	HFI_COLOR_FORMAT_Y10_MSB_ALIGN              = 0x05000011,
	HFI_COLOR_FORMAT_A10_MSB_ALIGN              = 0x05000012,
	HFI_COLOR_FORMAT_Y8                         = 0x05000013,
	HFI_COLOR_FORMAT_LINEAR_MAX                 = 0x05ffffff,
	/* Compressed UBWC 3.0 / 4.x / 5.0 Lossless / FSC */
	HFI_COLOR_FORMAT_UBWC_LOSSLESS_MIN          = 0x06000000,
	HFI_COLOR_FORMAT_UBWC_NV12                  = 0x06000001,
	HFI_COLOR_FORMAT_UBWC_NV12_INTERLACE        = 0x06000002,
	HFI_COLOR_FORMAT_UBWC_TP10                  = 0x06000003,
	HFI_COLOR_FORMAT_UBWC_P010                  = 0x06000004,
	HFI_COLOR_FORMAT_UBWC_FSC_R                 = 0x06000005,
	HFI_COLOR_FORMAT_UBWC_FSC_G                 = 0x06000006,
	HFI_COLOR_FORMAT_UBWC_FSC_B                 = 0x06000007,
	HFI_COLOR_FORMAT_UBWC_FSC_ALPHA             = 0x06000008,
	HFI_COLOR_FORMAT_UBWC_FSC_R_4R              = 0x06000009,
	HFI_COLOR_FORMAT_UBWC_FSC_G_4R              = 0x0600000a,
	HFI_COLOR_FORMAT_UBWC_FSC_B_4R              = 0x0600000b,
	HFI_COLOR_FORMAT_UBWC_FSC_ALPHA_4R          = 0x0600000c,
	HFI_COLOR_FORMAT_UBWC_ABGR_NV12_4R_4Y       = 0x0600000d,
	HFI_COLOR_FORMAT_UBWC_P210                  = 0x0600000e,
	HFI_COLOR_FORMAT_UBWC_Y10                   = 0x0600000f,
	HFI_COLOR_FORMAT_UBWC_A10                   = 0x06000010,

	HFI_COLOR_FORMAT_UBWC_LOSSLESS_MAX          = 0x06ffffff,
	/* Compressed UBWC 3.0 / 4.x / 5.0 Lossy */
	HFI_COLOR_FORMAT_UBWC_LOSSY_MIN             = 0x07000000,
	HFI_COLOR_FORMAT_UBWC_NV12_LOSSY            = 0x07000001,
	HFI_COLOR_FORMAT_UBWC_TP10_LOSSY            = 0x07000002,
	HFI_COLOR_FORMAT_UBWC_LOSSY_MAX             = 0x07ffffff,
};

/* Shared HFI buffer descriptor. */
struct hfi_buff {
	u32 addr_l;
	u32 addr_h;
	u32 size;
	u32 version;
	u32 flags;
};

/* Multi-plane HFI buffer descriptor. */
struct hfi_plane_buff {
	u32 addr_l[HFI_MAX_PLANES];
	u32 addr_h[HFI_MAX_PLANES];
	u32 size;
	u32 version;
	u32 flags;
	u32 format;
};

/* 64-bit HFI property value. */
struct hfi_prop_u64 {
	u32 val_lo;
	u32 val_hi;
};

#define HFI_BUFF_FEATURE_ENABLE         BIT(0)
#define HFI_BUFF_FEATURE_BROADCAST      BIT(1)
#define HFI_BUFF_FEATURE_HW_BLK_IDX_0   BIT(2)
#define HFI_BUFF_FEATURE_HW_BLK_IDX_1   BIT(3)
#define HFI_BUFF_FEATURE_HW_BLK_IDX_2   BIT(4)
#define HFI_BUFF_FEATURE_HW_BLK_IDX_3   BIT(5)
#define HFI_BUFF_3D_LUT_MODE_A          BIT(16)
#define HFI_BUFF_3D_LUT_MODE_B          BIT(17)
/* Buffer descriptor visible to the DPU. */
struct hfi_buff_dpu {
	u32 flags;
	u32 iova;
	u32 len;
};

/* HFI subsystem identifiers. */
enum hfi_subsystem_type {
	HFI_SUBSYSTEM_TYPE_LSR = 0,
	HFI_SUBSYSTEM_TYPE_GMU = 1
};

#endif /* __H_HFI_DEFS_COMMON_H__ */
