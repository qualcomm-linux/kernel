// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * DCP plane state validation and HFI property packet construction.
 */

#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_rect.h>

#include "msm_drv.h"

#include "dcp_crtc.h"
#include "dcp_hfi.h"
#include "dcp_dpu_kms.h"
#include "dcp_plane.h"

#define DCP_ZPOS_MAX			255
#define DCP_MAX_IMG_WIDTH		0x3FFF
#define DCP_MAX_IMG_HEIGHT		0x3FFF
#define DCP_SSPP_MAX_PITCH_SIZE	0xFF00
#define DCP_MAX_UPSCALE_RATIO	20
#define DCP_MAX_DOWNSCALE_RATIO	4

static const u32 dcp_plane_formats[] = {
	DRM_FORMAT_ARGB8888, DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_BGRA8888,
	DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888, DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGB888,   DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,   DRM_FORMAT_BGR565,
	DRM_FORMAT_ARGB1555, DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGBA5551, DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XRGB1555, DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGBX5551, DRM_FORMAT_BGRX5551,
	DRM_FORMAT_ARGB4444, DRM_FORMAT_ABGR4444,
	DRM_FORMAT_RGBA4444, DRM_FORMAT_BGRA4444,
	DRM_FORMAT_XRGB4444, DRM_FORMAT_XBGR4444,
	DRM_FORMAT_RGBX4444, DRM_FORMAT_BGRX4444,
	DRM_FORMAT_ARGB2101010, DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_ABGR2101010, DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_NV12, DRM_FORMAT_NV21,
	DRM_FORMAT_P010, DRM_FORMAT_YUV420, DRM_FORMAT_YVU420,
};

