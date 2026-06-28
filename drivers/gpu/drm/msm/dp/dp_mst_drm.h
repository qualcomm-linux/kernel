/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DP_MST_DRM_H_
#define _DP_MST_DRM_H_

#include "dp_display.h"

int msm_dp_mst_init(struct msm_dp *dp_display, u32 max_streams, struct drm_dp_aux *drm_aux);
int msm_dp_mst_display_set_mgr_state(struct msm_dp *dp_display, bool state);
void msm_dp_mst_display_hpd_irq(struct msm_dp *dp_display);

#endif /* _DP_MST_DRM_H_ */
