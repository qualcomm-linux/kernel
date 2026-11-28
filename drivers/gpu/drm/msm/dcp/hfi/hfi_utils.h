/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _HFI_UTILS_H_
#define _HFI_UTILS_H_

#include <linux/types.h>

#define HFI_UTIL_MAX_ALLOC   4096
#define HFI_PROP_ID(val)    ((val) & 0xfffff)
#define HFI_PROP_SZ(val)    ((val) >> 24)
#define HFI_VAL_L32(val)    ((val) & 0xffffffff)
#define HFI_VAL_H32(val)    ((val) >> 32)

/* Encode the major and minor feature versions in a property ID. */
#define HFI_UTIL_PACK_VERSION(major, minor, prop_id) \
	(((prop_id) & ~(0xf0f000)) | (((minor) & 0xf) << 20) | (((major) & 0xf) << 12))

/**
 * struct hfi_util_kv_parser - State for iterating key/value properties.
 * @max_index: Number of u32 words available in @payload.
 * @cur_offset: Current u32 offset.
 * @payload: Property payload buffer.
 */
struct hfi_util_kv_parser {
	u32 max_index;
	u32 cur_offset;
	u32 *payload;
};

/**
 * hfi_util_kv_parser_init() - Initialize a key/value parser.
 * @kv_parser: Parser state to initialize.
 * @bytes: Payload size in bytes; must be u32-aligned.
 * @payload: u32 payload buffer to parse.
 *
 * Return: 0 on success, or a negative errno.
 */
int hfi_util_kv_parser_init(struct hfi_util_kv_parser *kv_parser, u32 bytes, u32 *payload);

/**
 * hfi_util_kv_parser_get_next() - Read the next property from the payload.
 * @kv_parser: Parser state, updated in place.
 * @move: Number of u32 words occupied by the previous value.
 * @hfi_prop: Receives the packed property key, or zero at end of input.
 * @payload: Receives the property value pointer, or NULL at end of input.
 * @max_words: Receives the available value length in u32 words.
 *
 * Return: 0 on success or end of input, or a negative errno.
 */
int hfi_util_kv_parser_get_next(struct hfi_util_kv_parser *kv_parser, u32 move,
				u32 *hfi_prop, u32 **payload, u32 *max_words);

#endif /* _HFI_UTILS_H_ */
