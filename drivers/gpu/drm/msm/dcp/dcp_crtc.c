// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * DCP CRTC implementation.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_crtc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_rect.h>
#include <drm/drm_vblank.h>

#include <drm/drm_managed.h>

#include "msm_drv.h"

#include "dcp_crtc.h"
#include "dcp_drv.h"
#include "dcp_hfi.h"
#include "dcp_dpu_kms.h"
#include "dcp_plane_state.h"

#include "hfi/hfi_defs_display.h"
#include "hfi/hfi_properties_display.h"

#define DCP_FRAME_DONE_TIMEOUT_MS	500

static struct dcp_dpu_kms *dcp_crtc_get_kms(struct drm_crtc *crtc)
{
	struct msm_drm_private *priv = crtc->dev->dev_private;

	return to_dcp_dpu_kms(priv->kms);
}

static struct drm_crtc_state *
dcp_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct dcp_crtc_state *cstate;

	if (WARN_ON(!crtc->state))
		return NULL;

	cstate = kzalloc_obj(*cstate, GFP_KERNEL);
	if (!cstate)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &cstate->base);

	cstate->topology_mixer_count =
		to_dcp_crtc_state(crtc->state)->topology_mixer_count;
	cstate->topology_bits_per_component =
		to_dcp_crtc_state(crtc->state)->topology_bits_per_component;
	memcpy(cstate->mixer_roi, to_dcp_crtc_state(crtc->state)->mixer_roi,
	       sizeof(cstate->mixer_roi));
	cstate->update_roi = to_dcp_crtc_state(crtc->state)->update_roi;
	return &cstate->base;
}

static void dcp_crtc_atomic_destroy_state(struct drm_crtc *crtc,
					  struct drm_crtc_state *state)
{
	struct dcp_crtc_state *cstate = to_dcp_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(state);
	kfree(cstate);
}

static struct drm_crtc_state *
dcp_crtc_atomic_create_state(struct drm_crtc *crtc)
{
	struct dcp_crtc_state *cstate;

	cstate = kzalloc_obj(*cstate, GFP_KERNEL);
	if (!cstate)
		return ERR_PTR(-ENOMEM);

	__drm_atomic_helper_crtc_state_init(&cstate->base, crtc);
	cstate->topology_mixer_count = 1;
	cstate->topology_bits_per_component = 8;
	return &cstate->base;
}

static void dcp_crtc_aggregate_bw(struct drm_crtc_state *crtc_state,
				  bool enable)
{
	struct dcp_crtc_state *cstate = to_dcp_crtc_state(crtc_state);
	struct drm_plane *plane;
	const struct drm_plane_state *pstate;

	cstate->dram_ib  = 0;
	cstate->dram_ab  = 0;
	cstate->llcc_ib  = 0;
	cstate->llcc_ab  = 0;
	cstate->core_clk = 0;

	if (!enable)
		return;

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, crtc_state) {
		if (!pstate->visible || !pstate->fb)
			continue;

		const struct dcp_plane_state *dcp_pstate = to_dcp_plane_state(pstate);

		cstate->dram_ib  = max(cstate->dram_ib, dcp_pstate->fetch_bw);
		cstate->dram_ab += dcp_pstate->fetch_bw;
		cstate->llcc_ib  = max(cstate->llcc_ib, dcp_pstate->fetch_bw);
		cstate->llcc_ab += dcp_pstate->fetch_bw;
		cstate->core_clk = max(cstate->core_clk, dcp_pstate->clk);
	}
}

static enum drm_mode_status
dcp_crtc_mode_valid(struct drm_crtc *crtc,
		    const struct drm_display_mode *mode)
{
	struct dcp_dpu_kms *kms = dcp_crtc_get_kms(crtc);
	u32 max_width, max_height;

	dcp_mode_limits(mode, kms->caps->max_width, kms->caps->max_height,
			&max_width, &max_height);

	return drm_mode_validate_size(mode, max_width, max_height);
}

