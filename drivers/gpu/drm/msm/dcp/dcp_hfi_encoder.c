// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * HFI commands for display commits and encoder properties.
 */

#include <linux/device.h>

#include "dcp_hfi_priv.h"
#include "dcp_hfi.h"
#include "hfi/hfi_commands_display.h"
#include "hfi/hfi_defs_common.h"
#include "hfi/hfi_properties_display.h"
#include "hfi/hfi_packet.h"

int dcp_hfi_display_commit(struct dcp_hfi *hfi, u32 session_id)
{
	u32 commit_flag = HFI_COMMIT;
	int ret;

	ret = dcp_hfi_send_sync(hfi,
				HFI_COMMAND_DISPLAY_FRAME_TRIGGER,
				HFI_CMD_BUFF_DISPLAY,
				session_id,
				HFI_PAYLOAD_U32,
				&commit_flag, sizeof(commit_flag),
				HFI_TX_FLAGS_RESPONSE_REQUIRED |
				HFI_TX_FLAGS_NON_DISCARDABLE,
				NULL, 0,
				DCP_HFI_TIMEOUT_COMMIT_MS);
	if (ret)
		dev_err(hfi->dev, "HFI FRAME_TRIGGER(COMMIT) failed: %d\n",
			ret);

	return ret;
}

int dcp_hfi_set_display_property(struct dcp_hfi *hfi, u32 session_id,
				 u32 prop_id, u32 value)
{
	u32 payload[3];
	int ret;

	payload[0] = 1;
	payload[1] = HFI_PACK_KEY(prop_id, 0, 1);
	payload[2] = value;

	ret = dcp_hfi_send_sync(hfi,
				HFI_COMMAND_DISPLAY_SET_PROPERTY,
				HFI_CMD_BUFF_DISPLAY,
				session_id,
				HFI_PAYLOAD_U32_ARRAY,
				payload, sizeof(payload),
				HFI_TX_FLAGS_RESPONSE_REQUIRED |
				HFI_TX_FLAGS_NON_DISCARDABLE,
				NULL, 0,
				DCP_HFI_TIMEOUT_DISPLAY_MS);
	if (ret)
		dev_err(hfi->dev,
			"HFI SET_PROPERTY prop=0x%x failed: %d\n",
			prop_id, ret);

	return ret;
}
