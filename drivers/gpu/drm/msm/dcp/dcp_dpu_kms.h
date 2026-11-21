/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * DCP KMS state.
 */

#ifndef __DCP_DPU_KMS_H__
#define __DCP_DPU_KMS_H__

#include <linux/spinlock.h>

#include "msm_drv.h"
#include "msm_kms.h"

struct dcp_hfi;
struct dcp_caps;
struct dcp_dsi_bridge;
struct dcp_drv;
struct drm_crtc;

#define DCP_DPU_AUTOSUSPEND_DELAY_MS	5000
#define DCP_DPU_MAX_CRTCS	4
#define DCP_DPU_MAX_PLANES	12

struct dcp_dpu_kms {
	struct msm_kms		 base;
	struct drm_device	*dev;
	struct device		*pm_dev;
	struct dcp_drv		*drv;
	struct dcp_hfi		*hfi;
	struct dcp_caps		*caps;
	struct device_node	*dpus_np;
	struct device		*iommu_dev;
	bool			 rpm_enabled;
	bool			 commit_pm_active;
	struct dcp_dsi_bridge	*dsi_bridge[2];
	struct {
		struct drm_crtc *crtc;
		u32 display_id;
	} crtcs[DCP_DPU_MAX_CRTCS];
	unsigned int num_crtcs;
};

#define to_dcp_dpu_kms(x) container_of(x, struct dcp_dpu_kms, base)
struct drm_crtc *dcp_dpu_kms_get_crtc(struct dcp_dpu_kms *dcp_kms,
				      u32 display_id);
bool dcp_dpu_kms_needs_dirtyfb(struct dcp_dpu_kms *dcp_kms,
			       struct drm_crtc *crtc);
int dcp_dpu_kms_prepare_crtc_session(struct dcp_dpu_kms *dcp_kms,
				     struct drm_crtc *crtc);

int dcp_dpu_drm_obj_init(struct dcp_dpu_kms *dcp_kms);
void dcp_dpu_kms_cleanup_bridges(struct dcp_dpu_kms *dcp_kms);
int dcp_dpu_kms_init_dsi_bridges(struct dcp_dpu_kms *dcp_kms,
				 struct component_match **matchptr);
extern const struct msm_kms_funcs dcp_dpu_kms_funcs;
int dcp_kms_init(struct drm_device *ddev);

#endif /* __DCP_DPU_KMS_H__ */