static void dcp_crtc_atomic_begin(struct drm_crtc *crtc,
				  struct drm_atomic_commit *state)
{
	struct dcp_crtc *dcp_crtc = to_dcp_crtc(crtc);
	struct dcp_crtc_state *cstate = to_dcp_crtc_state(crtc->state);
	struct dcp_dpu_kms *dcp_kms = dcp_crtc_get_kms(crtc);
	u32 mixer_count;
	u32 session_id;
	u32 i;
	int ret;

	if (!crtc->state->active)
		return;
	if (!dcp_kms->commit_pm_active) {
		cstate->hfi_error = -EIO;
		return;
	}

	ret = dcp_dpu_kms_prepare_crtc_session(dcp_kms, crtc);
	if (ret) {
		cstate->hfi_error = ret;
		return;
	}

	if (!dcp_crtc_get_session(crtc, &session_id)) {
		cstate->hfi_error = -ENODEV;
		return;
	}

	if (cstate->core_clk > 0 || cstate->dram_ib > 0) {
		ret = dcp_hfi_crtc_set_bw(dcp_kms->hfi, session_id,
					  cstate->dram_ib,  cstate->dram_ab,
					  cstate->llcc_ib,  cstate->llcc_ab,
					  cstate->core_clk);
		if (ret) {
			cstate->hfi_error = ret;
			return;
		}
	}

	mixer_count = cstate->topology_mixer_count;
	if (mixer_count < 1 || mixer_count > DCP_MAX_MIXERS) {
		cstate->hfi_error = -EINVAL;
		return;
	}
	for (i = 0; i < mixer_count; i++) {
		struct dcp_mixer_roi *roi = &cstate->mixer_roi[i];

		if (!roi->w || !roi->h) {
			drm_err(crtc->dev, "CRTC%u: invalid zero-sized LM%u ROI\n",
				dcp_crtc->display_id, i);
			cstate->hfi_error = -EINVAL;
			return;
		}

		ret = dcp_hfi_crtc_set_resource(dcp_kms->hfi, session_id,
						i, roi->w, roi->h, roi->x, roi->y);
		if (ret) {
			cstate->hfi_error = ret;
			return;
		}
	}

	ret = dcp_hfi_crtc_set_dest_roi(dcp_kms->hfi, session_id,
					cstate->update_roi.x, cstate->update_roi.y,
					cstate->update_roi.w, cstate->update_roi.h);
	if (ret) {
		cstate->hfi_error = ret;
		return;
	}
}

