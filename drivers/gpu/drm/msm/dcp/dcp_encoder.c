// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP encoder wrapper for the CRTC-to-bridge connection.
 * Hardware programming and mode sequencing are handled by the bridge and HFI.
 */

#include <drm/drm_encoder.h>
#include <drm/drm_managed.h>

#include "dcp_encoder.h"

struct drm_encoder *dcp_encoder_init(struct drm_device *dev, int enc_type)
{
	struct drm_encoder *encoder;
	int ret;

	encoder = drmm_kzalloc(dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return ERR_PTR(-ENOMEM);

	ret = drmm_encoder_init(dev, encoder, NULL, enc_type, "encoder%d",
				dev->mode_config.num_encoder);
	if (ret)
		return ERR_PTR(ret);

	return encoder;
}
