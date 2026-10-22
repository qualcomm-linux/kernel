// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * DCP KMS driver.
 */

#include <drm/drm_crtc.h>

#include "dcp_dpu_kms.h"
#include "dcp_dsi_bridge.h"

int dcp_dpu_kms_prepare_crtc_session(struct dcp_dpu_kms *dcp_kms,
				     struct drm_crtc *crtc)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(dcp_kms->dsi_bridge); i++) {
		struct dcp_dsi_bridge *bridge = dcp_kms->dsi_bridge[i];

		if (!bridge)
			continue;
		if (dcp_dpu_kms_get_crtc(dcp_kms, bridge->fw_display_id) != crtc)
			continue;

		return dcp_dsi_bridge_prepare_session(bridge,
						      &crtc->state->adjusted_mode);
	}

	return -ENODEV;
}

struct drm_crtc *dcp_dpu_kms_get_crtc(struct dcp_dpu_kms *dcp_kms,
				      u32 display_id)
{
	unsigned int i;

	for (i = 0; i < dcp_kms->num_crtcs; i++)
		if (dcp_kms->crtcs[i].display_id == display_id)
			return dcp_kms->crtcs[i].crtc;

	return NULL;
}

bool dcp_dpu_kms_needs_dirtyfb(struct dcp_dpu_kms *dcp_kms,
			       struct drm_crtc *crtc)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(dcp_kms->dsi_bridge); i++) {
		struct dcp_dsi_bridge *bridge = dcp_kms->dsi_bridge[i];

		if (!bridge ||
		    dcp_dpu_kms_get_crtc(dcp_kms, bridge->fw_display_id) != crtc)
			continue;

		return bridge->dsi_device &&
			!(bridge->dsi_device->mode_flags & MIPI_DSI_MODE_VIDEO);
	}

	return false;
}
