/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP plane state definitions.
 */

#ifndef __DCP_PLANE_STATE_H__
#define __DCP_PLANE_STATE_H__

#include <drm/drm_plane.h>

#define DCP_PLANE_PROP_PKT_MAX_SIZE	512

struct dcp_plane_state {
	struct drm_plane_state base;

	u64 fetch_bw;
	u64 clk;

	u8     prop_pkt[DCP_PLANE_PROP_PKT_MAX_SIZE];
	size_t prop_pkt_size;
	u32 prop_pkt_id;

	bool needs_dirtyfb;

	u32 src_img_w;
	u32 src_img_h;

	u32 hfi_pipe_id;
	u32 hfi_format;
};

#define to_dcp_plane_state(x) \
	container_of(x, struct dcp_plane_state, base)

#endif /* __DCP_PLANE_STATE_H__ */