static const u64 dcp_format_modifiers[] = {
	DRM_FORMAT_MOD_QCOM_COMPRESSED,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static struct dcp_dpu_kms *_dcp_plane_get_kms(struct drm_plane *plane)
{
	struct msm_drm_private *priv = plane->dev->dev_private;

	return to_dcp_dpu_kms(priv->kms);
}

static bool dcp_plane_hfi_pipe_valid(struct dcp_dpu_kms *dcp_kms, u32 pipe_id)
{
	const struct dcp_caps *caps;
	u32 i;

	if (!dcp_kms->caps)
		return false;

	caps = dcp_kms->caps;
	for (i = 0; i < caps->num_vig_pipes; i++)
		if (caps->vig_pipe_ids[i] == pipe_id)
			return true;

	for (i = 0; i < caps->num_dma_pipes; i++)
		if (caps->dma_pipe_ids[i] == pipe_id)
			return true;

	return false;
}

static bool dcp_plane_pipe_is_vig(const struct dcp_caps *caps, u32 pipe_id)
{
	u32 i;

	for (i = 0; i < caps->num_vig_pipes; i++)
		if (caps->vig_pipe_ids[i] == pipe_id)
			return true;

	return false;
}

static bool dcp_plane_pipe_busy(struct drm_atomic_commit *state,
				struct drm_plane *self, u32 pipe_id)
{
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	struct drm_plane *iter_plane;
	struct drm_plane_state *new_plane_state;
	int i;

	drm_for_each_plane(plane, state->dev) {
		if (plane == self)
			continue;

		plane_state = plane->state;
		for_each_new_plane_in_state(state, iter_plane, new_plane_state, i) {
			if (iter_plane == plane) {
				plane_state = new_plane_state;
				break;
			}
		}

		if (plane_state && plane_state->crtc && plane_state->visible &&
		    to_dcp_plane_state(plane_state)->hfi_pipe_id == pipe_id)
			return true;
	}

	return false;
}

static u32 dcp_plane_assign_pipe(struct dcp_dpu_kms *dcp_kms,
				 struct drm_atomic_commit *state,
				 struct drm_plane *plane,
				 struct dcp_plane_state *pstate,
				 bool yuv, bool scaling)
{
	const struct dcp_caps *caps = dcp_kms->caps;
	u32 i, pipe_id;

	/* YUV scanout requires a VIG pipe with the CSC path. */
	if (dcp_plane_hfi_pipe_valid(dcp_kms, pstate->hfi_pipe_id) &&
	    (!yuv || dcp_plane_pipe_is_vig(caps, pstate->hfi_pipe_id)) &&
	    (!scaling || dcp_plane_pipe_is_vig(caps, pstate->hfi_pipe_id)) &&
	    !dcp_plane_pipe_busy(state, plane, pstate->hfi_pipe_id))
		return pstate->hfi_pipe_id;

	/*
	 * R1/virtual pipes are intentionally not allocated as independent
	 * planes yet. They require transactional physical-pipe/R1 ownership
	 * and multirect programming, which will be added with the resource
	 * manager.
	 */
	for (i = 0; i < caps->num_vig_pipes; i++) {
		pipe_id = caps->vig_pipe_ids[i];
		if (!dcp_plane_pipe_busy(state, plane, pipe_id))
			return pipe_id;
	}

	if (yuv || scaling)
		return U32_MAX;

	for (i = 0; i < caps->num_dma_pipes; i++) {
		pipe_id = caps->dma_pipe_ids[i];
		if (!dcp_plane_pipe_busy(state, plane, pipe_id))
			return pipe_id;
	}

	return U32_MAX;
}

static const struct {
	u32 fourcc;
	u64 modifier;
	u32 hfi_fmt;
} dcp_fmt_table[] = {
	{ DRM_FORMAT_ABGR8888,    DRM_FORMAT_MOD_QCOM_COMPRESSED, HFI_COLOR_FORMAT_UBWC_RGBA8888 },
	{ DRM_FORMAT_XBGR8888,    DRM_FORMAT_MOD_QCOM_COMPRESSED, HFI_COLOR_FORMAT_UBWC_RGBX8888 },
	{ DRM_FORMAT_ABGR2101010, DRM_FORMAT_MOD_QCOM_COMPRESSED,
		HFI_COLOR_FORMAT_UBWC_RGBA10_10_10_2 },
	{ DRM_FORMAT_XBGR2101010, DRM_FORMAT_MOD_QCOM_COMPRESSED,
		HFI_COLOR_FORMAT_UBWC_RGBX10_10_10_2 },
	{ DRM_FORMAT_BGR565,      DRM_FORMAT_MOD_QCOM_COMPRESSED, HFI_COLOR_FORMAT_UBWC_RGB565 },
	{ DRM_FORMAT_NV12,        DRM_FORMAT_MOD_QCOM_COMPRESSED, HFI_COLOR_FORMAT_UBWC_NV12 },
	{ DRM_FORMAT_P010,        DRM_FORMAT_MOD_QCOM_COMPRESSED, HFI_COLOR_FORMAT_UBWC_P010 },

	{ DRM_FORMAT_RGB565,       DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_BGR565 },
	{ DRM_FORMAT_BGR565,       DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_RGB565 },
	{ DRM_FORMAT_RGB888,       DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_BGR888 },
	{ DRM_FORMAT_BGR888,       DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_RGB888 },
	{ DRM_FORMAT_ARGB8888,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_BGRA8888 },
	{ DRM_FORMAT_ABGR8888,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_RGBA8888 },
	{ DRM_FORMAT_RGBA8888,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_ABGR8888 },
	{ DRM_FORMAT_BGRA8888,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_ARGB8888 },
	{ DRM_FORMAT_XRGB8888,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_BGRX8888 },
	{ DRM_FORMAT_XBGR8888,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_RGBX8888 },
	{ DRM_FORMAT_RGBX8888,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_XBGR8888 },
	{ DRM_FORMAT_BGRX8888,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_XRGB8888 },
	{ DRM_FORMAT_ARGB2101010,  DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_BGRA10_10_10_2 },
	{ DRM_FORMAT_ABGR2101010,  DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_RGBA10_10_10_2 },
	{ DRM_FORMAT_RGBA1010102,  DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_ABGR2_10_10_10 },
	{ DRM_FORMAT_BGRA1010102,  DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_ARGB2_10_10_10 },
	{ DRM_FORMAT_XRGB2101010,  DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_BGRX10_10_10_2 },
	{ DRM_FORMAT_XBGR2101010,  DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_RGBX10_10_10_2 },
	{ DRM_FORMAT_RGBX1010102,  DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_XBGR2_10_10_10 },
	{ DRM_FORMAT_BGRX1010102,  DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_XRGB2_10_10_10 },
	{ DRM_FORMAT_ARGB1555,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_BGRA5551 },
	{ DRM_FORMAT_ABGR1555,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_RGBA5551 },
	{ DRM_FORMAT_RGBA5551,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_ABGR1555 },
	{ DRM_FORMAT_BGRA5551,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_ARGB1555 },
	{ DRM_FORMAT_XRGB1555,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_BGRX5551 },
	{ DRM_FORMAT_XBGR1555,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_RGBX5551 },
	{ DRM_FORMAT_RGBX5551,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_XBGR1555 },
	{ DRM_FORMAT_BGRX5551,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_XRGB1555 },
	{ DRM_FORMAT_ARGB4444,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_BGRA4444 },
	{ DRM_FORMAT_ABGR4444,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_RGBA4444 },
	{ DRM_FORMAT_RGBA4444,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_ABGR4444 },
	{ DRM_FORMAT_BGRA4444,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_ARGB4444 },
	{ DRM_FORMAT_XRGB4444,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_BGRX4444 },
	{ DRM_FORMAT_XBGR4444,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_RGBX4444 },
	{ DRM_FORMAT_RGBX4444,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_XBGR4444 },
	{ DRM_FORMAT_BGRX4444,     DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_XRGB4444 },
	{ DRM_FORMAT_YUV420,       DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_YU12 },
	{ DRM_FORMAT_YVU420,       DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_YV12 },
	{ DRM_FORMAT_NV12,         DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_NV12 },
	{ DRM_FORMAT_NV21,         DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_NV21 },
	{ DRM_FORMAT_P010,         DRM_FORMAT_MOD_LINEAR, HFI_COLOR_FORMAT_P010 },
};

static u32 dcp_drm_to_hfi_format(u32 fourcc, u64 modifier)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dcp_fmt_table); i++) {
		if (dcp_fmt_table[i].fourcc == fourcc &&
		    dcp_fmt_table[i].modifier == modifier)
			return dcp_fmt_table[i].hfi_fmt;
	}

	return U32_MAX;
}

