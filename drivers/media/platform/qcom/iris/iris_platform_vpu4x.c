// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2025 Linaro Ltd
 */

#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_platform_common.h"
#include "iris_vpu_buffer.h"
#include "iris_vpu_common.h"

#include "iris_platform_kaanapali.h"

#define WRAPPER_INTR_STATUS_A2HWD_BMSK		BIT(3)

const struct iris_firmware_desc iris_vpu40_p2_s7_gen2_desc = {
	.firmware_data = &iris_hfi_gen2_data,
	.get_vpu_buffer_size = iris_vpu4x_buf_size,
	.fwname = "qcom/vpu/vpu40_p2_s7.mbn",
};

static struct iris_fmt iris_fmts_vpu4x_dec[] = {
	[IRIS_FMT_H264] = {
		.pixfmt = V4L2_PIX_FMT_H264,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
	},
	[IRIS_FMT_HEVC] = {
		.pixfmt = V4L2_PIX_FMT_HEVC,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
	},
	[IRIS_FMT_VP9] = {
		.pixfmt = V4L2_PIX_FMT_VP9,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
	},
	[IRIS_FMT_AV1] = {
		.pixfmt = V4L2_PIX_FMT_AV1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
	},
};

static const struct icc_info iris_icc_info_vpu4x[] = {
	{ "cpu-cfg",    1000, 1000     },
	{ "video-mem",  1000, 15000000 },
};

static const struct bw_info iris_bw_table_dec_vpu4x[] = {
	{ ((4096 * 2160) / 256) * 60, 1608000 },
	{ ((4096 * 2160) / 256) * 30,  826000 },
	{ ((1920 * 1080) / 256) * 60,  567000 },
	{ ((1920 * 1080) / 256) * 30,  294000 },
};

static const char * const iris_opp_pd_table_vpu4x[] = { "mxc", "mmcx" };

static struct platform_inst_caps iris_inst_cap_vpu4x = {
	.min_frame_width = 96,
	.max_frame_width = 8192,
	.min_frame_height = 96,
	.max_frame_height = 8192,
	.max_mbpf = (8192 * 4352) / 256,
	.mb_cycles_vpp = 200,
	.mb_cycles_fw = 489583,
	.mb_cycles_fw_vpp = 66234,
	.num_comv = 0,
	.max_frame_rate = MAXIMUM_FPS,
	.max_operating_rate = MAXIMUM_FPS,
};

static struct ubwc_config_data iris_ubwc_config_vpu4x = {
	.max_channels = 8,
	.mal_length = 32,
	.highest_bank_bit = 16,
	.bank_swzl_level = 0,
	.bank_swz2_level = 1,
	.bank_swz3_level = 1,
	.bank_spreading = 1,
};

const struct iris_platform_data kaanapali_data = {
	.firmware_desc = &iris_vpu40_p2_s7_gen2_desc,
	.vpu_ops = &iris_vpu4x_ops,
	.icc_tbl = iris_icc_info_vpu4x,
	.icc_tbl_size = ARRAY_SIZE(iris_icc_info_vpu4x),
	.clk_rst_tbl = kaanapali_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(kaanapali_clk_reset_table),
	.bw_tbl_dec = iris_bw_table_dec_vpu4x,
	.bw_tbl_dec_size = ARRAY_SIZE(iris_bw_table_dec_vpu4x),
	.pmdomain_tbl = kaanapali_pmdomain_table,
	.pmdomain_tbl_size = ARRAY_SIZE(kaanapali_pmdomain_table),
	.opp_pd_tbl = iris_opp_pd_table_vpu4x,
	.opp_pd_tbl_size = ARRAY_SIZE(iris_opp_pd_table_vpu4x),
	.clk_tbl = kaanapali_clk_table,
	.clk_tbl_size = ARRAY_SIZE(kaanapali_clk_table),
	.opp_clk_tbl = kaanapali_opp_clk_table,
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.inst_iris_fmts = iris_fmts_vpu4x_dec,
	.inst_iris_fmts_size = ARRAY_SIZE(iris_fmts_vpu4x_dec),
	.inst_caps = &iris_inst_cap_vpu4x,
	.tz_cp_config_data = tz_cp_config_kaanapali,
	.tz_cp_config_data_size = ARRAY_SIZE(tz_cp_config_kaanapali),
	.ubwc_config = &iris_ubwc_config_vpu4x,
	.num_vpp_pipe = 2,
	.wd_intr_mask = WRAPPER_INTR_STATUS_A2HWD_BMSK,
	.icc_ib_multiplier = 1,
	.max_session_count = 16,
	.max_core_mbpf = NUM_MBS_8K * 2,
	.max_core_mbps = ((8192 * 4352) / 256) * 60,
};
