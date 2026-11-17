// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP HFI core transport.
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/unaligned.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <linux/workqueue.h>

#include "dcp_hfi.h"
#include "dcp_hfi_priv.h"
#include "dcp_drv.h"
#include "dcp_crtc.h"
#include "hfi/hfi_packet.h"
#include "hfi/hfi_packer.h"
#include "hfi/hfi_unpacker.h"
#include "hfi/hfi_commands_device.h"
#include "hfi/hfi_commands_display.h"
#include "hfi/hfi_commands_display_events.h"
#include "hfi/hfi_defs_common.h"
#include "hfi/hfi_defs_display.h"

static void dcp_hfi_resource_vote_work(struct work_struct *work);
static int dcp_hfi_prepost_resp_bufs(struct dcp_hfi *hfi);

u32 dcp_hfi_next_packet_id(struct dcp_hfi *hfi)
{
	u32 old, next;

	do {
		old = (u32)atomic_read(&hfi->packet_id);
		next = old + 1;
		if (!next)
			next = 1;
	} while ((u32)atomic_cmpxchg(&hfi->packet_id, old, next) != old);

	return next;
}

#define DCP_HFI_BUF_ALLOC_RETRIES	3	/* Bound command-path stalls. */
#define DCP_HFI_BUF_ALLOC_SLEEP_US_MIN	30000
#define DCP_HFI_BUF_ALLOC_SLEEP_US_MAX	40000

static struct dcp_hfi_buf *_dcp_hfi_buf_alloc(struct dcp_hfi *hfi,
					      struct list_head *free_list,
					      spinlock_t *lock,
					      const char *pool_name)
{
	struct dcp_hfi_buf *buf = NULL;
	unsigned long flags;
	int retry;

	if (atomic_read(&hfi->shutdown_in_progress)) {
		dev_dbg(hfi->dev,
			"HFI shutdown in progress — refusing %s buffer allocation\n",
			pool_name);
		return NULL;
	}

	for (retry = 0; retry <= DCP_HFI_BUF_ALLOC_RETRIES; retry++) {
		if (atomic_read(&hfi->shutdown_in_progress))
			return NULL;

		spin_lock_irqsave(lock, flags);
		if (!list_empty(free_list)) {
			buf = list_first_entry(free_list,
					       struct dcp_hfi_buf, node);
			list_del(&buf->node);
		}
		spin_unlock_irqrestore(lock, flags);

		if (buf)
			return buf;

		if (retry < DCP_HFI_BUF_ALLOC_RETRIES)
			usleep_range(DCP_HFI_BUF_ALLOC_SLEEP_US_MIN,
				     DCP_HFI_BUF_ALLOC_SLEEP_US_MAX);
	}

	dev_warn_ratelimited(hfi->dev,
			     "HFI %s TX pool exhausted after %d retries — command dropped\n",
			     pool_name, DCP_HFI_BUF_ALLOC_RETRIES);
	return NULL;
}

static void _dcp_hfi_buf_free_locked(struct dcp_hfi_buf *buf,
				     struct list_head *free_list)
{
	buf->comp = NULL;
	list_add_tail(&buf->node, free_list);
}

struct dcp_hfi_buf *dcp_hfi_buf_alloc(struct dcp_hfi *hfi)
{
	return _dcp_hfi_buf_alloc(hfi, &hfi->cmd_free_list,
				   &hfi->cmd_lock, "cmd");
}

void dcp_hfi_buf_free(struct dcp_hfi *hfi, struct dcp_hfi_buf *buf)
{
	unsigned long flags;

	spin_lock_irqsave(&hfi->cmd_lock, flags);
	_dcp_hfi_buf_free_locked(buf, &hfi->cmd_free_list);
	spin_unlock_irqrestore(&hfi->cmd_lock, flags);
}

struct dcp_hfi_buf *dcp_hfi_buf_alloc_atomic(struct dcp_hfi *hfi)
{
	struct dcp_hfi_buf *buf = NULL;
	unsigned long flags;

	if (atomic_read(&hfi->shutdown_in_progress))
		return NULL;

	spin_lock_irqsave(&hfi->cmd_lock, flags);
	if (!list_empty(&hfi->cmd_free_list)) {
		buf = list_first_entry(&hfi->cmd_free_list,
				       struct dcp_hfi_buf, node);
		list_del(&buf->node);
	}
	spin_unlock_irqrestore(&hfi->cmd_lock, flags);

	return buf;
}

/* Multi-packet headers register each response completion before submission. */
int dcp_hfi_pending_add(struct dcp_hfi *hfi,
			struct dcp_hfi_completion *comp)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&hfi->pending_lock, flags);
	ret = xa_insert(&hfi->pending_xa, comp->packet_id, comp, GFP_ATOMIC);
	spin_unlock_irqrestore(&hfi->pending_lock, flags);

	if (ret < 0) {
		dev_err_ratelimited(hfi->dev,
				    "HFI pending XArray full or collision at pkt_id=%u: %d\n",
			comp->packet_id, ret);
		return -EBUSY;
	}
	return 0;
}

struct dcp_hfi_completion *dcp_hfi_pending_find(struct dcp_hfi *hfi,
						u32 packet_id)
{
	unsigned long flags;
	struct dcp_hfi_completion *comp;

	spin_lock_irqsave(&hfi->pending_lock, flags);
	comp = xa_erase(&hfi->pending_xa, packet_id);
	spin_unlock_irqrestore(&hfi->pending_lock, flags);
	return comp;
}

