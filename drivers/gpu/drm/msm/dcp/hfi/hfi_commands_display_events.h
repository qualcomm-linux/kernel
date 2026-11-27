/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_COMMANDS_DISPLAY_EVENTS_H
#define __H_HFI_COMMANDS_DISPLAY_EVENTS_H

/* Display-event command range. */
#define HFI_COMMAND_DISPLAY_EVENT_BEGIN                                         0x04000000

/* Report vertical sync and its index. */
#define HFI_COMMAND_DISPLAY_EVENT_VSYNC                                         0x04000001

/* Report frame-scan start and sequence ID. */
#define HFI_COMMAND_DISPLAY_EVENT_FRAME_SCAN_START                              0x04000002

/* Report frame-scan completion. */
#define HFI_COMMAND_DISPLAY_EVENT_FRAME_SCAN_COMPLETE                           0x04000003

/* Report display idle state. */
#define HFI_COMMAND_DISPLAY_EVENT_IDLE                                          0x04000004

/* Report display power-state changes. */
#define HFI_COMMAND_DISPLAY_EVENT_POWER                                         0x04000005

/* Report display hardware recovery. */
#define HFI_COMMAND_DISPLAY_EVENT_HW_RECOVERY                                   0x04000006

/* Report frame-capture completion. */
#define HFI_COMMAND_DISPLAY_EVENT_FRAME_CAPTURE_COMPLETE                        0x04000007

/* Report an unrecoverable panel failure. */
#define HFI_COMMAND_DISPLAY_EVENT_PANEL_DEAD                                    0x04000008

/* Report LTM statistics events. */
#define HFI_COMMAND_DISPLAY_EVENT_LTM                                           0x04000009

/* Report RGB histogram events. */
#define HFI_COMMAND_DISPLAY_EVENT_RGB_HIST                                      0x0400000a

/* Report PA histogram events. */
#define HFI_COMMAND_DISPLAY_EVENT_PA_HIST                                       0x0400000b

/* Report hotplug status. */
#define HFI_COMMAND_DISPLAY_EVENT_HPD_STATUS                                    0x0400000c

/* Report EDID and supported modes. */
#define HFI_COMMAND_DISPLAY_EVENT_EDID_INFO                                     0x0400000d

/* Report SPR/OPR values at frame scan. */
#define HFI_COMMAND_DISPLAY_EVENT_SPR_OPR                                       0x0400000e

/* Report interface MISR values at frame scan. */
#define HFI_COMMAND_DISPLAY_EVENT_INTERFACE_MISR                                0x0400000f

/* Request HDCP 1.x authentication start. */
#define HFI_COMMAND_DISPLAY_EVENT_HDCP1X_START                                  0x04000010

/* Request HDCP 1.x authentication stop. */
#define HFI_COMMAND_DISPLAY_EVENT_HDCP1X_STOP                                   0x04000011

/* Update HDCP 1.x encryption state. */
#define HFI_COMMAND_DISPLAY_EVENT_HDCP1X_ENC                                    0x04000012

/* Update HDCP 1.x repeater topology. */
#define HFI_COMMAND_DISPLAY_EVENT_HDCP1X_TOPOLOGY_UPDATE                        0x04000013

/* Start the HDCP 2.x sequence. */
#define HFI_COMMAND_DISPLAY_EVENT_HDCP2X_START                                  0x04000014

/* Process an HDCP 2.x message. */
#define HFI_COMMAND_DISPLAY_EVENT_HDCP2X_PROCESS_MSG                            0x04000015

/* Report an HDCP 2.x message timeout. */
#define HFI_COMMAND_DISPLAY_EVENT_HDCP2X_TIMEOUT                                0x04000016

/* Report sink HDCP capabilities. */
#define HFI_COMMAND_DISPLAY_EVENT_HDCP_FEATURE_SUPPORTED                        0x04000017

/* Report COPR status data. */
#define HFI_COMMAND_DISPLAY_EVENT_AIQE_COPR_READ                                0x04000018

/* Report programmable-line events. */
#define HFI_COMMAND_DISPLAY_EVENT_PROG_LINE                                     0x04000019

/* End of display-event command range. */
#define HFI_COMMAND_DISPLAY_EVENT_END                                           0x04ffffff

#endif /* __H_HFI_COMMANDS_DISPLAY_EVENTS_H */
