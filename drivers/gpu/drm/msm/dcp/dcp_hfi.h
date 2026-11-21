/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP HFI (Hardware Firmware Interface) protocol layer.
 */

#ifndef __DCP_HFI_H__
#define __DCP_HFI_H__

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/virtio.h>
#include <linux/workqueue.h>
#include <linux/xarray.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>

#include "hfi/hfi_packet.h"
#include "hfi/hfi_pack_unpack_common.h"
#include "hfi/hfi_packer.h"
#include "hfi/hfi_unpacker.h"
#include "hfi/hfi_commands_device.h"
#include "hfi/hfi_commands_display.h"
#include "hfi/hfi_commands_panel.h"
#include "hfi/hfi_commands_display_events.h"
#include "hfi/hfi_defs_common.h"
#include "hfi/hfi_defs_display.h"
#include "hfi/hfi_defs_device.h"
#include "hfi/hfi_defs_panel.h"
#include "hfi/hfi_properties_device.h"
#include "hfi/hfi_properties_display.h"
#include "hfi/hfi_properties_panel.h"

struct dcp_drv;

/* Queue 0 carries commands; queue 1 carries responses and events. */
#define DCP_VQ_CMD	0	/* TX: all commands */
#define DCP_VQ_RESP	1	/* RX: all responses and async events */
#define DCP_NUM_VQS	2

#define DCP_MAX_DISPLAYS	4
#define DCP_HFI_CAPS_BUF_SIZE	4096

#define DCP_HFI_RESOURCE_VOTE_BUF_SIZE	256

/* Wire-format resource vote entry. */
struct dcp_resource_vote_pair {
	u32 key;
	u64 value;
};

struct dcp_resource_vote_wire {
	__le32 key;
	__le64 value;
} __packed;

struct dcp_hfi_caps_cache {
	struct completion device_done;
	struct completion display_done;

	u8 device_buf[DCP_HFI_CAPS_BUF_SIZE];
	u32 device_len;
	u8 display_buf[DCP_HFI_CAPS_BUF_SIZE];
	u32 display_len;
	u8 vig_buf[DCP_HFI_CAPS_BUF_SIZE];
	u32 vig_len;
	u8 dma_buf[DCP_HFI_CAPS_BUF_SIZE];
	u32 dma_len;
	u8 vig_r1_buf[DCP_HFI_CAPS_BUF_SIZE];
	u32 vig_r1_len;
	u8 dma_r1_buf[DCP_HFI_CAPS_BUF_SIZE];
	u32 dma_r1_len;
	u32 pipe_caps_received;
};

#define DCP_MAX_SSPP_COUNT	16
#define DCP_MAX_VIG_PIPES	DCP_MAX_SSPP_COUNT
#define DCP_MAX_DMA_PIPES	DCP_MAX_SSPP_COUNT

#define DCP_MAX_BLOCKS		8

/* Pipe indices encode the rectangle ID in bits 31:24. */
#define DCP_REC_ID(x)		(((x) & 0xff000000) >> 24)

/* Capabilities parsed from DEVICE_INIT responses. */
struct dcp_caps {
	u32 dcp_hw_rev;
	u32 hw_rev;
	u32 fw_rev;

	u32 num_vig_pipes;
	u32 vig_pipe_ids[DCP_MAX_SSPP_COUNT];
	u32 virt_vig_count;
	u32 vig_r1_indices[DCP_MAX_SSPP_COUNT];

	u32 num_dma_pipes;
	u32 dma_pipe_ids[DCP_MAX_SSPP_COUNT];
	u32 virt_dma_count;
	u32 dma_r1_indices[DCP_MAX_SSPP_COUNT];

	/* Number of displays reported by firmware. */
	u32 num_displays;

	u32 dsi_count;
	u32 dsi_indices[DCP_MAX_BLOCKS];

	/* Maximum dimensions reported for DSI display modes. */
	u32 max_width;
	u32 max_height;

	u32 max_planes;
};

struct dcp_hfi_buf {
	void *data;
	dma_addr_t dma_addr;
	size_t size;
	struct scatterlist sg;
	struct dcp_hfi_completion *comp;
	struct list_head node;
};