static void dcp_hfi_complete_pending(struct dcp_hfi *hfi,
				     const struct hfi_packet_info *pkt_info,
				     int status)
{
	struct dcp_hfi_completion *comp;
	size_t n;

	comp = dcp_hfi_pending_find(hfi, pkt_info->packet_id);
	if (!comp) {
		if (!status)
			dev_warn_ratelimited(hfi->dev,
					     "HFI resp: SUCCESS cmd=0x%x pkt_id=%u no completion; payload dropped (size=%u)\n",
					     pkt_info->cmd, pkt_info->packet_id,
					     pkt_info->payload_size);
		return;
	}

	if (!status && comp->resp_data && comp->resp_len &&
	    pkt_info->payload_size) {
		n = min_t(size_t, comp->resp_len, pkt_info->payload_size);
		memcpy(comp->resp_data, pkt_info->payload_ptr, n);
	}

	comp->status = status;
	complete(&comp->done);
}

static void dcp_hfi_cancel_pending(struct dcp_hfi *hfi, int status)
{
	unsigned long flags;
	struct dcp_hfi_completion *comp;
	unsigned long id;
	void *entry;

	spin_lock_irqsave(&hfi->pending_lock, flags);
	xa_for_each(&hfi->pending_xa, id, entry) {
		comp = entry;
		comp->status = status;
		complete(&comp->done);
	}
	xa_destroy(&hfi->pending_xa);
	xa_init(&hfi->pending_xa);
	spin_unlock_irqrestore(&hfi->pending_lock, flags);
}

void dcp_hfi_cmd_callback(struct virtqueue *vq)
{
	struct dcp_drv *drv = vq->vdev->priv;
	struct dcp_hfi *hfi = drv ? drv->hfi : NULL;

	struct dcp_hfi_buf *buf;
	unsigned int len;
	unsigned long flags;

	if (!hfi)
		return;

	if (atomic_read(&hfi->shutdown_in_progress))
		return;

	spin_lock_irqsave(&hfi->cmd_lock, flags);
	virtqueue_disable_cb(vq);
	do {
		while ((buf = virtqueue_get_buf(vq, &len)) != NULL)
			_dcp_hfi_buf_free_locked(buf, &hfi->cmd_free_list);
	} while (!virtqueue_enable_cb(vq));
	spin_unlock_irqrestore(&hfi->cmd_lock, flags);
}

static void dcp_hfi_store_caps_payload(struct dcp_hfi *hfi,
				       struct completion *done,
				       u8 *dst, u32 *dst_len,
				       const struct hfi_packet_info *pkt_info,
				       const char *name)
{
	u32 n;

	if (!pkt_info->payload_ptr || !pkt_info->payload_size) {
		dev_dbg(hfi->dev, "%s: empty payload\n", name);
		return;
	}

	if (pkt_info->payload_size > DCP_HFI_CAPS_BUF_SIZE) {
		dev_warn(hfi->dev, "%s: payload too large: %u\n",
			 name, pkt_info->payload_size);
		return;
	}

	n = pkt_info->payload_size;
	memcpy(dst, pkt_info->payload_ptr, n);
	*dst_len = n;
	if (done)
		complete(done);
	dev_dbg(hfi->dev, "%s: stored %u bytes\n", name, n);
}

static void dcp_hfi_dispatch_frame_done(struct dcp_hfi *hfi,
					const struct hfi_packet_info *pkt_info)
{
	const struct hfi_display_frame_event_data *frame = NULL;
	u32 sequence_id = 0;
	u64 timestamp_ns = 0;

	if (pkt_info->payload_ptr &&
	    pkt_info->payload_size >= sizeof(*frame)) {
		frame = pkt_info->payload_ptr;
		sequence_id = frame->bufferflip_index;
		timestamp_ns = ((u64)frame->timestamp_hi << 32) |
			       frame->timestamp_lo;
	}
	dcp_crtc_hfi_frame_done_cb(hfi->drv, pkt_info->id & 0xFFFF,
				   sequence_id, timestamp_ns);
}

