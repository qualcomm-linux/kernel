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

static enum drm_mode_status
dcp_crtc_mode_valid(struct drm_crtc *crtc,
		    const struct drm_display_mode *mode)
{
	struct dcp_dpu_kms *kms = dcp_crtc_get_kms(crtc);

	if (mode->hdisplay > kms->caps->max_width)
		return MODE_BAD_WIDTH;

	return MODE_OK;
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

	/*
	 * MSM commits pass flags=0 to drm_atomic_helper_commit_planes().
	 * ACTIVE_ONLY would also skip atomic_flush() for inactive CRTCs, where
	 * this driver still needs to capture pending DRM events.
	 */
	if (!crtc->state->active)
		return;
	if (!dcp_kms->commit_pm_active) {
		cstate->hfi_error = -EIO;
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
		struct drm_rect *roi = &cstate->mixer_roi[i];

		if (drm_rect_width(roi) <= 0 || drm_rect_height(roi) <= 0) {
			drm_err(crtc->dev, "CRTC%u: invalid zero-sized LM%u ROI\n",
				dcp_crtc->display_id, i);
			cstate->hfi_error = -EINVAL;
			return;
		}

		ret = dcp_hfi_crtc_set_resource(dcp_kms->hfi, session_id,
						i, drm_rect_width(roi),
						drm_rect_height(roi),
						roi->x1, roi->y1);
		if (ret) {
			cstate->hfi_error = ret;
			return;
		}
	}

	ret = dcp_hfi_crtc_set_dest_roi(dcp_kms->hfi, session_id, 0, 0,
					crtc->state->adjusted_mode.hdisplay,
					crtc->state->adjusted_mode.vdisplay);
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

	/* Inactive CRTCs may carry an event but must not receive HFI updates. */
	if (!crtc->state->enable || !crtc->state->active)
		goto flush_done;
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

	dcp_crtc = to_dcp_crtc(crtc);
	spin_lock_irqsave(&dcp_crtc->spin_lock, flags);
	valid = dcp_crtc->session_valid;
	if (valid)
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
	struct dcp_dpu_kms *dcp_kms = dcp_crtc_get_kms(crtc);
	u32 session_id;
	int ret;

	/* A published session implies KMS/HFI setup has completed. */
	if (!dcp_crtc_get_session(crtc, &session_id))
		return -ENODEV;

	if (enable)
		ret = dcp_hfi_display_event_register_async(dcp_kms->hfi,
							   session_id, HFI_EVENT_VSYNC);
	else
		ret = dcp_hfi_display_event_deregister_async(dcp_kms->hfi,
							     session_id, HFI_EVENT_VSYNC);
	if (ret)
		return ret;

	return 0;
}

static void dcp_crtc_vblank_callback(struct drm_crtc *crtc, u64 timestamp_ns)
{
	struct dcp_crtc *dcp_crtc = to_dcp_crtc(crtc);

	/* DRM owns the vblank enable state and filters late HFI callbacks. */
	if (!READ_ONCE(drm_crtc_vblank_crtc(crtc)->enabled))
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

static struct drm_crtc *dcp_crtc_find_by_session_id(struct dcp_dpu_kms *dcp_kms,
						    u32 session_id)
{
	struct drm_crtc *crtc;
	u32 active_session_id;
	unsigned int i;

	for (i = 0; i < dcp_kms->num_crtcs; i++) {
		crtc = dcp_kms->crtcs[i].crtc;
		if (crtc && dcp_crtc_get_session(crtc, &active_session_id) &&
		    active_session_id == session_id)
			return crtc;
	}

	return NULL;
}

void dcp_crtc_hfi_vblank_cb(struct dcp_drv *drv, u32 session_id, u64 timestamp_ns)
{
	struct drm_crtc *crtc;

	/* Ignore async events until KMS is linked during probe or teardown. */
	if (!drv || !drv->kms)
		return;

	crtc = dcp_crtc_find_by_session_id(drv->kms, session_id);
	if (!crtc) {
		drm_err_ratelimited(drv->kms->dev,
				    "unknown CRTC for HFI VSYNC session %u\n",
				    session_id);
		return;
	}

	dcp_crtc_vblank_callback(crtc, timestamp_ns);
}

void dcp_crtc_hfi_frame_done_cb(struct dcp_drv *drv, u32 session_id,
				u32 sequence_id, u64 timestamp_ns)
{
	struct dcp_dpu_kms *dcp_kms;
	struct dcp_crtc *dcp_crtc;
	struct drm_crtc *crtc;

	/* Ignore async events until KMS is linked during probe or teardown. */
	if (!drv || !drv->kms)
		return;

	dcp_kms = drv->kms;
	crtc = dcp_crtc_find_by_session_id(dcp_kms, session_id);
	if (!crtc) {
		drm_err_ratelimited(dcp_kms->dev,
				    "unknown CRTC for HFI frame-done session %u seq=%u\n",
				    session_id, sequence_id);
		return;
	}

	dcp_crtc = to_dcp_crtc(crtc);
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
	atomic_add_unless(&dcp_crtc->pending_kickoff_cnt, -1, 0);
	wake_up(&dcp_crtc->frame_done_wq);
}

static int dcp_crtc_setup_mixer_rois(struct drm_crtc *crtc,
				     struct drm_crtc_state *crtc_state,
				     struct dcp_crtc_state *cstate)
{
	const struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	u32 mixer_count = cstate->topology_mixer_count;
	u32 mixer_width = mode->hdisplay / mixer_count;
	u32 i;

	if (!mixer_width)
		return -EINVAL;

	for (i = 0; i < mixer_count; i++) {
		struct drm_rect *roi = &cstate->mixer_roi[i];

		roi->x1 = (mode->hdisplay * i) / mixer_count;
		roi->y1 = 0;
		roi->x2 = (mode->hdisplay * (i + 1)) / mixer_count;
		roi->y2 = mode->vdisplay;
		drm_dbg_driver(crtc->dev,
			       "DCP_CHECK CRTC%u LM%u roi=(%d,%d %dx%d)\n",
				to_dcp_crtc(crtc)->display_id, i,
				roi->x1, roi->y1,
				drm_rect_width(roi), drm_rect_height(roi));
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
	u32 mixer_count;
	int ret;

	drm_dbg_atomic(crtc->dev, "CRTC%u: atomic_check enable=%d active=%d mode=%s\n",
		       crtc->base.id, crtc_state->enable, crtc_state->active,
		       crtc_state->mode.name[0] ? crtc_state->mode.name : "none");

	if (!crtc_state->enable || !crtc_state->active)
		return 0;

	mixer_count = cstate->topology_mixer_count;
	if (mixer_count < 1 || mixer_count > DCP_MAX_MIXERS) {
		drm_dbg_atomic(crtc->dev,
			       "CRTC%u: invalid mixer count %u (limit 1..%u)\n",
				crtc->base.id, mixer_count, DCP_MAX_MIXERS);
		return -EINVAL;
	}

	ret = dcp_crtc_setup_mixer_rois(crtc, crtc_state, cstate);
	if (ret)
		return ret;

	if (crtc_state->adjusted_mode.hdisplay > dcp_kms->caps->max_width) {
		drm_dbg_atomic(crtc->dev,
			       "CRTC%u: mode width %d exceeds DCP max width %u\n",
			       crtc->base.id,
			       crtc_state->adjusted_mode.hdisplay,
			       dcp_kms->caps->max_width);
		return -E2BIG;
	}

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
