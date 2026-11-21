/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Private HFI helpers; external users should include dcp_hfi.h.
 */

#ifndef __DCP_HFI_PRIV_H__
#define __DCP_HFI_PRIV_H__

#include "dcp_hfi.h"

#define DCP_HFI_CMD_POOL_SIZE					64
#define DCP_HFI_RESP_POOL_SIZE					64
#define DCP_HFI_BUF_SIZE						4096

#define DCP_HFI_TIMEOUT_INIT_MS					2000
#define DCP_HFI_TIMEOUT_RESOURCE_REGISTER_MS	5000
#define DCP_HFI_TIMEOUT_CAPS_MS					1000
#define DCP_HFI_TIMEOUT_DISPLAY_MS				1000
#define DCP_HFI_TIMEOUT_MODE_MS					3000
#define DCP_HFI_TIMEOUT_COMMIT_MS				2000
#define DCP_HFI_TIMEOUT_DISPLAY_DCS_MS			10000

struct dcp_hfi_buf *dcp_hfi_buf_alloc(struct dcp_hfi *hfi);
struct dcp_hfi_buf *dcp_hfi_buf_alloc_atomic(struct dcp_hfi *hfi);
void dcp_hfi_buf_free(struct dcp_hfi *hfi, struct dcp_hfi_buf *buf);

static inline void dcp_hfi_buf_init_sg(struct dcp_hfi_buf *buf, size_t len)
{
	sg_init_table(&buf->sg, 1);
	sg_dma_address(&buf->sg) = buf->dma_addr;
	sg_dma_len(&buf->sg) = len;
}

u32 dcp_hfi_next_packet_id(struct dcp_hfi *hfi);

int dcp_hfi_send_cmd_async(struct dcp_hfi *hfi,
			   struct dcp_hfi_buf *buf,
			   size_t cmd_len,
			   struct dcp_hfi_completion *comp);

int dcp_hfi_pending_add(struct dcp_hfi *hfi, struct dcp_hfi_completion *comp);

struct dcp_hfi_completion *dcp_hfi_pending_find(struct dcp_hfi *hfi,
						u32 packet_id);

#endif /* __DCP_HFI_PRIV_H__ */
