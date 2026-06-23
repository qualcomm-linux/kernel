// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <drm/drm_edid.h>
#include <drm/drm_managed.h>
#include <drm/drm_bridge.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <linux/pm_runtime.h>

#include "dp_mst_drm.h"
#include "dp_panel.h"

#define MAX_DPCD_TRANSACTION_BYTES 16

#define to_dp_mst_bridge(x)     container_of((x), struct msm_dp_mst_bridge, base)
#define to_dp_mst_bridge_state_priv(x) \
		container_of((x), struct msm_dp_mst_bridge_state, base)
#define to_dp_mst_bridge_state(x) \
		to_dp_mst_bridge_state_priv((x)->obj.state)
#define to_dp_mst_connector(x) \
		container_of((x), struct msm_dp_mst_connector, connector)

#define DP_MST_CONN_ID(x) ((x)->connector ? \
		(x)->connector->base.id : 0)

struct msm_dp_mst_bridge {
	struct drm_bridge base;
	struct drm_private_obj obj;
	u32 id;

	bool initialized;

	struct msm_dp *display;
	struct drm_encoder *encoder;

	struct drm_connector *connector;
	struct msm_dp_panel *msm_dp_panel;

	int vcpi;
	int pbn;
	int num_slots;
	int start_slot;
};

struct msm_dp_mst_bridge_state {
	struct drm_private_state base;
	struct drm_connector *connector;
	struct msm_dp_panel *msm_dp_panel;
	int num_slots;
};

struct msm_dp_mst_connector {
	struct drm_connector connector;
	struct drm_dp_mst_port *mst_port;
	struct msm_dp_mst *dp_mst;
	struct msm_dp_panel *dp_panel;
};

struct msm_dp_mst {
	struct drm_dp_mst_topology_mgr mst_mgr;
	struct msm_dp_mst_bridge *mst_bridge[DP_STREAM_MAX];
	struct msm_dp *msm_dp;
	struct drm_dp_aux *dp_aux;
	u32 max_streams;
	/* Protects MST bridge enable/disable handling. */
	struct mutex mst_lock;
	/* Serializes HPD IRQ handling between IRQ handler and poll_hpd_irq. */
	struct mutex hpd_irq_lock;
};

static struct drm_private_state *msm_dp_mst_duplicate_bridge_state(struct drm_private_obj *obj)
{
	struct msm_dp_mst_bridge_state *mst_bridge_state;

	mst_bridge_state = kmemdup(obj->state, sizeof(*mst_bridge_state), GFP_KERNEL);
	if (!mst_bridge_state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &mst_bridge_state->base);

	return &mst_bridge_state->base;
}

static void msm_dp_mst_destroy_bridge_state(struct drm_private_obj *obj,
					    struct drm_private_state *state)
{
	struct msm_dp_mst_bridge_state *mst_bridge_state =
		to_dp_mst_bridge_state_priv(state);

	kfree(mst_bridge_state);
}

static const struct drm_private_state_funcs msm_dp_mst_bridge_state_funcs = {
	.atomic_duplicate_state = msm_dp_mst_duplicate_bridge_state,
	.atomic_destroy_state = msm_dp_mst_destroy_bridge_state,
};

static struct msm_dp_mst_bridge_state *msm_dp_mst_br_priv_state(struct drm_atomic_state *st,
								struct msm_dp_mst_bridge *bridge)
{
	struct drm_device *dev = bridge->base.dev;
	struct drm_private_state *obj_state = drm_atomic_get_private_obj_state(st, &bridge->obj);

	WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));

	return to_dp_mst_bridge_state_priv(obj_state);
}

static void msm_dp_mst_update_timeslots(struct msm_dp_mst *mst,
					struct msm_dp_mst_bridge *mst_bridge,
					struct drm_dp_mst_atomic_payload *payload)
{
	int i;
	struct msm_dp_mst_bridge *msm_dp_bridge;
	int prev_start = 0;
	int prev_slots = 0;

	if (!payload) {
		DRM_ERROR("MST bridge [%d] update_timeslots failed, null payload\n",
			  mst_bridge->id);
		return;
	}

