/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_PROPERTIES_DISPLAY_H__
#define __H_HFI_PROPERTIES_DISPLAY_H__

/* Display-property range. */
#define HFI_PROPERTY_DISPLAY_BEGIN                                   0x00020000

/* Whether DIM layer is supported. */
#define HFI_PROPERTY_DISPLAY_HAS_DIM_LAYER                           0x00020001

/* Whether Idle PC is supported. */
#define HFI_PROPERTY_DISPLAY_HAS_IDLE_PC                             0x00020002

/* Whether LM Noise is supported. */
#define HFI_PROPERTY_DISPLAY_LM_NOISE                                0x00020003

/* Noise layer version. */
#define HFI_PROPERTY_DISPLAY_NOISE_VERSION                           0x00020004

/* Whether dedicated CWB is supported. */
#define HFI_PROPERTY_DISPLAY_FEATURE_DEDICATED_CWB                   0x00020005

/* Whether CWB feature is supported. */
#define HFI_PROPERTY_DISPLAY_FEATURE_CWB                             0x00020006

/* Whether system cache is used for static display traffic. */
#define HFI_PROPERTY_DISPLAY_SYS_CACHE_DISP                          0x00020007

/* Whether trusted-display mode is supported. */
#define HFI_PROPERTY_DISPLAY_FEATURE_TRUSTED_DISPLAY_MODE            0x00020008

/* Whether UBWC Statistics is supported. */
#define HFI_PROPERTY_DISPLAY_UBWC_STATS                              0x00020009

/* Whether dynamic clock control is supported. */
#define HFI_PROPERTY_DISPLAY_DYN_CLK_SUPPORT                         0x0002000a

/* Whether qsync mode is supported. */
#define HFI_PROPERTY_DISPLAY_QSYNC                                   0x0002000b

/* Whether AVR Step is supported. */
#define HFI_PROPERTY_DISPLAY_AVR_STEP                                0x0002000c

/* Whether CWB Dither is supported. */
#define HFI_PROPERTY_DISPLAY_WB_CWB_DITHER                           0x0002000d

/* Whether system cache is used for writeback. */
#define HFI_PROPERTY_DISPLAY_WB_SYS_CACHE_DISP                       0x0002000e

/* Whether writeback supports linear rotation. */
#define HFI_PROPERTY_DISPLAY_WB_LINEAR_ROTATION                      0x0002000f

/* The extended power modes supported by the Display. */
#define HFI_PROPERTY_DISPLAY_POWER_MODE                              0x00020010

/* The DPU DRAM IB vote for DRAM(LLCC(SDE)->DDR) in Kbps. */
#define HFI_PROPERTY_DISPLAY_DRAM_IB                                 0x00020011

/* The DPU DRAM AB vote for DRAM(LLCC(SDE)->DDR) in Kbps. */
#define HFI_PROPERTY_DISPLAY_DRAM_AB                                 0x00020012

/* The DPU LLCC IB vote for LLCC(SDE_master_on_MNOC->LLCC). */
#define HFI_PROPERTY_DISPLAY_LLCC_IB                                 0x00020013

/* The DPU LLCC IB vote for LLCC(SDE_master_on_MNOC->LLCC). */
#define HFI_PROPERTY_DISPLAY_LLCC_AB                                 0x00020014

/* The DPU IB vote for MNOC(virt_master-> virt_slave(NOC)). */
#define HFI_PROPERTY_DISPLAY_CORE_IB                                 0x00020015

/* The DPU AB vote for MNOC(virt_master-> virt_slave(NOC)). */
#define HFI_PROPERTY_DISPLAY_CORE_AB                                 0x00020016

/* The Mdp core clock in hertz. */
#define HFI_PROPERTY_DISPLAY_CORE_CLK                                0x00020017

/* Associates a layer before its properties are programmed. */
#define HFI_PROPERTY_DISPLAY_ATTACH_LAYER                            0x00020018

/* Removes a layer from the display. */
#define HFI_PROPERTY_DISPLAY_DETACH_LAYER                            0x00020019

