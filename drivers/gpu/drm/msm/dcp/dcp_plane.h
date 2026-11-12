/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP plane implementation and API.
 */

#ifndef __DCP_PLANE_H__
#define __DCP_PLANE_H__

#include "dcp_plane_state.h"

struct dcp_dpu_kms;
struct drm_display_mode;

struct dcp_plane_perf {
	u64 fetch_bw;
	u64 clk;
};

struct dcp_plane {
	struct drm_plane base;
	u32 hfi_pipe_id;
};

#define to_dcp_plane(x) container_of(x, struct dcp_plane, base)

void dcp_plane_calc_perf(const struct drm_plane_state *state,
			 const struct drm_display_mode *mode,
			 struct dcp_plane_perf *perf);
struct drm_plane *dcp_plane_init(struct drm_device *dev,
				 u32 hfi_pipe_id,
				 enum drm_plane_type type,
				 unsigned long possible_crtcs);

#endif /* __DCP_PLANE_H__ */
