/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP DSI panel configuration shared by the HFI panel implementation and
 * the DRM DSI bridge.
 */

#ifndef __DCP_DSI_PANEL_H__
#define __DCP_DSI_PANEL_H__

#include <linux/dma-mapping.h>
#include <linux/sizes.h>
#include <linux/types.h>

#define DCP_DSI_MAX_PHY_TIMINGS	32

/**
 * struct dcp_dsi_hfi_panel_config - Complete HFI PANEL_INIT configuration.
 *
 * This contains the bridge-owned panel configuration plus the
 * mode-dependent values assembled before the HFI PANEL_INIT command.
 */
struct dcp_dsi_hfi_panel_config {
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
	bool dsc_enabled;
	u32 dsc_version_major;
	u32 dsc_version_minor;
	u32 dsc_scr_version;
	u32 dsc_slice_height;
	u32 dsc_slice_width;
	u32 dsc_slices_per_pkt;
	u32 dsc_bits_per_component;
	u32 dsc_pps_delay_ms;
	u32 dsc_bits_per_pixel;
	u32 dsc_chroma_format;
	u32 dsc_color_space;
	u32 dsc_block_prediction_enable;
	/* Optional DSC rate-control override panel configuration. */
	bool dsc_rc_override;
	u8 dsc_rc_min_qp[15];
	u8 dsc_rc_max_qp[15];
	u8 dsc_rc_bpg_offsets[15];
	u32 topology_mixer_count;
	u32 topology_enc_count;
	u32 topology_intf_count;
};

/**
 * struct dcp_dsi_panel_state - Mode-dependent DSI panel state.
 *
 * This state is owned by a DRM atomic bridge state. It is combined with
 * the static panel configuration when constructing the HFI PANEL_INIT
 * payload.
 */
struct dcp_dsi_panel_state {
	u32 topology_mixer_count;
	u32 topology_enc_count;
	u32 topology_intf_count;
};

/* DCS buffers use IOVAs directly in the firmware command descriptor. */
#define DCP_DSI_DCS_TX_BUF_SIZE	SZ_4K
#define DCP_DSI_DCS_RX_BUF_SIZE	SZ_4K

#endif /* __DCP_DSI_PANEL_H__ */