/* Commit sequence returned with frame-scan events. */
#define HFI_PROPERTY_DISPLAY_SCAN_SEQUENCE_ID                        0x0002001a

/* Enables or disables interface auto-refresh. */
#define HFI_PROPERTY_DISPLAY_AUTOREFRESH_CFG                         0x0002001b

/* Configure the HFI queue. */
#define HFI_PROPERTY_DISPLAY_LSR_WB_HFI_CONFIG                       0x0002001c

/* Reports scratch-memory size required by LSR writeback. */
#define HFI_PROPERTY_DISPLAY_LSR_WB_NEEDS_SCRATCH_MEM            0x0002001d

/* Configure the scratch memory. */
#define HFI_PROPERTY_DISPLAY_SET_SCRATCH_MEM                         0x0002001e

/* Configure the sync display. */
#define HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_SYNC_TO                   0x0002001f

/* Configure the LSR. */
#define HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_MATRIX                0x00020020

/* Configure. */
#define HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT                0x00020021

/* Configure. */
#define HFI_PROPERTY_DISPLAY_INPUT_FENCE                             0x00020022

/* Provide a reusable fence. */
#define HFI_PROPERTY_DISPLAY_REUSABLE_FENCE                          0x00020023

/* Configure the HFI queue. */
#define HFI_PROPERTY_DISPLAY_LSR_WB_CVP_BUFF                       0x00020025

/* Configure the output buffers. */
#define HFI_PROPERTY_DISPLAY_LSR_WB_OUT_BUFFERS                       0x00020026

/* Configure the LSR. */
#define HFI_PROPERTY_DISPLAY_LSR_MAPPED_HFI_BASE_ADDR                  0x00020027

/* Configure the shared memory. */
#define HFI_PROPERTY_DISPLAY_HRP_HFI_CONFIG                            0x00020028

/* Attach output layer before. */
#define HFI_PROPERTY_DISPLAY_ATTACH_OUTPUT_LAYER                     0x00020029

/* Removes an output layer from the display. */
#define HFI_PROPERTY_DISPLAY_DETACH_OUTPUT_LAYER                     0x0002002a

/* Set destination ROI for display. */
#define HFI_PROPERTY_DISPLAY_DEST_ROI                                0x0002002b

/* Configures output fence. */
#define HFI_PROPERTY_DISPLAY_OUTPUT_FENCE                            0x0002002c

/* The dim layer parameters for display output. */
#define HFI_PROPERTY_DISPLAY_DIM_LAYER                               0x0002002d

/* The dim layer parameters for display output. */
#define HFI_PROPERTY_DISPLAY_MISR_CONFIG                             0x0002002e

/* QSYNC mode for video/command mode panels. */
#define HFI_PROPERTY_DISPLAY_QSYNC_MODE                              0x0002002f

/* Configure resource output. */
#define HFI_PROPERTY_DISPLAY_SET_RESOURCE_DATA                       0x00020030

/* Set VRR (Variable Refresh Rate). */
#define HFI_PROPERTY_DISPLAY_VRR_FRAME_PARAMS                        0x00020031

/* The Expected Preset Time which is used. */
#define HFI_PROPERTY_DISPLAY_EPT                                     0x00020032

/* This property sets line pointer offset time in nanoseconds. */
#define HFI_PROPERTY_DISPLAY_VSYNC_OFFSET                            0x00020033

/* Update the panel brightness level. */
#define HFI_PROPERTY_DISPLAY_BRIGHTNESS                              0x00020034

/* Set vsync source to watchdog. */
#define HFI_PROPERTY_DISPLAY_CUSTOM_WD_TE                                   0x00020035

/* Display color-property range. */
#define HFI_PROPERTY_DISPLAY_COLOR_BEGIN                             0x00020100

/* Setup 3D LUT. */
#define HFI_PROPERTY_DISPLAY_COLOR_3D_LUT                            0x00020101

/* Setup IGC. */
#define HFI_PROPERTY_DISPLAY_COLOR_IGC                               0x00020102

/* Setup GC. */
#define HFI_PROPERTY_DISPLAY_COLOR_GC                                0x00020103

