// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * DCP KMS driver.
 */

#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_of.h>
#include <drm/drm_plane.h>

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_kms.h"

#include "dcp_dpu_kms.h"
#include "dcp_dsi_bridge.h"
#include "dcp_crtc.h"
#include "dcp_drv.h"
#include "dcp_encoder.h"
#include "dcp_hfi.h"
#include "dcp_plane.h"

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

void dcp_dpu_kms_cleanup_bridges(struct dcp_dpu_kms *dcp_kms)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(dcp_kms->dsi_bridge); i++) {
		if (!dcp_kms->dsi_bridge[i])
			continue;

		dcp_dsi_bridge_cleanup(dcp_kms->dsi_bridge[i]);
		dcp_kms->dsi_bridge[i] = NULL;
	}
}

static void dcp_dpu_kms_dump_caps(struct dcp_dpu_kms *dcp_kms)
{
	const struct dcp_caps *caps = dcp_kms->caps;
	u32 i;

	drm_dbg_driver(dcp_kms->dev, "DCP caps: hw=%u.%u fw=%u displays=%u\n",
		       caps->dcp_hw_rev, caps->hw_rev, caps->fw_rev,
		       caps->num_displays);
	drm_dbg_driver(dcp_kms->dev, "DCP caps: max_planes=%u max=%ux%u\n",
		       caps->max_planes, caps->max_width, caps->max_height);

	drm_dbg_driver(dcp_kms->dev, "DCP caps: VIG pipes=%u virt=%u\n",
		       caps->num_vig_pipes, caps->virt_vig_count);
	for (i = 0; i < min_t(u32, caps->num_vig_pipes,
			      DCP_MAX_SSPP_COUNT); i++)
		drm_dbg_driver(dcp_kms->dev, "DCP caps: VIG[%u] pipe=%u r1=%u\n", i,
			       caps->vig_pipe_ids[i], caps->vig_r1_indices[i]);

	drm_dbg_driver(dcp_kms->dev, "DCP caps: DMA pipes=%u virt=%u\n",
		       caps->num_dma_pipes, caps->virt_dma_count);
	for (i = 0; i < min_t(u32, caps->num_dma_pipes,
			      DCP_MAX_SSPP_COUNT); i++)
		drm_dbg_driver(dcp_kms->dev, "DCP caps: DMA[%u] pipe=%u r1=%u\n", i,
			       caps->dma_pipe_ids[i], caps->dma_r1_indices[i]);

	drm_dbg_driver(dcp_kms->dev, "DCP caps: DSI count=%u\n", caps->dsi_count);
}

static int dcp_dpu_kms_hw_init(struct msm_kms *kms)
{
	struct dcp_dpu_kms *dcp_kms = to_dcp_dpu_kms(kms);
	struct drm_device *dev = dcp_kms->dev;
	struct drm_gpuvm *vm;
	unsigned int i;
	int ret;

	kms->irq = IRQ_NOTCONNECTED;
	dev->mode_config.cursor_width  = 512;
	dev->mode_config.cursor_height = 512;

	ret = pm_runtime_resume_and_get(dcp_kms->pm_dev);
	if (ret < 0) {
		drm_err(dev, "failed to enable power: %d\n", ret);
		return ret;
	}

	vm = msm_kms_init_vm(dev, NULL);
	if (IS_ERR(vm)) {
		drm_err(dev, "SMMU/IOMMU init failed: %pe\n", vm);
		ret = PTR_ERR(vm);
		goto err_cleanup;
	}
	kms->vm = vm;

	for (i = 0; i < ARRAY_SIZE(dcp_kms->dsi_bridge); i++) {
		if (!dcp_kms->dsi_bridge[i])
			continue;

		ret = dcp_dsi_bridge_map_buffers(dcp_kms->dsi_bridge[i]);
		if (ret) {
			drm_err(dev, "failed to map DSI%u DCS buffers: %d\n", i, ret);
			goto err_cleanup;
		}
	}

	if (!dcp_kms->hfi) {
		drm_err(dev, "HFI unavailable, cannot initialize DCP KMS\n");
		ret = -ENODEV;
		goto err_cleanup;
	}
	if (!dcp_kms->caps) {
		drm_err(dev, "DCP caps unavailable, cannot initialize DCP KMS\n");
		ret = -ENODEV;
		goto err_cleanup;
	}

	dcp_dpu_kms_dump_caps(dcp_kms);

	dev->mode_config.min_width  = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width  = max(dcp_kms->caps->max_width,
					  dcp_kms->caps->max_height);
	dev->mode_config.max_height = dev->mode_config.max_width;
	dev->max_vblank_count = 0xffffffff;
	dev->vblank_disable_immediate = true;

	ret = dcp_dpu_drm_obj_init(dcp_kms);
	if (ret) {
		dev_err_probe(dev->dev, ret, "DRM object init failed\n");
		goto err_cleanup;
	}

	pm_runtime_put_autosuspend(dcp_kms->pm_dev);
	drm_dbg_driver(dev, "DCP hw_init complete\n");
	return 0;

err_cleanup:
	for (i = 0; i < ARRAY_SIZE(dcp_kms->dsi_bridge); i++) {
		if (dcp_kms->dsi_bridge[i])
			dcp_dsi_bridge_unmap_buffers(dcp_kms->dsi_bridge[i]);
	}

	if (kms->vm) {
		struct msm_gem_vm *msm_vm = to_msm_vm(kms->vm);

		if (msm_vm->mmu && msm_vm->mmu->funcs->detach)
			msm_vm->mmu->funcs->detach(msm_vm->mmu);
		drm_gpuvm_put(kms->vm);
		kms->vm = NULL;
	}
	pm_runtime_put_autosuspend(dcp_kms->pm_dev);
	return ret;
}