static u32 dcp_drm_to_hfi_rotation(u32 drm_rotation)
{
	u32 hfi_rot = 0;

	if (drm_rotation & DRM_MODE_ROTATE_0)
		hfi_rot |= HFI_DISPLAY_ROTATION_0;
	if (drm_rotation & DRM_MODE_ROTATE_90)
		hfi_rot |= HFI_DISPLAY_ROTATION_90;
	if (drm_rotation & DRM_MODE_ROTATE_180)
		hfi_rot |= HFI_DISPLAY_ROTATION_180;
	if (drm_rotation & DRM_MODE_ROTATE_270)
		hfi_rot |= HFI_DISPLAY_ROTATION_270;
	if (drm_rotation & DRM_MODE_REFLECT_X)
		hfi_rot |= HFI_DISPLAY_REFLECT_X;
	if (drm_rotation & DRM_MODE_REFLECT_Y)
		hfi_rot |= HFI_DISPLAY_REFLECT_Y;

	if (!hfi_rot)
		hfi_rot = HFI_DISPLAY_ROTATION_0;

	return hfi_rot;
}

static u32 dcp_drm_to_hfi_blend_op(u32 drm_blend_mode)
{
	switch (drm_blend_mode) {
	case DRM_MODE_BLEND_PIXEL_NONE:	return HFI_BLEND_OP_OPAQUE;
	case DRM_MODE_BLEND_PREMULTI:	return HFI_BLEND_OP_PREMULTIPLIED;
	case DRM_MODE_BLEND_COVERAGE:	return HFI_BLEND_OP_COVERAGE;
	default:			return HFI_BLEND_OP_NOT_DEFINED;
	}
}

static u16 dcp_compute_bg_alpha(u32 pixel_blend_mode, u16 fg_alpha)
{
	switch (pixel_blend_mode) {
	case DRM_MODE_BLEND_PIXEL_NONE:
		return DRM_BLEND_ALPHA_OPAQUE - fg_alpha;
	case DRM_MODE_BLEND_PREMULTI:
	case DRM_MODE_BLEND_COVERAGE:
		return (fg_alpha != DRM_BLEND_ALPHA_OPAQUE) ? fg_alpha : 0;
	default:
		return DRM_BLEND_ALPHA_OPAQUE;
	}
}

static u32 dcp_hfi_scale_alpha(const struct drm_plane_state *state, u32 alpha)
{
	if (to_dcp_crtc_state(state->crtc->state)->topology_bits_per_component >= 10)
		return alpha;

	return alpha << 8;
}

static u64 dcp_plane_calc_bw(u32 bpp,
			     const struct drm_display_mode *mode,
			     const struct drm_rect *src,
			     const struct drm_rect *dst)
{
	int src_w = drm_rect_width(src) >> 16;
	int src_h = drm_rect_height(src) >> 16;
	int dst_h = drm_rect_height(dst);
	int fps   = drm_mode_vrefresh(mode);
	int vbp   = mode->vtotal - mode->vsync_end;
	int vpw   = mode->vsync_end - mode->vsync_start;
	int vfp   = mode->vsync_start - mode->vdisplay;
	u32 hw_latency = 25;
	u64 scale_factor = (src_h > dst_h) ? mult_frac(src_h, 1, dst_h) : 1;
	u64 plane_bw, prefill_bw;

	plane_bw = (u64)src_w * mode->vtotal * fps * bpp * scale_factor;
	prefill_bw = plane_bw * hw_latency;

	if ((vbp + vpw) > hw_latency)
		do_div(prefill_bw, (vbp + vpw));
	else if ((vbp + vpw + vfp) < hw_latency)
		do_div(prefill_bw, (vbp + vpw + vfp));
	else
		do_div(prefill_bw, hw_latency);

	/* HFI DRAM/LLCC votes are expressed in Kbps. */
	return div_u64(max(plane_bw, prefill_bw), 1000);
}

static u64 dcp_plane_calc_clk(const struct drm_display_mode *mode,
			      const struct drm_rect *src,
			      const struct drm_rect *dst)
{
	int src_h = drm_rect_height(src) >> 16;
	int dst_w = drm_rect_width(dst);
	int dst_h = drm_rect_height(dst);
	int fps   = drm_mode_vrefresh(mode);
	u64 clk   = (u64)dst_w * mode->vtotal * fps;

	if (src_h > dst_h) {
		clk *= src_h;
		do_div(clk, dst_h);
	}
	return clk;
}

void dcp_plane_calc_perf(const struct drm_plane_state *state,
			 const struct drm_display_mode *mode,
			 struct dcp_plane_perf *perf)
{
	struct drm_rect src, dst;
	u32 bpp;

	memset(perf, 0, sizeof(*perf));

	if (!state || !state->visible || !state->fb || !mode || !mode->clock ||
	    !drm_mode_vrefresh(mode))
		return;

	src = drm_plane_state_src(state);
	dst = drm_plane_state_dest(state);
	bpp = drm_format_info_bpp(state->fb->format, 0);

	perf->fetch_bw = dcp_plane_calc_bw(bpp, mode, &src, &dst);
	perf->clk = dcp_plane_calc_clk(mode, &src, &dst);
}

static inline u32 *prop_u32(u32 *p, u32 prop_id, u32 lid, u32 val)
{
	*p++ = HFI_PACK_KEY(prop_id, 0, 2);
	*p++ = lid;
	*p++ = val;
	return p;
}