/* Route asynchronous events and capability packets before flag handling. */
static bool dcp_hfi_dispatch_event(struct dcp_hfi *hfi,
				   const struct hfi_packet_info *pkt_info)
{
	switch (pkt_info->cmd) {
	case HFI_COMMAND_DISPLAY_EVENT_VSYNC: {
		const struct hfi_display_vsync_data *vsync = NULL;
		u64 timestamp_ns = 0;

		if (pkt_info->payload_ptr &&
		    pkt_info->payload_size >= sizeof(*vsync)) {
			vsync = pkt_info->payload_ptr;
			timestamp_ns = ((u64)vsync->timestamp_hi << 32) |
				       vsync->timestamp_lo;
		}
		dcp_crtc_hfi_vblank_cb(hfi->drv, pkt_info->id & 0xFFFF,
				       timestamp_ns);
		return true;
	}

	case HFI_COMMAND_DISPLAY_EVENT_FRAME_SCAN_START:
		return true;

	case HFI_COMMAND_DISPLAY_EVENT_FRAME_SCAN_COMPLETE:
		dcp_hfi_dispatch_frame_done(hfi, pkt_info);
		return true;

	case HFI_COMMAND_DISPLAY_EVENT_HPD_STATUS:
		return true;

	case HFI_COMMAND_DISPLAY_EVENT_PANEL_DEAD:
		dev_err(hfi->dev,
			"DCP: panel dead display %u\n",
			pkt_info->id & 0xFFFF);
		return true;

	case HFI_COMMAND_DISPLAY_EVENT_HW_RECOVERY:
		dev_err(hfi->dev,
			"DCP: HW recovery display %u\n",
			pkt_info->id & 0xFFFF);
		return true;

	case HFI_COMMAND_DISPLAY_POWER_CONTROL: {
		u32 display_id = pkt_info->id & 0xFFFF;
		u32 power_mask = 0;
		bool enable = true;
		unsigned long flags;
		void *power_priv;
		void (*power_cb)(void *priv, u32 display_id,
				 u32 power_mask, bool enable, u32 packet_id);

		if (pkt_info->payload_ptr &&
		    pkt_info->payload_size >= 2 * sizeof(u32)) {
			const u32 *p = pkt_info->payload_ptr;

			power_mask = p[0];
			enable     = (p[1] == HFI_TRUE);
		}

		spin_lock_irqsave(&hfi->resp_drain_lock, flags);
		power_cb = hfi->power_control_cb;
		power_priv = hfi->power_control_priv;
		spin_unlock_irqrestore(&hfi->resp_drain_lock, flags);

		if (power_cb)
			power_cb(power_priv, display_id, power_mask, enable,
				 pkt_info->packet_id);
		return true;
	}

	case HFI_COMMAND_DEVICE_INIT_DEVICE_CAPS: {
		unsigned long flags;

		spin_lock_irqsave(&hfi->caps_lock, flags);
		dcp_hfi_store_caps_payload(hfi, &hfi->caps_cache.device_done,
					   hfi->caps_cache.device_buf,
					   &hfi->caps_cache.device_len,
					   pkt_info, "DEVICE_INIT_DEVICE_CAPS");
		spin_unlock_irqrestore(&hfi->caps_lock, flags);
		return true;
	}

	case HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS: {
		unsigned long flags;

		spin_lock_irqsave(&hfi->caps_lock, flags);
		dcp_hfi_store_caps_payload(hfi, &hfi->caps_cache.display_done,
					   hfi->caps_cache.display_buf,
					   &hfi->caps_cache.display_len,
					   pkt_info, "DEVICE_INIT_DISPLAY_CAPS");
		spin_unlock_irqrestore(&hfi->caps_lock, flags);
		return true;
	}

	case HFI_COMMAND_DEVICE_INIT_VIG_CAPS: {
		unsigned long flags;

		spin_lock_irqsave(&hfi->caps_lock, flags);
		dcp_hfi_store_caps_payload(hfi, NULL, hfi->caps_cache.vig_buf,
					   &hfi->caps_cache.vig_len,
					   pkt_info, "VIG_CAPS");
		hfi->caps_cache.pipe_caps_received |= BIT(0);
		spin_unlock_irqrestore(&hfi->caps_lock, flags);
		return true;
	}

	case HFI_COMMAND_DEVICE_INIT_DMA_CAPS: {
		unsigned long flags;

		spin_lock_irqsave(&hfi->caps_lock, flags);
		dcp_hfi_store_caps_payload(hfi, NULL, hfi->caps_cache.dma_buf,
					   &hfi->caps_cache.dma_len,
					   pkt_info, "DMA_CAPS");
		hfi->caps_cache.pipe_caps_received |= BIT(1);
		spin_unlock_irqrestore(&hfi->caps_lock, flags);
		return true;
	}

	case HFI_COMMAND_DEVICE_INIT_VIG_R1_CAPS: {
		unsigned long flags;

		spin_lock_irqsave(&hfi->caps_lock, flags);
		dcp_hfi_store_caps_payload(hfi, NULL, hfi->caps_cache.vig_r1_buf,
					   &hfi->caps_cache.vig_r1_len,
					   pkt_info, "VIG_R1_CAPS");
		hfi->caps_cache.pipe_caps_received |= BIT(2);
		spin_unlock_irqrestore(&hfi->caps_lock, flags);
		return true;
	}

	case HFI_COMMAND_DEVICE_INIT_DMA_R1_CAPS: {
		unsigned long flags;

		spin_lock_irqsave(&hfi->caps_lock, flags);
		dcp_hfi_store_caps_payload(hfi, NULL, hfi->caps_cache.dma_r1_buf,
					   &hfi->caps_cache.dma_r1_len,
					   pkt_info, "DMA_R1_CAPS");
		hfi->caps_cache.pipe_caps_received |= BIT(3);
		spin_unlock_irqrestore(&hfi->caps_lock, flags);
		return true;
	}

	case HFI_COMMAND_DEVICE_INIT_COMMON_LAYER_CAPS: {
		unsigned long flags;

		spin_lock_irqsave(&hfi->caps_lock, flags);
		hfi->caps_cache.pipe_caps_received |= BIT(4);
		spin_unlock_irqrestore(&hfi->caps_lock, flags);
		dev_dbg(hfi->dev,
			"COMMON_LAYER_CAPS: received (size=%u)\n",
			pkt_info->payload_size);
		return true;
	}

	case HFI_COMMAND_DEVICE_INIT_DISPLAY_WB_CAPS: {
		unsigned long flags;

		spin_lock_irqsave(&hfi->caps_lock, flags);
		hfi->caps_cache.pipe_caps_received |= BIT(5);
		spin_unlock_irqrestore(&hfi->caps_lock, flags);
		dev_dbg(hfi->dev,
			"DISPLAY_WB_CAPS: received (size=%u) — all pipe caps received\n",
			pkt_info->payload_size);
		return true;
	}

	case HFI_COMMAND_DEVICE_CALLBACK_RESOURCE_VOTE: {
		unsigned long vote_flags;
		u32 payload_size = pkt_info->payload_size;
		bool valid = pkt_info->payload_ptr && payload_size &&
			payload_size <= DCP_HFI_RESOURCE_VOTE_BUF_SIZE &&
			payload_size % sizeof(struct dcp_resource_vote_wire) == 0;

		if (!valid) {
			dev_warn_ratelimited(hfi->dev,
					     "dropping malformed RESOURCE_VOTE payload size=%u max=%u\n",
					     payload_size, DCP_HFI_RESOURCE_VOTE_BUF_SIZE);
			payload_size = 0;
		}

		spin_lock_irqsave(&hfi->resource_vote_lock, vote_flags);
		if (payload_size)
			memcpy(hfi->resource_vote_buf, pkt_info->payload_ptr,
			       payload_size);
		hfi->resource_vote_len = payload_size;
		hfi->resource_vote_generation++;
		spin_unlock_irqrestore(&hfi->resource_vote_lock, vote_flags);

		if (payload_size)
			schedule_work(&hfi->resource_vote_work);
		return true;
	}

	default:
		return false;
	}
}