/* Setup PCC. */
#define HFI_PROPERTY_DISPLAY_COLOR_PCC                               0x00020104

/* Setup PA HSIC. */
#define HFI_PROPERTY_DISPLAY_COLOR_HSIC                              0x00020105

/* Setup PA MEMCOLOR SKIN. */
#define HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_SKIN                     0x00020106

/* Setup PA MEMCOLOR SKY. */
#define HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_SKY                      0x00020107

/* Setup PA MEMCOLOR FOLIAGE. */
#define HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_FOLIAGE                  0x00020108

/* Setup PA MEMCOLOR PROTECTION. */
#define HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_PROT                     0x00020109

/* Setup PA SIXZONE. */
#define HFI_PROPERTY_DISPLAY_COLOR_SIXZONE                           0x0002010a

/* Setup PA VLUT. */
#define HFI_PROPERTY_DISPLAY_COLOR_VLUT                              0x0002010b

/* Setup PPB DITHER. */
#define HFI_PROPERTY_DISPLAY_COLOR_DITHER                            0x0002010c

/* Setup RC. */
#define HFI_PROPERTY_DISPLAY_COLOR_RC                                0x0002010d

/* Setup SPR_INIT config. */
#define HFI_PROPERTY_DISPLAY_COLOR_SPR_INIT                          0x0002010e

/* Setup SPR_UDC config. */
#define HFI_PROPERTY_DISPLAY_COLOR_SPR_UDC                           0x0002010f

/* Setup DSPP SPR DITHER. */
#define HFI_PROPERTY_DISPLAY_COLOR_SPR_DITHER                        0x00020110

/* Setup DSPP PA DITHER. */
#define HFI_PROPERTY_DISPLAY_COLOR_PA_DITHER                         0x00020111

/* Setup Demura. */
#define HFI_PROPERTY_DISPLAY_COLOR_DEMURA_CFG                        0x00020112

/* Setup Demura tables. */
#define HFI_PROPERTY_DISPLAY_COLOR_DEMURA_TABLE                      0x00020113

/* Setup Demura backlight. */
#define HFI_PROPERTY_DISPLAY_COLOR_DEMURA_BACKLIGHT                  0x00020114

/* Setup destination scaler. */
#define HFI_PROPERTY_DISPLAY_DEST_SCALER                             0x00020115

/* Setup QRTC config. */
#define HFI_PROPERTY_DISPLAY_COLOR_QRTC_CONFIG                       0x00020116

/* Setup LTM initialization params. */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_INIT                          0x00020117

/* Setup LTM configuration params. */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_CFG                           0x00020118

/* Setup LTM noise threshold. */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_NOISE_THRESH                  0x00020119

/* Setup LTM histogram control. */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_HIST_CTRL                     0x0002011a

/* Setup LTM VLUT table. */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_VLUT                          0x0002011b

/* Setup LTM initial params. */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_QUEUE_BUF                     0x0002011c

/* Setup LTM initial params. */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_CLEAR_BUFS                    0x0002011d

/* Property to configure RGB hist control. */
#define HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CTRL                   0x0002011e

/* Property to queue RGB histogram. */
#define HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_QUEUE_BUFFER           0x0002011f

/* Property to clear all queued RGB. */
#define HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CLEAR_BUFFERS          0x00020120

/* Setup AI Scaler. */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_AI_SCALER                    0x00020121

/* Setup SSRC configuration. */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_SSRC_CONFIG                  0x00020122

/* Setup SSRC data. */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_SSRC_DATA                    0x00020123

/* Setup MDNIE. */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE                        0x00020124

/* Setup IPC. */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE_IPC                    0x00020125

/* Setup ART. */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE_ART                    0x00020126

/* Setup ABC. */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_ABC                          0x00020127

/* Setup RC for PU. */
#define HFI_PROPERTY_DISPLAY_COLOR_RC_PU                             0x00020128

/* Property to configure PA hist control. */
#define HFI_PROPERTY_DISPLAY_COLOR_PA_HIST_CTRL                      0x00020129

/* Property to queue PA histogram. */
#define HFI_PROPERTY_DISPLAY_COLOR_PA_HIST_QUEUE_BUFFER              0x0002012a

