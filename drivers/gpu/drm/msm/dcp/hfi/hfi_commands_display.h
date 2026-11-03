/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_COMMANDS_DISPLAY_H__
#define __H_HFI_COMMANDS_DISPLAY_H__

/* Display-command range. */
#define HFI_COMMAND_DISPLAY_BEGIN                                     0x02000000

/* Set display properties. */
#define HFI_COMMAND_DISPLAY_SET_PROPERTY                              0x02000001

/* Read display or layer properties. */
#define HFI_COMMAND_DISPLAY_GET_PROPERTY                              0x02000002

/* Commit previously submitted display state. */
#define HFI_COMMAND_DISPLAY_FRAME_TRIGGER                             0x02000003

/* Register a display event. */
#define HFI_COMMAND_DISPLAY_EVENT_REGISTER                            0x02000004

/* Unregister a display event. */
#define HFI_COMMAND_DISPLAY_EVENT_DEREGISTER                          0x02000005

/* Enable the display session. */
#define HFI_COMMAND_DISPLAY_ENABLE                                    0x02000006

/* Send post-enable panel commands. */
#define HFI_COMMAND_DISPLAY_POST_ENABLE                               0x02000007

/* Disable the display stream. */
#define HFI_COMMAND_DISPLAY_DISABLE                                   0x02000008

/* Release post-disable display resources. */
#define HFI_COMMAND_DISPLAY_POST_DISABLE                              0x02000009

/* Set a previously validated display mode. */
#define HFI_COMMAND_DISPLAY_SET_MODE                                  0x0200000a

/* Validate a display mode in firmware. */
#define HFI_COMMAND_DISPLAY_MODE_VALIDATE                             0x0200000b

/* Request host-side display power control. */
#define HFI_COMMAND_DISPLAY_POWER_CONTROL                             0x0200000c

/* Register display power-control handling. */
#define HFI_COMMAND_DISPLAY_POWER_REGISTER                            0x0200000d

/* Transfer one DCS command. */
#define HFI_COMMAND_DISPLAY_TRANSFER_DCS_CMD                          0x0200000e

/* Transfer a DCS command set. */
#define HFI_COMMAND_DISPLAY_TRANSFER_DCS_CMD_SET                      0x0200001d

/* Configure the display idle timer. */
#define HFI_COMMAND_DISPLAY_IDLE_TIMER_CONTROL                        0x0200000f

/* Configure DisplayPort transfer units. */
#define HFI_COMMAND_DISPLAY_SET_DP_TU                                 0x02000010

/* Configure HDR display properties. */
#define HFI_COMMAND_DISPLAY_CONFIG_HDR                                0x02000011

/* Configure display audio. */
#define HFI_COMMAND_DISPLAY_AUDIO_CONFIG                              0x02000012

/* Control display audio. */
#define HFI_COMMAND_DISPLAY_AUDIO_CONTROL                             0x02000013

/* Set the display color space. */
#define HFI_COMMAND_DISPLAY_SET_COLORSPACE                            0x02000014

/* Provide the HDCP 1.x AKSV. */
#define HFI_COMMAND_DISPLAY_HDCP1X_AKSV                               0x02000015

/* Provide an HDCP 2.x response. */
#define HFI_COMMAND_DISPLAY_HDCP2X_RESPONSE                           0x02000016

/* Report sink HDCP capabilities. */
#define HFI_COMMAND_DISPLAY_HDCP_FEATURE_SUPPORTED                    0x02000017

/* Configure DSI DCS command remapping. */
#define HFI_COMMAND_DISPLAY_DSI_CUSTOM_DCS_CMDS_SET_REMAP             0x02000018

/* Configure DSI DCS command replacement. */
#define HFI_COMMAND_DISPLAY_DSI_CUSTOM_DCS_CMDS_SET_REPLACE           0x02000019

/* Configure display batch mode. */
#define HFI_COMMAND_DISPLAY_BATCH_MODE                                0x0200001a

/* Configure the display HFI subsystem. */
#define HFI_COMMAND_DISPLAY_HFI_SUBSYSTEM_CONFIG                      0x0200001b

/* Execute a DCS low-power command type. */
#define HFI_COMMAND_DISPLAY_EXEC_DCS_CMD_TYPE                         0x0200001c

/* End of display-command range. */
#define HFI_COMMAND_DISPLAY_END                                       0x02ffffff

/* Alias for the display-command range end. */
#define HFI_COMMAND_DISPLAY_CLOSE                                     HFI_COMMAND_DISPLAY_END

#endif /* __H_HFI_COMMANDS_DISPLAY_H__ */