void dcp_hfi_resp_callback(struct virtqueue *vq)
{
	struct dcp_drv *drv = vq->vdev->priv;
	struct dcp_hfi *hfi = drv ? drv->hfi : NULL;
	struct dcp_hfi_buf *buf;
	unsigned int len;
	unsigned long drain_flags;

	if (!hfi || atomic_read(&hfi->shutdown_in_progress))
		return;

	spin_lock_irqsave(&hfi->resp_drain_lock, drain_flags);
	virtqueue_disable_cb(vq);
	for (;;) {
		while ((buf = virtqueue_get_buf(vq, &len)) != NULL) {
			struct hfi_cmd_buff_hdl hdl;
			struct hfi_header_info hdr_info;
			struct hfi_packet_info pkt_info;
			u32 pkt_num;
			int rc;

			spin_unlock_irqrestore(&hfi->resp_drain_lock, drain_flags);

			if (unlikely(len < sizeof(struct hfi_header) ||
				     len > DCP_HFI_BUF_SIZE)) {
				dev_warn_ratelimited(hfi->dev,
						     "HFI resp: short buffer len=%u (min=%zu) — skipping\n",
						     len, sizeof(struct hfi_header));
				goto repost;
			}

			hdl.cmd_buffer = buf->data;
			hdl.size = (u32)len;
			rc = hfi_unpacker_get_header_info(&hdl, &hdr_info);
			if (rc) {
				dev_warn_ratelimited(hfi->dev,
						     "HFI resp: invalid header (rc=%d) len=%u\n",
					     rc, len);
				print_hex_dump(KERN_WARNING, "HFI resp raw: ",
					       DUMP_PREFIX_OFFSET, 16, 4,
					       buf->data, min_t(u32, len, 64), false);
				goto repost;
			}

			for (pkt_num = 1; pkt_num <= hdr_info.num_packets; pkt_num++) {
				memset(&pkt_info, 0, sizeof(pkt_info));
				rc = hfi_unpacker_get_packet_info(&hdl, pkt_num,
								  &pkt_info);
				if (rc) {
					dev_warn_ratelimited(hfi->dev,
							     "HFI resp: bad pkt %u (rc=%d)\n",
							     pkt_num, rc);
					break;
				}

				dev_dbg(hfi->dev,
					"HFI resp: cmd=0x%x flags=0x%x pkt_id=%u id=%u payload_size=%u\n",
					pkt_info.cmd, pkt_info.flags, pkt_info.packet_id,
					pkt_info.id, pkt_info.payload_size);

				if (pkt_info.flags & HFI_RX_FLAGS_SYSTEM_ERROR) {
					dev_err(hfi->dev,
						"HFI system error: cmd=0x%x pkt_id=%u\n",
						pkt_info.cmd, pkt_info.packet_id);
					dcp_hfi_complete_pending(hfi, &pkt_info, -EIO);
				} else if (pkt_info.flags & HFI_RX_FLAGS_DEVICE_ERROR) {
					dev_err(hfi->dev,
						"HFI error: cmd=0x%x pkt_id=%u\n",
						pkt_info.cmd, pkt_info.packet_id);
					dcp_hfi_complete_pending(hfi, &pkt_info, -EIO);
				} else if (dcp_hfi_dispatch_event(hfi, &pkt_info)) {
					/* Event dispatch runs before flag handling. */
				} else if (pkt_info.flags & HFI_RX_FLAGS_SUCCESS) {
					if (pkt_info.flags & HFI_RX_FLAGS_INFORMATION)
						dev_dbg(hfi->dev,
							"HFI info with success: cmd=0x%x pkt_id=%u\n",
							pkt_info.cmd, pkt_info.packet_id);
					dcp_hfi_complete_pending(hfi, &pkt_info, 0);
				} else if (pkt_info.flags & HFI_RX_FLAGS_INFORMATION) {
					dev_dbg(hfi->dev,
						"HFI info with cmd=0x%x pkt_id=%u\n",
						pkt_info.cmd, pkt_info.packet_id);
					dcp_hfi_complete_pending(hfi, &pkt_info, 0);
				} else {
					dev_dbg_ratelimited(hfi->dev,
							    "HFI resp: unhandled cmd=0x%x flags=0x%x pkt_id=%u\n",
							    pkt_info.cmd, pkt_info.flags,
							    pkt_info.packet_id);
				}
			}

repost:
			spin_lock_irqsave(&hfi->resp_drain_lock, drain_flags);
			dcp_hfi_buf_init_sg(buf, DCP_HFI_BUF_SIZE);
			if (virtqueue_add_inbuf_premapped(vq, &buf->sg, 1, buf,
							  NULL, GFP_ATOMIC)) {
				list_add_tail(&buf->node, &hfi->resp_repost_list);
				schedule_delayed_work(&hfi->resp_repost_work,
						      msecs_to_jiffies(20));
			}
		}

		if (virtqueue_kick_prepare(vq))
			virtqueue_notify(vq);
		if (virtqueue_enable_cb(vq))
			break;
	}

	spin_unlock_irqrestore(&hfi->resp_drain_lock, drain_flags);
}