static inline u32 *prop_roi(u32 *p, u32 prop_id, u32 lid,
			    u32 x, u32 y, u32 w, u32 h)
{
	*p++ = HFI_PACK_KEY(prop_id, 0, 5);
	*p++ = lid;
	*p++ = x; *p++ = y; *p++ = w; *p++ = h;
	return p;
}

static inline u32 *prop_attach(u32 *p, u32 lid)
{
	*p++ = HFI_PACK_KEY(HFI_PROPERTY_DISPLAY_ATTACH_LAYER, 0, 1);
	*p++ = lid;
	return p;
}

static inline u32 *prop_src_addr(u32 *p, u32 lid,
				 const dma_addr_t *iova, u32 hfi_fmt)
{
	/* The firmware format is struct hfi_plane_buff: all low words,
	 * followed by all high words, then the common descriptor fields.
	 */
	p += HFI_PACK(p, HFI_PROPERTY_LAYER_SRC_ADDR, lid,
		      lower_32_bits(iova[0]), lower_32_bits(iova[1]),
		      lower_32_bits(iova[2]), lower_32_bits(iova[3]),
		      upper_32_bits(iova[0]), upper_32_bits(iova[1]),
		      upper_32_bits(iova[2]), upper_32_bits(iova[3]),
		      0, 0, 0, hfi_fmt);
	return p;
}

static inline u32 *prop_stride(u32 *p, u32 lid, const u32 *pitches)
{
	int i;

	*p++ = HFI_PACK_KEY(HFI_PROPERTY_LAYER_STRIDE, 0, 5);
	*p++ = lid;
	for (i = 0; i < 4; i++)
		*p++ = pitches[i];
	return p;
}

static inline u32 *prop_cache_attr(u32 *p, u32 lid, u32 state, u32 op)
{
	*p++ = HFI_PACK_KEY(HFI_PROPERTY_LAYER_CACHE_ATTR, 0, 3);
	*p++ = lid;
	*p++ = state;
	*p++ = op;
	return p;
}

static int dcp_plane_build_one_hfi_props_packet(struct dcp_plane_state *pstate,
						const struct drm_plane_state *state,
						u32 session_id, u32 packet_id,
						u8 *packet)
{
	struct drm_framebuffer *fb = state->fb;
	struct hfi_cmd_buff_hdl hdl;
	struct hfi_header_info hdr_info;
	struct hfi_packet_info pkt_info;
	u32 payload[DCP_PLANE_PROP_PKT_MAX_SIZE / sizeof(u32)];
	u32 *p = payload;
	u32 *num_props_ptr;
	u32 num_props = 0;
	u32 hfi_format;
	u32 hfi_rotation;
	u32 hfi_blend_op;
	u32 pitches[4] = { 0 };
	dma_addr_t fb_iova[HFI_MAX_PLANES] = { 0 };
	u32 src_x, src_y, src_w, src_h;
	u32 dst_x, dst_y, dst_w, dst_h;
	u32 hfi_alpha, hfi_bg_alpha;
	u32 lid = pstate->hfi_pipe_id;
	int i, ret;
	u32 payload_words;

	hfi_format = pstate->hfi_format;

	hfi_rotation = dcp_drm_to_hfi_rotation(state->rotation);
	hfi_blend_op = dcp_drm_to_hfi_blend_op(state->pixel_blend_mode);

	src_x = state->src_x >> 16;
	src_y = state->src_y >> 16;
	src_w = state->src_w >> 16;
	src_h = state->src_h >> 16;
	dst_x = state->crtc_x;
	dst_y = state->crtc_y;
	dst_w = state->crtc_w;
	dst_h = state->crtc_h;

	hfi_alpha = dcp_hfi_scale_alpha(state, state->alpha);
	hfi_bg_alpha = dcp_hfi_scale_alpha(state,
					   dcp_compute_bg_alpha(state->pixel_blend_mode,
								state->alpha));

	for (i = 0; i < 4 && i < fb->format->num_planes; i++)
		pitches[i] = fb->pitches[i];

	for (i = 0; i < HFI_MAX_PLANES && i < fb->format->num_planes; i++)
		fb_iova[i] = msm_framebuffer_iova(fb, i);

	num_props_ptr = p++;

	p = prop_roi(p, HFI_PROPERTY_LAYER_SRC_ROI, lid,
		     src_x, src_y, src_w, src_h);
	num_props++;

	p = prop_roi(p, HFI_PROPERTY_LAYER_DEST_ROI, lid,
		     dst_x, dst_y, dst_w, dst_h);
	num_props++;

	p = prop_attach(p, lid);
	num_props++;

	p = prop_src_addr(p, lid, fb_iova, hfi_format);
	num_props++;

	p = prop_stride(p, lid, pitches);
	num_props++;

	p = prop_u32(p, HFI_PROPERTY_LAYER_SRC_FORMAT, lid, hfi_format);
	num_props++;

	p = prop_u32(p, HFI_PROPERTY_LAYER_ROTATION, lid, hfi_rotation);
	num_props++;

	p = prop_cache_attr(p, lid,
			    HFI_CACHE_STATE_DISABLE,
			    HFI_CACHE_OP_TYPE_NONE);
	num_props++;

	p = prop_u32(p, HFI_PROPERTY_LAYER_ZPOS, lid,
		     state->normalized_zpos);
	num_props++;

	p = prop_u32(p, HFI_PROPERTY_LAYER_ALPHA, lid, hfi_alpha);
	num_props++;

	p = prop_u32(p, HFI_PROPERTY_LAYER_BG_ALPHA, lid, hfi_bg_alpha);
	num_props++;

	p = prop_u32(p, HFI_PROPERTY_LAYER_BLEND_TYPE, lid, hfi_blend_op);
	num_props++;

	p = prop_u32(p, HFI_PROPERTY_LAYER_SRC_IMG_SIZE_W, lid,
		     pstate->src_img_w);
	num_props++;

	p = prop_u32(p, HFI_PROPERTY_LAYER_SRC_IMG_SIZE_H, lid,
		     pstate->src_img_h);
	num_props++;

	*num_props_ptr = num_props;

	payload_words = p - payload;

	hdl.cmd_buffer = packet;
	hdl.size       = DCP_PLANE_PROP_PKT_MAX_SIZE;

	hdr_info.num_packets   = 0;
	hdr_info.cmd_buff_type = HFI_CMD_BUFF_DISPLAY;
	hdr_info.object_id     = session_id;
	hdr_info.header_id     = packet_id;

	ret = hfi_create_header(&hdl, &hdr_info);
	if (ret)
		return ret;

	pkt_info.cmd          = HFI_COMMAND_DISPLAY_SET_PROPERTY;
	pkt_info.id           = session_id & 0xFFFF;
	pkt_info.flags        = HFI_TX_FLAGS_NON_DISCARDABLE;
	pkt_info.packet_id    = packet_id;
	pkt_info.payload_type = HFI_PAYLOAD_U32_ARRAY;
	pkt_info.payload_size = payload_words * sizeof(u32);
	pkt_info.payload_ptr  = payload;

	ret = hfi_create_full_packet(&hdl, &pkt_info);
	if (ret) {
		pstate->prop_pkt_size = 0;
		return ret;
	}

	pstate->prop_pkt_size = hfi_get_header_size(&hdl);

	drm_dbg_driver(state->plane->dev,
		       "DCP_COMMIT p=%u pipe=%u src=%ux%u+%ux%u dst=%ux%u+%ux%u fmt=0x%x mod=0x%llx alpha=%u bg_alpha=%u props=%u words=%u size=%zu\n",
		       state->plane->base.id, lid, src_x, src_y, src_w, src_h,
		       dst_x, dst_y, dst_w, dst_h, hfi_format, fb->modifier,
		       hfi_alpha, hfi_bg_alpha, num_props, payload_words,
		       pstate->prop_pkt_size);
	return 0;
}