/* Property to clear all queued PA. */
#define HFI_PROPERTY_DISPLAY_COLOR_PA_HIST_CLEAR_BUFFERS             0x0002012b

/* Property to setup SPR partial update. */
#define HFI_PROPERTY_DISPLAY_COLOR_SPR_PU                            0x0002012c

/* Property to setup SPR partial update. */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_COPR                          0x0002012d

/* End of display color properties. */
#define HFI_PROPERTY_DISPLAY_COLOR_END                               0x000201ff

/* End of display-property range. */
#define HFI_PROPERTY_DISPLAY_END                                     0x0002ffff

/* Layer-property range. */
#define HFI_PROPERTY_LAYER_BEGIN                                     0x00030000

/* Whether layer decimation is supported. */
#define HFI_PROPERTY_LAYER_FEATURE_DECIMATION                        0x00030001

/* Blend operations set by Host. */
#define HFI_PROPERTY_LAYER_BLEND_TYPE                                0x00030002

/* Blending alpha of the plane. */
#define HFI_PROPERTY_LAYER_ALPHA                                     0x00030003

/* Z-order of the plane. */
#define HFI_PROPERTY_LAYER_ZPOS                                      0x00030004

/* Source ROI per layer. */
#define HFI_PROPERTY_LAYER_SRC_ROI                                   0x00030005

/* Destination ROI per layer. */
#define HFI_PROPERTY_LAYER_DEST_ROI                                  0x00030006

/* Configures blend roi. */
#define HFI_PROPERTY_LAYER_BLEND_ROI                                 0x00030007

/* Total Image Height. */
#define HFI_PROPERTY_LAYER_SRC_IMG_SIZE_H                            0x00030008

/* Total Image Width. */
#define HFI_PROPERTY_LAYER_SRC_IMG_SIZE_W                            0x00030009

/* SMMU mapped source Image start address. */
#define HFI_PROPERTY_LAYER_SRC_ADDR                                  0x0003000a

/* Source Format Configuration for each layer. */
#define HFI_PROPERTY_LAYER_SRC_FORMAT                                0x0003000b

/* Stride for each layer. */
#define HFI_PROPERTY_LAYER_STRIDE                                    0x0003000c

/* Multi rectangle fetch modes. */
#define HFI_PROPERTY_LAYER_MULTIRECT_MODE                            0x0003000d

/* Blending background alpha of the plane. */
#define HFI_PROPERTY_LAYER_BG_ALPHA                                  0x0003000e

/* Source Region of Interest (ROI). */
#define HFI_PROPERTY_OUTPUT_LAYER_SRC_ROI                            0x0003000f

/* Destination Region of Interest(ROI). */
#define HFI_PROPERTY_OUTPUT_LAYER_DST_ROI                            0x00030010

/* Total image height of output buffer. */
#define HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_H                      0x00030011

/* Total image width of output buffer. */
#define HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_W                      0x00030012

/* SMMU mapped start address. */
#define HFI_PROPERTY_OUTPUT_LAYER_DST_ADDR                            0x00030013

/* Destination Format Configuration of output buffer. */
#define HFI_PROPERTY_OUTPUT_LAYER_DST_FORMAT                         0x00030014

/* Stride for each layer. */
#define HFI_PROPERTY_OUTPUT_LAYER_STRIDE                             0x00030015

/* Rotation for each layer. */
#define HFI_PROPERTY_LAYER_ROTATION                                  0x00030016

/* Security policy settings applied. */
#define HFI_PROPERTY_LAYER_SECURITY_POLICY                           0x00030017

/* The LSR alpha buffer properties of the input layer. */
#define HFI_PROPERTY_LAYER_LSR_IN_A_BUFFER                           0x00030018

/* The LSR Reprojection layer's plane equation. */
#define HFI_PROPERTY_LAYER_LSR_REPROJ_PLANE_EQ                       0x0003001a

/* The layer's gamma type. */
#define HFI_PROPERTY_LAYER_GAMMA                                     0x0003001b