struct dcp_hfi_completion {
	struct completion done;
	int status;
	void *resp_data;
	size_t resp_len;
	u32 packet_id;
};

struct dcp_hfi {
	struct device *dev;
	struct dcp_drv *drv;

	struct device *dma_dev;

	struct virtqueue *cmd_vq;
	struct virtqueue *resp_vq;

	struct dcp_hfi_buf *cmd_pool;
	struct list_head cmd_free_list;
	/* Protects cmd_free_list and command buffer ownership. */
	spinlock_t cmd_lock;

	struct dcp_hfi_buf *resp_pool;
	/* Protects response queue draining and power callback publication. */
	spinlock_t resp_drain_lock;
	struct list_head resp_repost_list;
	struct delayed_work resp_repost_work;
	struct xarray pending_xa;
	/* Protects pending_xa and completion publication. */
	spinlock_t pending_lock;
	/* Allocates non-zero packet IDs for HFI transactions. */
	atomic_t packet_id;
	/* Blocks new work once transport teardown begins. */
	atomic_t shutdown_in_progress;
	struct work_struct resource_vote_work;
	/* Protects resource_vote_buf, resource_vote_len, and callback state. */
	spinlock_t resource_vote_lock;
	u8 resource_vote_buf[DCP_HFI_RESOURCE_VOTE_BUF_SIZE];
	u32 resource_vote_len;
	u32 resource_vote_generation;
	void (*resource_vote_cb)(struct dcp_drv *drv,
				 const struct dcp_resource_vote_pair *pairs,
				 u32 count);
	/* Protects capability payload buffers during event delivery. */
	spinlock_t caps_lock;
	/* Serializes capability transactions. */
	struct mutex caps_cache_lock;
	struct dcp_hfi_caps_cache caps_cache;

	void *power_control_priv;
	void (*power_control_cb)(void *priv, u32 display_id,
				 u32 power_mask, bool enable, u32 packet_id);
};

int dcp_hfi_init(struct dcp_drv *drv, struct virtio_device *vdev,
		 struct virtqueue *cmd_vq, struct virtqueue *resp_vq,
		 struct device *dma_dev);
void dcp_hfi_stop(struct dcp_hfi *hfi);
void dcp_hfi_quiesce_queues(struct dcp_hfi *hfi);
void dcp_hfi_deinit(struct dcp_hfi *hfi);

void dcp_hfi_resp_callback(struct virtqueue *vq);
void dcp_hfi_cmd_callback(struct virtqueue *vq);

int dcp_hfi_send_sync(struct dcp_hfi *hfi, u32 cmd,
		      enum hfi_cmd_buff_type cmd_type, u32 object_id,
		      enum hfi_packet_payload_type payload_type, void *payload,
		      u32 payload_size, u32 tx_flags, void *resp_data,
		      size_t resp_len, unsigned int timeout_ms);

int dcp_hfi_send_no_wait(struct dcp_hfi *hfi, u32 cmd,
			 enum hfi_cmd_buff_type cmd_type, u32 object_id,
			 enum hfi_packet_payload_type payload_type, void *payload,
			 u32 payload_size);

int dcp_hfi_send_no_wait_flags(struct dcp_hfi *hfi, u32 cmd,
			       enum hfi_cmd_buff_type cmd_type, u32 object_id,
			       enum hfi_packet_payload_type payload_type,
			       void *payload, u32 payload_size, u32 tx_flags);
int dcp_hfi_send_no_wait_atomic_flags(struct dcp_hfi *hfi, u32 cmd,
				      enum hfi_cmd_buff_type cmd_type, u32 object_id,
				      enum hfi_packet_payload_type payload_type,
				      void *payload, u32 payload_size, u32 tx_flags);
int dcp_hfi_display_event_register(struct dcp_hfi *hfi, u32 session_id,
				   u32 event_id);
int dcp_hfi_display_event_deregister(struct dcp_hfi *hfi, u32 session_id,
				     u32 event_id);
