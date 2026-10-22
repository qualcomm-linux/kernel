// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hfi_unpacker.h"

static int hfi_sanitize_cmd_buff(struct hfi_header *hdr)
{
	struct hfi_packet *packet;
	u32 packet_size, total_pkt_size = sizeof(*hdr);
	u32 hdr_size = GET_HEADER_SIZE(hdr->cmd_buff_info);
	int i;

	if (hdr_size < sizeof(*hdr) + sizeof(*packet) || !hdr->num_packets)
		return -EINVAL;

	packet = hfi_packet_first(hdr);
	for (i = 0; i < hdr->num_packets; i++) {
		if (total_pkt_size > hdr_size - sizeof(*packet))
			return -EINVAL;

		packet_size = GET_PACKET_SIZE(packet->payload_info);
		if (packet_size < sizeof(*packet) ||
		    packet_size > hdr_size - total_pkt_size)
			return -EINVAL;

		total_pkt_size += packet_size;
		packet = hfi_packet_next(packet);
	}

	return hdr_size == total_pkt_size ? 0 : -EINVAL;
}

static struct hfi_packet *hfi_packet_at(struct hfi_header *hdr, u32 packet_num)
{
	struct hfi_packet *packet = hfi_packet_first(hdr);

	while (--packet_num)
		packet = hfi_packet_next(packet);

	return packet;
}

int hfi_unpacker_get_header_info(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
				 struct hfi_header_info *header_info)
{
	struct hfi_header *hdr;
	u32 hdr_size;

	if (!cmd_buf_hdl || !cmd_buf_hdl->cmd_buffer || !header_info)
		return -EINVAL;

	hdr = cmd_buf_hdl->cmd_buffer;
	hdr_size = GET_HEADER_SIZE(hdr->cmd_buff_info);
	if (hdr_size < sizeof(*hdr) || hdr_size > cmd_buf_hdl->size)
		return -EINVAL;

	header_info->num_packets = hdr->num_packets;
	header_info->cmd_buff_type = GET_HEADER_CMD_BUFF_TYPE(hdr->cmd_buff_info);
	header_info->object_id = hdr->object_id;
	header_info->header_id = hdr->header_id;

	return 0;
}

int hfi_unpacker_get_packet_info(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
				 u32 packet_num,
				 struct hfi_packet_info *packet_info)
{
	struct hfi_header *hdr;
	struct hfi_packet *packet;
	u32 packet_size;

	if (!cmd_buf_hdl || !cmd_buf_hdl->cmd_buffer || !packet_info ||
	    !packet_num)
		return -EINVAL;

	hdr = cmd_buf_hdl->cmd_buffer;
	if (GET_HEADER_SIZE(hdr->cmd_buff_info) > cmd_buf_hdl->size ||
	    packet_num > hdr->num_packets || hfi_sanitize_cmd_buff(hdr))
		return -EINVAL;

	packet = hfi_packet_at(hdr, packet_num);
	packet_size = GET_PACKET_SIZE(packet->payload_info);
	packet_info->cmd = packet->cmd;
	packet_info->flags = packet->flags;
	packet_info->id = packet->id;
	packet_info->packet_id = packet->packet_id;
	packet_info->payload_type = GET_PACKET_PAYLOAD_TYPE(packet->payload_info);
	packet_info->payload_size = packet_size - sizeof(*packet);
	packet_info->payload_ptr = packet_info->payload_size ?
		(void *)(packet + 1) : NULL;

	return 0;
}

int hfi_unpacker_get_kv_pairs_count(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
				    struct hfi_packet_info *packet_info,
				    u32 kv_payload_offset, u32 *num_props)
{
	if (!cmd_buf_hdl || !cmd_buf_hdl->cmd_buffer || !packet_info ||
	    !packet_info->payload_ptr || !num_props ||
	    kv_payload_offset > packet_info->payload_size ||
	    !IS_ALIGNED(kv_payload_offset, sizeof(u32)) ||
	    packet_info->payload_size - kv_payload_offset < sizeof(u32))
		return -EINVAL;

	*num_props = *(u32 *)((u8 *)packet_info->payload_ptr + kv_payload_offset);

	return 0;
}

int hfi_unpacker_get_kv_pairs(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
			      struct hfi_packet_info *packet_info,
			      u32 kv_payload_offset, u32 num_props,
			      struct hfi_kv_info *kv_pairs)
{
	u32 *kv_pairs_counter_idx, *kv_pairs_key;
	u32 value_size, consumed;
	int i;

	if (!cmd_buf_hdl || !cmd_buf_hdl->cmd_buffer || !packet_info ||
	    !packet_info->payload_ptr || !kv_pairs ||
	    kv_payload_offset > packet_info->payload_size ||
	    !IS_ALIGNED(kv_payload_offset, sizeof(u32)) ||
	    packet_info->payload_size - kv_payload_offset < sizeof(u32))
		return -EINVAL;

	kv_pairs_counter_idx = (u32 *)((u8 *)packet_info->payload_ptr +
		kv_payload_offset);
	if (*kv_pairs_counter_idx != num_props)
		return -EINVAL;

	consumed = sizeof(u32);
	for (i = 0; i < num_props; i++) {
		u32 remaining;

		if (consumed > packet_info->payload_size - kv_payload_offset ||
		    packet_info->payload_size - kv_payload_offset - consumed <
		    sizeof(u32))
			return -EINVAL;

		kv_pairs_key = (u32 *)((u8 *)kv_pairs_counter_idx + consumed);
		value_size = (*kv_pairs_key & 0xff000000) >> 24;
		remaining = packet_info->payload_size - kv_payload_offset -
			consumed - sizeof(u32);
		if (value_size > remaining / sizeof(u32))
			return -EINVAL;

		kv_pairs[i].key = *kv_pairs_key;
		kv_pairs[i].value_ptr = kv_pairs_key + 1;
		consumed += sizeof(u32) * (1 + value_size);
	}

	return 0;
}
