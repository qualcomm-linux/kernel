// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP DSI bridge driver.
 *
 * Implements a DRM bridge for DSI displays in the DCP firmware-delegated
 * architecture. DCP firmware owns the DSI PHY and controller; this driver
 * uses HFI commands to configure DSI parameters rather than direct register
 * writes.
 */

#include <linux/component.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/jiffies.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>

#include <video/mipi_display.h>

#include "dcp_crtc.h"
#include "dcp_dsi_bridge.h"
#include "dcp_dpu_kms.h"
#include "dcp_hfi.h"
#include "hfi/hfi_defs_panel.h"

struct dcp_dsi_bridge_state {
	struct drm_bridge_state base;
	struct dcp_dsi_panel_state panel_state;
	int prepare_error;
};

#define to_dcp_dsi_bridge_state(s) \
	container_of(s, struct dcp_dsi_bridge_state, base)

static struct dcp_dsi_bridge_state *
dcp_dsi_bridge_get_state(struct dcp_dsi_bridge *bridge)
{
	return to_dcp_dsi_bridge_state(drm_priv_to_bridge_state(bridge->bridge.base.state));
}

static struct drm_bridge_state *
dcp_dsi_bridge_atomic_create_state(struct drm_bridge *bridge)
{
	struct dcp_dsi_bridge_state *state;
	struct dcp_dsi_bridge *dsi_bridge = to_dcp_dsi_bridge(bridge);

	state = kzalloc_obj(*state);
	if (!state)
		return ERR_PTR(-ENOMEM);

	__drm_atomic_helper_bridge_state_init(&state->base, bridge);
	state->panel_state.topology_mixer_count =
		dsi_bridge->hfi_panel_config.topology_mixer_count;
	state->panel_state.topology_enc_count =
		dsi_bridge->hfi_panel_config.topology_enc_count;
	state->panel_state.topology_intf_count =
		dsi_bridge->hfi_panel_config.topology_intf_count;
	return &state->base;
}

static struct drm_bridge_state *
dcp_dsi_bridge_atomic_duplicate_state(struct drm_bridge *bridge)
{
	struct dcp_dsi_bridge_state *state, *old_state;

	if (WARN_ON(!bridge->base.state))
		return NULL;

	state = kzalloc_obj(*state);
	if (!state)
		return NULL;

	old_state = to_dcp_dsi_bridge_state(drm_priv_to_bridge_state(bridge->base.state));
	__drm_atomic_helper_bridge_duplicate_state(bridge, &state->base);
	state->panel_state = old_state->panel_state;
	return &state->base;
}

static void dcp_dsi_bridge_atomic_destroy_state(struct drm_bridge *bridge,
						struct drm_bridge_state *bridge_state)
{
	kfree(to_dcp_dsi_bridge_state(bridge_state));
}

static void
dcp_dsi_bridge_build_hfi_config(struct dcp_dsi_bridge *dsi_bridge,
				const struct dcp_dsi_bridge_state *state,
				struct dcp_dsi_hfi_panel_config *cfg)
{
	*cfg = dsi_bridge->hfi_panel_config;
	cfg->topology_mixer_count = state->panel_state.topology_mixer_count;
	cfg->topology_enc_count = state->panel_state.topology_enc_count;
	cfg->topology_intf_count = state->panel_state.topology_intf_count;
}

static void dcp_dsi_host_dev_release(struct device *dev)
{
	of_node_put(dev->of_node);
}

static bool dcp_dsi_bridge_cmd_mode(const struct dcp_dsi_bridge *dsi_bridge)
{
	return dsi_bridge->dsi_device &&
		!(dsi_bridge->dsi_device->mode_flags & MIPI_DSI_MODE_VIDEO);
}

static u64 dcp_dsi_link_clock(const struct dcp_dsi_bridge *dsi_bridge,
			      const struct dcp_dsi_hfi_panel_config *cfg,
			      const struct drm_display_mode *mode)
{
	const struct mipi_dsi_device *dsi = dsi_bridge->dsi_device;
	int bpp;
	u32 bpp_x16;

	bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	if (bpp < 0)
		return 0;

	bpp_x16 = cfg->dsc_enabled && cfg->dsc_bits_per_pixel ?
		cfg->dsc_bits_per_pixel : bpp << 4;

	if (dsi->hs_rate)
		return dsi->hs_rate;

	return div_u64((u64)mode->clock * 1000 * bpp_x16, dsi->lanes * 16);
}

static void dcp_dsi_power_control_work(struct work_struct *work)
{
	struct dcp_dsi_bridge *dsi_bridge =
		container_of(work, struct dcp_dsi_bridge, power_control_work);
	struct dcp_dpu_kms *kms = dsi_bridge->kms;

	dcp_hfi_display_power_control_ack(kms->hfi,
					  dsi_bridge->session_id,
					  dsi_bridge->power_packet_id);

	complete(&dsi_bridge->power_done);
}

static void dcp_dsi_power_control_cb(void *priv, u32 display_id,
				     u32 power_mask, bool enable,
				     u32 packet_id)
{
	struct dcp_dpu_kms *kms = priv;
	struct dcp_dsi_bridge *dsi_bridge = NULL;
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(kms->dsi_bridge); idx++) {
		struct dcp_dsi_bridge *b = kms->dsi_bridge[idx];

		if (b && b->session_id == display_id) {
			dsi_bridge = b;
			break;
		}
	}

	if (!dsi_bridge) {
		dev_warn(kms->pm_dev,
			 "DSI: power_control for unknown display_id=%u\n", display_id);
		return;
	}

	dsi_bridge->power_packet_id = packet_id;
	schedule_work(&dsi_bridge->power_control_work);
}