	for (i = 0; i < mst->max_streams; i++) {
		msm_dp_bridge = mst->mst_bridge[i];
		if (mst_bridge == msm_dp_bridge) {
			if (payload->vc_start_slot < 0) {
				prev_start = msm_dp_bridge->start_slot;
				prev_slots = msm_dp_bridge->num_slots;
				msm_dp_bridge->pbn        = 0;
				msm_dp_bridge->start_slot = 1;
				msm_dp_bridge->num_slots  = 0;
				msm_dp_bridge->vcpi       = 0;
			} else {
				msm_dp_bridge->pbn        = payload->pbn;
				msm_dp_bridge->start_slot = payload->vc_start_slot;
				msm_dp_bridge->num_slots  = payload->time_slots;
				msm_dp_bridge->vcpi       = payload->vcpi;
			}
		}
	}

	for (i = 0; i < mst->max_streams; i++) {
		msm_dp_bridge = mst->mst_bridge[i];

		if (payload->vc_start_slot < 0 && msm_dp_bridge->start_slot > prev_start)
			msm_dp_bridge->start_slot -= prev_slots;

		msm_dp_display_set_stream_info(mst->msm_dp, msm_dp_bridge->msm_dp_panel,
					       msm_dp_bridge->id, msm_dp_bridge->start_slot,
					       msm_dp_bridge->num_slots,
					       msm_dp_bridge->pbn, msm_dp_bridge->vcpi);
	}
}

static int msm_dp_mst_bridge_pre_enable_part1(struct msm_dp_mst_bridge *dp_bridge,
					      struct drm_atomic_state *state)
{
	struct msm_dp *dp_display = dp_bridge->display;
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(dp_bridge->connector);
	struct drm_dp_mst_port *port = mst_conn->mst_port;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;
	struct msm_dp_panel *dp_panel = mst_conn->dp_panel;
	int pbn;
	int rc = 0;

	mst_state = drm_atomic_get_new_mst_topology_state(state, &mst->mst_mgr);

	pbn = drm_dp_calc_pbn_mode(dp_panel->msm_dp_mode.drm_mode.clock,
				   (mst_conn->connector.display_info.bpc * 3) << 4);

	payload = drm_atomic_get_mst_payload_state(mst_state, port);
	if (!payload || payload->time_slots <= 0) {
		DRM_ERROR("time slots not allocated for conn:%d\n", DP_MST_CONN_ID(dp_bridge));
		rc = -EINVAL;
		return rc;
	}

	drm_dbg_dp(dp_display->drm_dev, "conn:%d pbn:%d, slots:%d\n", DP_MST_CONN_ID(dp_bridge),
		   pbn, payload->time_slots);

	drm_dp_mst_update_slots(mst_state, DP_CAP_ANSI_8B10B);

	rc = drm_dp_add_payload_part1(&mst->mst_mgr, mst_state, payload);
	if (rc) {
		DRM_ERROR("payload allocation failure for conn:%d\n", DP_MST_CONN_ID(dp_bridge));
		return rc;
	}

	msm_dp_mst_update_timeslots(mst, dp_bridge, payload);

	return rc;
}

static void _msm_dp_mst_bridge_pre_enable_part2(struct msm_dp_mst_bridge *dp_bridge,
						struct drm_atomic_state *state)
{
	struct msm_dp *dp_display = dp_bridge->display;
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(dp_bridge->connector);
	struct drm_dp_mst_port *port = mst_conn->mst_port;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;

	drm_dp_check_act_status(&mst->mst_mgr);

	mst_state = drm_atomic_get_new_mst_topology_state(state, &mst->mst_mgr);
	payload = drm_atomic_get_mst_payload_state(mst_state, port);

	drm_dp_add_payload_part2(&mst->mst_mgr, payload);

	drm_dbg_dp(dp_display->drm_dev, "MST bridge [%d] _pre enable part-2 complete\n",
		   dp_bridge->id);
}

