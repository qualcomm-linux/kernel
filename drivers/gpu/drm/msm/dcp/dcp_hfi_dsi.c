// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * HFI commands for the DSI display-session lifecycle.
 * Panel initialization and DCS transfers are implemented in dcp_hfi_dsi_panel.c.
 */

#include <linux/device.h>
#include <drm/drm_modes.h>

#include "dcp_hfi.h"
#include "dcp_hfi_priv.h"
#include "hfi/hfi_commands_display.h"
#include "hfi/hfi_defs_common.h"
#include "hfi/hfi_defs_display.h"
#include "hfi/hfi_packet.h"
#include "hfi/hfi_packer.h"
#include "hfi/hfi_properties_display.h"

static void dcp_hfi_fill_mode_info(struct hfi_display_mode_info *info,
				   const struct drm_display_mode *mode)
{
	memset(info, 0, sizeof(*info));
	info->size            = sizeof(*info);
	info->h_active        = mode->hdisplay;
	info->h_back_porch    = mode->htotal - mode->hsync_end;
	info->h_sync_width    = mode->hsync_end - mode->hsync_start;
	info->h_front_porch   = mode->hsync_start - mode->hdisplay;
	info->h_skew          = mode->hskew;
	info->h_sync_polarity = !!(mode->flags & DRM_MODE_FLAG_PHSYNC);
	info->v_active        = mode->vdisplay;
	info->v_back_porch    = mode->vtotal - mode->vsync_end;
	info->v_sync_width    = mode->vsync_end - mode->vsync_start;
	info->v_front_porch   = mode->vsync_start - mode->vdisplay;
	info->v_sync_polarity = !!(mode->flags & DRM_MODE_FLAG_PVSYNC);
	info->refresh_rate    = drm_mode_vrefresh(mode);
	info->clk_rate_hz_lo = lower_32_bits((u64)mode->clock * 1000);
	info->clk_rate_hz_hi = upper_32_bits((u64)mode->clock * 1000);
}

static void dcp_hfi_dump_mode_info(struct dcp_hfi *hfi, const char *cmd,
				   u32 session_id,
				   const struct hfi_display_mode_info *info)
{
	dev_dbg(hfi->dev,
		"HFI %s s=%u h=%u/%u/%u/%u v=%u/%u/%u/%u p=%u/%u r=%u clk=%llu sz=%u\n",
		cmd, session_id,
		info->h_active, info->h_back_porch, info->h_sync_width,
		info->h_front_porch,
		info->v_active, info->v_back_porch, info->v_sync_width,
		info->v_front_porch,
		info->h_sync_polarity, info->v_sync_polarity,
		info->refresh_rate,
		(u64)info->clk_rate_hz_lo | ((u64)info->clk_rate_hz_hi << 32),
		info->size);
}

int dcp_hfi_display_mode_validate(struct dcp_hfi *hfi, u32 session_id,
				  const struct drm_display_mode *mode, u64 clk_rate_hz)
{
	struct hfi_display_mode_info info;
	int ret;

	dcp_hfi_fill_mode_info(&info, mode);
	info.clk_rate_hz_lo = lower_32_bits(clk_rate_hz);
	info.clk_rate_hz_hi = upper_32_bits(clk_rate_hz);
	dcp_hfi_dump_mode_info(hfi, "HFI MODE_VALIDATE", session_id, &info);

	ret = dcp_hfi_send_sync(hfi,
				HFI_COMMAND_DISPLAY_MODE_VALIDATE,
				HFI_CMD_BUFF_DISPLAY, session_id,
				HFI_PAYLOAD_U32_ARRAY,
				&info, sizeof(info),
				HFI_TX_FLAGS_RESPONSE_REQUIRED |
				HFI_TX_FLAGS_NON_DISCARDABLE,
				NULL, 0,
				DCP_HFI_TIMEOUT_MODE_MS);

	return ret;
}

int dcp_hfi_display_set_mode(struct dcp_hfi *hfi, u32 session_id,
			     const struct drm_display_mode *mode, u64 clk_rate_hz)
{
	struct hfi_display_mode_info info;
	int ret;

	dcp_hfi_fill_mode_info(&info, mode);
	info.clk_rate_hz_lo = lower_32_bits(clk_rate_hz);
	info.clk_rate_hz_hi = upper_32_bits(clk_rate_hz);
	dcp_hfi_dump_mode_info(hfi, "HFI SET_MODE", session_id, &info);

	ret = dcp_hfi_send_sync(hfi,
				HFI_COMMAND_DISPLAY_SET_MODE,
				HFI_CMD_BUFF_DISPLAY, session_id,
				HFI_PAYLOAD_U32_ARRAY,
				&info, sizeof(info),
				HFI_TX_FLAGS_RESPONSE_REQUIRED |
				HFI_TX_FLAGS_NON_DISCARDABLE,
				NULL, 0,
				DCP_HFI_TIMEOUT_MODE_MS);

	return ret;
}

int dcp_hfi_display_power_register(struct dcp_hfi *hfi, u32 session_id)
{
	return dcp_hfi_send_sync(hfi,
				 HFI_COMMAND_DISPLAY_POWER_REGISTER,
				 HFI_CMD_BUFF_DISPLAY, session_id,
				 HFI_PAYLOAD_NONE, NULL, 0,
				 HFI_TX_FLAGS_RESPONSE_REQUIRED |
				 HFI_TX_FLAGS_NON_DISCARDABLE,
				 NULL, 0,
				 DCP_HFI_TIMEOUT_DISPLAY_MS);
}