static void dcp_dpu_kms_enable_commit(struct msm_kms *kms)
{
	struct dcp_dpu_kms *dcp_kms = to_dcp_dpu_kms(kms);
	int ret;

	ret = pm_runtime_resume_and_get(dcp_kms->pm_dev);
	if (ret < 0) {
		dcp_kms->commit_pm_active = false;
		drm_err_ratelimited(dcp_kms->dev,
				    "pm_runtime_resume_and_get failed: %d\n",
				    ret);
		return;
	}

	dcp_kms->commit_pm_active = true;
}

static void dcp_dpu_kms_disable_commit(struct msm_kms *kms)
{
	struct dcp_dpu_kms *dcp_kms = to_dcp_dpu_kms(kms);

	if (dcp_kms->commit_pm_active) {
		pm_runtime_put_autosuspend(dcp_kms->pm_dev);
		dcp_kms->commit_pm_active = false;
	}
}

static int dcp_dpu_kms_check_mode_changed(struct msm_kms *kms,
					  struct drm_atomic_commit *state)
{
	struct drm_crtc_state *new_crtc_state, *old_crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
				      new_crtc_state, i) {
		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;
		if (!new_crtc_state->active)
			continue;
		new_crtc_state->planes_changed = true;
	}

	return 0;
}

static void dcp_dpu_kms_flush_commit(struct msm_kms *kms, unsigned int crtc_mask)
{
	struct dcp_dpu_kms *dcp_kms = to_dcp_dpu_kms(kms);
	struct drm_crtc *crtc;

	for_each_crtc_mask(dcp_kms->dev, crtc, crtc_mask) {
		if (!crtc->state || !crtc->state->active)
			continue;
		dcp_crtc_commit_kickoff(crtc);
	}
}

static void dcp_dpu_kms_wait_flush(struct msm_kms *kms, unsigned int crtc_mask)
{
	struct dcp_dpu_kms *dcp_kms = to_dcp_dpu_kms(kms);
	struct drm_crtc *crtc;

	for_each_crtc_mask(dcp_kms->dev, crtc, crtc_mask)
		dcp_crtc_wait_for_commit_done(crtc);
}

static void dcp_dpu_kms_complete_commit(struct msm_kms *kms, unsigned int crtc_mask)
{
	struct dcp_dpu_kms *dcp_kms = to_dcp_dpu_kms(kms);
	struct drm_crtc *crtc;

	for_each_crtc_mask(dcp_kms->dev, crtc, crtc_mask)
		dcp_crtc_complete_commit(crtc);
}

static int dcp_dpu_kms_enable_vblank(struct msm_kms *kms, struct drm_crtc *crtc)
{
	return dcp_crtc_vblank(crtc, true);
}

static void dcp_dpu_kms_disable_vblank(struct msm_kms *kms,
				       struct drm_crtc *crtc)
{
	dcp_crtc_vblank(crtc, false);
}