static void msm_dp_mst_bridge_pre_disable_part1(struct msm_dp_mst_bridge *dp_bridge,
						struct drm_atomic_state *state)
{
	struct msm_dp *dp_display = dp_bridge->display;
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(dp_bridge->connector);
	struct drm_dp_mst_port *port = mst_conn->mst_port;
	struct drm_dp_mst_topology_state *old_mst_state;
	struct drm_dp_mst_topology_state *new_mst_state;
	const struct drm_dp_mst_atomic_payload *old_payload;
	struct drm_dp_mst_atomic_payload *new_payload;

	old_mst_state = drm_atomic_get_old_mst_topology_state(state, &mst->mst_mgr);
	new_mst_state = drm_atomic_get_new_mst_topology_state(state, &mst->mst_mgr);

	old_payload = drm_atomic_get_mst_payload_state(old_mst_state, port);
	new_payload = drm_atomic_get_mst_payload_state(new_mst_state, port);

	if (!old_payload || !new_payload) {
		DRM_ERROR("MST bridge [%d] _pre disable part-1 failed, null payload\n",
			  dp_bridge->id);
		return;
	}

	drm_dp_remove_payload_part1(&mst->mst_mgr, new_mst_state, new_payload);
	drm_dp_remove_payload_part2(&mst->mst_mgr, new_mst_state, old_payload, new_payload);

	msm_dp_mst_update_timeslots(mst, dp_bridge, new_payload);

	drm_dbg_dp(dp_display->drm_dev, "MST bridge [%d] _pre disable part-1 complete\n",
		   dp_bridge->id);
}

static void msm_dp_mst_bridge_atomic_pre_enable(struct drm_bridge *drm_bridge,
						struct drm_atomic_state *state)
{
	int rc = 0;
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp *dp_display;
	struct msm_dp_mst_bridge_state *mst_bridge_state;
	struct msm_dp_mst *dp_mst;
	struct msm_dp_panel   *msm_dp_panel;

	if (!drm_bridge) {
		DRM_ERROR("Invalid params\n");
		return;
	}

	bridge = to_dp_mst_bridge(drm_bridge);
	mst_bridge_state = to_dp_mst_bridge_state(bridge);
	dp_display = bridge->display;
	dp_mst = dp_display->msm_dp_mst;

	/* to cover cases of bridge_disable/bridge_enable without modeset */
	bridge->connector = mst_bridge_state->connector;
	bridge->msm_dp_panel = mst_bridge_state->msm_dp_panel;

	if (!bridge->connector) {
		DRM_ERROR("Invalid connector\n");
		return;
	}

	msm_dp_panel = bridge->msm_dp_panel;
	mutex_lock(&dp_mst->mst_lock);

	rc = msm_dp_display_set_mode_helper(dp_display, state, drm_bridge->encoder, msm_dp_panel);
	if (rc) {
		DRM_ERROR("Failed to perform a mode set, rc=%d\n", rc);
		mutex_unlock(&dp_mst->mst_lock);
		return;
	}
	msm_dp_panel->pbn = drm_dp_calc_pbn_mode(msm_dp_panel->msm_dp_mode.drm_mode.clock,
						 msm_dp_panel->msm_dp_mode.bpp << 4);
	rc = msm_dp_display_prepare(dp_display);
	if (rc) {
		DRM_ERROR("[%d] DP display pre-enable failed, rc=%d\n", bridge->id, rc);
		msm_dp_display_unprepare(dp_display);
		mutex_unlock(&dp_mst->mst_lock);
		return;
	}

	rc = msm_dp_mst_bridge_pre_enable_part1(bridge, state);
	if (rc) {
		DRM_ERROR("[%d] DP display pre-enable failed, rc=%d\n", bridge->id, rc);
		mutex_unlock(&dp_mst->mst_lock);
		return;
	}

	msm_dp_display_enable_helper(dp_display, bridge->msm_dp_panel);

	_msm_dp_mst_bridge_pre_enable_part2(bridge, state);

	mutex_unlock(&dp_mst->mst_lock);

	drm_dbg_dp(dp_display->drm_dev, "conn:%d mode:%s pre enable done\n",
		   DP_MST_CONN_ID(bridge), bridge->msm_dp_panel->msm_dp_mode.drm_mode.name);
}