int dcp_hfi_send_cmd_async(struct dcp_hfi *hfi,
			   struct dcp_hfi_buf *buf,
			   size_t cmd_len,
			   struct dcp_hfi_completion *comp)
{
	unsigned long flags;
	int ret;

	buf->comp = comp;
	dcp_hfi_buf_init_sg(buf, cmd_len);

	/* Serialize shutdown with both queue submission and notification. */
	spin_lock_irqsave(&hfi->cmd_lock, flags);
	if (atomic_read(&hfi->shutdown_in_progress)) {
		spin_unlock_irqrestore(&hfi->cmd_lock, flags);
		dcp_hfi_buf_free(hfi, buf);
		return -ENODEV;
	}

	if (comp) {
		ret = dcp_hfi_pending_add(hfi, comp);
		if (ret) {
			spin_unlock_irqrestore(&hfi->cmd_lock, flags);
			dcp_hfi_buf_free(hfi, buf);
			return ret;
		}
	}

	ret = virtqueue_add_outbuf_premapped(hfi->cmd_vq, &buf->sg, 1, buf,
					     GFP_ATOMIC);
	if (ret) {
		if (comp)
			dcp_hfi_pending_find(hfi, comp->packet_id);
		spin_unlock_irqrestore(&hfi->cmd_lock, flags);
		dcp_hfi_buf_free(hfi, buf);
		dev_err(hfi->dev, "virtqueue_add_outbuf failed: %d\n", ret);
		return ret;
	}

	virtqueue_kick(hfi->cmd_vq);
	spin_unlock_irqrestore(&hfi->cmd_lock, flags);
	return 0;
}

static const char *dcp_hfi_cmd_buff_type_name(enum hfi_cmd_buff_type cmd_type)
{
	switch (cmd_type) {
	case HFI_CMD_BUFF_SYSTEM:         return "SYSTEM";
	case HFI_CMD_BUFF_DEVICE:         return "DEVICE";
	case HFI_CMD_BUFF_DISPLAY:        return "DISPLAY";
	case HFI_CMD_BUFF_DEBUG:          return "DEBUG";
	case HFI_CMD_BUFF_VIRTUALIZATION: return "VIRT";
	default:                          return "UNKNOWN";
	}
}

static int dcp_hfi_send_sync_on_vq(struct dcp_hfi *hfi,
				   struct virtqueue *vq __maybe_unused,
				   u32 cmd,
				   enum hfi_cmd_buff_type cmd_type,
				   u32 object_id,
				   enum hfi_packet_payload_type payload_type,
				   void *payload,
				   u32 payload_size,
				   u32 tx_flags,
				   void *resp_data,
				   size_t resp_len,
				   unsigned int timeout_ms)
{
	struct dcp_hfi_buf *buf;
	struct dcp_hfi_completion comp;
	struct hfi_cmd_buff_hdl hdl;
	struct hfi_header_info hdr_info;
	struct hfi_packet_info pkt_info;
	u32 packet_id;
	unsigned long flags;
	int ret;

	buf = dcp_hfi_buf_alloc(hfi);
	if (!buf) {
		dev_err(hfi->dev, "HFI TX buffer pool exhausted (cmd=0x%x)\n",
			cmd);
		return -ENOMEM;
	}

	hdl.cmd_buffer = buf->data;
	hdl.size       = DCP_HFI_BUF_SIZE;

	hdr_info.num_packets   = 0;
	hdr_info.cmd_buff_type = cmd_type;
	hdr_info.object_id     = object_id;
	hdr_info.header_id     = dcp_hfi_next_packet_id(hfi);

	ret = hfi_create_header(&hdl, &hdr_info);
	if (ret) {
		dcp_hfi_buf_free(hfi, buf);
		return ret;
	}

	packet_id = dcp_hfi_next_packet_id(hfi);

	pkt_info.cmd          = cmd;
	pkt_info.id           = object_id & 0xFFFF;
	pkt_info.flags        = tx_flags;
	pkt_info.packet_id    = packet_id;
	pkt_info.payload_type = payload_type;
	pkt_info.payload_size = payload_size;
	pkt_info.payload_ptr  = payload;

	if (payload_type == HFI_PAYLOAD_NONE || !payload || !payload_size)
		ret = hfi_create_packet_header(&hdl, &pkt_info);
	else
		ret = hfi_create_full_packet(&hdl, &pkt_info);

	if (ret) {
		dcp_hfi_buf_free(hfi, buf);
		return ret;
	}

	init_completion(&comp.done);
	comp.resp_data = resp_data;
	comp.resp_len  = resp_len;
	comp.status    = 0;
	comp.packet_id = packet_id;

	dev_dbg(hfi->dev,
		"HFI send cmd=0x%x type=%s obj=%u pkt=%u payload=%u timeout=%ums\n",
		cmd, dcp_hfi_cmd_buff_type_name(cmd_type), object_id,
		packet_id, payload_size, timeout_ms);

	ret = dcp_hfi_send_cmd_async(hfi, buf,
				     hfi_get_header_size(&hdl), &comp);
	if (ret)
		return ret;

	if (!wait_for_completion_timeout(&comp.done, msecs_to_jiffies(timeout_ms))) {
		dev_err(hfi->dev,
			"HFI cmd=0x%x buf_type=%s object_id=%u timed out after %ums (pkt_id=%u)\n",
			cmd, dcp_hfi_cmd_buff_type_name(cmd_type), object_id,
			timeout_ms, packet_id);
		spin_lock_irqsave(&hfi->resp_drain_lock, flags);
		dcp_hfi_pending_find(hfi, packet_id);
		spin_unlock_irqrestore(&hfi->resp_drain_lock, flags);
		return -ETIMEDOUT;
	}

	return comp.status;
}

