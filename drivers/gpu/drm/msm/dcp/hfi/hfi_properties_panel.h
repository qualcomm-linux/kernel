/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_PROPERTIES_PANEL_H__
#define __H_HFI_PROPERTIES_PANEL_H__

/* Panel-property range. */
#define HFI_PROPERTY_PANEL_BEGIN                                     0x00040000

/* Sets the total number of timing modes available for the panel. */
#define HFI_PROPERTY_PANEL_TIMING_MODE_COUNT                         0x00040001

/* Sets the index of the timing mode. */

#define HFI_PROPERTY_PANEL_INDEX                                     0x00040002
/* Sets the panel clock speed in Hz. */
#define HFI_PROPERTY_PANEL_CLOCKRATE                                 0x00040003

/* Sets the frame rate for the panel. */
#define HFI_PROPERTY_PANEL_FRAMERATE                                 0x00040004

/* Sets active dimensions, porches, pulse width, and sync skew. */
#define HFI_PROPERTY_PANEL_RESOLUTION_DATA                           0x00040005

/* Sets jitter as a numerator/denominator pair for early wakeup. */
#define HFI_PROPERTY_PANEL_JITTER                                    0x00040006

/* Sets all the compression data for VDC / DSC. */
#define HFI_PROPERTY_PANEL_COMPRESSION_DATA                          0x00040007

/* Lists available display topologies. */
#define HFI_PROPERTY_PANEL_DISPLAY_TOPOLOGY                          0x00040008

/* Sets the default topology index. */
#define HFI_PROPERTY_PANEL_DEFAULT_TOPOLOGY_INDEX                    0x00040009

/* Panel name available for current timing mode. */
#define HFI_PROPERTY_PANEL_NAME                                      0x0004000a

/* Specifies the type of the panel, OLED / LCD. */
#define HFI_PROPERTY_PANEL_PHYSICAL_TYPE                             0x0004000b

/* Sets the panel bits per pixel. */
#define HFI_PROPERTY_PANEL_BPP                                       0x0004000c

/* Enables or disables each DSI data lane. */
#define HFI_PROPERTY_PANEL_LANES_STATE                               0x0004000d

/* Specifies the panel lane map. */
#define HFI_PROPERTY_PANEL_LANE_MAP                                  0x0004000e

/* Specifies the R, G and B channel ordering. */
#define HFI_PROPERTY_PANEL_COLOR_ORDER                               0x0004000f

/* Specifies the trigger mechanism to be used for DMA path. */
#define HFI_PROPERTY_PANEL_DMA_TRIGGER                               0x00040010

/* Property used to enable appending end of transmission packets. */
#define HFI_PROPERTY_PANEL_TX_EOT_APPEND                             0x00040011

/* Selects the DSI lane state at BLLP end-of-frame. */
#define HFI_PROPERTY_PANEL_BLLP_EOF_POWER_MODE                       0x00040012

/* Selects the DSI lane state during BLLP. */
#define HFI_PROPERTY_PANEL_BLLP_POWER_MODE                           0x00040013

/* Specifies the panel traffic mode. */
#define HFI_PROPERTY_PANEL_TRAFFIC_MODE                              0x00040014

/* Specifies the virtual channel identifier. */
#define HFI_PROPERTY_PANEL_VIRTUAL_CHANNEL_ID                        0x00040015

/* DCS command for write_memory_start. */
#define HFI_PROPERTY_PANEL_WR_MEM_START                              0x00040016

/* DCS command for write_memory_continue. */
#define HFI_PROPERTY_PANEL_WR_MEM_CONTINUE                           0x00040017

/* Inserts the DCS command. */
#define HFI_PROPERTY_PANEL_TE_DCS_COMMAND                            0x00040018

/* Specifies the panel operating mode. */
#define HFI_PROPERTY_PANEL_OPERATING_MODE                            0x00040019

/* Specifies the min backlight level supported by the panel. */
#define HFI_PROPERTY_PANEL_BL_MIN_LEVEL                              0x0004001a

/* Specifies the max backlight level supported by the panel. */
#define HFI_PROPERTY_PANEL_BL_MAX_LEVEL                              0x0004001b

/* Specifies the max brightness level supported. */
#define HFI_PROPERTY_PANEL_BRIGHTNESS_MAX_LEVEL                      0x0004001c

/* Specifies the DSI controllers to use for primary panel. */
#define HFI_PROPERTY_PANEL_CTRL_NUM                                  0x0004001d

/* Specifies the DSI PHYs to use for primary panel. */
#define HFI_PROPERTY_PANEL_PHY_NUM                                   0x0004001e

/* An array that lists the sequence of reset GPIO values and sleeps. */
#define HFI_PROPERTY_PANEL_RESET_SEQUENCE                            0x0004001f

/* Specifies the implementation of backlight control for this panel. */
#define HFI_PROPERTY_PANEL_BL_PMIC_CONTROL_TYPE                      0x00040020

