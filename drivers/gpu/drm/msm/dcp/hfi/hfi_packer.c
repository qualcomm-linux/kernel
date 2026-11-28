// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hfi_packer.h"

static int hfi_validate_cmd_buff_size(struct hfi_header *hdr,
				      u32 allocated_size, u32 bytes_to_add)
{
	u32 bytes_used;

	if (allocated_size < sizeof(struct hfi_header))
		return -EINVAL;

	bytes_used = GET_HEADER_SIZE(hdr->cmd_buff_info);
	if (bytes_used < sizeof(*hdr) || bytes_used > allocated_size ||
	    bytes_to_add > allocated_size - bytes_used ||
	    bytes_used + bytes_to_add > HFI_HEADER_SIZE_MAX)
		return -EINVAL;

	return 0;
}

u32 hfi_get_header_size(struct hfi_cmd_buff_hdl *cmd_buf_hdl)
{
	struct hfi_header *hdr = cmd_buf_hdl->cmd_buffer;

	return GET_HEADER_SIZE(hdr->cmd_buff_info);
}

int hfi_create_header(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
		      struct hfi_header_info *header_info)
{
	struct hfi_header *hdr;

	if (!cmd_buf_hdl || !cmd_buf_hdl->cmd_buffer || !header_info)
		return -EINVAL;

	hdr = cmd_buf_hdl->cmd_buffer;
	if (cmd_buf_hdl->size < sizeof(*hdr))
		return -EINVAL;

	memset(hdr, 0, sizeof(*hdr));
	hdr->cmd_buff_info = (header_info->cmd_buff_type <<
		HFI_HEADER_CMD_BUFF_TYPE_START_BIT) | sizeof(*hdr);
	hdr->object_id = header_info->object_id;
	hdr->header_id = header_info->header_id;

	return 0;
}

static int hfi_create_packet(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
			     struct hfi_packet_info *packet_info,
			     const void *payload, u32 payload_size)
{
	struct hfi_header *hdr;
	struct hfi_packet *pkt_hdr;
	u32 pkt_size;
	int ret;

	if (!cmd_buf_hdl || !cmd_buf_hdl->cmd_buffer || !packet_info ||
	    (payload_size && !payload))
		return -EINVAL;

	if (payload_size > HFI_PACKET_SIZE_MAX - sizeof(struct hfi_packet))
		return -EINVAL;

	hdr = cmd_buf_hdl->cmd_buffer;
	pkt_size = sizeof(*pkt_hdr) + payload_size;
	ret = hfi_validate_cmd_buff_size(hdr, cmd_buf_hdl->size, pkt_size);
	if (ret)
		return ret;

	pkt_hdr = hfi_packet_append(hdr);
	memset(pkt_hdr, 0, sizeof(*pkt_hdr));
	pkt_hdr->payload_info = (packet_info->payload_type <<
		HFI_PACKET_PAYLOAD_TYPE_START_BIT) | pkt_size;
	pkt_hdr->cmd = packet_info->cmd;
	pkt_hdr->flags = packet_info->flags;
	pkt_hdr->id = packet_info->id;
	pkt_hdr->packet_id = packet_info->packet_id;
	if (payload_size)
		memcpy(pkt_hdr + 1, payload, payload_size);

	hdr->cmd_buff_info += pkt_size;
	hdr->num_packets++;

	return 0;
}

int hfi_create_full_packet(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
			   struct hfi_packet_info *packet_info)
{
	if (!packet_info)
		return -EINVAL;

	return hfi_create_packet(cmd_buf_hdl, packet_info,
		packet_info->payload_ptr, packet_info->payload_size);
}

int hfi_create_packet_header(struct hfi_cmd_buff_hdl *cmd_buf_hdl,
			     struct hfi_packet_info *packet_info)
{
	return hfi_create_packet(cmd_buf_hdl, packet_info, NULL, 0);
}