static const struct component_ops dcp_dsi_component_ops;

static int dcp_dsi_host_attach(struct mipi_dsi_host *host,
			       struct mipi_dsi_device *dsi)
{
	struct dcp_dsi_bridge *dsi_bridge = to_dcp_dsi_host(host);
	struct dcp_dsi_hfi_panel_config *cfg = &dsi_bridge->hfi_panel_config;
	int ret;

	if (!dsi->lanes || dsi->lanes > 4)
		return -EINVAL;

	cfg->dsc_enabled = false;
	cfg->dsc_version_major = 0;
	cfg->dsc_version_minor = 0;
	cfg->dsc_scr_version = 0;
	cfg->dsc_slice_height = 0;
	cfg->dsc_slice_width = 0;
	cfg->dsc_slices_per_pkt = 0;
	cfg->dsc_bits_per_component = 0;
	cfg->dsc_pps_delay_ms = 0;
	cfg->dsc_bits_per_pixel = 0;
	cfg->dsc_chroma_format = 0;
	cfg->dsc_color_space = 0;
	cfg->dsc_block_prediction_enable = 0;
	cfg->dsc_rc_override = false;
	memset(cfg->dsc_rc_min_qp, 0, sizeof(cfg->dsc_rc_min_qp));
	memset(cfg->dsc_rc_max_qp, 0, sizeof(cfg->dsc_rc_max_qp));
	memset(cfg->dsc_rc_bpg_offsets, 0, sizeof(cfg->dsc_rc_bpg_offsets));

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB888:
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB666_PACKED:
	case MIPI_DSI_FMT_RGB565:
	case MIPI_DSI_FMT_RGB101010:
		break;
	default:
		dev_err(&dsi->dev, "unsupported pixel format %u\n", dsi->format);
		return -EINVAL;
	}

	dsi_bridge->dsi_device = dsi;

	if (dsi->dsc) {
		struct drm_dsc_config *dsc = dsi->dsc;
		u32 chroma_format = 0;
		u32 color_space = 0;

		if (dsc->native_422)
			chroma_format = 1;
		else if (dsc->native_420)
			chroma_format = 2;
		if (dsc->native_422 || dsc->native_420)
			color_space = 1;

		cfg->dsc_enabled                 = true;
		cfg->dsc_version_major           = dsc->dsc_version_major;
		cfg->dsc_version_minor           = dsc->dsc_version_minor;
		cfg->dsc_scr_version              = 0;
		cfg->dsc_slice_height             = dsc->slice_height;
		cfg->dsc_slice_width              = dsc->slice_width;
		cfg->dsc_slices_per_pkt           = 1;
		cfg->dsc_bits_per_component       = dsc->bits_per_component;
		cfg->dsc_pps_delay_ms             = 0;
		cfg->dsc_bits_per_pixel           = dsc->bits_per_pixel;
		cfg->dsc_chroma_format            = chroma_format;
		cfg->dsc_color_space              = color_space;
		cfg->dsc_block_prediction_enable  = dsc->block_pred_enable;

		dev_dbg(&dsi->dev,
			"DSI%u: DSC enabled: version=%u.%u slice=%ux%u bpc=%u bpp=%u.%u\n",
			dsi_bridge->intf_idx, dsc->dsc_version_major,
			dsc->dsc_version_minor, dsc->slice_width,
			dsc->slice_height, dsc->bits_per_component,
			dsc->bits_per_pixel >> 4, (dsc->bits_per_pixel & 0xf) * 100 / 16);
	}

	ret = component_add(&dsi_bridge->dsi_host_dev,
			    &dcp_dsi_component_ops);
	if (ret)
		return ret;
	dsi_bridge->component_added = true;

	return 0;
}

static int dcp_dsi_host_detach(struct mipi_dsi_host *host,
			       struct mipi_dsi_device *dsi)
{
	struct dcp_dsi_bridge *dsi_bridge = to_dcp_dsi_host(host);

	if (dsi_bridge->component_added) {
		component_del(&dsi_bridge->dsi_host_dev,
			      &dcp_dsi_component_ops);
		dsi_bridge->component_added = false;
	}
	dsi_bridge->panel_bridge = NULL;
	dsi_bridge->dsi_device = NULL;
	dsi_bridge->hfi_panel_config.dsc_enabled = false;
	return 0;
}

static ssize_t dcp_dsi_host_transfer(struct mipi_dsi_host *host,
				     const struct mipi_dsi_msg *msg)
{
	struct dcp_dsi_bridge *dsi_bridge = to_dcp_dsi_host(host);
	struct dcp_dpu_kms *kms = dsi_bridge->kms;
	u32 session_id;
	bool temporary_session = false;
	ssize_t ret;

	guard(mutex)(&dsi_bridge->session_lock);
	if (!dsi_bridge->dcs_tx_buf)
		return -ENODEV;

	/* Serialize DCS transfers */
	guard(mutex)(&dsi_bridge->dcs_lock);

	if (dsi_bridge->session_open) {
		session_id = dsi_bridge->session_id;
	} else {
		/*
		 * mipi_dsi_host_ops::transfer() may be called while the host is
		 * disabled. Open a transaction-only DCP session in that case;
		 * atomic_pre_enable() will create the full display session later.
		 */
		ret = dcp_hfi_display_enable(kms->hfi, dsi_bridge->intf_idx,
					     dsi_bridge->fw_display_id,
					     &session_id);
		if (ret)
			return ret;

		temporary_session = true;
	}

	ret = dcp_hfi_dcs_transfer(kms->hfi, session_id, msg,
				   dsi_bridge->dcs_tx_buf, dsi_bridge->dcs_tx_iova,
				   dsi_bridge->dcs_rx_buf, dsi_bridge->dcs_rx_iova, 0,
				   dsi_bridge->hfi_panel_config.ctrl_indices[0]);
	if (temporary_session) {
		int close_ret = dcp_hfi_display_close(kms->hfi, session_id);

		if (ret >= 0 && close_ret)
			ret = close_ret;
	}
	if (ret < 0)
		dev_warn(kms->pm_dev,
			 "DSI%u: live DCS command transfer failed: %zd\n",
			 dsi_bridge->intf_idx, ret);

	return ret;
}

