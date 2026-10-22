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

#include "hfi/hfi_packet.h"
#include "hfi/hfi_pack_unpack_common.h"
#include "hfi/hfi_packer.h"
#include "hfi/hfi_unpacker.h"
#include "hfi/hfi_defs_common.h"

struct dcp_drv;

/* Queue 0 carries commands; queue 1 carries responses and events. */
#define DCP_VQ_CMD	0	/* TX: all commands */
#define DCP_VQ_RESP	1	/* RX: all responses and async events */
#define DCP_NUM_VQS	2

#define DCP_MAX_DISPLAYS	4
#define DCP_HFI_CAPS_BUF_SIZE	4096

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
#endif /* __DCP_HFI_H__ */