int dcp_hfi_display_power_control_ack(struct dcp_hfi *hfi, u32 session_id,
				      u32 packet_id)
{
	struct dcp_hfi_buf *buf;
	struct hfi_cmd_buff_hdl hdl;
	struct hfi_header_info hdr_info;
	struct hfi_packet_info pkt_info;
	int ret;

	buf = dcp_hfi_buf_alloc(hfi);
	if (!buf)
		return -ENOMEM;

	hdl.cmd_buffer = buf->data;
	hdl.size       = DCP_HFI_BUF_SIZE;

	hdr_info.num_packets   = 0;
	hdr_info.cmd_buff_type = HFI_CMD_BUFF_DISPLAY;
	hdr_info.object_id     = session_id;
	hdr_info.header_id     = dcp_hfi_next_packet_id(hfi);

	ret = hfi_create_header(&hdl, &hdr_info);
	if (ret)
		goto err_free_buf;

	pkt_info.cmd          = HFI_COMMAND_DISPLAY_POWER_CONTROL;
	pkt_info.id           = session_id & 0xFFFF;
	pkt_info.flags        = HFI_RX_FLAGS_SUCCESS;
	pkt_info.packet_id    = packet_id;
	pkt_info.payload_type = HFI_PAYLOAD_NONE;
	pkt_info.payload_size = 0;
	pkt_info.payload_ptr  = NULL;

	ret = hfi_create_packet_header(&hdl, &pkt_info);
	if (ret)
		goto err_free_buf;

	return dcp_hfi_send_cmd_async(hfi, buf, hfi_get_header_size(&hdl), NULL);

err_free_buf:
	dcp_hfi_buf_free(hfi, buf);
	return ret;
}

int dcp_hfi_display_enable(struct dcp_hfi *hfi, u32 intf_idx, u32 display_id,
			   u32 *session_id)
{
	int ret;

	ret = dcp_hfi_send_sync(hfi,
				HFI_COMMAND_DISPLAY_ENABLE,
				HFI_CMD_BUFF_DISPLAY, display_id,
				HFI_PAYLOAD_NONE, NULL, 0,
				HFI_TX_FLAGS_RESPONSE_REQUIRED |
				HFI_TX_FLAGS_NON_DISCARDABLE,
				NULL, 0,
				DCP_HFI_TIMEOUT_DISPLAY_MS);
	if (ret) {
		dev_err(hfi->dev,
			"HFI DISPLAY_ENABLE intf=%u display_id=%u failed: %d\n",
			intf_idx, display_id, ret);
		return ret;
	}

	*session_id = display_id;

	return 0;
}

int dcp_hfi_display_post_enable(struct dcp_hfi *hfi, u32 session_id)
{
	return dcp_hfi_send_sync(hfi,
				 HFI_COMMAND_DISPLAY_POST_ENABLE,
				 HFI_CMD_BUFF_DISPLAY, session_id,
				 HFI_PAYLOAD_NONE, NULL, 0,
				 HFI_TX_FLAGS_RESPONSE_REQUIRED |
				 HFI_TX_FLAGS_NON_DISCARDABLE,
				 NULL, 0,
				 DCP_HFI_TIMEOUT_DISPLAY_MS);
}

int dcp_hfi_display_disable(struct dcp_hfi *hfi, u32 session_id)
{
	return dcp_hfi_send_sync(hfi,
				 HFI_COMMAND_DISPLAY_DISABLE,
				 HFI_CMD_BUFF_DISPLAY, session_id,
				 HFI_PAYLOAD_NONE, NULL, 0,
				 HFI_TX_FLAGS_RESPONSE_REQUIRED |
				 HFI_TX_FLAGS_NON_DISCARDABLE,
				 NULL, 0,
				 DCP_HFI_TIMEOUT_DISPLAY_MS);
}

int dcp_hfi_display_post_disable(struct dcp_hfi *hfi, u32 session_id)
{
	return dcp_hfi_send_sync(hfi,
				 HFI_COMMAND_DISPLAY_POST_DISABLE,
				 HFI_CMD_BUFF_DISPLAY, session_id,
				 HFI_PAYLOAD_NONE, NULL, 0,
				 HFI_TX_FLAGS_RESPONSE_REQUIRED |
				 HFI_TX_FLAGS_NON_DISCARDABLE,
				 NULL, 0,
				 DCP_HFI_TIMEOUT_DISPLAY_MS);
}

int dcp_hfi_display_close(struct dcp_hfi *hfi, u32 session_id)
{
	int ret;

	ret = dcp_hfi_display_disable(hfi, session_id);
	if (ret) {
		dev_warn(hfi->dev, "HFI DISPLAY_DISABLE failed: %d\n", ret);
		return ret;
	}

	ret = dcp_hfi_display_post_disable(hfi, session_id);
	if (ret) {
		dev_warn(hfi->dev, "HFI DISPLAY_POST_DISABLE failed: %d\n", ret);
		return ret;
	}

	ret = dcp_hfi_send_sync(hfi,
				HFI_COMMAND_DISPLAY_CLOSE,
				HFI_CMD_BUFF_DISPLAY, session_id,
				HFI_PAYLOAD_NONE, NULL, 0,
				HFI_TX_FLAGS_RESPONSE_REQUIRED |
				HFI_TX_FLAGS_NON_DISCARDABLE,
				NULL, 0,
				DCP_HFI_TIMEOUT_DISPLAY_MS);
	if (ret)
		dev_warn(hfi->dev, "HFI DISPLAY_CLOSE failed: %d\n", ret);

	return ret;
}