static void msm_dp_mst_bridge_atomic_disable(struct drm_bridge *drm_bridge,
					     struct drm_atomic_state *state)
{
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp *dp_display;
	struct msm_dp_mst *mst;

	if (!drm_bridge) {
		DRM_ERROR("Invalid params\n");
		return;
	}

	bridge = to_dp_mst_bridge(drm_bridge);
	if (!bridge->connector) {
		DRM_ERROR("Invalid connector\n");
		return;
	}

	dp_display = bridge->display;
	mst = dp_display->msm_dp_mst;

	mutex_lock(&mst->mst_lock);

	msm_dp_mst_bridge_pre_disable_part1(bridge, state);

	msm_dp_display_disable_helper(dp_display, bridge->msm_dp_panel);

	drm_dp_check_act_status(&mst->mst_mgr);

	mutex_unlock(&mst->mst_lock);

	drm_dbg_dp(dp_display->drm_dev, "MST bridge:%d disable complete\n", bridge->id);
}

static void msm_dp_mst_bridge_atomic_post_disable(struct drm_bridge *drm_bridge,
						  struct drm_atomic_state *state)
{
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp *dp_display;
	struct msm_dp_mst *mst;

	if (!drm_bridge) {
		DRM_ERROR("Invalid params\n");
		return;
	}

	bridge = to_dp_mst_bridge(drm_bridge);
	if (!bridge->connector) {
		DRM_ERROR("Invalid connector\n");
		return;
	}

	dp_display = bridge->display;
	mst = dp_display->msm_dp_mst;

	mutex_lock(&mst->mst_lock);

	msm_dp_display_atomic_post_disable_helper(dp_display, bridge->msm_dp_panel);

	if (!dp_display->mst_active)
		msm_dp_display_unprepare(dp_display);

	bridge->connector = NULL;
	bridge->msm_dp_panel =  NULL;

	mutex_unlock(&mst->mst_lock);

	drm_dbg_dp(dp_display->drm_dev, "MST bridge:%d conn:%d post disable complete\n",
		   bridge->id, DP_MST_CONN_ID(bridge));
}

static int msm_dp_mst_bridge_atomic_check(struct drm_bridge *drm_bridge,
					  struct drm_bridge_state *bridge_state,
					  struct drm_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state)
{
	struct drm_atomic_state *state = crtc_state->state;
	struct drm_connector *connector = conn_state->connector;
	struct drm_dp_mst_topology_state *mst_state;
	struct msm_dp_mst_connector *mst_conn;
	struct msm_dp_mst *mst;
	int rc = 0, pbn, slots;
	struct msm_dp_mst_bridge_state *mst_bridge_state;
	u32 bpp;

	if (!drm_atomic_crtc_needs_modeset(crtc_state) || !crtc_state->enable)
		return 0;

	mst_conn = to_dp_mst_connector(connector);
	mst = mst_conn->dp_mst;

	bpp = connector->display_info.bpc * 3;

	if (!bpp)
		bpp = 24;

	pbn = drm_dp_calc_pbn_mode(crtc_state->mode.clock, bpp << 4);

	mst_state = to_drm_dp_mst_topology_state(mst->mst_mgr.base.state);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	if (!dfixed_trunc(mst_state->pbn_div)) {
		mst_state->pbn_div =
			drm_dp_get_vc_payload_bw(mst_conn->dp_panel->link_info.rate,
						 mst_conn->dp_panel->link_info.num_lanes);
	}

	slots = drm_dp_atomic_find_time_slots(state, &mst->mst_mgr, mst_conn->mst_port, pbn);

	drm_dbg_dp(drm_bridge->dev, "add slots, conn:%d pbn:%d slots:%d rc:%d\n",
		   connector->base.id, pbn, slots, rc);

	if (slots < 0)
		return slots;

	mst_bridge_state = msm_dp_mst_br_priv_state(state, to_dp_mst_bridge(drm_bridge));
	mst_bridge_state->num_slots = slots;

