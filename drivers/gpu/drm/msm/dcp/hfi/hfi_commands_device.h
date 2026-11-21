/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_COMMANDS_DEVICE_H__
#define __H_HFI_COMMANDS_DEVICE_H__

/* Device-level command range. */
#define HFI_COMMAND_DEVICE_BEGIN                                     0x01000000

/* Initialize the device and return firmware capabilities. */
#define HFI_COMMAND_DEVICE_INIT                                      0x01000001

/* Device, display, and pipeline capability response. */
#define HFI_COMMAND_DEVICE_INIT_DEVICE_CAPS                          0x01000002

/* VIG pipeline capability response. */
#define HFI_COMMAND_DEVICE_INIT_VIG_CAPS                             0x01000003

/* DMA pipeline capability response. */
#define HFI_COMMAND_DEVICE_INIT_DMA_CAPS                             0x01000004

/* Common VIG/DMA capability response. */
#define HFI_COMMAND_DEVICE_INIT_COMMON_LAYER_CAPS                    0x01000005

/* Display capability response. */
#define HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS                         0x01000006

/* Writeback capability response. */
#define HFI_COMMAND_DEVICE_INIT_DISPLAY_WB_CAPS                      0x01000007

/* Register clocks and bandwidth resources with firmware. */
#define HFI_COMMAND_DEVICE_RESOURCE_REGISTER                         0x01000008

/* Firmware requests a resource vote from the host. */
#define HFI_COMMAND_DEVICE_CALLBACK_RESOURCE_VOTE                    0x01000009

/* VIG rect-1 capability response. */
#define HFI_COMMAND_DEVICE_INIT_VIG_R1_CAPS                          0x0100000a

/* DMA rect-1 capability response. */
#define HFI_COMMAND_DEVICE_INIT_DMA_R1_CAPS                          0x0100000b

/* Provide the last-command buffer to firmware. */
#define HFI_COMMAND_DEVICE_LUT_DMA_LAST_CMD                          0x0100000c

/* Deinitialize the device. */
#define HFI_COMMAND_DEVICE_DEINIT                                    0x0100000d

/* LSR writeback CSC layer capability response. */
#define HFI_COMMAND_DEVICE_INIT_LSR_WB_CSC_LAYER_CAPS                0x0100000e

/* LSR writeback reprojection layer capability response. */
#define HFI_COMMAND_DEVICE_INIT_LSR_WB_REPROJ_LAYER_CAPS             0x0100000f

/* LSR writeback CSC output capability response. */
#define HFI_COMMAND_DEVICE_INIT_DISPLAY_LSR_WB_CSC_CAPS              0x01000010

/* LSR writeback reprojection output capability response. */
#define HFI_COMMAND_DEVICE_INIT_DISPLAY_LSR_WB_REPROJ_CAPS           0x01000011

/* Configure the hardware-fence HFI queue. */
#define HFI_COMMAND_DEVICE_HWFENCE_HFI_CONFIG                        0x01000012

/* Update hotplug state and provide EDID/mode buffers. */
#define HFI_COMMAND_DEVICE_HOT_PLUG_DETECT                           0x01000013

/* Configure resources shared across virtual machines. */
#define HFI_COMMAND_DEVICE_RESOURCE_CTRL                             0x01000014

/* Notify firmware of a subsystem restart. */
#define HFI_COMMAND_DEVICE_SUBSYSTEM_RESTART_NOTIFY                  0x01000015

/* End of device-level command range. */
#define HFI_COMMAND_DEVICE_END                                       0x01ffffff

#endif /* __H_HFI_COMMANDS_DEVICE_H__ */
