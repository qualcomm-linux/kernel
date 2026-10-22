// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/errno.h>
#include <linux/types.h>

#include "hfi_utils.h"

int hfi_util_kv_parser_init(struct hfi_util_kv_parser *kv_parser,
			    u32 bytes, u32 *payload)
{
	if (!kv_parser || !payload || bytes > HFI_UTIL_MAX_ALLOC)
		return -EINVAL;

	if (bytes % sizeof(u32))
		return -EINVAL;

	kv_parser->max_index  = bytes / sizeof(u32);
	kv_parser->cur_offset = 0;
	kv_parser->payload    = payload;

	return 0;
}

int hfi_util_kv_parser_get_next(struct hfi_util_kv_parser *kv_parser,
				u32 move, u32 *hfi_prop,
				u32 **payload, u32 *max_words)
{
	if (!kv_parser || !payload || !hfi_prop || !max_words)
		return -EINVAL;

	if (kv_parser->cur_offset > kv_parser->max_index ||
	    move > kv_parser->max_index - kv_parser->cur_offset)
		return -EINVAL;

	kv_parser->cur_offset += move;
	*hfi_prop  = 0;
	*payload   = NULL;
	*max_words = 0;

	/* End of payload is a valid result. */
	if (kv_parser->cur_offset == kv_parser->max_index)
		return 0;

	if (kv_parser->cur_offset > kv_parser->max_index)
		return -EINVAL;

	if ((kv_parser->max_index - kv_parser->cur_offset) < 2)
		return -EINVAL;

	*hfi_prop  = kv_parser->payload[kv_parser->cur_offset++];
	*payload   = &kv_parser->payload[kv_parser->cur_offset];
	*max_words = kv_parser->max_index - kv_parser->cur_offset;
	if (HFI_PROP_SZ(*hfi_prop) && HFI_PROP_SZ(*hfi_prop) > *max_words)
		return -EINVAL;
	if (HFI_PROP_SZ(*hfi_prop))
		*max_words = HFI_PROP_SZ(*hfi_prop);

	return 0;
}