int dcp_hfi_send_sync(struct dcp_hfi *hfi,
		      u32 cmd,
		      enum hfi_cmd_buff_type cmd_type,
		      u32 object_id,
		      enum hfi_packet_payload_type payload_type,
		      void *payload,
		      u32 payload_size,
		      u32 tx_flags,
		      void *resp_data,
		      size_t resp_len,
		      unsigned int timeout_ms)
{
	int ret;

	ret = dcp_hfi_send_sync_on_vq(hfi, NULL,
				      cmd, cmd_type, object_id,
				      payload_type, payload, payload_size,
				      tx_flags, resp_data, resp_len,
				      timeout_ms);
	if (ret)
		dev_err(hfi->dev, "HFI command 0x%x failed: %d\n", cmd, ret);

	return ret;
}

int dcp_hfi_send_no_wait_flags(struct dcp_hfi *hfi,
			       u32 cmd,
			       enum hfi_cmd_buff_type cmd_type,
			       u32 object_id,
			       enum hfi_packet_payload_type payload_type,
			       void *payload,
			       u32 payload_size,
			       u32 tx_flags)
{
	struct dcp_hfi_buf *buf;
	struct hfi_cmd_buff_hdl hdl;
	struct hfi_header_info hdr_info;
	struct hfi_packet_info pkt_info;
	int ret;

	buf = dcp_hfi_buf_alloc(hfi);
	if (!buf) {
		dev_warn_ratelimited(hfi->dev,
				     "HFI TX pool exhausted (no-wait cmd=0x%x)\n", cmd);
		return -ENOMEM;
	}

	hdl.cmd_buffer = buf->data;
	hdl.size       = DCP_HFI_BUF_SIZE;

	hdr_info.num_packets   = 0;
	hdr_info.cmd_buff_type = cmd_type;
	hdr_info.object_id     = object_id;
	hdr_info.header_id     = dcp_hfi_next_packet_id(hfi);

	ret = hfi_create_header(&hdl, &hdr_info);
	if (ret) {
		dcp_hfi_buf_free(hfi, buf);
		return ret;
	}

	pkt_info.cmd          = cmd;
	pkt_info.id           = object_id & 0xFFFF;
	pkt_info.flags        = tx_flags;
	pkt_info.packet_id    = dcp_hfi_next_packet_id(hfi);
	pkt_info.payload_type = payload_type;
	pkt_info.payload_size = payload_size;
	pkt_info.payload_ptr  = payload;

	if (payload_type == HFI_PAYLOAD_NONE || !payload || !payload_size)
		ret = hfi_create_packet_header(&hdl, &pkt_info);
	else
		ret = hfi_create_full_packet(&hdl, &pkt_info);

	if (ret) {
		dcp_hfi_buf_free(hfi, buf);
		return ret;
	}

	ret = dcp_hfi_send_cmd_async(hfi, buf,
				     hfi_get_header_size(&hdl), NULL);
	if (ret)
		dev_warn(hfi->dev,
			 "HFI send (no-wait) cmd=0x%x pkt_id=%u failed: %d\n",
			 cmd, pkt_info.packet_id, ret);

	return ret;
}

int dcp_hfi_send_no_wait_atomic_flags(struct dcp_hfi *hfi,
				      u32 cmd,
				      enum hfi_cmd_buff_type cmd_type,
				      u32 object_id,
				      enum hfi_packet_payload_type payload_type,
				      void *payload,
				      u32 payload_size,
				      u32 tx_flags)
{
	struct dcp_hfi_buf *buf;
	struct hfi_cmd_buff_hdl hdl;
	struct hfi_header_info hdr_info;
	struct hfi_packet_info pkt_info;
	int ret;

	buf = dcp_hfi_buf_alloc_atomic(hfi);
	if (!buf)
		return -ENOMEM;

	hdl.cmd_buffer = buf->data;
	hdl.size       = DCP_HFI_BUF_SIZE;
	hdr_info.num_packets   = 0;
	hdr_info.cmd_buff_type = cmd_type;
	hdr_info.object_id     = object_id;
	hdr_info.header_id     = dcp_hfi_next_packet_id(hfi);

	ret = hfi_create_header(&hdl, &hdr_info);
	if (ret)
		goto free_buf;

	pkt_info.cmd          = cmd;
	pkt_info.id           = object_id & 0xFFFF;
	pkt_info.flags        = tx_flags;
	pkt_info.packet_id    = dcp_hfi_next_packet_id(hfi);
	pkt_info.payload_type = payload_type;
	pkt_info.payload_size = payload_size;
	pkt_info.payload_ptr  = payload;

	if (payload_type == HFI_PAYLOAD_NONE || !payload || !payload_size)
		ret = hfi_create_packet_header(&hdl, &pkt_info);
	else
		ret = hfi_create_full_packet(&hdl, &pkt_info);
	if (ret)
		goto free_buf;

	return dcp_hfi_send_cmd_async(hfi, buf,
				      hfi_get_header_size(&hdl), NULL);

free_buf:
	dcp_hfi_buf_free(hfi, buf);
	return ret;
}

int dcp_hfi_send_no_wait(struct dcp_hfi *hfi,
			 u32 cmd,
			 enum hfi_cmd_buff_type cmd_type,
			 u32 object_id,
			 enum hfi_packet_payload_type payload_type,
			 void *payload,
			 u32 payload_size)
{
	return dcp_hfi_send_no_wait_flags(hfi, cmd, cmd_type, object_id,
			payload_type, payload, payload_size, HFI_TX_FLAGS_NONE);
}

