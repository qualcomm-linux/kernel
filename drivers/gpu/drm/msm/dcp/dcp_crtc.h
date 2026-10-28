/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP CRTC implementation.
 */

#ifndef __DCP_CRTC_H__
#define __DCP_CRTC_H__

#include <linux/completion.h>
#include <linux/spinlock.h>

#include <drm/drm_crtc.h>

struct dcp_dpu_kms;
struct drm_plane;

#define DCP_MAX_MIXERS 6
#define DCP_MAX_BLEND_STAGES 12

static inline void dcp_mode_limits(const struct drm_display_mode *mode,
				   u32 cap_width, u32 cap_height,
				   u32 *max_width, u32 *max_height)
{
	*max_width = cap_width;
	*max_height = cap_height;

	if (mode->hdisplay < mode->vdisplay && cap_width > cap_height) {
		*max_width = cap_height;
		*max_height = cap_width;
	}
}

struct dcp_mixer_roi {
	u32 x;
	u32 y;
	u32 w;
	u32 h;
};

struct dcp_crtc {
	struct drm_crtc base;
	u32 display_id;
	u32 session_id;
	bool session_valid;

	struct drm_pending_vblank_event *event;
	wait_queue_head_t frame_done_wq;
	atomic_t          pending_kickoff_cnt;
	/* Protects CRTC-local vblank and commit state. */
	spinlock_t        spin_lock;
	u32     vblank_cb_count;
	bool    vblank_enabled;
	ktime_t vblank_cb_time;
	/* Latest firmware-reported VSYNC timestamp; 0 until the first HFI VSYNC. */
	u64     hw_vblank_timestamp_ns;
	bool    enabled;
	u32 commit_seq_id;
	u32 pending_seq_id;
};

struct dcp_crtc_state {
	struct drm_crtc_state base;

	u32 topology_mixer_count;
	u32 topology_bits_per_component;
	struct dcp_mixer_roi mixer_roi[DCP_MAX_MIXERS];

	struct dcp_mixer_roi update_roi;
	int hfi_error;
	u64 bw_ctl;

	u64 dram_ib;
	u64 dram_ab;
	u64 llcc_ib;
	u64 llcc_ab;
	u64 core_clk;
};

#define to_dcp_crtc(x)       container_of(x, struct dcp_crtc, base)
#define to_dcp_crtc_state(x) container_of(x, struct dcp_crtc_state, base)

struct drm_crtc *dcp_crtc_init(struct dcp_dpu_kms *kms,
			       struct drm_plane *primary_plane,
			       u32 display_id);

void dcp_crtc_commit_kickoff(struct drm_crtc *crtc);
void dcp_crtc_wait_for_commit_done(struct drm_crtc *crtc);
void dcp_crtc_complete_commit(struct drm_crtc *crtc);
int dcp_crtc_vblank(struct drm_crtc *crtc, bool enable);
void dcp_crtc_vblank_callback(struct drm_crtc *crtc, u64 timestamp_ns);
void dcp_crtc_frame_done_callback(struct drm_crtc *crtc);

struct dcp_drv;
void dcp_crtc_hfi_vblank_cb(struct dcp_drv *drv, u32 session_id, u64 timestamp_ns);
void dcp_crtc_hfi_frame_done_cb(struct dcp_drv *drv, u32 session_id,
				u32 sequence_id, u64 timestamp_ns);

void dcp_crtc_set_session(struct drm_crtc *crtc, u32 session_id);
void dcp_crtc_clear_session(struct drm_crtc *crtc);
bool dcp_crtc_get_session(struct drm_crtc *crtc, u32 *session_id);
void dcp_crtc_set_topology_state(struct drm_crtc_state *state,
				 u32 mixer_count,
				 u32 bits_per_component);

#endif /* __DCP_CRTC_H__ */