static int dcp_dsi_component_bind(struct device *dev,
				  struct device *master, void *data)
{
	struct dcp_dsi_bridge *dsi_bridge =
		container_of(dev, struct dcp_dsi_bridge, dsi_host_dev);

	dsi_bridge->panel_bridge =
		devm_drm_of_get_bridge(dev, dsi_bridge->dcp_np,
				       dsi_bridge->dcp_port, 0);
	if (IS_ERR(dsi_bridge->panel_bridge)) {
		int ret = PTR_ERR(dsi_bridge->panel_bridge);

		dsi_bridge->panel_bridge = NULL;
		return ret;
	}

	return 0;
}

static void dcp_dsi_component_unbind(struct device *dev,
				     struct device *master, void *data)
{
	struct dcp_dsi_bridge *dsi_bridge =
		container_of(dev, struct dcp_dsi_bridge, dsi_host_dev);

	dsi_bridge->panel_bridge = NULL;
}

static const struct component_ops dcp_dsi_component_ops = {
	.bind = dcp_dsi_component_bind,
	.unbind = dcp_dsi_component_unbind,
};

static const struct mipi_dsi_host_ops dcp_dsi_host_ops = {
	.attach   = dcp_dsi_host_attach,
	.detach   = dcp_dsi_host_detach,
	.transfer = dcp_dsi_host_transfer,
};

static int dcp_dsi_bridge_attach(struct drm_bridge *bridge,
				 struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct dcp_dsi_bridge *dsi_bridge = to_dcp_dsi_bridge(bridge);
	struct drm_bridge *panel_bridge;
	int ret;

	panel_bridge = dsi_bridge->panel_bridge;
	if (!panel_bridge)
		return -EPROBE_DEFER;

	ret = drm_bridge_attach(encoder, panel_bridge, bridge, flags);
	if (ret) {
		drm_err(bridge->dev,
			"DSI%u: panel bridge attach failed: %d\n",
			dsi_bridge->intf_idx, ret);
		return ret;
	}

	drm_dbg_driver(dsi_bridge->kms->dev,
		       "DSI%u: panel bridge attached\n",
		       dsi_bridge->intf_idx);

	dcp_hfi_set_power_control_cb(dsi_bridge->kms->hfi,
				     dsi_bridge->kms, dcp_dsi_power_control_cb);

	return 0;
}

static u32 dcp_dsi_bridge_get_mixer_count(struct dcp_dsi_bridge *dsi_bridge,
					  const struct dcp_dsi_hfi_panel_config *cfg,
					  const struct drm_display_mode *mode)
{
	u32 mixer_count = cfg->topology_mixer_count;

	if (!mixer_count)
		mixer_count = 1;

	if (dsi_bridge->dsi_device && dsi_bridge->dsi_device->dsc) {
		const struct drm_dsc_config *dsc = dsi_bridge->dsi_device->dsc;

		if (dsc->slice_count)
			mixer_count = dsc->slice_count;
		else if (dsc->slice_width && mode->hdisplay)
			mixer_count = DIV_ROUND_UP(mode->hdisplay,
						   dsc->slice_width);
	}

	return mixer_count;
}

static void dcp_dsi_bridge_publish_session(struct dcp_dsi_bridge *dsi_bridge)
{
	struct dcp_dpu_kms *kms = dsi_bridge->kms;
	struct drm_crtc *crtc;

	crtc = dcp_dpu_kms_get_crtc(kms, dsi_bridge->fw_display_id);
	if (crtc)
		dcp_crtc_set_session(crtc, dsi_bridge->session_id);
	else
		drm_err(kms->dev, "DSI%u: no CRTC for session %u\n",
			dsi_bridge->intf_idx, dsi_bridge->session_id);
}

static void dcp_dsi_bridge_clear_session(struct dcp_dsi_bridge *dsi_bridge)
{
	dcp_crtc_clear_session(dcp_dpu_kms_get_crtc(dsi_bridge->kms,
						    dsi_bridge->fw_display_id));
}

