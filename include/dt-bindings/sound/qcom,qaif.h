/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DAI IDs for the Qualcomm Audio Interface (QAIF) controller.
 * QAIF_MI2S_AIF* and QAIF_TDM_AIF* values are used in sound-dai
 * references and as the reg value of aif@N child nodes.
 * QAIF_CDC_DMA_* values are for sound-dai references only and must
 * not be used as aif@N child node reg values.
 */
#ifndef __DT_QCOM_QAIF_H
#define __DT_QCOM_QAIF_H

/*
 * MI2S DAI IDs -- one per physical AIF port in MI2S mode.
 * Each port supports up to 2 channels (stereo I2S) over a single
 * data lane sharing a bit clock and frame sync.
 */
#define QAIF_MI2S_AIF0		200
#define QAIF_MI2S_AIF1		201
#define QAIF_MI2S_AIF2		202
#define QAIF_MI2S_AIF3		203
#define QAIF_MI2S_AIF4		204
#define QAIF_MI2S_AIF5		205
#define QAIF_MI2S_AIF6		206
#define QAIF_MI2S_AIF7		207
#define QAIF_MI2S_AIF8		208
#define QAIF_MI2S_AIF9		209
#define QAIF_MI2S_AIF10		210
#define QAIF_MI2S_AIF11		211
#define QAIF_MI2S_AIF12		212

/*
 * TDM DAI IDs -- one per physical AIF port in TDM mode.
 * Each port supports up to 8 channels over up to 8 independent data
 * lanes sharing a single bit clock and frame sync.
 */
#define QAIF_TDM_AIF0		213
#define QAIF_TDM_AIF1		214
#define QAIF_TDM_AIF2		215
#define QAIF_TDM_AIF3		216
#define QAIF_TDM_AIF4		217
#define QAIF_TDM_AIF5		218
#define QAIF_TDM_AIF6		219
#define QAIF_TDM_AIF7		220
#define QAIF_TDM_AIF8		221
#define QAIF_TDM_AIF9		222
#define QAIF_TDM_AIF10		223
#define QAIF_TDM_AIF11		224
#define QAIF_TDM_AIF12		225

/*
 * CIF (Codec Interface) RX DAI IDs -- playback to internal codec.
 * RDDMA channels fetch audio from memory and drain it to the codec.
 */
#define QAIF_CDC_DMA_RX0	226
#define QAIF_CDC_DMA_RX1	227
#define QAIF_CDC_DMA_RX2	228
#define QAIF_CDC_DMA_RX3	229
#define QAIF_CDC_DMA_RX4	230
#define QAIF_CDC_DMA_RX5	231
#define QAIF_CDC_DMA_RX6	232
#define QAIF_CDC_DMA_RX7	233
#define QAIF_CDC_DMA_RX8	234
#define QAIF_CDC_DMA_RX9	235

/*
 * CIF (Codec Interface) TX DAI IDs -- capture from internal codec.
 * WRDMA channels collect audio from the codec and write it to memory.
 */
#define QAIF_CDC_DMA_TX0	236
#define QAIF_CDC_DMA_TX1	237
#define QAIF_CDC_DMA_TX2	238
#define QAIF_CDC_DMA_TX3	239
#define QAIF_CDC_DMA_TX4	240
#define QAIF_CDC_DMA_TX5	241
#define QAIF_CDC_DMA_TX6	242
#define QAIF_CDC_DMA_TX7	243
#define QAIF_CDC_DMA_TX8	244
#define QAIF_CDC_DMA_TX9	245

/*
 * CIF (Codec Interface) VA TX DAI IDs -- capture from voice activity codec.
 * WRDMA channels collect audio from the VA codec and write it to memory.
 */
#define QAIF_CDC_DMA_VA_TX0	246
#define QAIF_CDC_DMA_VA_TX1	247
#define QAIF_CDC_DMA_VA_TX2	248
#define QAIF_CDC_DMA_VA_TX3	249
#define QAIF_CDC_DMA_VA_TX4	250
#define QAIF_CDC_DMA_VA_TX5	251
#define QAIF_CDC_DMA_VA_TX6	252
#define QAIF_CDC_DMA_VA_TX7	253
#define QAIF_CDC_DMA_VA_TX8	254
#define QAIF_CDC_DMA_VA_TX9	255

#endif /* __DT_QCOM_QAIF_H */