static void dcp_dpu_kms_destroy(struct msm_kms *kms)
{
	struct dcp_dpu_kms *dcp_kms = to_dcp_dpu_kms(kms);

	dcp_dpu_kms_cleanup_bridges(dcp_kms);

	if (kms->vm) {
		struct msm_gem_vm *msm_vm = to_msm_vm(kms->vm);

		if (msm_vm->mmu && msm_vm->mmu->funcs->detach) {
			drm_dbg_driver(dcp_kms->dev,
				       "detaching IOMMU domain from dcp-dpus\n");
			msm_vm->mmu->funcs->detach(msm_vm->mmu);
		}

		drm_gpuvm_put(kms->vm);
		kms->vm = NULL;
	}

	if (dcp_kms->rpm_enabled) {
		pm_runtime_disable(dcp_kms->pm_dev);
		dcp_kms->rpm_enabled = false;
	}

	msm_kms_destroy(&dcp_kms->base);
}

static void dcp_dpu_kms_irq_preinstall(struct msm_kms *kms) {}
static void dcp_dpu_kms_irq_uninstall(struct msm_kms *kms) {}

static irqreturn_t dcp_dpu_kms_irq(struct msm_kms *kms)
{
	return IRQ_NONE;
}

static void dcp_dpu_kms_snapshot(struct msm_disp_state *disp_state,
				 struct msm_kms *kms)
{
	struct dcp_dpu_kms *dcp_kms = to_dcp_dpu_kms(kms);

	drm_dbg_driver(dcp_kms->dev,
		       "DCP snapshot: max %ux%u\n",
		       dcp_kms->caps->max_width,
		       dcp_kms->caps->max_height);
}

const struct msm_kms_funcs dcp_dpu_kms_funcs = {
	.hw_init            = dcp_dpu_kms_hw_init,
	.irq_preinstall     = dcp_dpu_kms_irq_preinstall,
	.irq_uninstall      = dcp_dpu_kms_irq_uninstall,
	.irq                = dcp_dpu_kms_irq,
	.enable_commit      = dcp_dpu_kms_enable_commit,
	.disable_commit     = dcp_dpu_kms_disable_commit,
	.check_mode_changed = dcp_dpu_kms_check_mode_changed,
	.flush_commit       = dcp_dpu_kms_flush_commit,
	.wait_flush         = dcp_dpu_kms_wait_flush,
	.complete_commit    = dcp_dpu_kms_complete_commit,
	.enable_vblank      = dcp_dpu_kms_enable_vblank,
	.disable_vblank     = dcp_dpu_kms_disable_vblank,
	.destroy            = dcp_dpu_kms_destroy,
	.snapshot           = dcp_dpu_kms_snapshot,
};

static u32 dcp_dpu_kms_resolve_fw_display_id(struct dcp_dpu_kms *dcp_kms,
					     u32 intf_idx)
{
	const struct dcp_caps *caps = dcp_kms->caps;
	u32 count;
	const u32 *ids;
	u32 id;

	count = caps->dsi_count;
	ids = caps->dsi_indices;

	if (intf_idx >= count) {
		drm_err(dcp_kms->dev,
			"DSI%u: no catalog entry (%u)\n", intf_idx, count);
		return U32_MAX;
	}

	id = ids[intf_idx];

	if (id > 0xFF) {
		drm_err(dcp_kms->dev,
			"DSI%u: catalog id 0x%x exceeds 8-bit range\n",
			intf_idx, id);
		return U32_MAX;
	}

	return id;
}

int dcp_dpu_kms_init_dsi_bridges(struct dcp_dpu_kms *dcp_kms,
				 struct component_match **matchptr)
{
	struct device_node *dpu_np = dcp_kms->dpus_np;
	struct device_node *ep_np, *dsi_np, *child_np;
	struct of_endpoint ep;
	int ret;