static void dcp_hfi_resource_vote_work(struct work_struct *work)
{
	struct dcp_hfi *hfi = container_of(work, struct dcp_hfi,
					     resource_vote_work);
	u8 buf[DCP_HFI_RESOURCE_VOTE_BUF_SIZE];
	struct dcp_resource_vote_pair pairs[DCP_HFI_RESOURCE_VOTE_BUF_SIZE /
							 sizeof(struct dcp_resource_vote_wire)];
	const struct dcp_resource_vote_wire *wire;
	u32 len, generation, count, i;
	bool dirty;
	unsigned long flags;

	do {
		spin_lock_irqsave(&hfi->resource_vote_lock, flags);
		len = hfi->resource_vote_len;
		generation = hfi->resource_vote_generation;
		if (len)
			memcpy(buf, hfi->resource_vote_buf, len);
		spin_unlock_irqrestore(&hfi->resource_vote_lock, flags);

		if (!len || !hfi->resource_vote_cb)
			return;
		if (len % sizeof(struct dcp_resource_vote_wire)) {
			dev_warn_ratelimited(hfi->dev,
					     "dropping malformed RESOURCE_VOTE size=%u\n", len);
			return;
		}
		count = len / sizeof(struct dcp_resource_vote_wire);
		wire = (const struct dcp_resource_vote_wire *)buf;
		for (i = 0; i < count; i++) {
			pairs[i].key = le32_to_cpu(wire[i].key);
			pairs[i].value = get_unaligned_le64(&wire[i].value);
		}

		hfi->resource_vote_cb(hfi->drv, pairs, count);
		spin_lock_irqsave(&hfi->resource_vote_lock, flags);
		dirty = generation != hfi->resource_vote_generation;
		spin_unlock_irqrestore(&hfi->resource_vote_lock, flags);
	} while (dirty);
}

static void dcp_hfi_free_pool(struct dcp_hfi *hfi, struct dcp_hfi_buf *pool,
			      u32 count)
{
	u32 i;

	if (!pool)
		return;

	for (i = 0; i < count; i++) {
		if (!pool[i].data)
			continue;

		dma_free_coherent(hfi->dma_dev, DCP_HFI_BUF_SIZE,
				  pool[i].data, pool[i].dma_addr);
		pool[i].data = NULL;
	}
}

static int dcp_hfi_prepost_resp_bufs(struct dcp_hfi *hfi)
{
	int i, ret;

	for (i = 0; i < DCP_HFI_RESP_POOL_SIZE; i++) {
		struct dcp_hfi_buf *buf = &hfi->resp_pool[i];

		dcp_hfi_buf_init_sg(buf, DCP_HFI_BUF_SIZE);

		ret = virtqueue_add_inbuf_premapped(hfi->resp_vq, &buf->sg, 1,
						    buf, NULL, GFP_KERNEL);
		if (ret) {
			dev_warn(hfi->dev,
				 "resp_vq full at buf %d/%d: %d\n",
				 i, DCP_HFI_RESP_POOL_SIZE, ret);
			break;
		}
	}
	return ret;
}

static void dcp_hfi_resp_repost_work(struct work_struct *work)
{
	struct dcp_hfi *hfi = container_of(to_delayed_work(work), struct dcp_hfi,
					      resp_repost_work);
	struct dcp_hfi_buf *buf, *tmp;
	unsigned long flags;
	bool retry = false;

	if (atomic_read(&hfi->shutdown_in_progress))
		return;
	spin_lock_irqsave(&hfi->resp_drain_lock, flags);
	list_for_each_entry_safe(buf, tmp, &hfi->resp_repost_list, node) {
		dcp_hfi_buf_init_sg(buf, DCP_HFI_BUF_SIZE);
		if (virtqueue_add_inbuf_premapped(hfi->resp_vq, &buf->sg, 1,
						  buf, NULL, GFP_ATOMIC)) {
			retry = true;
			break;
		}
		list_del_init(&buf->node);
	}
	if (virtqueue_kick_prepare(hfi->resp_vq))
		virtqueue_notify(hfi->resp_vq);
	spin_unlock_irqrestore(&hfi->resp_drain_lock, flags);
	if (retry && !atomic_read(&hfi->shutdown_in_progress))
		schedule_delayed_work(&hfi->resp_repost_work, msecs_to_jiffies(20));
}

int dcp_hfi_init(struct dcp_drv *drv,
		 struct virtio_device *vdev,
		 struct virtqueue *cmd_vq,
		 struct virtqueue *resp_vq,
		 struct device *dma_dev)
{
	struct dcp_hfi *hfi;
	int i, ret;

	if (!dma_dev)
		return -ENODEV;

	hfi = devm_kzalloc(&vdev->dev, sizeof(*hfi), GFP_KERNEL);
	if (!hfi)
		return -ENOMEM;

	hfi->dev     = &vdev->dev;
	hfi->drv     = drv;
	hfi->cmd_vq  = cmd_vq;
	hfi->resp_vq = resp_vq;
	hfi->dma_dev = dma_dev;

	spin_lock_init(&hfi->cmd_lock);
	spin_lock_init(&hfi->pending_lock);
	spin_lock_init(&hfi->resource_vote_lock);
	spin_lock_init(&hfi->resp_drain_lock);
	spin_lock_init(&hfi->caps_lock);
	mutex_init(&hfi->caps_cache_lock);

	atomic_set(&hfi->packet_id, 0);
	atomic_set(&hfi->shutdown_in_progress, 0);

	INIT_LIST_HEAD(&hfi->cmd_free_list);

	xa_init(&hfi->pending_xa);

