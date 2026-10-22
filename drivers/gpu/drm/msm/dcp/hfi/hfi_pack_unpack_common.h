/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_PACK_UNPACK_COMMON_H__
#define __H_HFI_PACK_UNPACK_COMMON_H__

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include "hfi_packet.h"

/* Header size field width. */
#define HFI_HEADER_SIZE_MAX                                   0x00ffffff
/* Packet size field width. */
#define HFI_PACKET_SIZE_MAX                                   0x000fffff

#define HFI_HEADER_CMD_BUFF_TYPE_START_BIT                                   24
#define HFI_PACKET_PAYLOAD_TYPE_START_BIT                                21

#define GET_HEADER_SIZE(data)           \
	((data) & ((1 << HFI_HEADER_CMD_BUFF_TYPE_START_BIT) - 1))
#define GET_HEADER_CMD_BUFF_TYPE(data)         \
	((data) >> HFI_HEADER_CMD_BUFF_TYPE_START_BIT)
#define GET_PACKET_SIZE(data)           \
	((data) & ((1 << HFI_PACKET_PAYLOAD_TYPE_START_BIT) - 1))
#define GET_PACKET_PAYLOAD_TYPE(data)   \
	((data) >> HFI_PACKET_PAYLOAD_TYPE_START_BIT)

static inline struct hfi_packet *hfi_packet_first(struct hfi_header *hdr)
{
	return (struct hfi_packet *)((u8 *)hdr + sizeof(*hdr));
}

static inline struct hfi_packet *hfi_packet_append(struct hfi_header *hdr)
{
	return (struct hfi_packet *)((u8 *)hdr +
		GET_HEADER_SIZE(hdr->cmd_buff_info));
}

static inline struct hfi_packet *hfi_packet_next(struct hfi_packet *packet)
{
	return (struct hfi_packet *)((u8 *)packet +
		GET_PACKET_SIZE(packet->payload_info));
}

/* Shared command-buffer handle. */
struct hfi_cmd_buff_hdl {
	void *cmd_buffer;
	u32 size;
};

/* Packed HFI property key/value entry. */
struct hfi_kv_info {
	u32 key;
	void *value_ptr;
};

/* HFI header fields. */
struct hfi_header_info {
	u32 num_packets;
	enum hfi_cmd_buff_type cmd_buff_type;
	u32 object_id;
	u32 header_id;
};

/* HFI packet fields. */
struct hfi_packet_info {
	u32 cmd;
	u32 id;
	u32 flags;
	u32 packet_id;
	enum hfi_packet_payload_type payload_type;
	u32 payload_size;
	void *payload_ptr;
};

#endif /* __H_HFI_PACK_UNPACK_COMMON_H__ */
