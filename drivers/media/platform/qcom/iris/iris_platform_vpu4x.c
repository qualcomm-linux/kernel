// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_hfi_gen2.h"
#include "iris_vpu_buffer.h"
#include "iris_vpu_common.h"

#include "iris_platform_sm8550.h"
#include "iris_platform_kaanapali.h"

static const struct iris_firmware_desc iris_vpu40_p2_s7_gen2_desc = {
	.firmware_data = &iris_hfi_gen2_data,
	.get_vpu_buffer_size = iris_vpu4x_buf_size,
	.fwname = "qcom/vpu/vpu40_p2_s7.mbn",
};

static const u32 iris_fmts_vpu4x_dec[] = {
	V4L2_PIX_FMT_H264,
	V4L2_PIX_FMT_HEVC,
	V4L2_PIX_FMT_VP9,
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

const struct iris_platform_data kaanapali_data = {
	.firmware_desc_gen2 = &iris_vpu40_p2_s7_gen2_desc,
	.vpu_ops = &iris_vpu4x_ops,
	.icc_tbl = iris_icc_info_vpu4x,
	.icc_tbl_size = ARRAY_SIZE(iris_icc_info_vpu4x),
	.clk_rst_tbl = iris_kaanapali_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(iris_kaanapali_clk_reset_table),
	.bw_tbl_dec = iris_bw_table_dec_vpu4x,
	.bw_tbl_dec_size = ARRAY_SIZE(iris_bw_table_dec_vpu4x),
	.ctrl_data = &iris_kaanapali_ctrl_data,
	.vcodec_data = iris_kaanapali_vcodec_data,
	.vcodec_vpp0_data = &iris_kaanapali_vpp0_data,
	.vcodec_vpp1_data = &iris_kaanapali_vpp1_data,
	.apv_data = &iris_kaanapali_apv_data,
	.opp_pd_tbl = iris_opp_pd_table_vpu4x,
	.opp_pd_tbl_size = ARRAY_SIZE(iris_opp_pd_table_vpu4x),
	.opp_clk_tbl = iris_kaanapali_opp_clk_table,
	/* Upper bound of DMA address range */
	.dma_mask = 0xffc00000 - 1,
	.inst_iris_fmts = iris_fmts_vpu4x_dec,
	.inst_iris_fmts_size = ARRAY_SIZE(iris_fmts_vpu4x_dec),
	.inst_caps = &platform_inst_cap_sm8550,
	.tz_cp_config_data = iris_kaanapali_tz_cp_config,
	.tz_cp_config_data_size = ARRAY_SIZE(iris_kaanapali_tz_cp_config),
	.num_vpp_pipe = 2,
	.max_session_count = 16,
	.max_core_mbpf = NUM_MBS_8K * 2,
	.max_core_mbps = ((8192 * 4320) / 256) * 60,
};