	for_each_of_graph_port(dpu_np, port_np) {
		u32 dsi_idx, fw_display_id;
		struct drm_bridge *bridge;

		ep_np = of_get_next_child(port_np, NULL);
		if (!ep_np)
			continue;

		ret = of_graph_parse_endpoint(ep_np, &ep);
		of_node_put(ep_np);
		if (ret)
			continue;

		dsi_idx = ep.port;
		if (dsi_idx >= ARRAY_SIZE(dcp_kms->dsi_bridge))
			return -E2BIG;

		if (dcp_kms->dsi_bridge[dsi_idx])
			continue;

		dsi_np = NULL;
		for_each_available_child_of_node(dpu_np, child_np) {
			u32 reg;

			if (!of_node_name_prefix(child_np, "dsi") ||
			    of_property_read_u32(child_np, "reg", &reg) ||
			    reg != dsi_idx)
				continue;

			dsi_np = of_node_get(child_np);
			break;
		}
		of_node_put(child_np);
		if (!dsi_np)
			return -ENODEV;

		fw_display_id =
			dcp_dpu_kms_resolve_fw_display_id(dcp_kms, dsi_idx);
		if (fw_display_id == U32_MAX) {
			of_node_put(dsi_np);
			return -EINVAL;
		}

		bridge = dcp_dsi_bridge_init(dcp_kms, dsi_np, dpu_np,
					     ep.port, dsi_idx, fw_display_id);
		if (IS_ERR(bridge)) {
			ret = PTR_ERR(bridge);
			of_node_put(dsi_np);
			return ret;
		}

		dcp_kms->dsi_bridge[dsi_idx] = to_dcp_dsi_bridge(bridge);
		drm_of_component_match_add(dcp_kms->pm_dev, matchptr,
					   component_compare_of, dsi_np);
		of_node_put(dsi_np);
	}

	return 0;
}

static int dcp_dpu_kms_setup_bridges(struct dcp_dpu_kms *dcp_kms,
				     struct device_node *dpu_np)
{
	struct drm_device *dev = dcp_kms->dev;
	struct device_node *ep_np, *remote_np;
	struct of_endpoint ep;
	struct drm_encoder *encoder;
	struct drm_bridge *bridge;
	struct drm_connector *connector;
	int ret = 0;
	int dsi_idx = 0;

	for_each_of_graph_port(dpu_np, port_np) {
		ep_np = of_get_next_child(port_np, NULL);
		if (!ep_np)
			continue;

		ret = of_graph_parse_endpoint(ep_np, &ep);
		if (ret) {
			of_node_put(ep_np);
			continue;
		}

		remote_np = of_graph_get_remote_port_parent(ep_np);
		of_node_put(ep_np);
		if (!remote_np)
			continue;

		if (!of_device_is_available(remote_np)) {
			of_node_put(remote_np);
			continue;
		}

		dsi_idx = ep.port;
		if (dsi_idx >= ARRAY_SIZE(dcp_kms->dsi_bridge)) {
			of_node_put(remote_np);
			drm_err(dev, "too many DSI outputs\n");
			ret = -E2BIG;
			goto out;
		}

		drm_dbg_driver(dev, "port@%u: DSI bridge intf_idx=%u\n",
			       ep.port, dsi_idx);
		if (!dcp_kms->dsi_bridge[dsi_idx]) {
			of_node_put(remote_np);
			drm_err(dev, "port@%u: no DSI host for index %u\n",
				ep.port, dsi_idx);
			ret = -ENODEV;
			goto out;
		}
		bridge = &dcp_kms->dsi_bridge[dsi_idx]->bridge;

		of_node_put(remote_np);

		encoder = dcp_encoder_init(dev, DRM_MODE_ENCODER_DSI);
		if (IS_ERR(encoder)) {
			ret = PTR_ERR(encoder);
			drm_err(dev, "encoder init failed: %d\n", ret);
			goto out;
		}

		ret = drm_bridge_attach(encoder, bridge, NULL,
					DRM_BRIDGE_ATTACH_NO_CONNECTOR);
		if (ret) {
			if (ret == -EPROBE_DEFER) {
				drm_dbg_driver(dev,
					       "port@%u bridge deferred; panel not ready\n",
					       ep.port);
				goto out;
			}
			drm_err(dev, "bridge attach failed: %d\n", ret);
			goto out;
		}

		connector = drm_bridge_connector_init(dev, encoder);
		if (IS_ERR(connector)) {
			ret = PTR_ERR(connector);
			drm_err(dev, "bridge connector init failed: %d\n", ret);
			goto out;
		}
	}

out:
	if (ret)
		dcp_dpu_kms_cleanup_bridges(dcp_kms);
	return ret;
}

static int dcp_dpu_setup_pipeline(struct dcp_dpu_kms *dcp_kms,
				  struct device_node *dpu_np)
{
	struct drm_device *dev = dcp_kms->dev;
	struct drm_plane *primary_planes[DCP_DPU_MAX_CRTCS];
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	int primary_idx = 0;
	int max_crtc_count;
	int total_planes;
	int i, ret;

	ret = dcp_dpu_kms_setup_bridges(dcp_kms, dpu_np);
	if (ret)
		return dev_err_probe(dev->dev, ret, "bridge setup failed\n");

	max_crtc_count = 0;
	drm_for_each_encoder(encoder, dev)
		max_crtc_count++;