static int dcp_plane_build_hfi_props_packet(struct dcp_plane_state *pstate,
					    const struct drm_plane_state *state,
					    u32 session_id, u32 packet_id)
{
	int ret;

	pstate->prop_pkt_size = 0;

	ret = dcp_plane_build_one_hfi_props_packet(pstate, state, session_id,
						   packet_id, pstate->prop_pkt);
	return ret;
}

static int dcp_plane_prepare_fb(struct drm_plane *plane,
				struct drm_plane_state *new_state)
{
	struct dcp_plane_state *pstate = to_dcp_plane_state(new_state);
	int ret;

	drm_dbg_driver(plane->dev,
		       "plane%u: prepare_fb fb=%u\n",
		       plane->base.id,
		       new_state->fb ? new_state->fb->base.id : 0);

	if (!new_state->fb)
		return 0;

	ret = drm_gem_plane_helper_prepare_fb(plane, new_state);
	if (ret)
		return ret;

	ret = msm_framebuffer_prepare(new_state->fb, pstate->needs_dirtyfb);
	if (ret) {
		drm_err(plane->dev, "plane%u: framebuffer prepare failed: %d\n",
			plane->base.id, ret);
		return ret;
	}

	return 0;
}

static void dcp_plane_cleanup_fb(struct drm_plane *plane,
				 struct drm_plane_state *old_state)
{
	struct dcp_plane_state *pstate = to_dcp_plane_state(old_state);

	drm_dbg_driver(plane->dev,
		       "plane%u: cleanup_fb fb=%u\n",
		       plane->base.id,
		       old_state->fb ? old_state->fb->base.id : 0);

	if (!old_state->fb)
		return;

	msm_framebuffer_cleanup(old_state->fb, pstate->needs_dirtyfb);
}