	return 0;
}

/* DP MST Bridge APIs */
static const struct drm_bridge_funcs msm_dp_mst_bridge_ops = {
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset           = drm_atomic_helper_bridge_reset,
	.atomic_pre_enable   = msm_dp_mst_bridge_atomic_pre_enable,
	.atomic_disable      = msm_dp_mst_bridge_atomic_disable,
	.atomic_post_disable = msm_dp_mst_bridge_atomic_post_disable,
	.atomic_check        = msm_dp_mst_bridge_atomic_check,
};

int msm_dp_mst_attach_encoder(struct msm_dp *dp_display, struct drm_encoder *encoder)
{
	int rc = 0;
	struct msm_dp_mst_bridge *bridge = NULL;
	struct msm_dp_mst_bridge_state *mst_bridge_state;
	struct drm_device *dev;
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	int i;

	for (i = 0; i < mst->max_streams; i++) {
		if (!mst->mst_bridge[i]->initialized) {
			bridge = mst->mst_bridge[i];
			bridge->encoder = encoder;
			bridge->initialized = true;
			bridge->id = i;
			break;
		}
	}

	if (i == mst->max_streams) {
		DRM_ERROR("MST supports only %d bridges\n", mst->max_streams);
		rc = -EACCES;
		goto end;
	}

	dev = dp_display->drm_dev;
	bridge->display = dp_display;
	bridge->base.encoder = encoder;
	bridge->base.type = dp_display->connector_type;
	bridge->base.ops = DRM_BRIDGE_OP_MODES;
	drm_bridge_add(&bridge->base);

	rc = drm_bridge_attach(encoder, &bridge->base, NULL, 0);
	if (rc) {
		DRM_ERROR("failed to attach bridge, rc=%d\n", rc);
		goto end;
	}

	mst_bridge_state = kzalloc(sizeof(*mst_bridge_state), GFP_KERNEL);
	if (!mst_bridge_state) {
		rc = -ENOMEM;
		goto end;
	}

	drm_atomic_private_obj_init(dev, &bridge->obj,
				    &mst_bridge_state->base,
				    &msm_dp_mst_bridge_state_funcs);

	drm_dbg_dp(dp_display->drm_dev, "MST drm bridge init. bridge id:%d\n", i);

	return 0;

end:
	return rc;
}

int msm_dp_mst_display_set_mgr_state(struct msm_dp *dp_display, bool state)
{
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	int rc;

	rc = drm_dp_mst_topology_mgr_set_mst(&mst->mst_mgr, state);
	if (rc < 0) {
		DRM_ERROR("failed to set topology mgr state to %d. rc %d\n",
			  state, rc);
	}

	drm_dbg_dp(dp_display->drm_dev, "dp_mst_display_set_mgr_state state:%d\n", state);
	return rc;
}

/* DP MST HPD IRQ callback */
void msm_dp_mst_display_hpd_irq(struct msm_dp *dp_display)
{
	int rc;
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	u8 ack[8] = {};
	u8 esi[4];
	unsigned int esi_res = DP_SINK_COUNT_ESI + 1;
	bool handled;

	mutex_lock(&mst->hpd_irq_lock);

	rc = drm_dp_dpcd_read_data(mst->dp_aux, DP_SINK_COUNT_ESI, esi, 4);
	if (rc < 0) {
		DRM_ERROR("DPCD sink status read failed, rlen=%d\n", rc);
		goto out_unlock;
	}

	drm_dbg_dp(dp_display->drm_dev, "MST irq: esi1[0x%x] esi2[0x%x] esi3[%x]\n",
		   esi[1], esi[2], esi[3]);

	rc = drm_dp_mst_hpd_irq_handle_event(&mst->mst_mgr, esi, ack, &handled);

	/* ack the request */
	if (handled) {
		rc = drm_dp_dpcd_write_byte(mst->dp_aux, esi_res, ack[1]);
		if (rc < 0) {
			DRM_ERROR("DPCD esi_res failed. rc=%d\n", rc);
			goto out_unlock;
		}

		drm_dp_mst_hpd_irq_send_new_request(&mst->mst_mgr);
	}
	drm_dbg_dp(dp_display->drm_dev, "MST display hpd_irq handled:%d rc:%d\n", handled, rc);

out_unlock:
	mutex_unlock(&mst->hpd_irq_lock);
}