/* Specifies the implementation of backlight control for secondary panel. */
#define HFI_PROPERTY_PANEL_SEC_BL_PMIC_CONTROL_TYPE                  0x00040021

/* Specifies whether to invert the display brightness value. */
#define HFI_PROPERTY_PANEL_BL_INVERTED_DBV                           0x00040022

/* Sets DCS command buffer data for different types of DCS commands. */
#define HFI_PROPERTY_PANEL_DCS_CMD_INFO                              0x00040024

/* Provides iova of DPU Mapped buffer of HW understandable panel commands. */
#define HFI_PROPERTY_PANEL_DPU_ADDRESS                              0x00040025

/* Provides dva of DCP Mapped buffer of structures of cmd descriptions. */
#define HFI_PROPERTY_PANEL_DCP_ADDRESS                              0x00040026

/* Provides D-PHY timing parameters. */
#define HFI_PROPERTY_PANEL_DPHY_TIMINGS                              0x00040027

/* Entry to select vsync source from GPIO's/Watchdog timer. */
#define HFI_PROPERTY_PANEL_VSYNC_SOURCE                              0x00040028

/* Selects the pixel-stream trigger for command mode. */
#define HFI_PROPERTY_PANEL_STREAM_TRIGGER                            0x00040029

/* Specifies the TE Path for command mode panels. */
#define HFI_PROPERTY_PANEL_TE_MODE                                   0x0004002a

/* Reports whether the panel uses C-PHY. */
#define HFI_PROPERTY_PANEL_CPHY_MODE                                 0x0004002b

/* Provides the shared DCP address for DCS command DMA. */
#define HFI_PROPERTY_PANEL_DCS_CMD_TX_BUF_DVA                        0x0004002c

/* Provides the shared DPU address for DCS command DMA. */
#define HFI_PROPERTY_PANEL_DCS_CMD_TX_BUF_IOVA                       0x0004002d

/* Sets the compression rate control override value for the panel. */
#define HFI_PROPERTY_PANEL_COMPRESSION_RC_OVERRIDE                   0x0004002e

/* Selects the line that triggers DSI command DMA. */
#define HFI_PROPERTY_PANEL_DMA_SCHEDULE_LINE                         0x0004002f

/* Provides the panel ESD config data required for ESD status check to the DCP. */
#define HFI_PROPERTY_PANEL_ESD_CONFIG                                0x00040030

/* Specifies the QSYNC parameters of the panel. */
#define HFI_PROPERTY_PANEL_QSYNC_PARAMS                              0x00040031

/* Provides esync properties for VHM panel. */
#define HFI_PROPERTY_PANEL_ESYNC_CAPS				     0x00040032

/* Specifies the DFPS capabilities supported by the panel. */
#define HFI_PROPERTY_PANEL_DFPS_CAPS                                 0x00040033

/* Specifies the frequency stepping pattern of the panel. */
#define HFI_PROPERTY_PANEL_FREQ_PATTERN				     0x00040034

/* Specifies whether panel requires LP11_INIT mode. */
#define HFI_PROPERTY_PANEL_LP11_INIT                                 0x00040035

/* Specifies the width of the DMA scheduling window, expressed in number of display lines. */
#define HFI_PROPERTY_PANEL_DMA_SCHEDULE_WINDOW                       0x00040036

/* Provides DSI bit clock frequencies for dynamic refresh. */
#define HFI_PROPERTY_PANEL_DYN_REF_CLK_FREQS                         0x00040038

/* Reports supported operating-mode switches. */
#define HFI_PROPERTY_PANEL_OPERATING_SWITCH_CAPABILITY               0x00040039

/* Selects the display type and priority. */
#define HFI_PROPERTY_PANEL_DISPLAY_TYPE                              0x0004003a

/* Specifies whether ULPS (Ultra Low Power State) is supported by the panel. */
#define HFI_PROPERTY_PANEL_ULPS_SUPPORTED                            0x0004003b

/* Specifies the DSI custom command set start index and count. */
#define HFI_PROPERTY_PANEL_DSI_CUSTOM_DCS_CMDS_SET_INFO              0x00040040

/* Provides per-mode esync timing capabilities for VHM panel. */
#define HFI_PROPERTY_PANEL_ESYNC_TIMING_CAPS                         0x00040041

/* Provides per-mode qsync timing parameters for panels supporting QSYNC/AVR. */
#define HFI_PROPERTY_PANEL_QSYNC_TIMING_PARAMS                       0x00040042

/* Specifies the current display format being used by the panel. */
#define HFI_PROPERTY_PANEL_COLOR_FORMAT                              0x00040043

/* End of panel-property range. */
#define HFI_PROPERTY_PANEL_END                                       0x0004ffff

#endif /* __H_HFI_PROPERTIES_PANEL_H__ */