static void dcp_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_atomic_commit *state)
{
	struct dcp_crtc *dcp_crtc = to_dcp_crtc(crtc);
	struct dcp_crtc_state *cstate = to_dcp_crtc_state(crtc->state);
	struct dcp_dpu_kms *dcp_kms = dcp_crtc_get_kms(crtc);
	unsigned long flags;
	u32 session_id;
	int ret;

	if (!crtc->state->enable)
		return;
	drm_dbg_driver(crtc->dev, "DCP_FLUSH CRTC%u mask=0x%x err=%d\n",
		       dcp_crtc->display_id, crtc->state->plane_mask,
		       cstate->hfi_error);

	if (!dcp_crtc_get_session(crtc, &session_id)) {
		drm_dbg_driver(crtc->dev,
			       "CRTC%u: atomic flush without HFI session\n",
			       dcp_crtc->display_id);
		goto flush_done;
	}

	if (cstate->hfi_error)
		goto flush_done;

	ret = dcp_hfi_crtc_set_scan_sequence_id(dcp_kms->hfi, session_id,
						++dcp_crtc->commit_seq_id);
	if (ret)
		goto flush_error;

	ret = dcp_hfi_crtc_set_autorefresh(dcp_kms->hfi,
					   session_id, true, 1);
	if (ret)
		goto flush_error;

	goto flush_done;

flush_error:
	cstate->hfi_error = ret;

flush_done:
	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	dcp_crtc->event = crtc->state->event;
	crtc->state->event = NULL;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

void dcp_crtc_set_session(struct drm_crtc *crtc, u32 session_id)
{
	struct dcp_crtc *dcp_crtc;
	unsigned long flags;

	if (!crtc)
		return;

	dcp_crtc = to_dcp_crtc(crtc);
	spin_lock_irqsave(&dcp_crtc->spin_lock, flags);
	dcp_crtc->session_id = session_id;
	dcp_crtc->session_valid = true;
	spin_unlock_irqrestore(&dcp_crtc->spin_lock, flags);
}

void dcp_crtc_clear_session(struct drm_crtc *crtc)
{
	struct dcp_crtc *dcp_crtc;
	unsigned long flags;

	if (!crtc)
		return;

	dcp_crtc = to_dcp_crtc(crtc);
	spin_lock_irqsave(&dcp_crtc->spin_lock, flags);
	dcp_crtc->session_valid = false;
	dcp_crtc->session_id = 0;
	spin_unlock_irqrestore(&dcp_crtc->spin_lock, flags);
}

bool dcp_crtc_get_session(struct drm_crtc *crtc, u32 *session_id)
{
	struct dcp_crtc *dcp_crtc;
	unsigned long flags;
	bool valid;

	if (!crtc)
		return false;

	dcp_crtc = to_dcp_crtc(crtc);
	spin_lock_irqsave(&dcp_crtc->spin_lock, flags);
	valid = dcp_crtc->session_valid;
	if (valid && session_id)
		*session_id = dcp_crtc->session_id;
	spin_unlock_irqrestore(&dcp_crtc->spin_lock, flags);

	return valid;
}

void dcp_crtc_set_topology_state(struct drm_crtc_state *state,
				 u32 mixer_count,
				 u32 bits_per_component)
{
	struct dcp_crtc_state *cstate = to_dcp_crtc_state(state);

	cstate->topology_mixer_count = mixer_count;
	cstate->topology_bits_per_component = bits_per_component;
}

void dcp_crtc_commit_kickoff(struct drm_crtc *crtc)
{
	struct dcp_crtc *dcp_crtc = to_dcp_crtc(crtc);
	struct dcp_dpu_kms *dcp_kms = dcp_crtc_get_kms(crtc);
	struct dcp_crtc_state *cstate = to_dcp_crtc_state(crtc->state);
	u32 session_id;
	int ret;

	if (cstate->hfi_error) {
		drm_err(crtc->dev,
			"DCP_TRIGGER CRTC%u suppressed after HFI error %d\n",
			dcp_crtc->display_id, cstate->hfi_error);
		return;
	}

	if (!dcp_crtc_get_session(crtc, &session_id)) {
		drm_warn(crtc->dev,
			 "CRTC%u: no active session, skipping FRAME_TRIGGER\n",
			 dcp_crtc->display_id);
		return;
	}

	/* MSM serializes commits, so only one frame completion may be pending. */
	if (atomic_cmpxchg(&dcp_crtc->pending_kickoff_cnt, 0, 1)) {
		drm_err(crtc->dev,
			"CRTC%u: frame completion already pending (dropped commit_seq_id=%u, in-flight pending_seq_id=%u)\n",
			dcp_crtc->display_id, dcp_crtc->commit_seq_id,
			dcp_crtc->pending_seq_id);
		cstate->hfi_error = -EBUSY;
		return;
	}
	dcp_crtc->pending_seq_id = dcp_crtc->commit_seq_id;

	ret = dcp_hfi_display_commit(dcp_kms->hfi, session_id);
	if (ret) {
		drm_err(crtc->dev,
			"HFI commit failed for CRTC%u (session=%u): %d\n",
			dcp_crtc->display_id, session_id, ret);
		atomic_dec(&dcp_crtc->pending_kickoff_cnt);
		wake_up(&dcp_crtc->frame_done_wq);
	}
}

void dcp_crtc_wait_for_commit_done(struct drm_crtc *crtc)
{
	struct dcp_crtc *dcp_crtc = to_dcp_crtc(crtc);
	unsigned long timeout = msecs_to_jiffies(DCP_FRAME_DONE_TIMEOUT_MS);

	if (!atomic_read(&dcp_crtc->pending_kickoff_cnt))
		return;

	if (!wait_event_timeout(dcp_crtc->frame_done_wq,
				atomic_read(&dcp_crtc->pending_kickoff_cnt) == 0,
				timeout)) {
		drm_err(crtc->dev,
			"frame done timeout for CRTC%u pending_seq_id=%u commit_seq_id=%u\n",
			dcp_crtc->display_id, dcp_crtc->pending_seq_id,
			dcp_crtc->commit_seq_id);
		atomic_set(&dcp_crtc->pending_kickoff_cnt, 0);
	}
}

void dcp_crtc_complete_commit(struct drm_crtc *crtc)
{
	struct dcp_crtc *dcp_crtc = to_dcp_crtc(crtc);
	struct dcp_crtc_state *cstate = to_dcp_crtc_state(crtc->state);
	unsigned long flags;

	if (!crtc->state->active)
		dcp_crtc_aggregate_bw(crtc->state, false);

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	if (dcp_crtc->event) {
		drm_dbg_driver(crtc->dev,
			       "DCP_TS CRTC%u: page-flip event send time=%lld vblank_count=%llu commit_seq_id=%u pending_seq_id=%u hfi_error=%d%s\n",
			       dcp_crtc->display_id, ktime_to_ns(ktime_get()),
			       drm_crtc_vblank_count(crtc),
			       dcp_crtc->commit_seq_id, dcp_crtc->pending_seq_id,
			       cstate->hfi_error,
			       dcp_crtc->commit_seq_id != dcp_crtc->pending_seq_id ?
			       " MISMATCH-UNCONFIRMED-FRAME" : "");
		drm_crtc_send_vblank_event(crtc, dcp_crtc->event);
		dcp_crtc->event = NULL;
	}
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

int dcp_crtc_vblank(struct drm_crtc *crtc, bool enable)
{
	struct dcp_crtc *dcp_crtc = to_dcp_crtc(crtc);
	struct dcp_dpu_kms *dcp_kms = dcp_crtc_get_kms(crtc);
	u32 session_id;
	int ret;

	if (!dcp_kms->hfi || !dcp_crtc_get_session(crtc, &session_id))
		return -ENODEV;

	if (enable)
		ret = dcp_hfi_display_event_register_async(dcp_kms->hfi,
							   session_id, HFI_EVENT_VSYNC);
	else
		ret = dcp_hfi_display_event_deregister_async(dcp_kms->hfi,
							     session_id, HFI_EVENT_VSYNC);
	if (ret)
		return ret;

	WRITE_ONCE(dcp_crtc->vblank_enabled, enable);
	return 0;
}

void dcp_crtc_vblank_callback(struct drm_crtc *crtc, u64 timestamp_ns)
{
	struct dcp_crtc *dcp_crtc = to_dcp_crtc(crtc);

	if (!READ_ONCE(dcp_crtc->vblank_enabled))
		return;

	if (timestamp_ns)
		WRITE_ONCE(dcp_crtc->hw_vblank_timestamp_ns, timestamp_ns);

	if (ktime_compare(dcp_crtc->vblank_cb_time, ktime_set(0, 0)) == 0)
		dcp_crtc->vblank_cb_time = ktime_get();
	else
		dcp_crtc->vblank_cb_count++;

	drm_dbg_driver(crtc->dev,
		       "DCP_TS CRTC%u: HFI VSYNC time=%lld hw_time=%llu vblank_count=%llu (pre-handle)\n",
		       dcp_crtc->display_id, ktime_to_ns(ktime_get()),
		       timestamp_ns, drm_crtc_vblank_count(crtc));
	drm_crtc_handle_vblank(crtc);
}

void dcp_crtc_frame_done_callback(struct drm_crtc *crtc)
{
	struct dcp_crtc *dcp_crtc = to_dcp_crtc(crtc);

	atomic_add_unless(&dcp_crtc->pending_kickoff_cnt, -1, 0);
	wake_up(&dcp_crtc->frame_done_wq);
}

static struct drm_crtc *dcp_crtc_find_by_session_id(struct dcp_drv *drv,
						    u32 session_id)
{
	struct dcp_dpu_kms *dcp_kms;
	struct drm_crtc *crtc;
	u32 active_session_id;

	if (!drv || !drv->kms)
		return NULL;

	dcp_kms = drv->kms;

	crtc = dcp_dpu_kms_get_crtc(dcp_kms, session_id);
	if (!crtc || !dcp_crtc_get_session(crtc, &active_session_id) ||
	    active_session_id != session_id)
		return NULL;

	return crtc;
}

void dcp_crtc_hfi_vblank_cb(struct dcp_drv *drv, u32 session_id, u64 timestamp_ns)
{
	struct drm_device *dev = drv && drv->kms ? drv->kms->dev : NULL;
	struct drm_crtc *crtc = dcp_crtc_find_by_session_id(drv, session_id);

	if (crtc)
		dcp_crtc_vblank_callback(crtc, timestamp_ns);
	else if (dev)
		drm_err_ratelimited(dev,
				    "unknown CRTC for HFI VSYNC session %u\n",
				    session_id);
}

void dcp_crtc_hfi_frame_done_cb(struct dcp_drv *drv, u32 session_id,
				u32 sequence_id, u64 timestamp_ns)
{
	struct drm_device *dev = drv && drv->kms ? drv->kms->dev : NULL;
	struct drm_crtc *crtc = dcp_crtc_find_by_session_id(drv, session_id);

	if (crtc) {
		struct dcp_crtc *dcp_crtc = to_dcp_crtc(crtc);

		/* A single pending commit makes equality valid across sequence wrap. */
		if (sequence_id != dcp_crtc->pending_seq_id) {
			drm_warn(crtc->dev,
				 "CRTC%u: ignoring stale FRAME_SCAN_COMPLETE seq=%u expected=%u\n",
				 dcp_crtc->display_id, sequence_id,
				 dcp_crtc->pending_seq_id);
			return;
		}
		drm_dbg_driver(crtc->dev,
			       "DCP_TS CRTC%u: FRAME_SCAN_COMPLETE seq=%u time=%lld hw_time=%llu vblank_count=%llu\n",
			       dcp_crtc->display_id, sequence_id,
			       ktime_to_ns(ktime_get()), timestamp_ns,
			       drm_crtc_vblank_count(crtc));
		dcp_crtc_frame_done_callback(crtc);
	} else if (dev)
		drm_err_ratelimited(dev,
				    "unknown CRTC for HFI frame-done session %u seq=%u\n",
				    session_id, sequence_id);
}

static int dcp_crtc_setup_mixer_rois(struct drm_crtc *crtc,
				     struct drm_crtc_state *crtc_state,
				     struct dcp_crtc_state *cstate)
{
	const struct drm_plane_state *pstate;
	struct drm_plane *plane;
	const struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	u32 mixer_count = cstate->topology_mixer_count;
	u32 mixer_width = mode->hdisplay / mixer_count;
	u32 i;

	if (!mixer_width)
		return -EINVAL;

	cstate->update_roi.x = 0;
	cstate->update_roi.y = 0;
	cstate->update_roi.w = mode->hdisplay;
	cstate->update_roi.h = mode->vdisplay;
	for (i = 0; i < mixer_count; i++) {
		cstate->mixer_roi[i].x = (mode->hdisplay * i) / mixer_count;
		cstate->mixer_roi[i].y = 0;
		cstate->mixer_roi[i].w = (mode->hdisplay * (i + 1)) / mixer_count -
			cstate->mixer_roi[i].x;
		cstate->mixer_roi[i].h = mode->vdisplay;
		drm_dbg_driver(crtc->dev,
			       "DCP_CHECK CRTC%u LM%u roi=(%u,%u %ux%u)\n",
				to_dcp_crtc(crtc)->display_id, i,
				cstate->mixer_roi[i].x, cstate->mixer_roi[i].y,
				cstate->mixer_roi[i].w, cstate->mixer_roi[i].h);
	}

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, crtc_state) {
		bool fullscreen;
		bool assigned = false;
		u32 mixer_idx;

		if (!pstate || !pstate->visible || !pstate->fb)
			continue;

		for (mixer_idx = 0; mixer_idx < mixer_count; mixer_idx++) {
			struct dcp_mixer_roi *mixer = &cstate->mixer_roi[mixer_idx];

			if (pstate->crtc_x >= mixer->x &&
			    pstate->crtc_x + pstate->crtc_w <= mixer->x + mixer->w) {
				assigned = true;
				break;
			}
		}

		/* Any plane may cover the complete split display. Plane type is
		 * not a resource-selection mechanism in the universal-plane API.
		 */
		fullscreen = pstate->crtc_x == 0 && pstate->crtc_y == 0 &&
			pstate->crtc_w == mode->hdisplay &&
			pstate->crtc_h == mode->vdisplay;

		if (!assigned && !fullscreen && mixer_count > 1) {
			/*
			 * Split-pipe/source-split and multirect allocation are
			 * deferred until the atomic pipe resource manager is added.
			 * Reject planes needing multiple mixer rectangles for now.
			 */
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int dcp_crtc_atomic_check(struct drm_crtc *crtc,
				 struct drm_atomic_commit *state)
{
	struct dcp_dpu_kms *dcp_kms = dcp_crtc_get_kms(crtc);
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_new_crtc_state(state, crtc);
	struct dcp_crtc_state *cstate = to_dcp_crtc_state(crtc_state);
	struct drm_plane *plane;
	const struct drm_plane_state *pstate;
	u32 max_width, max_height;
	u32 mixer_count;
	bool needs_dirtyfb;
	int num_planes = 0;
	int ret;

	drm_dbg_atomic(crtc->dev, "CRTC%u: atomic_check enable=%d active=%d mode=%s\n",
		       crtc->base.id, crtc_state->enable, crtc_state->active,
		       crtc_state->mode.name[0] ? crtc_state->mode.name : "none");

	if (!crtc_state->enable || !crtc_state->active)
		return 0;

	needs_dirtyfb = dcp_dpu_kms_needs_dirtyfb(dcp_kms, crtc);

	mixer_count = cstate->topology_mixer_count;
	if (mixer_count < 1 || mixer_count > DCP_MAX_MIXERS) {
		drm_err(crtc->dev,
			"CRTC%u: invalid mixer count %u (limit 1..%u)\n",
				crtc->base.id, mixer_count, DCP_MAX_MIXERS);
		return -EINVAL;
	}

	ret = dcp_crtc_setup_mixer_rois(crtc, crtc_state, cstate);
	if (ret)
		return ret;

	dcp_mode_limits(&crtc_state->adjusted_mode,
			dcp_kms->caps->max_width, dcp_kms->caps->max_height,
			&max_width, &max_height);

	if (crtc_state->adjusted_mode.hdisplay > max_width ||
	    crtc_state->adjusted_mode.vdisplay > max_height) {
		drm_err(crtc->dev, "CRTC%u: mode %dx%d exceeds DCP caps %dx%d\n",
			crtc->base.id,
			crtc_state->adjusted_mode.hdisplay,
			crtc_state->adjusted_mode.vdisplay,
			dcp_kms->caps->max_width,
			dcp_kms->caps->max_height);
		return -E2BIG;
	}

	/* Limit visible planes to the firmware blend-stage count. */
	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, crtc_state) {
		struct dcp_plane_state *dcp_pstate;

		if (!pstate || !pstate->visible)
			continue;

		dcp_pstate = to_dcp_plane_state((struct drm_plane_state *)pstate);
		dcp_pstate->needs_dirtyfb = needs_dirtyfb;
		num_planes++;
	}

	if (num_planes > DCP_MAX_BLEND_STAGES) {
		drm_err(crtc->dev, "CRTC%u: %d planes exceeds max blend stages %d\n",
			crtc->base.id, num_planes,
			DCP_MAX_BLEND_STAGES);
		return -EINVAL;
	}

	dcp_crtc_aggregate_bw(crtc_state, true);

	return 0;
}

static u32 dcp_crtc_get_vblank_counter(struct drm_crtc *crtc)
{
	return drm_crtc_vblank_count(crtc);
}

static bool dcp_crtc_get_vblank_timestamp(struct drm_crtc *crtc, int *max_error,
					  ktime_t *vblank_time, bool in_vblank_irq)
{
	struct dcp_crtc *dcp_crtc = to_dcp_crtc(crtc);
	u64 timestamp_ns = READ_ONCE(dcp_crtc->hw_vblank_timestamp_ns);

	if (!timestamp_ns)
		return false;

	*vblank_time = ns_to_ktime(timestamp_ns);
	return true;
}

static void dcp_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_atomic_commit *state)
{
	struct dcp_dpu_kms *dcp_kms = dcp_crtc_get_kms(crtc);
	struct dcp_crtc_state *cstate = to_dcp_crtc_state(crtc->state);
	u32 session_id;
	int ret;

	if (!dcp_crtc_get_session(crtc, &session_id))
		return;

	ret = dcp_hfi_crtc_set_power_mode(dcp_kms->hfi, session_id,
					  HFI_MODE_DPMS_ON);
	if (ret) {
		drm_err(crtc->dev, "CRTC%u: DPMS ON failed: %d\n",
			to_dcp_crtc(crtc)->display_id, ret);
		cstate->hfi_error = ret;
	}

	drm_crtc_vblank_on(crtc);
}

static void dcp_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_atomic_commit *state)
{
	struct dcp_dpu_kms *dcp_kms = dcp_crtc_get_kms(crtc);
	struct dcp_crtc_state *cstate = to_dcp_crtc_state(crtc->state);
	u32 session_id;
	int ret;

	if (dcp_crtc_get_session(crtc, &session_id)) {
		ret = dcp_hfi_crtc_set_bw(dcp_kms->hfi, session_id, 0, 0, 0, 0, 0);
		if (ret) {
			drm_err(crtc->dev,
				"CRTC%u: clearing bandwidth vote failed: %d\n",
				to_dcp_crtc(crtc)->display_id, ret);
			cstate->hfi_error = ret;
		}

		ret = dcp_hfi_crtc_set_power_mode(dcp_kms->hfi, session_id,
						  HFI_MODE_DPMS_OFF);
		if (ret) {
			drm_err(crtc->dev, "CRTC%u: DPMS OFF failed: %d\n",
				to_dcp_crtc(crtc)->display_id, ret);
			cstate->hfi_error = ret;
		}
	}

	drm_crtc_vblank_off(crtc);
}

static const struct drm_crtc_funcs dcp_crtc_funcs = {
	.set_config             = drm_atomic_helper_set_config,
	.page_flip              = drm_atomic_helper_page_flip,
	.atomic_create_state    = dcp_crtc_atomic_create_state,
	.atomic_duplicate_state = dcp_crtc_atomic_duplicate_state,
	.atomic_destroy_state   = dcp_crtc_atomic_destroy_state,
	.enable_vblank          = msm_crtc_enable_vblank,
	.disable_vblank         = msm_crtc_disable_vblank,
	.get_vblank_counter     = dcp_crtc_get_vblank_counter,
	.get_vblank_timestamp   = dcp_crtc_get_vblank_timestamp,
};

static const struct drm_crtc_helper_funcs dcp_crtc_helper_funcs = {
	.atomic_check         = dcp_crtc_atomic_check,
	.atomic_enable        = dcp_crtc_atomic_enable,
	.atomic_disable       = dcp_crtc_atomic_disable,
	.atomic_begin         = dcp_crtc_atomic_begin,
	.atomic_flush         = dcp_crtc_atomic_flush,
	.mode_valid           = dcp_crtc_mode_valid,
};

struct drm_crtc *dcp_crtc_init(struct dcp_dpu_kms *kms,
			       struct drm_plane *primary_plane,
			       u32 display_id)
{
	struct drm_device *dev = kms->dev;
	struct dcp_crtc *dcp_crtc;
	int ret;

	dcp_crtc = drmm_kzalloc(dev, sizeof(*dcp_crtc), GFP_KERNEL);
	if (!dcp_crtc)
		return ERR_PTR(-ENOMEM);

	dcp_crtc->display_id = display_id;

	spin_lock_init(&dcp_crtc->spin_lock);
	init_waitqueue_head(&dcp_crtc->frame_done_wq);
	atomic_set(&dcp_crtc->pending_kickoff_cnt, 0);

	ret = drmm_crtc_init_with_planes(dev, &dcp_crtc->base,
					 primary_plane, NULL,
					 &dcp_crtc_funcs,
					 NULL);
	if (ret) {
		drm_err(dev, "CRTC%u init failed: %d\n", display_id, ret);
		return ERR_PTR(ret);
	}

	drm_crtc_helper_add(&dcp_crtc->base, &dcp_crtc_helper_funcs);

	drm_dbg_driver(dev, "CRTC%u initialized\n", display_id);
	return &dcp_crtc->base;
}