int dcp_hfi_display_event_register_async(struct dcp_hfi *hfi, u32 session_id,
					 u32 event_id);
int dcp_hfi_display_event_deregister_async(struct dcp_hfi *hfi,
					   u32 session_id, u32 event_id);
int dcp_hfi_crtc_set_resource(struct dcp_hfi *hfi, u32 session_id,
			      u32 lm_idx, u32 width, u32 height,
			      u32 roi_x, u32 roi_y);
int dcp_hfi_crtc_set_dest_roi(struct dcp_hfi *hfi, u32 session_id,
			      u32 x, u32 y, u32 width, u32 height);
int dcp_hfi_crtc_set_scan_sequence_id(struct dcp_hfi *hfi, u32 session_id,
				      u32 seq_id);
int dcp_hfi_crtc_set_power_mode(struct dcp_hfi *hfi, u32 session_id,
				enum hfi_display_power_mode mode);
int dcp_hfi_crtc_set_autorefresh(struct dcp_hfi *hfi, u32 session_id,
				 bool enable, u32 frame_count);
int dcp_hfi_crtc_set_bw(struct dcp_hfi *hfi, u32 session_id,
			u64 dram_ib, u64 dram_ab, u64 llcc_ib,
			u64 llcc_ab, u64 core_clk);
int dcp_hfi_submit_prop_pkt(struct dcp_hfi *hfi, const void *pkt,
			    size_t pkt_size);
int dcp_hfi_plane_disable(struct dcp_hfi *hfi, u32 session_id, u32 pipe_idx);
int dcp_hfi_display_commit(struct dcp_hfi *hfi, u32 session_id);
int dcp_hfi_send_device_init(struct dcp_hfi *hfi);
int dcp_hfi_get_capabilities(struct dcp_hfi *hfi, struct dcp_caps *caps);
int dcp_hfi_send_resource_register(struct dcp_hfi *hfi);
int dcp_hfi_set_display_property(struct dcp_hfi *hfi, u32 session_id,
				 u32 prop_id, u32 value);

struct dcp_dsi_hfi_panel_config;
int dcp_hfi_panel_init(struct dcp_hfi *hfi, u32 intf_idx,
		       const struct drm_display_mode *mode,
		       const struct mipi_dsi_device *dsi,
		       const struct dcp_dsi_hfi_panel_config *cfg);
int dcp_hfi_display_enable(struct dcp_hfi *hfi, u32 intf_idx,
			   u32 display_id, u32 *session_id);
int dcp_hfi_display_close(struct dcp_hfi *hfi, u32 session_id);
int dcp_hfi_display_mode_validate(struct dcp_hfi *hfi, u32 session_id,
				  const struct drm_display_mode *mode,
				  u64 clk_rate_hz);
int dcp_hfi_display_set_mode(struct dcp_hfi *hfi, u32 session_id,
			     const struct drm_display_mode *mode,
			     u64 clk_rate_hz);
int dcp_hfi_display_power_register(struct dcp_hfi *hfi, u32 session_id);
int dcp_hfi_display_power_control_ack(struct dcp_hfi *hfi, u32 session_id,
				      u32 packet_id);
int dcp_hfi_display_post_enable(struct dcp_hfi *hfi, u32 session_id);
int dcp_hfi_display_disable(struct dcp_hfi *hfi, u32 session_id);
int dcp_hfi_display_post_disable(struct dcp_hfi *hfi, u32 session_id);
ssize_t dcp_hfi_dcs_transfer(struct dcp_hfi *hfi, u32 session_id,
			     const struct mipi_dsi_msg *msg,
			     void *tx_buf, dma_addr_t tx_iova,
			     void *rx_buf, dma_addr_t rx_iova,
			     u32 post_wait_ms, u32 ctrl_idx);
#endif /* __DCP_HFI_H__ */
void dcp_hfi_set_power_control_cb(struct dcp_hfi *hfi, void *priv,
				  void (*cb)(void *priv, u32 display_id,
					     u32 power_mask, bool enable,
					     u32 packet_id));
void dcp_hfi_clear_power_control_cb(struct dcp_hfi *hfi, void *priv);
