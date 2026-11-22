/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_PACKET_H__
#define __H_HFI_PACKET_H__

#include "hfi_defs_common.h"

/* HFI header: size [23:0], command-buffer type [31:24]. */
struct hfi_header {
	u32 cmd_buff_info;
	u16 device_id;
	u16 object_id;
	u32 timestamp_hi;
	u32 timestamp_lo;
	u32 header_id;
	u32 reserved[2];
	u32 num_packets;
};

/* HFI packet: size [20:0], payload type [31:21]. */
struct hfi_packet {
	u32 payload_info;
	u32 cmd;
	u32 flags;
	u32 id;
	u32 packet_id;
	u32 reserved[3];
};

/* Command-buffer flags stored in object_id[15:8]. */
enum hfi_cmd_buff_flag {
	HFI_CMD_BUFF_FLAG_NONE    = 0x00,
	HFI_CMD_BUFF_FLAG_ASYNC   = 0x01,
	HFI_CMD_BUFF_FLAG_NO_WAIT = 0x02,
};

/* Packet payload encodings. */
enum hfi_packet_payload_type {
	HFI_PAYLOAD_NONE                = 0x0,
	HFI_PAYLOAD_U32                 = 0x1,
	HFI_PAYLOAD_U32_ARRAY           = 0x2,
	HFI_PAYLOAD_U64                 = 0x3,
	HFI_PAYLOAD_U64_ARRAY           = 0x4,
	HFI_PAYLOAD_BLOB                = 0x5,
};

/* Command-buffer groups encoded in command headers. */
enum hfi_cmd_buff_type {
	HFI_CMD_BUFF_SYSTEM          = 0x1,
	HFI_CMD_BUFF_DEVICE          = 0x2,
	HFI_CMD_BUFF_DISPLAY         = 0x3,
	HFI_CMD_BUFF_DEBUG           = 0x4,
	HFI_CMD_BUFF_VIRTUALIZATION  = 0x5,
};

/* Host-to-firmware packet flags. */
enum hfi_packet_tx_flags {
	HFI_TX_FLAGS_NONE              = 0x0,
	HFI_TX_FLAGS_INTR_REQUIRED     = 0x1,
	HFI_TX_FLAGS_RESPONSE_REQUIRED = 0x2,
	HFI_TX_FLAGS_NON_DISCARDABLE   = 0x4,
};

/* Firmware-to-host packet flags. */
enum hfi_packet_rx_flags {
	HFI_RX_FLAGS_NONE               = 0x0,
	HFI_RX_FLAGS_SUCCESS            = 0x1,
	HFI_RX_FLAGS_INFORMATION        = 0x2,
	HFI_RX_FLAGS_DEVICE_ERROR       = 0x4,
	HFI_RX_FLAGS_SYSTEM_ERROR       = 0x8,
};

#endif /* __H_HFI_PACKET_H__ */