	max_crtc_count = min(max_crtc_count, DCP_DPU_MAX_CRTCS);
	max_crtc_count = min_t(int, max_crtc_count,
			       dcp_kms->caps->num_displays);
	max_crtc_count = min_t(int, max_crtc_count,
			       ARRAY_SIZE(dcp_kms->dsi_bridge));

	total_planes = min_t(int, dcp_kms->caps->max_planes,
			     DCP_DPU_MAX_CRTCS * DCP_DPU_MAX_PLANES);

	for (i = 0; i < total_planes; i++) {
		enum drm_plane_type type;
		unsigned long possible_crtcs;

		possible_crtcs = ((1UL << max_crtc_count) - 1)
				 << dev->mode_config.num_crtc;

		if (primary_idx < max_crtc_count)
			type = DRM_PLANE_TYPE_PRIMARY;
		else
			type = DRM_PLANE_TYPE_OVERLAY;

		plane = dcp_plane_init(dev, U32_MAX, type, possible_crtcs);
		if (IS_ERR(plane)) {
			ret = PTR_ERR(plane);
			drm_err(dev, "plane init failed: %d\n", ret);
			goto err_cleanup_bridges;
		}

		if (type == DRM_PLANE_TYPE_PRIMARY)
			primary_planes[primary_idx++] = plane;
	}

	max_crtc_count = min(max_crtc_count, primary_idx);

	dcp_kms->num_crtcs = 0;
	for (i = 0; i < max_crtc_count; i++) {
		u32 display_id;

		if (!dcp_kms->dsi_bridge[i]) {
			drm_err(dev, "missing DSI bridge for CRTC%u\n", i);
			ret = -ENODEV;
			goto err_cleanup_bridges;
		}

		display_id = dcp_kms->dsi_bridge[i]->fw_display_id;
		if (dcp_dpu_kms_get_crtc(dcp_kms, display_id)) {
			drm_err(dev, "duplicate firmware display ID %u\n",
				display_id);
			ret = -EINVAL;
			goto err_cleanup_bridges;
		}

		crtc = dcp_crtc_init(dcp_kms, primary_planes[i],
				     display_id);
		if (IS_ERR(crtc)) {
			ret = PTR_ERR(crtc);
			drm_err(dev, "CRTC init failed: %d\n", ret);
			goto err_cleanup_bridges;
		}
		dcp_kms->crtcs[dcp_kms->num_crtcs].crtc = crtc;
		dcp_kms->crtcs[dcp_kms->num_crtcs++].display_id = display_id;
	}

	return 0;

err_cleanup_bridges:
	dcp_dpu_kms_cleanup_bridges(dcp_kms);
	return ret;
}

int dcp_dpu_drm_obj_init(struct dcp_dpu_kms *dcp_kms)
{
	struct drm_device *dev = dcp_kms->dev;
	struct device_node *dpus_np = dcp_kms->dpus_np;
	struct drm_encoder *encoder;
	int enc_idx = 0;
	int ret;

	if (!dpus_np) {
		drm_err(dev, "no DCP subsystem node — cannot create DRM objects\n");
		return -ENODEV;
	}

	ret = dcp_dpu_setup_pipeline(dcp_kms, dpus_np);
	if (ret)
		return dev_err_probe(dev->dev, ret, "pipeline setup failed\n");

	/* v1 - Encoders map one-to-one to CRTCs; cloning is unsupported. */
	drm_for_each_encoder(encoder, dev) {
		encoder->possible_crtcs = BIT(enc_idx);
		encoder->possible_clones = 0;
		enc_idx++;
	}

	drm_dbg_driver(dev,
		       "DCP KMS: DPU ready — %d CRTCs, %d planes, %d encoders\n",
		       dev->mode_config.num_crtc,
		       dev->mode_config.num_total_plane,
		       dev->mode_config.num_encoder);

	return 0;
}

int dcp_kms_init(struct drm_device *ddev)
{
	struct msm_drm_private *priv = ddev->dev_private;
	struct dcp_dpu_kms *dcp_kms = to_dcp_dpu_kms(priv->kms);
	struct dcp_drv *drv = dcp_kms->drv;

	dcp_kms->dev = ddev;
	if (drv) {
		drv->kms = dcp_kms;
		dcp_kms->pm_dev = &drv->dcp_subsystem_dev->dev;
	}

	if (!dcp_kms->hfi)
		return -ENODEV;
	if (!dcp_kms->caps)
		return -ENODEV;

	return 0;
}