static int dcp_dsi_bridge_setup_dsc(struct dcp_dsi_bridge *dsi_bridge,
				    struct dcp_dsi_hfi_panel_config *cfg,
				    const struct drm_display_mode *mode)
{
	struct drm_dsc_config *dsc = dsi_bridge->dsi_device->dsc;
	enum drm_dsc_params_type params_type;
	unsigned int i;
	int ret;

	dsc->pic_width = mode->hdisplay;
	dsc->pic_height = mode->vdisplay;
	dsc->convert_rgb = !dsc->native_422 && !dsc->native_420;
	dsc->slice_chunk_size = DIV_ROUND_UP(dsc->slice_width *
					dsc->bits_per_pixel, 8 * 16);
	dsc->simple_422 = 0;
	dsc->vbr_enable = 0;

	if (dsc->dsc_version_major != 1)
		return -EOPNOTSUPP;

	switch (dsc->dsc_version_minor) {
	case 1:
		if (dsc->native_422 || dsc->native_420)
			return -EOPNOTSUPP;
		params_type = DRM_DSC_1_1_PRE_SCR;
		break;
	case 2:
		if (dsc->native_422)
			params_type = DRM_DSC_1_2_422;
		else if (dsc->native_420)
			params_type = DRM_DSC_1_2_420;
		else
			params_type = DRM_DSC_1_2_444;
		break;
	default:
		return -EOPNOTSUPP;
	}

	drm_dsc_set_const_params(dsc);
	drm_dsc_set_rc_buf_thresh(dsc);
	ret = drm_dsc_setup_rc_params(dsc, params_type);
	if (ret)
		return ret;

	dsc->initial_scale_value = drm_dsc_initial_scale_value(dsc);
	dsc->line_buf_depth = dsc->bits_per_component + 1;
	ret = drm_dsc_compute_rc_parameters(dsc);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(cfg->dsc_rc_min_qp); i++) {
		cfg->dsc_rc_min_qp[i] = dsc->rc_range_params[i].range_min_qp;
		cfg->dsc_rc_max_qp[i] = dsc->rc_range_params[i].range_max_qp;
		cfg->dsc_rc_bpg_offsets[i] = dsc->rc_range_params[i].range_bpg_offset;
	}

	return 0;
}

int dcp_dsi_bridge_prepare_session(struct dcp_dsi_bridge *dsi_bridge,
				   const struct drm_display_mode *mode)
{
	struct dcp_dpu_kms *kms = dsi_bridge->kms;
	struct dcp_dsi_bridge_state *state = dcp_dsi_bridge_get_state(dsi_bridge);
	struct dcp_dsi_hfi_panel_config cfg;
	int ret;
	u32 opened_session_id;

	if (!mode)
		return -ENODEV;

	guard(mutex)(&dsi_bridge->session_lock);
	if (dsi_bridge->session_open)
		return 0;

	dsi_bridge->session_id = dsi_bridge->fw_display_id;

	if (!dsi_bridge->dsi_device)
		return -ENODEV;

	dcp_dsi_bridge_build_hfi_config(dsi_bridge, state, &cfg);

	if (dsi_bridge->dsi_device->dsc) {
		ret = dcp_dsi_bridge_setup_dsc(dsi_bridge, &cfg, mode);
		if (ret) {
			drm_err(kms->dev, "DSI%u: DSC setup failed: %d\n",
				dsi_bridge->intf_idx, ret);
			goto err_close;
		}
	}

	ret = dcp_hfi_panel_init(kms->hfi, dsi_bridge->intf_idx,
				 mode,
				 dsi_bridge->dsi_device,
				 &cfg);
	if (ret) {
		drm_err(kms->dev, "DSI%u: HFI PANEL_INIT failed: %d\n",
			dsi_bridge->intf_idx, ret);
		goto err_close;
	}

	if (mode->clock) {
		u64 link_clock;

		link_clock = dcp_dsi_link_clock(dsi_bridge, &cfg,
						mode);
		ret = dcp_hfi_display_mode_validate(kms->hfi,
						    dsi_bridge->session_id,
						    mode, link_clock);
		if (ret) {
			drm_err(kms->dev, "DSI%u: HFI MODE_VALIDATE failed: %d\n",
				dsi_bridge->intf_idx, ret);
			goto err_close;
		}

		ret = dcp_hfi_display_set_mode(kms->hfi,
					       dsi_bridge->session_id,
					       mode,
					       dcp_dsi_link_clock(dsi_bridge, &cfg,
								  mode));
		if (ret) {
			drm_err(kms->dev, "DSI%u: HFI SET_MODE failed: %d\n",
				dsi_bridge->intf_idx, ret);
			goto err_close;
		}
	} else {
		drm_warn(kms->dev, "DSI%u: no valid mode, skipping SET_MODE\n",
			 dsi_bridge->intf_idx);
	}

	dcp_hfi_set_power_control_cb(kms->hfi, kms,
				     dcp_dsi_power_control_cb);

	reinit_completion(&dsi_bridge->power_done);

	ret = dcp_hfi_display_power_register(kms->hfi, dsi_bridge->session_id);
	if (ret) {
		drm_err(kms->dev, "DSI%u: HFI POWER_REGISTER failed: %d\n",
			dsi_bridge->intf_idx, ret);
		goto err_close;
	}

	ret = dcp_hfi_display_enable(kms->hfi,
				     dsi_bridge->intf_idx,
				     dsi_bridge->fw_display_id,
				     &opened_session_id);
	if (ret) {
		drm_err(kms->dev, "DSI%u: HFI DISPLAY_ENABLE failed: %d\n",
			dsi_bridge->intf_idx, ret);
		goto err_close;
	}
	dsi_bridge->session_id   = opened_session_id;
	dsi_bridge->session_open = true;
	dcp_dsi_bridge_publish_session(dsi_bridge);

	drm_dbg_driver(kms->dev, "DSI%u: session prepared (session_id=%u)\n",
		       dsi_bridge->intf_idx, dsi_bridge->session_id);
	return 0;

err_close:
	dcp_hfi_clear_power_control_cb(kms->hfi, kms);
	dcp_hfi_display_close(kms->hfi, dsi_bridge->session_id);
	dsi_bridge->session_id   = 0;
	dsi_bridge->session_open = false;
	dcp_dsi_bridge_clear_session(dsi_bridge);

	return ret;
}

