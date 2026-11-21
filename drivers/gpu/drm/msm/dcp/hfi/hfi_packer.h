/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_PACKER_H__
#define __H_HFI_PACKER_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <linux/kernel.h>
#include <linux/string.h>

#include "hfi_pack_unpack_common.h"

/* Pack a property ID, version, and size into one key. */
#define HFI_PACK_KEY(property_id, version, dsize) \
	((property_id) | ((version) << 20) | ((dsize) << 24))

/* Pack a version-zero property and return its serialized word count. */
#define HFI_PACK(payload, property_id, ...) \
({ \
	const u32 __hfi_values[] = { __VA_ARGS__ }; \
	u32 *__hfi_payload = (payload); \
	u32 __hfi_count = ARRAY_SIZE(__hfi_values); \
\
	*__hfi_payload++ = HFI_PACK_KEY(property_id, 0, __hfi_count); \
	memcpy(__hfi_payload, __hfi_values, sizeof(__hfi_values)); \
	__hfi_count + 1; \
})

/* Pack a property with an explicitly selected protocol version. */
#define HFI_PACK_VERSION(payload, property_id, version, ...) \
({ \
	const u32 __hfi_values[] = { __VA_ARGS__ }; \
	u32 *__hfi_payload = (payload); \
	u32 __hfi_count = ARRAY_SIZE(__hfi_values); \
\
	*__hfi_payload++ = HFI_PACK_KEY(property_id, version, __hfi_count); \
	memcpy(__hfi_payload, __hfi_values, sizeof(__hfi_values)); \
	__hfi_count + 1; \
})

/** hfi_get_header_size() - Return the bytes used by an HFI header. */
u32 hfi_get_header_size(struct hfi_cmd_buff_hdl *cmd_buf_hdl);

/** hfi_create_header() - Initialize an HFI command header. */
int hfi_create_header(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
		      struct hfi_header_info *header_info);

/** hfi_create_full_packet() - Append a packet and its payload. */
int hfi_create_full_packet(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
			   struct hfi_packet_info *packet_info);

/** hfi_create_packet_header() - Append a packet without a payload. */
int hfi_create_packet_header(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
			     struct hfi_packet_info *packet_info);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __H_HFI_PACKER_H__ */
