// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * DCP KMS driver.
 */

#include <drm/drm_crtc.h>

#include "dcp_dpu_kms.h"

struct drm_crtc *dcp_dpu_kms_get_crtc(struct dcp_dpu_kms *dcp_kms,
				      u32 display_id)
{
	unsigned int i;

	for (i = 0; i < dcp_kms->num_crtcs; i++)
		if (dcp_kms->crtcs[i].display_id == display_id)
			return dcp_kms->crtcs[i].crtc;

	return NULL;
}