static void dcp_dsi_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					     struct drm_atomic_commit *state)
{
	struct dcp_dsi_bridge *dsi_bridge = to_dcp_dsi_bridge(bridge);
	int ret;

	ret = dsi_bridge->session_open ? 0 : -ENODEV;
	dcp_dsi_bridge_get_state(dsi_bridge)->prepare_error = ret;
	if (ret)
		drm_err(bridge->dev, "DSI%u: session prepare failed: %d\n",
			dsi_bridge->intf_idx, ret);
}

static void dcp_dsi_bridge_atomic_enable(struct drm_bridge *bridge,
					 struct drm_atomic_commit *state)
{
	struct dcp_dsi_bridge *dsi_bridge = to_dcp_dsi_bridge(bridge);
	struct dcp_dpu_kms *kms = dsi_bridge->kms;
	bool scan_start_registered = false;
	bool scan_complete_registered = false;
	bool autorefresh_enabled = false;
	int ret;

	drm_dbg_driver(kms->dev,
		       "DSI%u: bridge atomic_enable (session_id=%u)\n",
		       dsi_bridge->intf_idx, dsi_bridge->session_id);

	if (dcp_dsi_bridge_get_state(dsi_bridge)->prepare_error)
		return;
	if (!dsi_bridge->session_open)
		return;

	ret = dcp_hfi_display_post_enable(kms->hfi, dsi_bridge->session_id);
	if (ret) {
		drm_err(bridge->dev,
			"DSI%u: HFI DISPLAY_POST_ENABLE failed: %d\n",
			dsi_bridge->intf_idx, ret);
		goto err_close;
	}

	dcp_dsi_bridge_publish_session(dsi_bridge);

	ret = dcp_hfi_display_event_register(kms->hfi,
					     dsi_bridge->session_id, HFI_EVENT_FRAME_SCAN_START);
	if (ret)
		goto err_close;
	scan_start_registered = true;

	ret = dcp_hfi_display_event_register(kms->hfi,
					     dsi_bridge->session_id, HFI_EVENT_FRAME_SCAN_COMPLETE);
	if (ret)
		goto err_close;
	scan_complete_registered = true;

	if (dcp_dsi_bridge_cmd_mode(dsi_bridge)) {
		ret = dcp_hfi_crtc_set_autorefresh(kms->hfi,
						   dsi_bridge->session_id, true, 1);
		if (ret) {
			drm_err(bridge->dev,
				"DSI%u: initial autorefresh setup failed: %d\n",
				dsi_bridge->intf_idx, ret);
			goto err_close;
		}
		autorefresh_enabled = true;
	}

	drm_dbg_driver(bridge->dev,
		       "DSI%u: enable complete (session_id=%u)\n",
		       dsi_bridge->intf_idx, dsi_bridge->session_id);
	return;

err_close:
	if (autorefresh_enabled)
		dcp_hfi_crtc_set_autorefresh(kms->hfi, dsi_bridge->session_id,
					     false, 0);
	if (scan_complete_registered)
		dcp_hfi_display_event_deregister(kms->hfi, dsi_bridge->session_id,
						 HFI_EVENT_FRAME_SCAN_COMPLETE);
	if (scan_start_registered)
		dcp_hfi_display_event_deregister(kms->hfi, dsi_bridge->session_id,
						 HFI_EVENT_FRAME_SCAN_START);
	drm_err(bridge->dev,
		"DSI%u: bridge enable failed; session closed\n",
		dsi_bridge->intf_idx);
	dcp_hfi_display_close(kms->hfi, dsi_bridge->session_id);
	{
		guard(mutex)(&dsi_bridge->session_lock);
		dsi_bridge->session_id   = 0;
		dsi_bridge->session_open = false;
	}
	dcp_dsi_bridge_clear_session(dsi_bridge);
}

static void dcp_dsi_bridge_atomic_disable(struct drm_bridge *bridge,
					  struct drm_atomic_commit *state)
{
	struct dcp_dsi_bridge *dsi_bridge = to_dcp_dsi_bridge(bridge);
	struct dcp_dpu_kms *kms = dsi_bridge->kms;
	int ret;

	drm_dbg_driver(kms->dev,
		       "DSI%u: atomic_disable (session_open=%d session_id=%u)\n",
		       dsi_bridge->intf_idx, dsi_bridge->session_open,
		       dsi_bridge->session_id);

	guard(mutex)(&dsi_bridge->session_lock);
	if (!dsi_bridge->session_open)
		return;

	if (dcp_dsi_bridge_cmd_mode(dsi_bridge)) {
		ret = dcp_hfi_crtc_set_autorefresh(kms->hfi,
						   dsi_bridge->session_id, false, 0);
		if (ret)
			drm_warn(bridge->dev,
				 "DSI%u: autorefresh disable failed: %d\n",
				 dsi_bridge->intf_idx, ret);
	}

	dcp_hfi_display_event_deregister(kms->hfi, dsi_bridge->session_id,
					 HFI_EVENT_FRAME_SCAN_COMPLETE);
	dcp_hfi_display_event_deregister(kms->hfi, dsi_bridge->session_id,
					 HFI_EVENT_FRAME_SCAN_START);