static int dcp_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_commit *state)
{
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct dcp_plane_state *pstate = to_dcp_plane_state(new_state);
	struct dcp_dpu_kms *dcp_kms = _dcp_plane_get_kms(plane);
	struct drm_crtc_state *crtc_state;
	struct drm_rect src;
	struct drm_rect fb_rect = { 0 };
	u32 max_width, max_height;
	int min_scale = DRM_PLANE_NO_SCALING;
	int max_scale = DRM_PLANE_NO_SCALING;
	bool scaling;
	int ret, i;
	struct dcp_plane_perf perf;

	drm_dbg_driver(plane->dev,
		       "DCP_CHECK plane%u pipe=%u src=(%u,%u %ux%u) dst=(%d,%d %ux%u)\n",
			plane->base.id, pstate->hfi_pipe_id,
			new_state->src_x >> 16, new_state->src_y >> 16,
			new_state->src_w >> 16, new_state->src_h >> 16,
			new_state->crtc_x, new_state->crtc_y,
			new_state->crtc_w, new_state->crtc_h);

	pstate->prop_pkt_size = 0;
	pstate->fetch_bw = 0;
	pstate->clk = 0;

	if (!new_state->crtc)
		return 0;

	crtc_state = drm_atomic_get_new_crtc_state(state, new_state->crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		drm_err(plane->dev, "plane%u: failed to get CRTC state: %d\n",
			plane->base.id, ret);
		return ret;
	}

	if (!new_state->fb)
		return 0;

	scaling = new_state->src_w != ((u32)new_state->crtc_w << 16) ||
			  new_state->src_h != ((u32)new_state->crtc_h << 16);

	pstate->hfi_pipe_id = dcp_plane_assign_pipe(dcp_kms, state, plane,
						    pstate, new_state->fb->format->is_yuv,
						    scaling);
	if (pstate->hfi_pipe_id == U32_MAX) {
		drm_err(plane->dev, "plane%u: no compatible HFI pipe available\n",
			plane->base.id);
		return -ENOSPC;
	}

	if (dcp_plane_pipe_is_vig(dcp_kms->caps, pstate->hfi_pipe_id)) {
		min_scale = FRAC_16_16(1, DCP_MAX_UPSCALE_RATIO);
		max_scale = DCP_MAX_DOWNSCALE_RATIO << 16;
	}

	ret = drm_atomic_helper_check_plane_state(new_state, crtc_state,
						  min_scale, max_scale,
						  true, true);
	if (ret) {
		drm_err(plane->dev, "plane%u: plane state check failed: %d\n",
			plane->base.id, ret);
		return ret;
	}

	if (!new_state->visible)
		return 0;
	if (new_state->crtc_x < 0 || new_state->crtc_y < 0 ||
	    new_state->crtc_w <= 0 || new_state->crtc_h <= 0 ||
	    (u64)new_state->crtc_x + new_state->crtc_w >
			crtc_state->adjusted_mode.hdisplay ||
	    (u64)new_state->crtc_y + new_state->crtc_h >
			crtc_state->adjusted_mode.vdisplay) {
		drm_err(plane->dev,
			"plane%u: destination out of bounds: (%d,%d %ux%u), mode %ux%u\n",
			plane->base.id, new_state->crtc_x, new_state->crtc_y,
			new_state->crtc_w, new_state->crtc_h,
			crtc_state->adjusted_mode.hdisplay,
			crtc_state->adjusted_mode.vdisplay);
		return -EINVAL;
	}

	if (new_state->normalized_zpos >= DCP_MAX_BLEND_STAGES) {
		drm_err(plane->dev,
			"plane%u: zpos %u exceeds blend stages %u\n",
			plane->base.id, new_state->normalized_zpos,
			DCP_MAX_BLEND_STAGES);
		return -EINVAL;
	}

	dcp_plane_calc_perf(new_state, &crtc_state->adjusted_mode, &perf);
	pstate->fetch_bw = perf.fetch_bw;
	pstate->clk = perf.clk;

	pstate->hfi_format = dcp_drm_to_hfi_format(new_state->fb->format->format,
						   new_state->fb->modifier);
	if (pstate->hfi_format == U32_MAX) {
		drm_dbg_atomic(plane->dev, "plane%u: unsupported format 0x%x mod 0x%llx\n",
			       plane->base.id,
				 new_state->fb->format->format,
				 new_state->fb->modifier);
		drm_err(plane->dev,
			"plane%u: unsupported format 0x%x modifier 0x%llx\n",
			plane->base.id, new_state->fb->format->format,
			new_state->fb->modifier);
		return -EINVAL;
	}

	fb_rect.x2 = new_state->fb->width;
	fb_rect.y2 = new_state->fb->height;

	if (drm_rect_width(&fb_rect) > DCP_MAX_IMG_WIDTH ||
	    drm_rect_height(&fb_rect) > DCP_MAX_IMG_HEIGHT) {
		drm_err(plane->dev,
			"plane%u: framebuffer too large: %ux%u (limit %ux%u)\n",
			plane->base.id, new_state->fb->width, new_state->fb->height,
			DCP_MAX_IMG_WIDTH, DCP_MAX_IMG_HEIGHT);
		return -E2BIG;
	}

	dcp_mode_limits(&crtc_state->adjusted_mode,
			dcp_kms->caps->max_width, dcp_kms->caps->max_height,
			&max_width, &max_height);

	if (new_state->fb->width > max_width ||
	    new_state->fb->height > max_height) {
		drm_err(plane->dev,
			"plane%u: framebuffer %ux%u exceeds caps %ux%u\n",
			plane->base.id, new_state->fb->width, new_state->fb->height,
			dcp_kms->caps->max_width, dcp_kms->caps->max_height);
		return -E2BIG;
	}

	for (i = 0; i < new_state->fb->format->num_planes; i++) {
		if (new_state->fb->pitches[i] > DCP_SSPP_MAX_PITCH_SIZE) {
			drm_err(plane->dev,
				"plane%u: pitch[%d]=%u exceeds limit %u\n",
				plane->base.id, i, new_state->fb->pitches[i],
				DCP_SSPP_MAX_PITCH_SIZE);
			return -E2BIG;
		}
	}

	src = drm_plane_state_src(new_state);

	pstate->src_img_w = new_state->fb->width;
	pstate->src_img_h = new_state->fb->height;

	if (new_state->fb->format->is_yuv) {
		u32 src_x = src.x1 >> 16;
		u32 src_y = src.y1 >> 16;
		u32 src_w = drm_rect_width(&src) >> 16;
		u32 src_h = drm_rect_height(&src) >> 16;

		if (src_w < 2 || src_h < 2) {
			drm_err(plane->dev,
				"plane%u: YUV source too small: %ux%u\n",
				plane->base.id, src_w, src_h);
			return -EINVAL;
		}

		if (src_x & 0x1 || src_y & 0x1 || src_w & 0x1 || src_h & 0x1) {
			drm_err(plane->dev,
				"plane%u: unaligned YUV source: (%u,%u %ux%u)\n",
				plane->base.id, src_x, src_y, src_w, src_h);
			return -EINVAL;
		}
	}

	return 0;
}