/* DP MST Connector OPs */
static int
msm_dp_mst_connector_detect(struct drm_connector *connector,
			    struct drm_modeset_acquire_ctx *ctx,
			    bool force)
{
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;
	struct msm_dp *dp_display = mst->msm_dp;
	struct device *dev = dp_display->drm_dev->dev;
	enum drm_connector_status status = connector_status_disconnected;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return status;

	if (dp_display->mst_active)
		status = drm_dp_mst_detect_port(connector,
						ctx, &mst->mst_mgr, mst_conn->mst_port);

	pm_runtime_put_autosuspend(dev);

	return status;
}

static int msm_dp_mst_connector_get_modes(struct drm_connector *connector)
{
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;
	const struct drm_edid *drm_edid;
	int num_modes;

	drm_edid = drm_dp_mst_edid_read(connector, &mst->mst_mgr, mst_conn->mst_port);
	drm_edid_connector_update(connector, drm_edid);
	num_modes = drm_edid_connector_add_modes(connector);
	drm_edid_free(drm_edid);

	return num_modes;
}

static enum drm_mode_status msm_dp_mst_connector_mode_valid(struct drm_connector *connector,
							    const struct drm_display_mode *mode)
{
	struct msm_dp_mst_connector *mst_conn;
	struct drm_dp_mst_port *mst_port;
	struct msm_dp *dp_display;
	int required_pbn;

	if (drm_connector_is_unregistered(connector))
		return 0;

	mst_conn = to_dp_mst_connector(connector);
	mst_port = mst_conn->mst_port;
	dp_display = mst_conn->dp_mst->msm_dp;

	if (!mst_port)
		return MODE_ERROR;

	required_pbn = drm_dp_calc_pbn_mode(mode->clock, (6 * 3) << 4);

	if (required_pbn > mst_port->full_pbn) {
		drm_dbg_dp(dp_display->drm_dev, "mode:%s not supported.\n", mode->name);
		return MODE_CLOCK_HIGH;
	}

	return msm_dp_display_mode_valid(dp_display, &connector->display_info, mode);
}

static struct drm_encoder *
msm_dp_mst_atomic_best_encoder(struct drm_connector *connector, struct drm_atomic_state *state)
{
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;
	struct msm_dp *dp_display = mst->msm_dp;
	struct drm_encoder *enc = NULL;
	struct msm_dp_mst_bridge_state *mst_bridge_state;
	u32 i;
	struct drm_connector_state *conn_state = drm_atomic_get_new_connector_state(state,
										    connector);

	if (conn_state && conn_state->best_encoder)
		return conn_state->best_encoder;

	for (i = 0; i < mst->max_streams; i++) {
		mst_bridge_state = msm_dp_mst_br_priv_state(state, mst->mst_bridge[i]);
		if (IS_ERR(mst_bridge_state))
			goto end;

		if (mst_bridge_state->connector == connector) {
			enc = mst->mst_bridge[i]->encoder;
			goto end;
		}
	}

	for (i = 0; i < mst->max_streams; i++) {
		mst_bridge_state = msm_dp_mst_br_priv_state(state, mst->mst_bridge[i]);

		if (!mst_bridge_state->connector) {
			mst_bridge_state->connector = connector;
			mst_bridge_state->msm_dp_panel = mst_conn->dp_panel;
			enc = mst->mst_bridge[i]->encoder;
			break;
		}
	}

end:
	if (enc)
		drm_dbg_dp(dp_display->drm_dev, "MST connector:%d atomic best encoder:%d\n",
			   connector->base.id, i);
	else
		drm_dbg_dp(dp_display->drm_dev, "MST connector:%d atomic best encoder failed\n",
			   connector->base.id);

	return enc;
}

