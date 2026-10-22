// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * HFI commands for display events and CRTC properties.
 */

#include <linux/device.h>

#include "dcp_hfi_priv.h"
#include "dcp_hfi.h"
#include "hfi/hfi_commands_display.h"
#include "hfi/hfi_commands_display_events.h"
#include "hfi/hfi_defs_common.h"
#include "hfi/hfi_defs_display.h"
#include "hfi/hfi_properties_display.h"

int dcp_hfi_display_event_register(struct dcp_hfi *hfi, u32 session_id,
				   u32 event_cmd)
{
	int ret = dcp_hfi_send_sync(hfi,
				      HFI_COMMAND_DISPLAY_EVENT_REGISTER,
				      HFI_CMD_BUFF_DISPLAY, session_id,
				      HFI_PAYLOAD_U32,
				      &event_cmd, sizeof(event_cmd),
				      HFI_TX_FLAGS_RESPONSE_REQUIRED |
				      HFI_TX_FLAGS_NON_DISCARDABLE,
				      NULL, 0,
				      DCP_HFI_TIMEOUT_DISPLAY_MS);
	return ret;
}

int dcp_hfi_display_event_deregister(struct dcp_hfi *hfi, u32 session_id,
				     u32 event_cmd)
{
	int ret = dcp_hfi_send_sync(hfi,
				      HFI_COMMAND_DISPLAY_EVENT_DEREGISTER,
				      HFI_CMD_BUFF_DISPLAY, session_id,
				      HFI_PAYLOAD_U32,
				      &event_cmd, sizeof(event_cmd),
				      HFI_TX_FLAGS_RESPONSE_REQUIRED |
				      HFI_TX_FLAGS_NON_DISCARDABLE,
				      NULL, 0,
				      DCP_HFI_TIMEOUT_DISPLAY_MS);
	return ret;
}

int dcp_hfi_display_event_register_async(struct dcp_hfi *hfi,
					 u32 session_id, u32 event_cmd)
{
	return dcp_hfi_send_no_wait_atomic_flags(hfi,
			HFI_COMMAND_DISPLAY_EVENT_REGISTER,
			HFI_CMD_BUFF_DISPLAY, session_id, HFI_PAYLOAD_U32,
			&event_cmd, sizeof(event_cmd), HFI_TX_FLAGS_NON_DISCARDABLE);
}

int dcp_hfi_display_event_deregister_async(struct dcp_hfi *hfi,
					   u32 session_id, u32 event_cmd)
{
	return dcp_hfi_send_no_wait_atomic_flags(hfi,
			HFI_COMMAND_DISPLAY_EVENT_DEREGISTER,
			HFI_CMD_BUFF_DISPLAY, session_id, HFI_PAYLOAD_U32,
			&event_cmd, sizeof(event_cmd), HFI_TX_FLAGS_NON_DISCARDABLE);
}

int dcp_hfi_crtc_set_resource(struct dcp_hfi *hfi, u32 session_id,
			      u32 lm_idx, u32 width, u32 height,
			      u32 roi_x, u32 roi_y)
{
	u32 payload[7];

	payload[0] = 1;
	HFI_PACK(&payload[1], HFI_PROPERTY_DISPLAY_SET_RESOURCE_DATA,
		 HFI_RESOURCE_LM, lm_idx, width, height,
		 (roi_x << 16) | (roi_y & 0xFFFF));

	return dcp_hfi_send_no_wait_flags(hfi,
				       HFI_COMMAND_DISPLAY_SET_PROPERTY,
				       HFI_CMD_BUFF_DISPLAY,
				       session_id,
				       HFI_PAYLOAD_U32_ARRAY,
				       payload, sizeof(payload),
				       HFI_TX_FLAGS_NON_DISCARDABLE);
}

int dcp_hfi_crtc_set_dest_roi(struct dcp_hfi *hfi, u32 session_id,
			      u32 x, u32 y, u32 width, u32 height)
{
	u32 payload[9];

	payload[0] = 1;
	HFI_PACK(&payload[1], HFI_PROPERTY_DISPLAY_DEST_ROI,
		 session_id, PANEL_ROI, 1, x, y, width, height);

	return dcp_hfi_send_no_wait_flags(hfi,
				       HFI_COMMAND_DISPLAY_SET_PROPERTY,
				       HFI_CMD_BUFF_DISPLAY, session_id,
				       HFI_PAYLOAD_U32_ARRAY, payload,
				       sizeof(payload), HFI_TX_FLAGS_NON_DISCARDABLE);
}