static void dcp_plane_set_hfi_error(const struct drm_plane_state *state, int ret)
{
	struct dcp_crtc_state *cstate;

	cstate = to_dcp_crtc_state(state->crtc->state);
	if (!cstate->hfi_error)
		cstate->hfi_error = ret;
}

static int dcp_plane_submit_update(struct drm_plane *plane,
				   struct drm_plane_state *new_state)
{
	struct dcp_plane_state *pstate = to_dcp_plane_state(new_state);
	struct dcp_dpu_kms *dcp_kms = _dcp_plane_get_kms(plane);
	u32 session_id;
	int ret;

	if (!dcp_crtc_get_session(new_state->crtc, &session_id)) {
		drm_dbg_driver(plane->dev,
			       "plane%u: update before HFI session open, deferring\n",
			       plane->base.id);
		return 0;
	}

	pstate->prop_pkt_id =
		(u32)atomic_inc_return(&dcp_kms->hfi->packet_id);

	ret = dcp_plane_build_hfi_props_packet(pstate, new_state, session_id,
					       pstate->prop_pkt_id);
	if (ret)
		return ret;

	ret = dcp_hfi_submit_prop_pkt(dcp_kms->hfi, pstate->prop_pkt,
				      pstate->prop_pkt_size);
	if (ret)
		return ret;

	drm_dbg_driver(plane->dev,
		       "plane%u: submitted HFI update pipe=%u bytes=%zu\n",
		       plane->base.id, pstate->hfi_pipe_id, pstate->prop_pkt_size);

	return 0;
}

static int dcp_plane_submit_disable(struct drm_plane *plane,
				    struct drm_plane_state *old_state)
{
	struct dcp_plane_state *pstate = to_dcp_plane_state(old_state);
	struct dcp_dpu_kms *dcp_kms = _dcp_plane_get_kms(plane);
	u32 session_id;

	if (!old_state->crtc)
		return 0;

	if (!dcp_crtc_get_session(old_state->crtc, &session_id)) {
		drm_dbg_driver(plane->dev,
			       "plane%u: disable after HFI session close, skipping detach\n",
			       plane->base.id);
		return 0;
	}

	return dcp_hfi_plane_disable(dcp_kms->hfi, session_id,
				     pstate->hfi_pipe_id);
}

static void dcp_plane_atomic_update(struct drm_plane *plane,
				    struct drm_atomic_commit *state)
{
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct dcp_plane_state *pstate = to_dcp_plane_state(new_state);
	int ret;

	drm_dbg_driver(plane->dev,
		       "plane%u: atomic_update visible=%d fb=%u pipe_id=%u\n",
		       plane->base.id, new_state->visible,
		       new_state->fb ? new_state->fb->base.id : 0,
		       pstate->hfi_pipe_id);

	pstate->prop_pkt_size = 0;

	if (!new_state->visible || !new_state->fb)
		return;

	ret = dcp_plane_submit_update(plane, new_state);
	if (ret) {
		drm_err(plane->dev,
			"plane%u: HFI update failed: %d\n", plane->base.id, ret);
		dcp_plane_set_hfi_error(new_state, ret);
		return;
	}
}

static void dcp_plane_atomic_disable(struct drm_plane *plane,
				     struct drm_atomic_commit *state)
{
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(state, plane);
	struct dcp_plane_state *pstate = to_dcp_plane_state(old_state);
	int ret;

	drm_dbg_driver(plane->dev,
		       "plane%u: atomic_disable pipe_id=%u\n",
		       plane->base.id, pstate->hfi_pipe_id);

	ret = dcp_plane_submit_disable(plane, old_state);
	if (ret) {
		drm_err(plane->dev,
			"plane%u: HFI detach failed: %d\n", plane->base.id, ret);
		dcp_plane_set_hfi_error(old_state, ret);
	}

	pstate->prop_pkt_size = 0;
	pstate->prop_pkt_id = 0;
}

