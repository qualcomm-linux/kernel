// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * HFI commands for panel initialization and DCS transfers.
 * Firmware owns the DSI controller and PHY.
 */

#include <linux/device.h>
#include <linux/math64.h>
#include <drm/drm_modes.h>
#include <drm/drm_mipi_dsi.h>

#include "dcp_dsi_panel.h"
#include "dcp_hfi.h"
#include "dcp_hfi_priv.h"
#include "hfi/hfi_commands_display.h"
#include "hfi/hfi_commands_panel.h"
#include "hfi/hfi_defs_common.h"
#include "hfi/hfi_defs_panel.h"
#include "hfi/hfi_packet.h"
#include "hfi/hfi_packer.h"
#include "hfi/hfi_properties_panel.h"

#define DCP_DSI_CTRL_CMD_READ              BIT(0)
#define DCP_DSI_CTRL_CMD_FETCH_MEMORY      BIT(5)
#define DCP_DSI_CTRL_CMD_LAST_COMMAND      BIT(6)

static u32 dcp_bpp_to_hfi(u32 bpp)
{
	switch (bpp) {
	case 16: return HFI_PANEL_BPP_16;
	case 18: return HFI_PANEL_BPP_18;
	case 30: return HFI_PANEL_BPP_30;
	case 24:
	default: return HFI_PANEL_BPP_24;
	}
}

static u32 dcp_dsi_traffic_mode(const struct mipi_dsi_device *dsi)
{
	if (!(dsi->mode_flags & MIPI_DSI_MODE_VIDEO))
		return HFI_PANEL_TRAFFIC_NON_BURST_SYNC_EVENT_MODE;
	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		return HFI_PANEL_TRAFFIC_BURST_MODE;
	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		return HFI_PANEL_TRAFFIC_NON_BURST_SYNC_PULSE_MODE;
	return HFI_PANEL_TRAFFIC_NON_BURST_SYNC_EVENT_MODE;
}

static u32 dcp_lanes_to_hfi(u32 num_lanes)
{
	u32 mask = 0;

	if (num_lanes >= 1)
		mask |= HFI_PANEL_LANE_0;
	if (num_lanes >= 2)
		mask |= HFI_PANEL_LANE_1;
	if (num_lanes >= 3)
		mask |= HFI_PANEL_LANE_2;
	if (num_lanes >= 4)
		mask |= HFI_PANEL_LANE_3;

	return mask ?: HFI_PANEL_LANE_0;
}

static bool dcp_hfi_panel_wait(struct dcp_hfi *hfi,
			       struct dcp_hfi_completion *comp)
{
	unsigned long flags;

	if (wait_for_completion_timeout(&comp->done,
					msecs_to_jiffies(DCP_HFI_TIMEOUT_DISPLAY_MS)))
		return false;

	/* Serialize removal with response dispatch before the stack object dies. */
	spin_lock_irqsave(&hfi->resp_drain_lock, flags);
	dcp_hfi_pending_find(hfi, comp->packet_id);
	spin_unlock_irqrestore(&hfi->resp_drain_lock, flags);
	return true;
}