	INIT_WORK(&hfi->resource_vote_work, dcp_hfi_resource_vote_work);
	INIT_DELAYED_WORK(&hfi->resp_repost_work, dcp_hfi_resp_repost_work);
	INIT_LIST_HEAD(&hfi->resp_repost_list);
	hfi->resource_vote_len = 0;
	hfi->resource_vote_generation = 0;

	hfi->cmd_pool = devm_kcalloc(hfi->dev, DCP_HFI_CMD_POOL_SIZE,
				     sizeof(*hfi->cmd_pool), GFP_KERNEL);
	if (!hfi->cmd_pool)
		return -ENOMEM;

	hfi->resp_pool = devm_kcalloc(hfi->dev, DCP_HFI_RESP_POOL_SIZE,
				      sizeof(*hfi->resp_pool), GFP_KERNEL);
	if (!hfi->resp_pool)
		return -ENOMEM;
	for (i = 0; i < DCP_HFI_CMD_POOL_SIZE; i++) {
		hfi->cmd_pool[i].data = dma_alloc_coherent(hfi->dma_dev,
							   DCP_HFI_BUF_SIZE,
							   &hfi->cmd_pool[i].dma_addr,
							   GFP_KERNEL);
		if (!hfi->cmd_pool[i].data) {
			ret = -ENOMEM;
			goto err;
		}
		hfi->cmd_pool[i].size = DCP_HFI_BUF_SIZE;
		list_add_tail(&hfi->cmd_pool[i].node, &hfi->cmd_free_list);
	}

	for (i = 0; i < DCP_HFI_RESP_POOL_SIZE; i++) {
		hfi->resp_pool[i].data =
			dma_alloc_coherent(hfi->dma_dev, DCP_HFI_BUF_SIZE,
					   &hfi->resp_pool[i].dma_addr, GFP_KERNEL);
		if (!hfi->resp_pool[i].data) {
			ret = -ENOMEM;
			goto err;
		}
		hfi->resp_pool[i].size = DCP_HFI_BUF_SIZE;
		INIT_LIST_HEAD(&hfi->resp_pool[i].node);
	}

	ret = dcp_hfi_prepost_resp_bufs(hfi);
	if (ret)
		goto err;

	drv->hfi = hfi;
	dev_dbg(hfi->dev,
		"HFI core initialized: cmd_vq=%p resp_vq=%p\n",
		cmd_vq, resp_vq);
	return 0;

err:
	atomic_set(&hfi->shutdown_in_progress, 1);
	cancel_work_sync(&hfi->resource_vote_work);
	cancel_delayed_work_sync(&hfi->resp_repost_work);
	while (virtqueue_detach_unused_buf(hfi->cmd_vq))
		;
	while (virtqueue_detach_unused_buf(hfi->resp_vq))
		;
	xa_destroy(&hfi->pending_xa);

	dcp_hfi_free_pool(hfi, hfi->cmd_pool, DCP_HFI_CMD_POOL_SIZE);
	dcp_hfi_free_pool(hfi, hfi->resp_pool, DCP_HFI_RESP_POOL_SIZE);
	return ret;
}

void dcp_hfi_set_power_control_cb(struct dcp_hfi *hfi, void *priv,
				  void (*cb)(void *priv, u32 display_id,
					     u32 power_mask, bool enable,
					     u32 packet_id))
{
	unsigned long flags;

	spin_lock_irqsave(&hfi->resp_drain_lock, flags);
	hfi->power_control_priv = priv;
	hfi->power_control_cb = cb;
	spin_unlock_irqrestore(&hfi->resp_drain_lock, flags);
}

void dcp_hfi_clear_power_control_cb(struct dcp_hfi *hfi, void *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&hfi->resp_drain_lock, flags);
	if (hfi->power_control_priv == priv) {
		hfi->power_control_priv = NULL;
		hfi->power_control_cb = NULL;
	}
	spin_unlock_irqrestore(&hfi->resp_drain_lock, flags);
}

void dcp_hfi_stop(struct dcp_hfi *hfi)
{
	unsigned long flags;

	spin_lock_irqsave(&hfi->cmd_lock, flags);
	atomic_set(&hfi->shutdown_in_progress, 1);
	spin_unlock_irqrestore(&hfi->cmd_lock, flags);

	cancel_work_sync(&hfi->resource_vote_work);
	cancel_delayed_work_sync(&hfi->resp_repost_work);
	dcp_hfi_cancel_pending(hfi, -ENODEV);
}

void dcp_hfi_quiesce_queues(struct dcp_hfi *hfi)
{
	unsigned long flags;

	spin_lock_irqsave(&hfi->resp_drain_lock, flags);
	if (hfi->cmd_vq)
		virtqueue_disable_cb(hfi->cmd_vq);
	if (hfi->resp_vq)
		virtqueue_disable_cb(hfi->resp_vq);
	spin_unlock_irqrestore(&hfi->resp_drain_lock, flags);

	if (hfi->cmd_vq)
		while (virtqueue_detach_unused_buf(hfi->cmd_vq))
			;
	if (hfi->resp_vq)
		while (virtqueue_detach_unused_buf(hfi->resp_vq))
			;
}

void dcp_hfi_deinit(struct dcp_hfi *hfi)
{
	mutex_lock(&hfi->caps_cache_lock);
	memset(&hfi->caps_cache, 0, sizeof(hfi->caps_cache));
	mutex_unlock(&hfi->caps_cache_lock);

	xa_destroy(&hfi->pending_xa);

	dcp_hfi_free_pool(hfi, hfi->cmd_pool, DCP_HFI_CMD_POOL_SIZE);
	dcp_hfi_free_pool(hfi, hfi->resp_pool, DCP_HFI_RESP_POOL_SIZE);
}