static int msm_dp_mst_connector_atomic_check(struct drm_connector *connector,
					     struct drm_atomic_state *state)
{
	int rc = 0, slots;
	struct drm_connector_state *old_conn_state;
	struct drm_connector_state *new_conn_state;
	struct drm_crtc *old_crtc;
	struct drm_crtc_state *crtc_state;
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp_mst_bridge_state *mst_bridge_state;
	struct drm_bridge *drm_bridge;
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;
	struct msm_dp *dp_display = mst->msm_dp;

	if (!state)
		return rc;

	new_conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (!new_conn_state)
		return rc;

	old_conn_state = drm_atomic_get_old_connector_state(state, connector);
	if (!old_conn_state)
		goto end;

	old_crtc = old_conn_state->crtc;
	if (!old_crtc)
		goto end;

	crtc_state = drm_atomic_get_new_crtc_state(state, old_crtc);

	/* attempt to release vcpi slots on a modeset change for crtc state */
	if (drm_atomic_crtc_needs_modeset(crtc_state)) {
		if (WARN_ON(!old_conn_state->best_encoder)) {
			rc = -EINVAL;
			goto end;
		}

		drm_bridge = drm_bridge_chain_get_first_bridge(old_conn_state->best_encoder);
		if (WARN_ON(!drm_bridge)) {
			rc = -EINVAL;
			goto end;
		}
		bridge = to_dp_mst_bridge(drm_bridge);

		mst_bridge_state = msm_dp_mst_br_priv_state(state, bridge);

		slots = mst_bridge_state->num_slots;
		if (slots > 0) {
			rc = drm_dp_atomic_release_time_slots(state,
							      &mst->mst_mgr,
							      mst_conn->mst_port);
			if (rc) {
				DRM_ERROR("failed releasing %d vcpi slots %d\n", slots, rc);
				goto end;
			}
		}

		if (!new_conn_state->crtc) {
			/* for cases where crtc is not disabled the slots are not
			 * freed by drm_dp_atomic_release_time_slots. this results
			 * in subsequent atomic_check failing since internal slots
			 * were freed but not the DP MST mgr's
			 */
			mst_bridge_state->connector = NULL;
			mst_bridge_state->msm_dp_panel = NULL;
			mst_bridge_state->num_slots = 0;
			drm_dbg_dp(dp_display->drm_dev, "clear best encoder: %d\n", bridge->id);
		}
	}

end:
	drm_dbg_dp(dp_display->drm_dev, "mst connector:%d atomic check ret %d\n",
		   connector->base.id, rc);
	return rc;
}

static void dp_mst_connector_destroy(struct drm_connector *connector)
{
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);

	drm_connector_cleanup(connector);
	drm_dp_mst_put_port_malloc(mst_conn->mst_port);
	kfree(mst_conn);
}

/* DRM MST callbacks */
static const struct drm_connector_helper_funcs msm_dp_drm_mst_connector_helper_funcs = {
	.get_modes =    msm_dp_mst_connector_get_modes,
	.detect_ctx =   msm_dp_mst_connector_detect,
	.mode_valid =   msm_dp_mst_connector_mode_valid,
	.atomic_best_encoder = msm_dp_mst_atomic_best_encoder,
	.atomic_check = msm_dp_mst_connector_atomic_check,
};

static const struct drm_connector_funcs msm_dp_drm_mst_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.destroy = dp_mst_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_connector *
msm_dp_mst_add_connector(struct drm_dp_mst_topology_mgr *mgr,
			 struct drm_dp_mst_port *port, const char *pathprop)
{
	struct msm_dp_mst *dp_mst;
	struct drm_device *dev;
	struct msm_dp *dp_display;
	struct msm_dp_mst_connector *mst_conn;
	struct drm_connector *connector;
	int rc, i;

	dp_mst = container_of(mgr, struct msm_dp_mst, mst_mgr);

	dp_display = dp_mst->msm_dp;
	dev = dp_display->drm_dev;

	mst_conn = kzalloc(sizeof(*mst_conn), GFP_KERNEL);

	if (!mst_conn)
		return NULL;

	drm_modeset_lock_all(dev);