int dcp_hfi_panel_init(struct dcp_hfi *hfi, u32 intf_idx,
		       const struct drm_display_mode *mode,
		       const struct mipi_dsi_device *dsi,
		       const struct dcp_dsi_hfi_panel_config *cfg)
{
	u32 num_lanes;
	u32 bpp;
	bool is_cmd_mode;
	u32 display_type;
	u32 vsync_source;
	u32 traffic_mode;
	u32 phy_type;

	struct dcp_hfi_buf *buf1, *buf2;
	struct hfi_cmd_buff_hdl hdl1, hdl2;
	struct hfi_header_info hdr_info1, hdr_info2;
	struct hfi_packet_info pkt_info;
	struct dcp_hfi_completion comp1, comp2, comp3;
	u32 payload[128];
	u32 *p;
	u32 packet_id, packet_id2, packet_id3;
	u64 clk_hz;
	int ret;
	bool timeout1, timeout2, timeout3;
	u32 link_bpp_x16;

	if (!hfi || !mode || !dsi || !dsi->lanes || !cfg)
		return -EINVAL;
	if (cfg->num_ctrls > ARRAY_SIZE(cfg->ctrl_indices) ||
	    cfg->num_phys > ARRAY_SIZE(cfg->phy_indices))
		return -EINVAL;

	display_type = cfg->display_type;
	vsync_source = cfg->vsync_source;
	phy_type = cfg->phy_type;
	num_lanes = dsi->lanes;
	bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	if (bpp < 0)
		return -EINVAL;
	is_cmd_mode = !(dsi->mode_flags & MIPI_DSI_MODE_VIDEO);
	traffic_mode = dcp_dsi_traffic_mode(dsi);
	link_bpp_x16 = bpp << 4;

	buf1 = dcp_hfi_buf_alloc(hfi);
	if (!buf1) {
		dev_err(hfi->dev, "HFI PANEL_INIT: TX buffer exhausted\n");
		return -ENOMEM;
	}

	hdl1.cmd_buffer = buf1->data;
	hdl1.size       = DCP_HFI_BUF_SIZE;

	hdr_info1.num_packets   = 0;
	hdr_info1.cmd_buff_type = HFI_CMD_BUFF_DISPLAY;
	hdr_info1.object_id     = intf_idx;
	hdr_info1.header_id     = dcp_hfi_next_packet_id(hfi);

	ret = hfi_create_header(&hdl1, &hdr_info1);
	if (ret) {
		dev_err(hfi->dev, "PANEL_INIT: hfi_create_header (A) failed: %d\n",
			ret);
		goto err_free_buf1;
	}

	{
		u32 num_props = 0;

		p = payload + 1;
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_TIMING_MODE_COUNT, 1);

		/* DCS TX buffer is described by both DVA and IOVA properties. */
		if (cfg->dcs_tx_iova && cfg->dcs_tx_dva &&
		    !(cfg->dcs_tx_iova & 0x3ff) && !(cfg->dcs_tx_dva & 0x3ff)) {
			num_props++;
			p += HFI_PACK(p, HFI_PROPERTY_PANEL_DCS_CMD_TX_BUF_DVA,
				       lower_32_bits(cfg->dcs_tx_dva),
				       upper_32_bits(cfg->dcs_tx_dva),
				       cfg->dcs_tx_buf_size, 0, 0);
			num_props++;
			p += HFI_PACK(p, HFI_PROPERTY_PANEL_DCS_CMD_TX_BUF_IOVA,
				       lower_32_bits(cfg->dcs_tx_iova),
				       upper_32_bits(cfg->dcs_tx_iova),
				       cfg->dcs_tx_buf_size, 0, 0);
		}
		payload[0] = num_props;
	}

	packet_id = dcp_hfi_next_packet_id(hfi);
	pkt_info.cmd          = HFI_COMMAND_PANEL_INIT_PANEL_CAPS;
	pkt_info.id           = intf_idx & 0xFFFF;
	pkt_info.flags        = HFI_TX_FLAGS_RESPONSE_REQUIRED |
				 HFI_TX_FLAGS_NON_DISCARDABLE;
	pkt_info.packet_id    = packet_id;
	pkt_info.payload_type = HFI_PAYLOAD_U32_ARRAY;
	pkt_info.payload_size = (u32)((p - payload) * sizeof(u32));
	pkt_info.payload_ptr  = payload;

	ret = hfi_create_full_packet(&hdl1, &pkt_info);
	if (ret)
		goto err_free_buf1;

	dev_dbg(hfi->dev,
		"intf%u: DCP mode %dx%d@%dHz clk=%ukHz ht=%u hs=%u/%u vt=%u vs=%u/%u f=0x%x\n",
		intf_idx, mode->hdisplay, mode->vdisplay,
		drm_mode_vrefresh(mode), mode->clock,
		mode->htotal, mode->hsync_start, mode->hsync_end,
		mode->vtotal, mode->vsync_start, mode->vsync_end,
		mode->flags);

	{
		u32 te_dcs     = cfg->te_dcs_command;
		u32 dma_trig   = cfg->dma_trigger;
		u32 str_trig   = cfg->stream_trigger;
		u32 wr_start   = cfg->wr_mem_start;
		u32 wr_cont    = cfg->wr_mem_continue;
		u32 lane_map   = cfg->lane_map;
		u32 color_ord  = cfg->color_order;
		u32 vc_id      = dsi->channel;
		u32 esd_mode   = cfg->esd_mode;
		u32 dma_win    = cfg->dma_sched_window;
		bool lp11      = cfg->lp11_init;
		bool cphy      = cfg->cphy_mode;
		bool ulps      = cfg->ulps_supported;
		bool tx_eot    = !(dsi->mode_flags & MIPI_DSI_MODE_NO_EOT_PACKET);
		bool bllp_eof  = cfg->bllp_eof_power_mode;
		bool bllp_pwr  = cfg->bllp_power_mode;
		u32 num_ctrls  = cfg->num_ctrls;
		u32 num_phys   = cfg->num_phys;
		u32 num_props = 0;

		p = payload + 1;

		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_PHYSICAL_TYPE, phy_type);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_BPP, dcp_bpp_to_hfi(bpp));
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_LANES_STATE,
			       dcp_lanes_to_hfi(num_lanes));
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_OPERATING_MODE,
			       is_cmd_mode ? HFI_PANEL_CMD_MODE_8_BIT :
			       HFI_PANEL_VIDEO_MODE_8_BIT);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_DISPLAY_TYPE, display_type);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_VSYNC_SOURCE, vsync_source);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_TRAFFIC_MODE, traffic_mode);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_TE_DCS_COMMAND, te_dcs);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_DMA_TRIGGER, dma_trig);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_STREAM_TRIGGER, str_trig);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_WR_MEM_START, wr_start);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_WR_MEM_CONTINUE, wr_cont);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_LP11_INIT, lp11 ? HFI_TRUE : HFI_FALSE);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_CPHY_MODE, cphy ? HFI_TRUE : HFI_FALSE);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_ULPS_SUPPORTED,
			       ulps ? HFI_TRUE : HFI_FALSE);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_TX_EOT_APPEND,
			       tx_eot ? HFI_TRUE : HFI_FALSE);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_BLLP_EOF_POWER_MODE,
			       bllp_eof ? HFI_TRUE : HFI_FALSE);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_BLLP_POWER_MODE,
			       bllp_pwr ? HFI_TRUE : HFI_FALSE);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_COLOR_ORDER, color_ord);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_VIRTUAL_CHANNEL_ID, vc_id);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_LANE_MAP, lane_map);
		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_ESD_CONFIG,
			       10 * sizeof(u32), esd_mode,
			       0, 0, 0, 0, 0, 0, 0, 0);

		num_props++;
		*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_CTRL_NUM, 0, 1 + num_ctrls);
		*p++ = num_ctrls;
		memcpy(p, cfg->ctrl_indices, num_ctrls * sizeof(*p));
		p += num_ctrls;

		num_props++;
		*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_PHY_NUM, 0, 1 + num_phys);
		*p++ = num_phys;
		memcpy(p, cfg->phy_indices, num_phys * sizeof(*p));
		p += num_phys;

		num_props++;
		p += HFI_PACK(p, HFI_PROPERTY_PANEL_DMA_SCHEDULE_WINDOW, dma_win);
		payload[0] = num_props;
	}

	packet_id3 = dcp_hfi_next_packet_id(hfi);
	pkt_info.cmd          = HFI_COMMAND_PANEL_INIT_GENERIC_CAPS;
	pkt_info.flags        = HFI_TX_FLAGS_NON_DISCARDABLE | HFI_TX_FLAGS_RESPONSE_REQUIRED;
	pkt_info.packet_id    = packet_id3;
	pkt_info.payload_size = (u32)((p - payload) * sizeof(u32));
	pkt_info.payload_ptr  = payload;

	ret = hfi_create_full_packet(&hdl1, &pkt_info);
	if (ret)
		goto err_free_buf1;

	buf2 = dcp_hfi_buf_alloc(hfi);
	if (!buf2) {
		dev_err(hfi->dev, "HFI PANEL_INIT: TX buffer exhausted (B)\n");
		ret = -ENOMEM;
		goto err_free_buf1;
	}

	hdl2.cmd_buffer = buf2->data;
	hdl2.size       = DCP_HFI_BUF_SIZE;

	hdr_info2.num_packets   = 0;
	hdr_info2.cmd_buff_type = HFI_CMD_BUFF_DISPLAY;
	hdr_info2.object_id     = intf_idx;
	hdr_info2.header_id     = dcp_hfi_next_packet_id(hfi);

	ret = hfi_create_header(&hdl2, &hdr_info2);
	if (ret) {
		dev_err(hfi->dev, "PANEL_INIT: hfi_create_header (B) failed: %d\n",
			ret);
		goto err_free_both;
	}

	{
		u32 num_props = 7; /* timing properties; DPHY and DSC are optional */

		if (cfg->dsc_enabled)
			num_props++;
		if (cfg->dsc_enabled && cfg->dsc_rc_override)
			num_props++;
		if (cfg->num_phy_timings)
			num_props++;
		p = payload;
		*p++ = num_props;
	}
	p = payload + 1;

	*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_INDEX, 0, 1);
	*p++ = 0;

	/* PANEL_CLOCKRATE is the panel-selected DSI bit clock per lane. */
	if (cfg->dsc_enabled && cfg->dsc_bits_per_pixel)
		link_bpp_x16 = cfg->dsc_bits_per_pixel;
	clk_hz = dsi->hs_rate ? dsi->hs_rate :
		div_u64((u64)mode->clock * 1000 * link_bpp_x16,
			num_lanes * 16);
	*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_CLOCKRATE, 0, 2);
	*p++ = lower_32_bits(clk_hz);
	*p++ = upper_32_bits(clk_hz);

	*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_FRAMERATE, 0, 1);
	*p++ = drm_mode_vrefresh(mode);

	/* Firmware expects numerator/denominator timing jitter per mode. */
	*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_JITTER, 0, 2);
	*p++ = cfg->jitter_numer;
	*p++ = cfg->jitter_denom;

	*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_RESOLUTION_DATA, 0, 14);
	*p++ = mode->hdisplay;                       /* active_width */
	*p++ = mode->vdisplay;                       /* active_height */
	*p++ = mode->hsync_start - mode->hdisplay;   /* h_front_porch */
	*p++ = mode->htotal - mode->hsync_end;       /* h_back_porch */
	*p++ = 0;                                    /* h_left_border */
	*p++ = 0;                                    /* h_right_border */
	*p++ = mode->hsync_end - mode->hsync_start;  /* h_pulse_width */
	*p++ = mode->hskew;                          /* h_sync_skew */
	*p++ = 0;                                    /* h_sync_pulse (TODO: confirm) */
	*p++ = mode->vsync_start - mode->vdisplay;   /* v_front_porch */
	*p++ = mode->vtotal - mode->vsync_end;       /* v_back_porch */
	*p++ = 0;                                    /* v_top_border */
	*p++ = 0;                                    /* v_bottom_border */
	*p++ = mode->vsync_end - mode->vsync_start;  /* v_pulse_width */

	/* Topology fields follow the firmware panel format. */
	*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_DISPLAY_TOPOLOGY, 0, 4);
	*p++ = 1;
	*p++ = cfg->topology_mixer_count;
	*p++ = cfg->topology_enc_count;
	*p++ = cfg->topology_intf_count;

	*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_DEFAULT_TOPOLOGY_INDEX, 0, 1);
	*p++ = 0;

	if (cfg->dsc_enabled) {
		*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_COMPRESSION_DATA, 0, 12);
		*p++ = HFI_PANEL_COMPRESSION_DSC;
		*p++ = (cfg->dsc_version_major << 4) | cfg->dsc_version_minor;
		*p++ = cfg->dsc_scr_version;
		*p++ = cfg->dsc_slice_height;
		*p++ = cfg->dsc_slice_width;
		*p++ = cfg->dsc_slices_per_pkt;
		*p++ = cfg->dsc_bits_per_component;
		*p++ = cfg->dsc_pps_delay_ms;
		*p++ = cfg->dsc_bits_per_pixel;
		*p++ = cfg->dsc_chroma_format;
		*p++ = cfg->dsc_color_space;
		*p++ = cfg->dsc_block_prediction_enable;
	}

	if (cfg->dsc_enabled && cfg->dsc_rc_override) {
		struct hfi_panel_compression_rc_override rc_override = {};

		memcpy(rc_override.min_qp, cfg->dsc_rc_min_qp,
		       sizeof(rc_override.min_qp));
		memcpy(rc_override.max_qp, cfg->dsc_rc_max_qp,
		       sizeof(rc_override.max_qp));
		memcpy(rc_override.offsets, cfg->dsc_rc_bpg_offsets,
		       sizeof(rc_override.offsets));

		*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_COMPRESSION_RC_OVERRIDE, 0,
				sizeof(rc_override) / sizeof(u32));
		memcpy(p, &rc_override, sizeof(rc_override));
		p += sizeof(rc_override) / sizeof(u32);
	}

	/* DCP computes PHY timings unless an explicit override is supplied. */
	if (cfg->num_phy_timings) {
		u32 n = min_t(u32, cfg->num_phy_timings, DCP_DSI_MAX_PHY_TIMINGS);

		*p++ = HFI_PACK_KEY(HFI_PROPERTY_PANEL_DPHY_TIMINGS, 0, n + 1);
		*p++ = n;
		memcpy(p, cfg->phy_timings, n * sizeof(*p));
		p += n;
	}

	packet_id2 = dcp_hfi_next_packet_id(hfi);
	pkt_info.cmd          = HFI_COMMAND_PANEL_INIT_TIMING_MODE_CAPS;
	pkt_info.flags        = HFI_TX_FLAGS_NON_DISCARDABLE | HFI_TX_FLAGS_RESPONSE_REQUIRED;
	pkt_info.packet_id    = packet_id2;
	pkt_info.payload_size = (u32)((p - payload) * sizeof(u32));
	pkt_info.payload_ptr  = payload;

	ret = hfi_create_full_packet(&hdl2, &pkt_info);
	if (ret)
		goto err_free_both;

	/*
	 * Header A carries 2 packets (PANEL_CAPS, GENERIC_CAPS); firmware
	 * ACKs each individually by its own packet_id. comp1 is registered
	 * internally by dcp_hfi_send_cmd_async() below; comp3 must be
	 * pre-registered here or its response is dropped as "no pending
	 * completion". Header B carries only TIMING_MODE_CAPS (the topology
	 * property that drives DCP's MDP resource reservation) — comp2 is
	 * registered internally by its own dcp_hfi_send_cmd_async() call.
	 */
	init_completion(&comp1.done);
	comp1.resp_data = NULL;
	comp1.resp_len  = 0;
	comp1.status    = 0;
	comp1.packet_id = packet_id;

	init_completion(&comp2.done);
	comp2.resp_data = NULL;
	comp2.resp_len  = 0;
	comp2.status    = 0;
	comp2.packet_id = packet_id2;

	init_completion(&comp3.done);
	comp3.resp_data = NULL;
	comp3.resp_len  = 0;
	comp3.status    = 0;
	comp3.packet_id = packet_id3;

	ret = dcp_hfi_pending_add(hfi, &comp3);
	if (ret) {
		dev_err(hfi->dev,
			"intf%u: PANEL_INIT: failed to register GENERIC_CAPS completion: %d\n",
			intf_idx, ret);
		goto err_free_both;
	}

	ret = dcp_hfi_send_cmd_async(hfi, buf1,
				     hfi_get_header_size(&hdl1), &comp1);
	if (ret) {
		dev_err(hfi->dev,
			"intf%u: PANEL_INIT caps/generic (header A) failed\n",
			intf_idx);
		dcp_hfi_pending_find(hfi, packet_id3);
		dcp_hfi_buf_free(hfi, buf2);
		return ret;
	}

	/* Stack completions must be retired before returning after submission. */
	ret = dcp_hfi_send_cmd_async(hfi, buf2,
				     hfi_get_header_size(&hdl2), &comp2);
	if (ret) {
		dev_err(hfi->dev,
			"intf%u: HFI_COMMAND_PANEL_INIT_TIMING_MODE_CAPS (header B) failed\n",
			intf_idx);
		comp2.packet_id = 0; /* never registered/in-flight, nothing to wait on */
	}

	timeout1 = dcp_hfi_panel_wait(hfi, &comp1);
	timeout3 = dcp_hfi_panel_wait(hfi, &comp3);
	if (comp2.packet_id)
		timeout2 = dcp_hfi_panel_wait(hfi, &comp2);
	else
		timeout2 = false;

	if (timeout1 || timeout2 || timeout3) {
		dev_err(hfi->dev,
			"intf%u: HFI PANEL_INIT timed out (caps=%d timing_mode=%d generic=%d)\n",
			intf_idx, !timeout1, !timeout2, !timeout3);
		return -ETIMEDOUT;
	}

	if (ret)
		return ret; /* header B send_cmd_async failure, comp1/comp3 now retired */

	if (comp1.status || comp2.status || comp3.status)
		dev_err(hfi->dev,
			"intf%u: PANEL_INIT failed: caps=%d topology=%d generic=%d\n",
			intf_idx, comp1.status, comp2.status, comp3.status);

	return comp1.status ? comp1.status :
	       comp2.status ? comp2.status : comp3.status;

