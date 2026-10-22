/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_UNPACKER_H__
#define __H_HFI_UNPACKER_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "hfi_pack_unpack_common.h"

/* Extract the property ID, version, and value size from a key. */
#define HFI_UNPACK_KEY(key, property_id, version, size) \
	do { \
		typeof(key) __key = (key); \
		(property_id) = __key & 0x000fffff; \
		(version) = (__key & 0x00f00000) >> 20; \
		(size) = (__key & 0xff000000) >> 24; \
	} while (0)

/**
 * hfi_unpacker_get_header_info() - Parse the command-buffer header.
 * @cmd_buf_hdl: Command buffer to inspect.
 * @header_info: Receives the decoded header information.
 *
 * Return: 0 on success, or a negative errno.
 */
int hfi_unpacker_get_header_info(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
				 struct hfi_header_info *header_info);

/**
 * hfi_unpacker_get_packet_info() - Parse one packet in a command buffer.
 * @cmd_buf_hdl: Command buffer to inspect.
 * @packet_num: One-based packet index.
 * @packet_info: Receives the decoded packet information.
 *
 * Return: 0 on success, or a negative errno.
 */
int hfi_unpacker_get_packet_info(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
				 u32 packet_num, struct hfi_packet_info *packet_info);

/**
 * hfi_unpacker_get_kv_pairs_count() - Count properties in a packet payload.
 * @cmd_buf_hdl: Command buffer to inspect.
 * @packet_info: Packet information returned by hfi_unpacker_get_packet_info().
 * @kv_payload_offset: Byte offset where key/value pairs begin.
 * @num_props: Receives the property count.
 *
 * Return: 0 on success, or a negative errno.
 */
int hfi_unpacker_get_kv_pairs_count(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
				    struct hfi_packet_info *packet_info,
				    u32 kv_payload_offset, u32 *num_props);

/**
 * hfi_unpacker_get_kv_pairs() - Decode properties from a packet payload.
 * @cmd_buf_hdl: Command buffer to inspect.
 * @packet_info: Packet information returned by hfi_unpacker_get_packet_info().
 * @kv_payload_offset: Byte offset where key/value pairs begin.
 * @num_props: Number of properties to decode.
 * @kv_pairs: Receives the decoded property entries.
 *
 * Return: 0 on success, or a negative errno.
 */
int hfi_unpacker_get_kv_pairs(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
			      struct hfi_packet_info *packet_info,
			      u32 kv_payload_offset, u32 num_props,
			      struct hfi_kv_info *kv_pairs);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __H_HFI_UNPACKER_H__ */