	ret = dcp_hfi_display_disable(kms->hfi, dsi_bridge->session_id);
	if (ret)
		drm_warn(bridge->dev,
			 "DSI%u: HFI DISPLAY_DISABLE failed: %d\n",
			 dsi_bridge->intf_idx, ret);
}

static void dcp_dsi_bridge_atomic_post_disable(struct drm_bridge *bridge,
					       struct drm_atomic_commit *state)
{
	struct dcp_dsi_bridge *dsi_bridge = to_dcp_dsi_bridge(bridge);
	struct dcp_dpu_kms *kms = dsi_bridge->kms;
	int ret;

	drm_dbg_driver(kms->dev,
		       "DSI%u: atomic_post_disable\n",
		       dsi_bridge->intf_idx);

	guard(mutex)(&dsi_bridge->session_lock);
	if (!dsi_bridge->session_open)
		return;

	ret = dcp_hfi_display_post_disable(kms->hfi, dsi_bridge->session_id);
	if (ret)
		drm_warn(bridge->dev,
			 "DSI%u: HFI DISPLAY_POST_DISABLE failed: %d\n",
			 dsi_bridge->intf_idx, ret);

	ret = dcp_hfi_send_sync(kms->hfi,
				HFI_COMMAND_DISPLAY_CLOSE,
				HFI_CMD_BUFF_DISPLAY, dsi_bridge->session_id,
				HFI_PAYLOAD_NONE, NULL, 0,
				HFI_TX_FLAGS_RESPONSE_REQUIRED |
				HFI_TX_FLAGS_NON_DISCARDABLE,
				NULL, 0,
				2000 /* ms */);
	if (ret)
		drm_warn(bridge->dev,
			 "DSI%u: HFI DISPLAY_CLOSE failed: %d\n",
			 dsi_bridge->intf_idx, ret);

	dsi_bridge->session_id   = 0;
	dsi_bridge->session_open = false;

	dcp_dsi_bridge_clear_session(dsi_bridge);
}

static int dcp_dsi_bridge_atomic_check(struct drm_bridge *bridge,
				       struct drm_bridge_state *bridge_state,
				       struct drm_crtc_state *crtc_state,
				       struct drm_connector_state *conn_state)
{
	struct dcp_dsi_bridge *dsi_bridge = to_dcp_dsi_bridge(bridge);
	struct dcp_dsi_bridge_state *state =
		to_dcp_dsi_bridge_state(bridge_state);
	struct dcp_dpu_kms *kms = dsi_bridge->kms;
	const struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	u32 max_width, max_height;
	u32 mixer_count, bpc;

	state->prepare_error = 0;

	if (!mode->clock || !drm_mode_vrefresh(mode))
		return -EINVAL;

	if (dsi_bridge->dsi_device &&
	    (!dsi_bridge->dsi_device->lanes ||
	     !dcp_dsi_link_clock(dsi_bridge, &dsi_bridge->hfi_panel_config, mode)))
		return -EINVAL;

	if (dsi_bridge->dsi_device && dsi_bridge->dsi_device->dsc) {
		state->panel_state.topology_mixer_count =
			dcp_dsi_bridge_get_mixer_count(dsi_bridge,
						       &dsi_bridge->hfi_panel_config, mode);
		state->panel_state.topology_enc_count =
			state->panel_state.topology_mixer_count;
		state->panel_state.topology_intf_count = 1;
	}

	mixer_count = dcp_dsi_bridge_get_mixer_count(dsi_bridge,
						     &dsi_bridge->hfi_panel_config, mode);
	bpc = dsi_bridge->hfi_panel_config.dsc_bits_per_component ?
		dsi_bridge->hfi_panel_config.dsc_bits_per_component : 8;
	dcp_crtc_set_topology_state(crtc_state, mixer_count, bpc);

	dcp_mode_limits(mode, kms->caps->max_width, kms->caps->max_height,
			&max_width, &max_height);

	if (mode->hdisplay > max_width || mode->vdisplay > max_height) {
		drm_err(dsi_bridge->kms->dev,
			"DSI%u: mode %dx%d exceeds DCP caps %dx%d\n",
				 dsi_bridge->intf_idx, mode->hdisplay, mode->vdisplay,
				 kms->caps->max_width, kms->caps->max_height);
		return -EINVAL;
	}
	return 0;
}

static const struct drm_bridge_funcs dcp_dsi_bridge_funcs = {
	.attach                  = dcp_dsi_bridge_attach,
	.atomic_pre_enable       = dcp_dsi_bridge_atomic_pre_enable,
	.atomic_enable           = dcp_dsi_bridge_atomic_enable,
	.atomic_disable          = dcp_dsi_bridge_atomic_disable,
	.atomic_post_disable     = dcp_dsi_bridge_atomic_post_disable,
	.atomic_check            = dcp_dsi_bridge_atomic_check,
	.atomic_create_state     = dcp_dsi_bridge_atomic_create_state,
	.atomic_duplicate_state  = dcp_dsi_bridge_atomic_duplicate_state,
	.atomic_destroy_state    = dcp_dsi_bridge_atomic_destroy_state,
};

#define DCP_DSI_DUAL_MAP_ALIGN	SZ_1K