err_free_both:
	dcp_hfi_buf_free(hfi, buf2);
err_free_buf1:
	dcp_hfi_buf_free(hfi, buf1);
	return ret;
}

ssize_t dcp_hfi_dcs_transfer(struct dcp_hfi *hfi, u32 session_id,
			     const struct mipi_dsi_msg *msg,
			     void *tx_buf, dma_addr_t tx_iova,
			     void *rx_buf, dma_addr_t rx_iova,
			     u32 post_wait_ms, u32 ctrl_idx)
{
	struct hfi_dsi_cmd_desc cmd_desc = {};
	static atomic_t seq_no = ATOMIC_INIT(0);
	int ret;

	if (!msg || !msg->tx_buf || !msg->tx_len)
		return -EINVAL;

	if (msg->rx_len && (!msg->rx_buf || !rx_buf))
		return -EINVAL;

	if (msg->tx_len > DCP_DSI_DCS_TX_BUF_SIZE) {
		dev_err(hfi->dev, "DCS TX too large: %zu > %u\n",
			msg->tx_len, DCP_DSI_DCS_TX_BUF_SIZE);
		return -EINVAL;
	}

	memcpy(tx_buf, msg->tx_buf, msg->tx_len);

	cmd_desc.size             = sizeof(cmd_desc);
	cmd_desc.channel          = msg->channel;
	cmd_desc.type             = msg->type;
	cmd_desc.flags            = msg->flags;
	cmd_desc.tx_len           = msg->tx_len;
	cmd_desc.tx_buff_addr_lsb = lower_32_bits(tx_iova);
	cmd_desc.tx_buff_addr_msb = upper_32_bits(tx_iova);
	cmd_desc.ctrl_idx         = ctrl_idx;
	cmd_desc.ctrl_flags       = DCP_DSI_CTRL_CMD_FETCH_MEMORY |
					DCP_DSI_CTRL_CMD_LAST_COMMAND;
	if (msg->rx_len)
		cmd_desc.ctrl_flags |= DCP_DSI_CTRL_CMD_READ;
	cmd_desc.last_command     = 1;
	cmd_desc.post_wait_ms     = post_wait_ms;
	cmd_desc.seq_no           = atomic_inc_return(&seq_no);

	if (msg->rx_len > 0) {
		if (msg->rx_len > DCP_DSI_DCS_RX_BUF_SIZE)
			return -EINVAL;
		memset(rx_buf, 0, msg->rx_len);
		cmd_desc.rx_len           = msg->rx_len;
		cmd_desc.rx_buff_addr_lsb = lower_32_bits(rx_iova);
		cmd_desc.rx_buff_addr_msb = upper_32_bits(rx_iova);
	}

	ret = dcp_hfi_send_sync(hfi,
				HFI_COMMAND_DISPLAY_TRANSFER_DCS_CMD,
				HFI_CMD_BUFF_DISPLAY,
				session_id,
				HFI_PAYLOAD_U32_ARRAY,
				&cmd_desc, sizeof(cmd_desc),
				HFI_TX_FLAGS_RESPONSE_REQUIRED |
				HFI_TX_FLAGS_NON_DISCARDABLE,
				NULL, 0,
				DCP_HFI_TIMEOUT_DISPLAY_DCS_MS);
	if (ret) {
		dev_err(hfi->dev,
			"HFI DCS_TRANSFER type=0x%x len=%zu failed: %d\n",
			msg->type, msg->tx_len, ret);
		return ret;
	}

	if (msg->rx_len > 0)
		memcpy(msg->rx_buf, rx_buf, msg->rx_len);

	return msg->tx_len;
}