	connector = &mst_conn->connector;
	rc = drm_connector_dynamic_init(dev, connector,
					&msm_dp_drm_mst_connector_funcs,
					DRM_MODE_CONNECTOR_DisplayPort, NULL);
	if (rc) {
		kfree(mst_conn);
		drm_modeset_unlock_all(dev);
		return NULL;
	}

	mst_conn->dp_panel = msm_dp_display_get_panel(dp_display);
	if (!mst_conn->dp_panel) {
		DRM_ERROR("failed to get dp_panel for connector\n");
		kfree(mst_conn);
		drm_modeset_unlock_all(dev);
		return NULL;
	}

	mst_conn->dp_panel->connector = connector;
	mst_conn->dp_mst = dp_mst;

	drm_connector_helper_add(connector, &msm_dp_drm_mst_connector_helper_funcs);

	if (connector->funcs->reset)
		connector->funcs->reset(connector);

	/* add all encoders as possible encoders */
	for (i = 0; i < dp_mst->max_streams; i++) {
		rc = drm_connector_attach_encoder(connector, dp_mst->mst_bridge[i]->encoder);

		if (rc) {
			DRM_ERROR("failed to attach encoder to connector, %d\n", rc);
			kfree(mst_conn);
			drm_modeset_unlock_all(dev);
			return NULL;
		}
	}

	mst_conn->mst_port = port;
	drm_dp_mst_get_port_malloc(mst_conn->mst_port);

	drm_object_attach_property(&connector->base,
				   dev->mode_config.path_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.tile_property, 0);
	drm_connector_set_path_property(connector, pathprop);
	drm_modeset_unlock_all(dev);

	drm_dbg_dp(dp_display->drm_dev, "add MST connector id:%d\n", connector->base.id);

	return connector;
}

static void msm_dp_mst_poll_hpd_irq(struct drm_dp_mst_topology_mgr *mgr)
{
	struct msm_dp_mst *mst = container_of(mgr, struct msm_dp_mst, mst_mgr);

	msm_dp_mst_display_hpd_irq(mst->msm_dp);
}

static const struct drm_dp_mst_topology_cbs msm_dp_mst_drm_cbs = {
	.add_connector = msm_dp_mst_add_connector,
	.poll_hpd_irq  = msm_dp_mst_poll_hpd_irq,
};

int msm_dp_mst_init(struct msm_dp *dp_display, u32 max_streams, struct drm_dp_aux *drm_aux)
{
	struct drm_device *dev = dp_display->drm_dev;
	int conn_base_id = 0;
	int ret;
	struct msm_dp_mst *msm_dp_mst;

	msm_dp_mst = devm_kzalloc(dev->dev, sizeof(*msm_dp_mst), GFP_KERNEL);
	if (!msm_dp_mst)
		return -ENOMEM;

	memset(&msm_dp_mst->mst_mgr, 0, sizeof(msm_dp_mst->mst_mgr));
	msm_dp_mst->mst_mgr.cbs = &msm_dp_mst_drm_cbs;
	conn_base_id = dp_display->connector->base.id;
	msm_dp_mst->msm_dp = dp_display;
	msm_dp_mst->max_streams = max_streams;

	for (int i = 0; i < DP_STREAM_MAX; i++) {
		msm_dp_mst->mst_bridge[i] =
			devm_drm_bridge_alloc(dev->dev, struct msm_dp_mst_bridge, base,
					      &msm_dp_mst_bridge_ops);
	}

	msm_dp_mst->dp_aux = drm_aux;

	ret = drm_dp_mst_topology_mgr_init(&msm_dp_mst->mst_mgr, dev,
					   drm_aux,
					   MAX_DPCD_TRANSACTION_BYTES,
					   max_streams,
					   conn_base_id);
	if (ret) {
		DRM_ERROR("DP DRM MST topology manager init failed\n");
		return ret;
	}

	dp_display->msm_dp_mst = msm_dp_mst;

	mutex_init(&msm_dp_mst->mst_lock);
	mutex_init(&msm_dp_mst->hpd_irq_lock);
	return ret;
}