static int dcp_dsi_dual_map_buf(struct dcp_dpu_kms *kms,
				struct device *dma_dev, void *cpu_addr,
				dma_addr_t iova, size_t size, int prot)
{
	struct sg_table sgt = { 0 };
	struct iommu_domain *domain;
	ssize_t mapped;
	int ret;

	if (!kms->iommu_dev || !cpu_addr || !iova ||
	    !IS_ALIGNED(iova, DCP_DSI_DUAL_MAP_ALIGN))
		return -EINVAL;

	domain = iommu_get_domain_for_dev(kms->iommu_dev);
	if (!domain)
		return -ENODEV;

	ret = dma_get_sgtable(dma_dev, &sgt, cpu_addr, iova, size);
	if (ret)
		return ret;

	mapped = iommu_map_sgtable(domain, iova, &sgt, prot);
	if (mapped != PAGE_ALIGN(size)) {
		if (mapped > 0)
			iommu_unmap(domain, iova, mapped);
		ret = mapped < 0 ? mapped : -EIO;
		drm_err(kms->dev,
			"failed to dual-map iova=%pad into iommu_dev domain: %zd\n",
			&iova, mapped);
		sg_free_table(&sgt);
		return ret;
	}

	sg_free_table(&sgt);
	return 0;
}

static void dcp_dsi_dual_unmap_buf(struct dcp_dpu_kms *kms, dma_addr_t iova,
				   size_t size)
{
	struct iommu_domain *domain;

	if (!kms->iommu_dev || !iova)
		return;

	domain = iommu_get_domain_for_dev(kms->iommu_dev);
	if (!domain)
		return;

	iommu_unmap(domain, iova, PAGE_ALIGN(size));
}

static void dcp_dsi_init_hfi_panel_defaults(struct dcp_dsi_hfi_panel_config *cfg,
					    u32 intf_idx)
{
	memset(cfg, 0, sizeof(*cfg));

	cfg->display_type = HFI_PANEL_DISPLAY_TYPE_BUILT_IN_0;
	cfg->num_ctrls = 1;
	cfg->ctrl_indices[0] = intf_idx;
	cfg->num_phys = 1;
	cfg->phy_indices[0] = intf_idx;

	cfg->dma_trigger = HFI_PANEL_TRIGGER_SW;
	cfg->stream_trigger = HFI_PANEL_TRIGGER_NONE;
	cfg->te_dcs_command = 1;
	cfg->wr_mem_start = 0x2C;
	cfg->wr_mem_continue = 0x3C;

	cfg->phy_type = HFI_PANEL_PHY_OLED;
	cfg->bllp_eof_power_mode = true;
	cfg->bllp_power_mode = true;
	cfg->lane_map = 1;
	cfg->color_order = 1;

	cfg->jitter_numer = 4;
	cfg->jitter_denom = 1;
	cfg->dcs_tx_buf_size = DCP_DSI_DCS_TX_BUF_SIZE;

	cfg->topology_mixer_count = 1;
	cfg->topology_intf_count = 1;
}

struct drm_bridge *dcp_dsi_bridge_init(struct dcp_dpu_kms *kms,
				       struct device_node *np,
				       struct device_node *dcp_np,
				       u32 dcp_port, u32 intf_idx,
				       u32 fw_display_id)
{
	struct dcp_dsi_bridge *dsi_bridge;
	struct dcp_dsi_hfi_panel_config *cfg;
	struct device *dev = kms->pm_dev;
	struct device *dma_dev = kms->hfi->dma_dev;
	int ret;

	dsi_bridge = devm_drm_bridge_alloc(dev, struct dcp_dsi_bridge,
					   bridge, &dcp_dsi_bridge_funcs);
	if (IS_ERR(dsi_bridge))
		return ERR_CAST(dsi_bridge);

	dsi_bridge->dcp_np        = of_node_get(dcp_np);
	dsi_bridge->dcp_port      = dcp_port;
	dsi_bridge->kms           = kms;
	dsi_bridge->intf_idx      = intf_idx;
	dsi_bridge->fw_display_id = fw_display_id;

	INIT_WORK(&dsi_bridge->power_control_work, dcp_dsi_power_control_work);
	init_completion(&dsi_bridge->power_done);
	mutex_init(&dsi_bridge->session_lock);
	mutex_init(&dsi_bridge->dcs_lock);

	cfg = &dsi_bridge->hfi_panel_config;
	dcp_dsi_init_hfi_panel_defaults(cfg, intf_idx);

	/*
	 * Allocate the buffers for the firmware DMA domain here. The second
	 * mapping into the display domain is deferred until msm_kms_init_vm()
	 * has installed the final display IOMMU domain.
	 */
	dsi_bridge->dcs_tx_buf = dma_alloc_coherent(dma_dev, DCP_DSI_DCS_TX_BUF_SIZE,
						    &dsi_bridge->dcs_tx_iova, GFP_KERNEL);
	if (!dsi_bridge->dcs_tx_buf) {
		ret = -ENOMEM;
		goto err_unwind_dma;
	}

	dsi_bridge->hfi_panel_config.dcs_tx_dva = dsi_bridge->dcs_tx_iova;
	dsi_bridge->hfi_panel_config.dcs_tx_iova = dsi_bridge->dcs_tx_iova;

	dsi_bridge->dcs_rx_buf = dma_alloc_coherent(dma_dev, DCP_DSI_DCS_RX_BUF_SIZE,
						    &dsi_bridge->dcs_rx_iova, GFP_KERNEL);
	if (!dsi_bridge->dcs_rx_buf) {
		ret = -ENOMEM;
		goto err_unwind_dma;
	}

	device_initialize(&dsi_bridge->dsi_host_dev);
	dsi_bridge->dsi_host_dev.release = dcp_dsi_host_dev_release;
	dsi_bridge->dsi_host_dev.parent  = dev;
	dsi_bridge->dsi_host_dev.of_node = of_node_get(np);
	dev_set_name(&dsi_bridge->dsi_host_dev, "%pOFn", np);
	dsi_bridge->dsi_host.ops = &dcp_dsi_host_ops;
	dsi_bridge->dsi_host.dev = &dsi_bridge->dsi_host_dev;

	ret = device_add(&dsi_bridge->dsi_host_dev);
	if (ret) {
		dev_err(dev,
			"DSI%u: failed to add dsi_host_dev: %d\n", intf_idx, ret);
		put_device(&dsi_bridge->dsi_host_dev);
		goto err_unwind_dma;
	}

	ret = mipi_dsi_host_register(&dsi_bridge->dsi_host);
	if (ret) {
		dev_err(dev,
			"DSI%u: failed to register MIPI DSI host: %d\n",
			intf_idx, ret);
		device_del(&dsi_bridge->dsi_host_dev);
		put_device(&dsi_bridge->dsi_host_dev);
		goto err_unwind_dma;
	}

	dsi_bridge->bridge.of_node = of_node_get(np);
	dsi_bridge->bridge.type    = DRM_MODE_CONNECTOR_DSI;

	drm_bridge_add(&dsi_bridge->bridge);

	return &dsi_bridge->bridge;

err_unwind_dma:
	if (dsi_bridge->dcs_rx_buf) {
		dma_free_coherent(dma_dev, DCP_DSI_DCS_RX_BUF_SIZE,
				  dsi_bridge->dcs_rx_buf,
				  dsi_bridge->dcs_rx_iova);
	}
	if (dsi_bridge->dcs_tx_buf) {
		dma_free_coherent(dma_dev, DCP_DSI_DCS_TX_BUF_SIZE,
				  dsi_bridge->dcs_tx_buf,
				  dsi_bridge->dcs_tx_iova);
	}
	cancel_work_sync(&dsi_bridge->power_control_work);
	mutex_destroy(&dsi_bridge->session_lock);
	mutex_destroy(&dsi_bridge->dcs_lock);
	return ERR_PTR(ret);
}

