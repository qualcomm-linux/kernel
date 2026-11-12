/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP encoder interface between the CRTC and bridge chain.
 * Hardware programming is delegated to DCP firmware.
 */

#ifndef __DCP_ENCODER_H__
#define __DCP_ENCODER_H__

#include <drm/drm_encoder.h>

struct drm_encoder *dcp_encoder_init(struct drm_device *dev, int enc_type);

#endif /* __DCP_ENCODER_H__ */
