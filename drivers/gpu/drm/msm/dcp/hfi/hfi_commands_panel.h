/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_COMMANDS_PANEL_H__
#define __H_HFI_COMMANDS_PANEL_H__

/* Panel-command range. */
#define HFI_COMMAND_PANEL_BEGIN                                         0x03000000

/* Start panel initialization; send with timing and generic packets in one HFI header. */
#define HFI_COMMAND_PANEL_INIT_PANEL_CAPS                            0x03000001

/* Provide one panel timing-mode description. */
#define HFI_COMMAND_PANEL_INIT_TIMING_MODE_CAPS                      0x03000002

/* Provide non-timing panel properties. */
#define HFI_COMMAND_PANEL_INIT_GENERIC_CAPS                          0x03000003

/* Deinitialize panel attributes. */
#define HFI_COMMAND_PANEL_DEINIT                                     0x03000004

/* End of panel-command range. */
#define HFI_COMMAND_PANEL_END                                        0x03ffffff

#endif /* __H_HFI_COMMANDS_PANEL_H__ */