int dcp_hfi_crtc_set_scan_sequence_id(struct dcp_hfi *hfi, u32 session_id,
				      u32 seq_id)
{
	u32 payload[3];

	payload[0] = 1;
	payload[1] = HFI_PACK_KEY(HFI_PROPERTY_DISPLAY_SCAN_SEQUENCE_ID, 0, 1);
	payload[2] = seq_id;

	return dcp_hfi_send_no_wait(hfi,
				      HFI_COMMAND_DISPLAY_SET_PROPERTY,
				      HFI_CMD_BUFF_DISPLAY,
				      session_id,
				      HFI_PAYLOAD_U32_ARRAY,
				      payload, sizeof(payload));
}

int dcp_hfi_crtc_set_power_mode(struct dcp_hfi *hfi, u32 session_id,
				enum hfi_display_power_mode mode)
{
	u32 payload[3];

	payload[0] = 1;
	payload[1] = HFI_PACK_KEY(HFI_PROPERTY_DISPLAY_POWER_MODE, 0, 1);
	payload[2] = (u32)mode;

	return dcp_hfi_send_no_wait(hfi,
				      HFI_COMMAND_DISPLAY_SET_PROPERTY,
				      HFI_CMD_BUFF_DISPLAY,
				      session_id,
				      HFI_PAYLOAD_U32_ARRAY,
				      payload, sizeof(payload));
}

int dcp_hfi_crtc_set_autorefresh(struct dcp_hfi *hfi, u32 session_id,
				 bool enable, u32 frame_count)
{
	u32 payload[4];

	payload[0] = 1;
	payload[1] = HFI_PACK_KEY(HFI_PROPERTY_DISPLAY_AUTOREFRESH_CFG, 0, 2);
	payload[2] = enable ? 1 : 0;
	payload[3] = frame_count;

	return dcp_hfi_send_sync(hfi,
				 HFI_COMMAND_DISPLAY_SET_PROPERTY,
				 HFI_CMD_BUFF_DISPLAY,
				 session_id,
				 HFI_PAYLOAD_U32_ARRAY,
				 payload, sizeof(payload),
				 HFI_TX_FLAGS_RESPONSE_REQUIRED |
				 HFI_TX_FLAGS_NON_DISCARDABLE,
				 NULL, 0,
				 DCP_HFI_TIMEOUT_DISPLAY_MS);
}

int dcp_hfi_crtc_set_bw(struct dcp_hfi *hfi, u32 session_id,
			u64 dram_ib, u64 dram_ab,
			u64 llcc_ib, u64 llcc_ab,
			u64 core_clk)
{
	u32 payload[16];
	u32 *p = payload;

	*p++ = 5;

#define PACK_U64_PROP(prop_id, val) \
	do { \
		typeof(val) __val = (val); \
		*p++ = HFI_PACK_KEY((prop_id), 0, 2); \
		*p++ = lower_32_bits(__val); \
		*p++ = upper_32_bits(__val); \
	} while (0)

	PACK_U64_PROP(HFI_PROPERTY_DISPLAY_DRAM_IB,  dram_ib);
	PACK_U64_PROP(HFI_PROPERTY_DISPLAY_DRAM_AB,  dram_ab);
	PACK_U64_PROP(HFI_PROPERTY_DISPLAY_LLCC_IB,  llcc_ib);
	PACK_U64_PROP(HFI_PROPERTY_DISPLAY_LLCC_AB,  llcc_ab);
	PACK_U64_PROP(HFI_PROPERTY_DISPLAY_CORE_CLK, core_clk);

#undef PACK_U64_PROP

	return dcp_hfi_send_no_wait(hfi,
				      HFI_COMMAND_DISPLAY_SET_PROPERTY,
				      HFI_CMD_BUFF_DISPLAY,
				      session_id,
				      HFI_PAYLOAD_U32_ARRAY,
				      payload,
				      (u32)((p - payload) * sizeof(u32)));
}