static struct drm_plane_state *
dcp_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct dcp_plane_state *pstate, *old_pstate;

	if (WARN_ON(!plane->state))
		return NULL;

	old_pstate = to_dcp_plane_state(plane->state);
	pstate = kzalloc_obj(*pstate, GFP_KERNEL);
	if (!pstate)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &pstate->base);
	pstate->hfi_pipe_id     = old_pstate->hfi_pipe_id;
	pstate->src_img_w       = old_pstate->src_img_w;
	pstate->src_img_h       = old_pstate->src_img_h;
	pstate->fetch_bw       = old_pstate->fetch_bw;
	pstate->clk            = old_pstate->clk;
	pstate->needs_dirtyfb  = old_pstate->needs_dirtyfb;
	pstate->prop_pkt_size   = 0;
	pstate->prop_pkt_id     = old_pstate->prop_pkt_id;

	return &pstate->base;
}

static void dcp_plane_atomic_destroy_state(struct drm_plane *plane,
					   struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(to_dcp_plane_state(state));
}

static struct drm_plane_state *
dcp_plane_atomic_create_state(struct drm_plane *plane)
{
	struct dcp_plane_state *pstate;
	u32 hfi_pipe_id = to_dcp_plane(plane)->hfi_pipe_id;

	pstate = kzalloc_obj(*pstate, GFP_KERNEL);
	if (!pstate)
		return NULL;

	__drm_atomic_helper_plane_state_init(&pstate->base, plane);
	pstate->hfi_pipe_id = hfi_pipe_id;

	return &pstate->base;
}

static void dcp_plane_atomic_print_state(struct drm_printer *p,
					 const struct drm_plane_state *state)
{
	const struct dcp_plane_state *pstate = to_dcp_plane_state(state);

	drm_printf(p, "\tprop_pkt_size=%zu\n", pstate->prop_pkt_size);
	drm_printf(p, "\thfi_pipe_id=%u\n", pstate->hfi_pipe_id);

	if (pstate->prop_pkt_size) {
		u32 hfi_rot = dcp_drm_to_hfi_rotation(state->rotation);

		drm_printf(p, "\thfi_format=0x%x\n", pstate->hfi_format);
		drm_printf(p, "\thfi_rotation=0x%x\n", hfi_rot);
	}
}

static bool dcp_plane_format_mod_supported(struct drm_plane *plane,
					   u32 format, uint64_t modifier)
{
	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	if (modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED)
		return dcp_drm_to_hfi_format(format, modifier) != U32_MAX;

	return false;
}

static const struct drm_plane_helper_funcs dcp_plane_helper_funcs = {
	.prepare_fb     = dcp_plane_prepare_fb,
	.cleanup_fb     = dcp_plane_cleanup_fb,
	.atomic_check   = dcp_plane_atomic_check,
	.atomic_update  = dcp_plane_atomic_update,
	.atomic_disable = dcp_plane_atomic_disable,
};

static const struct drm_plane_funcs dcp_plane_funcs = {
	.update_plane           = drm_atomic_helper_update_plane,
	.disable_plane          = drm_atomic_helper_disable_plane,
	.atomic_create_state    = dcp_plane_atomic_create_state,
	.atomic_duplicate_state = dcp_plane_atomic_duplicate_state,
	.atomic_destroy_state   = dcp_plane_atomic_destroy_state,
	.format_mod_supported   = dcp_plane_format_mod_supported,
	.atomic_print_state     = dcp_plane_atomic_print_state,
};

struct drm_plane *dcp_plane_init(struct drm_device *dev,
				 u32 hfi_pipe_id,
				 enum drm_plane_type type,
				 unsigned long possible_crtcs)
{
	struct dcp_plane *dcp_plane;
	struct drm_plane *plane;
	int ret;

	dcp_plane = drmm_universal_plane_alloc(dev, struct dcp_plane, base,
					       possible_crtcs,
							       &dcp_plane_funcs,
							       dcp_plane_formats,
							       ARRAY_SIZE(dcp_plane_formats),
							       dcp_format_modifiers,
					   type, NULL);
	if (IS_ERR(dcp_plane))
		return ERR_CAST(dcp_plane);

	plane = &dcp_plane->base;
	drm_plane_helper_add(plane, &dcp_plane_helper_funcs);

	ret = drm_plane_create_zpos_property(plane, 0, 0, DCP_ZPOS_MAX);
	if (ret)
		drm_dbg_driver(plane->dev, "plane%u: zpos property failed\n", hfi_pipe_id);

	drm_plane_create_alpha_property(plane);

	drm_plane_create_blend_mode_property(plane,
					     BIT(DRM_MODE_BLEND_PIXEL_NONE) |
		BIT(DRM_MODE_BLEND_PREMULTI) |
		BIT(DRM_MODE_BLEND_COVERAGE));

	drm_plane_create_rotation_property(plane,
					   DRM_MODE_ROTATE_0,
		DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_90 |
		DRM_MODE_ROTATE_180 | DRM_MODE_ROTATE_270 |
		DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y);

	drm_plane_enable_fb_damage_clips(plane);

	dcp_plane->hfi_pipe_id = hfi_pipe_id;
	if (plane->state)
		to_dcp_plane_state(plane->state)->hfi_pipe_id = hfi_pipe_id;

	drm_dbg_driver(dev, "plane initialized (type=%d, hfi_pipe_id=%u)\n",
		       type, hfi_pipe_id);
	return plane;
}
