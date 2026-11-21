// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_platform_common.h"
#include "iris_vpu_buffer.h"
#include "iris_vpu_common.h"

#include "iris_platform_sm8550.h"
#include "iris_platform_hawi.h"

static const struct iris_firmware_desc iris_vpu50_p2_s8_gen2_desc = {
	.firmware_data = &iris_hfi_gen2_data,
	.get_vpu_buffer_size = iris_vpu5x_buf_size,
	.fwname = "qcom/vpu/vpu50_p2_s8.mbn",
};

static const u32 iris_fmts_vpu5_dec[] = {
	V4L2_PIX_FMT_H264,
	V4L2_PIX_FMT_HEVC,
	V4L2_PIX_FMT_VP9,
	V4L2_PIX_FMT_AV1,
};

static const struct icc_info iris_icc_info_vpu5[] = {
	{ "cpu-cfg",    1000, 1000     },
	{ "video-mem",  1000, 15000000 },
};

static const struct bw_info iris_bw_table_dec_vpu5[] = {
	{ ((4096 * 2160) / 256) * 60, 1608000 },
	{ ((4096 * 2160) / 256) * 30,  826000 },
	{ ((1920 * 1080) / 256) * 60,  567000 },
	{ ((1920 * 1080) / 256) * 30,  294000 },
};

static const char * const iris_opp_pd_table_vpu5[] = { "mxc", "mmcx" };

static const struct tz_cp_config tz_cp_config_vpu5[] = {
	{
		.cp_start = VIDEO_REGION_VM0_SECURE_NP_ID,
		.cp_size = 0,
		.cp_nonpixel_start = 0x0d000000,
		.cp_nonpixel_size = 0x25000000,
	},
	{
		.cp_start = VIDEO_REGION_VM0_NONSECURE_NP_ID,
		.cp_size = 0,
		.cp_nonpixel_start = 0x32000000,
		.cp_nonpixel_size = 0xcdc00000,
	},
};

const struct iris_platform_data hawi_data = {
	.firmware_desc_gen2 = &iris_vpu50_p2_s8_gen2_desc,
	.vpu_ops = &iris_vpu5x_ops,
	.icc_tbl = iris_icc_info_vpu5,
	.icc_tbl_size = ARRAY_SIZE(iris_icc_info_vpu5),
	.clk_rst_tbl = hawi_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(hawi_clk_reset_table),
	.bw_tbl_dec = iris_bw_table_dec_vpu5,
	.bw_tbl_dec_size = ARRAY_SIZE(iris_bw_table_dec_vpu5),
	.ctrl_data = &hawi_ctrl_data,
	.vcodec_data = hawi_vcodec_data,
	.vcodec_vpp0_data = &hawi_vcodec_vpp0_data,
	.vcodec_vpp1_data = &hawi_vcodec_vpp1_data,
	.apv_data = &hawi_apv_data,
	.mm_int_data = &hawi_mm_int_data,
	.cx_int_data = &hawi_cx_int_data,
	.opp_pd_tbl = iris_opp_pd_table_vpu5,
	.opp_pd_tbl_size = ARRAY_SIZE(iris_opp_pd_table_vpu5),
	.opp_clk_tbl = hawi_opp_clk_table,
	/* Upper bound of DMA address range */
	.dma_mask = 0xffc00000 - 1,
	.inst_iris_fmts = iris_fmts_vpu5_dec,
	.inst_iris_fmts_size = ARRAY_SIZE(iris_fmts_vpu5_dec),
	.inst_caps = &platform_inst_cap_sm8550,
	.tz_cp_config_data = tz_cp_config_vpu5,
	.tz_cp_config_data_size = ARRAY_SIZE(tz_cp_config_vpu5),
	.num_vpp_pipe = 2,
	.max_session_count = 16,
	.max_core_mbpf = NUM_MBS_8K * 2,
	.max_core_mbps = NUM_MBS_8K * 60,
};
