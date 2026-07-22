// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pm_runtime.h>
#include <media/v4l2-mem2mem.h>

#include "iris_instance.h"
#include "iris_utils.h"

bool iris_res_is_less_than(u32 width, u32 height,
			   u32 ref_width, u32 ref_height)
{
	u32 num_mbs = NUM_MBS_PER_FRAME(height, width);
	u32 max_side = max(ref_width, ref_height);

	if (num_mbs < NUM_MBS_PER_FRAME(ref_height, ref_width) &&
	    width < max_side &&
	    height < max_side)
		return true;

	return false;
}

int iris_get_mbpf(struct iris_inst *inst)
{
	struct v4l2_format *inp_f = inst->fmt_src;
	u32 height = max(inp_f->fmt.pix_mp.height, inst->crop.height);
	u32 width = max(inp_f->fmt.pix_mp.width, inst->crop.width);

	return NUM_MBS_PER_FRAME(height, width);
}

bool iris_split_mode_enabled(struct iris_inst *inst)
{
	return inst->fmt_dst->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_NV12 ||
		inst->fmt_dst->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_QC08C ||
		inst->fmt_dst->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_P010 ||
		inst->fmt_dst->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_QC10C;
}

bool iris_fmt_is_8bit(u32 pixelformat)
{
	return pixelformat == V4L2_PIX_FMT_NV12 ||
		pixelformat == V4L2_PIX_FMT_QC08C;
}

bool iris_fmt_is_10bit(u32 pixelformat)
{
	return pixelformat == V4L2_PIX_FMT_P010 ||
		pixelformat == V4L2_PIX_FMT_QC10C;
}

void iris_helper_buffers_done(struct iris_inst *inst, unsigned int type,
			      enum vb2_buffer_state state)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct vb2_v4l2_buffer *buf;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		while ((buf = v4l2_m2m_src_buf_remove(m2m_ctx)))
			v4l2_m2m_buf_done(buf, state);
	} else if (V4L2_TYPE_IS_CAPTURE(type)) {
		while ((buf = v4l2_m2m_dst_buf_remove(m2m_ctx)))
			v4l2_m2m_buf_done(buf, state);
	}
}

int iris_wait_for_session_response(struct iris_inst *inst, bool is_flush)
{
	struct completion *done;
	int ret;

	done = is_flush ? &inst->flush_completion : &inst->completion;

	mutex_unlock(&inst->lock);
	ret = wait_for_completion_timeout(done, msecs_to_jiffies(HW_RESPONSE_TIMEOUT_VALUE));
	mutex_lock(&inst->lock);
	if (!ret) {
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return -ETIMEDOUT;
	}

	return 0;
}

struct iris_inst *iris_get_instance(struct iris_core *core, u32 session_id)
{
	struct iris_inst *inst;

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (inst->session_id == session_id) {
			mutex_unlock(&core->lock);
			return inst;
		}
	}

	mutex_unlock(&core->lock);
	return NULL;
}

static u32 iris_get_mbps(struct iris_inst *inst)
{
	u32 fps = max(inst->frame_rate, inst->operating_rate);

	return iris_get_mbpf(inst) * fps;
}

static void iris_get_core_load(struct iris_core *core, u32 *core_load, u32 *core_session, bool mbpf)
{
	bool dual_core = core->iris_platform_data->dual_core;
	struct iris_inst *inst;
	u32 load;

	core_load[0] = 0;
	core_load[1] = 0;
	core_session[0] = 0;
	core_session[1] = 0;

	list_for_each_entry(inst, &core->instances, list) {
		if (mbpf)
			load = iris_get_mbpf(inst);
		else
			load = iris_get_mbps(inst);

		if (inst->core_id == IRIS_VCODEC0) {
			core_load[0] += load;
			core_session[0]++;
		} else if (dual_core && inst->core_id == IRIS_VCODEC1) {
			core_load[1] += load;
			core_session[1]++;
		}
	}
}

static int iris_select_core_id(struct iris_inst *inst, u32 *core_load, u32 *core_session,
			       u32 max_load, u32 new_load)
{
	u32 max_session = inst->core->iris_platform_data->max_session_count;
	bool dual_core = inst->core->iris_platform_data->dual_core;
	u32 core_index;

	core_index = (core_load[0] > core_load[1] && dual_core) ? 1 : 0;

	if (core_session[core_index] >= max_session)
		core_index = core_index == 0 && dual_core ? 1 : 0;

	if (core_session[core_index] >= max_session)
		return -ENOMEM;

	if (core_load[core_index] + new_load <= max_load)
		inst->core_id = core_index == 0 ? IRIS_VCODEC0 : IRIS_VCODEC1;
	else
		return -ENOMEM;

	return 0;
}

int iris_check_core_mbpf(struct iris_inst *inst)
{
	u32 max_core_mbpf = inst->core->iris_platform_data->max_core_mbpf;
	u32 core_mbpf[2], core_session[2], new_mbpf;
	struct iris_core *core = inst->core;
	int ret;

	mutex_lock(&core->lock);
	inst->core_id = 0;
	iris_get_core_load(inst->core, core_mbpf, core_session, true);
	new_mbpf = iris_get_mbpf(inst);
	ret = iris_select_core_id(inst, core_mbpf, core_session, max_core_mbpf, new_mbpf);
	mutex_unlock(&core->lock);

	return ret;
}

int iris_check_core_mbps(struct iris_inst *inst)
{
	u32 max_core_mbps = inst->core->iris_platform_data->max_core_mbps;
	u32 core_mbps[2] = {0, 0}, core_session[2], new_mbps;
	struct iris_core *core = inst->core;
	int ret;

	mutex_lock(&core->lock);
	inst->core_id = 0;
	iris_get_core_load(inst->core, core_mbps, core_session, false);
	new_mbps = iris_get_mbps(inst);
	ret = iris_select_core_id(inst, core_mbps, core_session, max_core_mbps, new_mbps);
	mutex_unlock(&core->lock);

	return ret;
}

bool is_rotation_90_or_270(struct iris_inst *inst)
{
	return inst->fw_caps[ROTATION].value == 90 ||
		inst->fw_caps[ROTATION].value == 270;
}
