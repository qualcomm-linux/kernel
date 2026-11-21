// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * HFI commands for plane properties and layer detach.
 */

#include <linux/device.h>

#include "dcp_hfi.h"
#include "dcp_hfi_priv.h"

int dcp_hfi_submit_prop_pkt(struct dcp_hfi *hfi,
			    const void *pkt, size_t pkt_size)
{
	struct dcp_hfi_buf *buf;

	if (!hfi || !pkt || !pkt_size)
		return -EINVAL;

	buf = dcp_hfi_buf_alloc(hfi);
	if (!buf) {
		dev_err(hfi->dev, "HFI prop pkt: TX buffer pool exhausted\n");
		return -ENOMEM;
	}

	if (pkt_size > DCP_HFI_BUF_SIZE) {
		dev_err(hfi->dev, "HFI prop pkt too large: %zu > %d\n",
			pkt_size, DCP_HFI_BUF_SIZE);
		dcp_hfi_buf_free(hfi, buf);
		return -EINVAL;
	}

	memcpy(buf->data, pkt, pkt_size);

	return dcp_hfi_send_cmd_async(hfi, buf, pkt_size, NULL);
}

int dcp_hfi_plane_disable(struct dcp_hfi *hfi, u32 session_id, u32 pipe_idx)
{
	u32 payload[3];
	int ret;

	payload[0] = 1;
	payload[1] = HFI_PACK_KEY(HFI_PROPERTY_DISPLAY_DETACH_LAYER, 0, 1);
	payload[2] = pipe_idx;

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
		dev_warn(hfi->dev,
			 "HFI DETACH_LAYER pipe=%u failed: %d\n",
			 pipe_idx, ret);
	return ret;
}
