/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_PROPERTIES_DEVICE_H__
#define __H_HFI_PROPERTIES_DEVICE_H__

/* Device-property range. */
#define HFI_PROPERTY_DEVICE_BEGIN                                    0x00010000

/* DCP hardware version. */
#define HFI_PROPERTY_DEVICE_INIT_DCP_HW_VERSION                      0x00010001

/* MDSS hardware version. */
#define HFI_PROPERTY_DEVICE_INIT_MDSS_HW_VERSION                      0x00010002

/* VIG layer indices; the high byte identifies the rectangle. */
#define HFI_PROPERTY_DEVICE_INIT_VIG_INDICES                         0x00010004

/* DMA layer indices; the high byte identifies the rectangle. */
#define HFI_PROPERTY_DEVICE_INIT_DMA_INDICES                         0x00010005

/* DCP firmware version. */
#define HFI_PROPERTY_DEVICE_INIT_DCP_FW_VERSION                      0x00010003

/* Maximum display count. */
#define HFI_PROPERTY_DEVICE_INIT_MAX_DISPLAY_COUNT                   0x00010006

/* Writeback block indices. */
#define HFI_PROPERTY_DEVICE_INIT_WB_INDICES                          0x00010007

/* Maximum linear writeback size: width [31:16], height [15:0]. */
#define HFI_PROPERTY_DEVICE_INIT_MAX_WB_LINEAR_RESOLUTION            0x00010008

/* Maximum UBWC writeback size: width [31:16], height [15:0]. */
#define HFI_PROPERTY_DEVICE_INIT_MAX_WB_UBWC_RESOLUTION              0x00010009

/* DSI interface indices. */
#define HFI_PROPERTY_DEVICE_INIT_DSI_INDICES                         0x0001000a

/* Maximum DSI size: width [31:16], height [15:0]. */
#define HFI_PROPERTY_DEVICE_INIT_MAX_DSI_RESOLUTION                  0x0001000b

/* DisplayPort interface indices. */
#define HFI_PROPERTY_DEVICE_INIT_DP_INDICES                          0x0001000c

/* Maximum DisplayPort size: width [31:16], height [15:0]. */
#define HFI_PROPERTY_DEVICE_INIT_MAX_DP_RESOLUTION                   0x0001000d

/* Downscaler indices. */
#define HFI_PROPERTY_DEVICE_INIT_DS_INDICES                          0x0001000e

/* Maximum downscaler size: width [31:16], height [15:0]. */
#define HFI_PROPERTY_DEVICE_INIT_MAX_DS_RESOLUTION                   0x0001000f

/* Core average-bandwidth vote. */
#define HFI_PROPERTY_DEVICE_CORE_AB                                  0x00010010

/* Core instantaneous-bandwidth vote. */
#define HFI_PROPERTY_DEVICE_CORE_IB                                  0x00010011

/* Core power-rail vote. */
#define HFI_PROPERTY_DEVICE_CORE_POWER_RAIL                          0x00010012

/* LLCC average-bandwidth vote. */
#define HFI_PROPERTY_DEVICE_LLCC_AB                                  0x00010013

/* LLCC instantaneous-bandwidth vote. */
#define HFI_PROPERTY_DEVICE_LLCC_IB                                  0x00010014

/* DRAM average-bandwidth vote. */
#define HFI_PROPERTY_DEVICE_DRAM_AB                                  0x00010015

/* DRAM instantaneous-bandwidth vote. */
#define HFI_PROPERTY_DEVICE_DRAM_IB                                  0x00010016

/* LSR writeback CSC indices. */
#define HFI_PROPERTY_DEVICE_INIT_LSR_WB_CSC_INDICES                  0x00010017

/* LSR writeback reprojection indices. */
#define HFI_PROPERTY_DEVICE_INIT_LSR_WB_REPROJ_INDICES               0x00010018

/* LSR writeback CSC layer indices. */
#define HFI_PROPERTY_DEVICE_INIT_LSR_WB_CSC_LAYER_INDICES            0x00010019

/* LSR writeback reprojection layer indices. */
#define HFI_PROPERTY_DEVICE_INIT_LSR_WB_REPROJ_LAYER_INDICES         0x0001001a

/* System-cache staling configuration. */
#define HFI_PROPERTY_DEVICE_INIT_SYS_CACHE_STALING                   0x0001001b

/* Active layers per display. */
#define HFI_PROPERTY_DEVICE_INIT_ACTIVE_LAYERS                        0x0001001c

/* Writeback DNSC support indices. */
#define HFI_PROPERTY_DEVICE_INIT_WB_DNSC_SUPPORT_INDICES             0x0001001d

/* End of device-property range. */
#define HFI_PROPERTY_DEVICE_END                                      0x0001ffff

#endif /* __H_HFI_PROPERTIES_DEVICE_H__ */
