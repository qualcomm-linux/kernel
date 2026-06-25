/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DAI IDs for the Qualcomm Audio Interface (QAIF) controller.
 * These values are used in devicetree sound-dai references and as
 * the reg value of aif-interface child nodes.
 */
#ifndef __DT_QCOM_QAIF_H
#define __DT_QCOM_QAIF_H

/* qcom,qaif-aif-sync-mode values */
#define QAIF_AIF_SYNC_MODE_SHORT	0	/* Short (pulse) sync */
#define QAIF_AIF_SYNC_MODE_LONG		1	/* Long (level) sync */

/* qcom,qaif-aif-sync-src values */
#define QAIF_AIF_SYNC_SRC_SLAVE		0	/* Sync slave -- clock from external */
#define QAIF_AIF_SYNC_SRC_MASTER	1	/* Sync master -- drive clock/frame */

/* qcom,qaif-aif-lane-config enable values */
#define QAIF_AIF_LANE_DISABLE		0
#define QAIF_AIF_LANE_ENABLE		1

/* qcom,qaif-aif-lane-config direction values */
#define QAIF_AIF_LANE_DIR_TX		0	/* TX (playback, speaker) */
#define QAIF_AIF_LANE_DIR_RX		1	/* RX (capture, mic) */

/*
 * AIF (Unified Audio Interface) DAI IDs -- AIF0 through AIF12.
 * Each AIF supports PCM, TDM and MI2S serial protocols over up to
 * 8 independent data lanes sharing a single bit clock and frame sync.
 */
#define QAIF_MI2S_TDM_AIF0	0
#define QAIF_MI2S_TDM_AIF1	1
#define QAIF_MI2S_TDM_AIF2	2
#define QAIF_MI2S_TDM_AIF3	3
#define QAIF_MI2S_TDM_AIF4	4
#define QAIF_MI2S_TDM_AIF5	5
#define QAIF_MI2S_TDM_AIF6	6
#define QAIF_MI2S_TDM_AIF7	7
#define QAIF_MI2S_TDM_AIF8	8
#define QAIF_MI2S_TDM_AIF9	9
#define QAIF_MI2S_TDM_AIF10	10
#define QAIF_MI2S_TDM_AIF11	11
#define QAIF_MI2S_TDM_AIF12	12

/*
 * CIF (Codec Interface) RX DAI IDs -- playback to internal codec.
 * RDDMA channels fetch audio from memory and drain it to the codec.
 */
#define QAIF_CDC_DMA_RX0	13
#define QAIF_CDC_DMA_RX1	14
#define QAIF_CDC_DMA_RX2	15
#define QAIF_CDC_DMA_RX3	16
#define QAIF_CDC_DMA_RX4	17
#define QAIF_CDC_DMA_RX5	18
#define QAIF_CDC_DMA_RX6	19
#define QAIF_CDC_DMA_RX7	20
#define QAIF_CDC_DMA_RX8	21
#define QAIF_CDC_DMA_RX9	22

/*
 * CIF (Codec Interface) TX DAI IDs -- capture from internal codec.
 * WRDMA channels collect audio from the codec and write it to memory.
 */
#define QAIF_CDC_DMA_TX0	23
#define QAIF_CDC_DMA_TX1	24
#define QAIF_CDC_DMA_TX2	25
#define QAIF_CDC_DMA_TX3	26
#define QAIF_CDC_DMA_TX4	27
#define QAIF_CDC_DMA_TX5	28
#define QAIF_CDC_DMA_TX6	29
#define QAIF_CDC_DMA_TX7	30
#define QAIF_CDC_DMA_TX8	31
#define QAIF_CDC_DMA_TX9	32

/*
 * CIF (Codec Interface) VA TX DAI IDs -- capture from voice activity codec.
 * WRDMA channels collect audio from the VA codec and write it to memory.
 */
#define QAIF_CDC_DMA_VA_TX0	33
#define QAIF_CDC_DMA_VA_TX1	34
#define QAIF_CDC_DMA_VA_TX2	35
#define QAIF_CDC_DMA_VA_TX3	36
#define QAIF_CDC_DMA_VA_TX4	37
#define QAIF_CDC_DMA_VA_TX5	38
#define QAIF_CDC_DMA_VA_TX6	39
#define QAIF_CDC_DMA_VA_TX7	40
#define QAIF_CDC_DMA_VA_TX8	41
#define QAIF_CDC_DMA_VA_TX9	42

#endif /* __DT_QCOM_QAIF_H */