int dcp_dsi_bridge_map_buffers(struct dcp_dsi_bridge *dsi_bridge)
{
	struct dcp_dpu_kms *kms = dsi_bridge->kms;
	struct device *dma_dev = kms->hfi->dma_dev;
	int ret;

	if (dsi_bridge->dcs_mapped)
		return 0;

	ret = dcp_dsi_dual_map_buf(kms, dma_dev, dsi_bridge->dcs_tx_buf,
				   dsi_bridge->dcs_tx_iova,
				   DCP_DSI_DCS_TX_BUF_SIZE, IOMMU_READ);
	if (ret)
		return ret;

	ret = dcp_dsi_dual_map_buf(kms, dma_dev, dsi_bridge->dcs_rx_buf,
				   dsi_bridge->dcs_rx_iova,
				   DCP_DSI_DCS_RX_BUF_SIZE, IOMMU_WRITE);
	if (ret) {
		dcp_dsi_dual_unmap_buf(kms, dsi_bridge->dcs_tx_iova,
				       DCP_DSI_DCS_TX_BUF_SIZE);
		return ret;
	}

	dsi_bridge->dcs_mapped = true;
	drm_dbg_driver(kms->dev,
		       "DSI%u: mapped DCS buffers tx=%pad rx=%pad in display domain\n",
		       dsi_bridge->intf_idx, &dsi_bridge->dcs_tx_iova,
		       &dsi_bridge->dcs_rx_iova);

	return 0;
}

void dcp_dsi_bridge_unmap_buffers(struct dcp_dsi_bridge *dsi_bridge)
{
	struct dcp_dpu_kms *kms = dsi_bridge->kms;

	if (!dsi_bridge->dcs_mapped)
		return;

	dcp_dsi_dual_unmap_buf(kms, dsi_bridge->dcs_tx_iova,
			       DCP_DSI_DCS_TX_BUF_SIZE);
	dcp_dsi_dual_unmap_buf(kms, dsi_bridge->dcs_rx_iova,
			       DCP_DSI_DCS_RX_BUF_SIZE);
	dsi_bridge->dcs_mapped = false;
}

void dcp_dsi_bridge_cleanup(struct dcp_dsi_bridge *dsi_bridge)
{
	struct device *dma_dev = dsi_bridge->kms->hfi->dma_dev;

	dcp_hfi_clear_power_control_cb(dsi_bridge->kms->hfi, dsi_bridge->kms);
	cancel_work_sync(&dsi_bridge->power_control_work);

	drm_bridge_remove(&dsi_bridge->bridge);
	of_node_put(dsi_bridge->bridge.of_node);
	of_node_put(dsi_bridge->dcp_np);

	mipi_dsi_host_unregister(&dsi_bridge->dsi_host);
	device_del(&dsi_bridge->dsi_host_dev);
	put_device(&dsi_bridge->dsi_host_dev);

	dcp_dsi_bridge_unmap_buffers(dsi_bridge);
	dma_free_coherent(dma_dev, DCP_DSI_DCS_TX_BUF_SIZE,
			  dsi_bridge->dcs_tx_buf, dsi_bridge->dcs_tx_iova);

	dma_free_coherent(dma_dev, DCP_DSI_DCS_RX_BUF_SIZE,
			  dsi_bridge->dcs_rx_buf, dsi_bridge->dcs_rx_iova);

	mutex_destroy(&dsi_bridge->session_lock);
	mutex_destroy(&dsi_bridge->dcs_lock);
}
