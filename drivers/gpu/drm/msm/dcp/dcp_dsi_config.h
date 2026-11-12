/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP DSI output configuration shared by the HFI implementation and the
 * DRM DSI bridge.
 */

#ifndef __DCP_DSI_CONFIG_H__
#define __DCP_DSI_CONFIG_H__

#include <linux/dma-mapping.h>
#include <linux/sizes.h>
#include <linux/types.h>

struct drm_dsc_config;

#define DCP_DSI_MAX_PHY_TIMINGS	32

/**
 * struct dcp_dsi_hfi_config - DSI configuration passed to HFI.
 *
 * This contains the DSI output configuration plus mode-dependent values
 * assembled before the HFI configuration commands are sent.
 */
struct dcp_dsi_hfi_config {
	u32 display_type;
	u32 num_ctrls;
	u32 ctrl_indices[4];
	u32 num_phys;
	u32 phy_indices[4];
	u32 vsync_source;
	u32 te_dcs_command;
	u32 dma_trigger;
	u32 stream_trigger;
	u32 wr_mem_start;
	u32 wr_mem_continue;
	u32 dma_sched_line;
	u32 dma_sched_window;
	u32 phy_type;
	bool cphy_mode;
	bool lp11_init;
	bool ulps_supported;
	bool bllp_eof_power_mode;
	bool bllp_power_mode;
	u32 lane_map;
	u32 color_order;
	u32 esd_mode;
	u32 jitter_numer;
	u32 jitter_denom;
	dma_addr_t dcs_tx_dva;
	dma_addr_t dcs_tx_iova;
	u32 dcs_tx_buf_size;
	u32 phy_timings[DCP_DSI_MAX_PHY_TIMINGS];
	u32 num_phy_timings;
	const struct drm_dsc_config *dsc;
	bool dsc_rc_override; /* Send the DRM DSC rate-control ranges to HFI. */
	u32 topology_mixer_count;
	u32 topology_enc_count;
	u32 topology_intf_count;
};

/* DCS buffers use IOVAs directly in the firmware command descriptor. */
#define DCP_DSI_DCS_TX_BUF_SIZE	SZ_4K
#define DCP_DSI_DCS_RX_BUF_SIZE	SZ_4K

#endif /* __DCP_DSI_CONFIG_H__ */
