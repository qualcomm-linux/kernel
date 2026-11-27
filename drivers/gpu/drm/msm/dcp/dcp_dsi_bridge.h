/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP DSI bridge state and panel configuration.
 * DCP firmware owns the DSI PHY and controller; the bridge submits HFI commands.
 */

#ifndef __DCP_DSI_BRIDGE_H__
#define __DCP_DSI_BRIDGE_H__

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>

#include <drm/drm_bridge.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>

#include "dcp_dsi_panel.h"

struct dcp_dpu_kms;
struct device_node;

struct dcp_dsi_bridge {
	struct drm_bridge    bridge;
	struct device        dsi_host_dev;
	struct mipi_dsi_host  dsi_host;
	struct mipi_dsi_device *dsi_device;
	struct drm_bridge *panel_bridge;
	bool component_added;

	struct dcp_dpu_kms  *kms;
	struct device_node  *dcp_np;
	u32                  dcp_port;
	u32                  intf_idx;
	u32                  fw_display_id;
	u32                  session_id;
	bool                 session_open;
	/* Protects session_id and session_open. */
	struct mutex         session_lock;

	struct dcp_dsi_hfi_panel_config hfi_panel_config;
	/* Serializes DCS transfers. */
	struct mutex dcs_lock;

	void       *dcs_tx_buf;
	dma_addr_t  dcs_tx_iova;
	void       *dcs_rx_buf;
	dma_addr_t  dcs_rx_iova;
	bool         dcs_mapped;
	struct work_struct power_control_work;
	struct completion  power_done;
	u32                power_packet_id;
};

#define to_dcp_dsi_host(h) \
	container_of(h, struct dcp_dsi_bridge, dsi_host)

#define to_dcp_dsi_bridge(b) \
	container_of(b, struct dcp_dsi_bridge, bridge)

struct drm_bridge *dcp_dsi_bridge_init(struct dcp_dpu_kms *kms,
				       struct device_node *np,
				       struct device_node *dcp_np,
				       u32 dcp_port, u32 intf_idx,
				       u32 fw_display_id);
int dcp_dsi_bridge_map_buffers(struct dcp_dsi_bridge *dsi_bridge);
void dcp_dsi_bridge_unmap_buffers(struct dcp_dsi_bridge *dsi_bridge);
int dcp_dsi_bridge_prepare_session(struct dcp_dsi_bridge *dsi_bridge,
				   const struct drm_display_mode *mode);
void dcp_dsi_bridge_cleanup(struct dcp_dsi_bridge *dsi_bridge);

#endif /* __DCP_DSI_BRIDGE_H__ */
