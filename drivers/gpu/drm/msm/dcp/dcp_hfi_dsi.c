// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * HFI commands for the DSI display-session lifecycle.
 * DSI configuration and DCS transfers are implemented in dcp_hfi_dsi_config.c.
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