/* The LSR layer's local/remote/other type. */
#define HFI_PROPERTY_LAYER_LSR_LAYER_TYPE                            0x0003001c

/* The LSR layer's lock type. */
#define HFI_PROPERTY_LAYER_LSR_LOCK_TYPE                             0x0003001d

/* Sets the LSR reprojection render pose. */
#define HFI_PROPERTY_LAYER_LSR_RENDER_POSE                           0x0003001e

/* The LSR render frustum properties of the layer. */
#define HFI_PROPERTY_LAYER_LSR_RENDER_FRUSTUM                        0x0003001f

/* Custom buffers for this layer. */
#define HFI_PROPERTY_LAYER_LSR_X_BUFFER                              0x00030020

/* The LSR Reprojection pose properties of the layer. */
#define HFI_PROPERTY_LAYER_LSR_REPROJ_POSE                           0x00030021

/* Source image size of each layer in bytes. */
#define HFI_PROPERTY_LAYER_SRC_IMAGE_SIZE                           0x00030022

/* Rotation for output layer. */
#define HFI_PROPERTY_OUTPUT_LAYER_ROTATION                           0x00030023

/* Sys cache related attributes for each layer. */
#define HFI_PROPERTY_LAYER_CACHE_ATTR                                0x00030024

/* LLCC scid of the system cache to be used. */
#define HFI_PROPERTY_LAYER_LLCC_SCID                                 0x00030025

/* Sets system-cache attributes for the output layer. */
#define HFI_PROPERTY_OUTPUT_LAYER_CACHE_ATTR                         0x00030026

/* LLCC scid of the system cache to be used. */
#define HFI_PROPERTY_OUTPUT_LAYER_LLCC_SCID                          0x00030027

/* The CWB tap point for output layer. */
#define HFI_PROPERTY_OUTPUT_LAYER_CWB_TAP_POINT                      0x00030028

/* Configure downscale blur. */
#define HFI_PROPERTY_OUTPUT_LAYER_DNSC_BLUR_CFG                      0x00030029

/* Source exclusion rectangle region. */
#define HFI_PROPERTY_LAYER_EXCLUSION_RECTANGLE_ROI                   0x0003002a

/* Configure downscale. */
#define HFI_PROPERTY_OUTPUT_LAYER_DNSC_CFG                           0x0003002b

/* The security policy for output layer. */
#define HFI_PROPERTY_OUTPUT_LAYER_SECURITY_POLICY                    0x00030031

/* Sets the disparity-layer injection interval. */
#define HFI_PROPERTY_LAYER_LSR_DISPARITY_PHASE                     0x00030032

/* Layer color-property range. */
#define HFI_PROPERTY_LAYER_COLOR_BEGIN                               0x00030100

/* Setup VIG GAMUT. */
#define HFI_PROPERTY_LAYER_COLOR_3D_LUT                              0x00030101

/* Setup UCSC IGC. */
#define HFI_PROPERTY_LAYER_COLOR_UCSC_IGC                            0x00030102

/* Setup UCSC UNMULT. */
#define HFI_PROPERTY_LAYER_COLOR_UCSC_UNMULT                         0x00030103

/* Setup UCSC GC. */
#define HFI_PROPERTY_LAYER_COLOR_UCSC_GC                             0x00030104

/* Setup UCSC ALPHA DITHER. */
#define HFI_PROPERTY_LAYER_COLOR_UCSC_ALPHA_DITHER                   0x00030105

/* Setup UCSC CSC. */
#define HFI_PROPERTY_LAYER_COLOR_UCSC_CSC                            0x00030106

/* Setup CSC. */
#define HFI_PROPERTY_LAYER_COLOR_CSC                                 0x00030107

/* Setup VIG scaler. */
#define HFI_PROPERTY_LAYER_COLOR_SCALER                              0x00030108

/* End of layer color properties. */
#define HFI_PROPERTY_LAYER_COLOR_END                                 0x000301ff

/* End of layer-property range. */
#define HFI_PROPERTY_LAYER_END                                       0x0003ffff

#endif /* __H_HFI_PROPERTIES_DISPLAY_H__ */
