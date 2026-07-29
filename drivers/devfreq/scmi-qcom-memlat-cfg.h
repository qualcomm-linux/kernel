/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __DRIVERS_DEVFREQ_SCMI_QCOM_MEMLAT_CONFIG_H__
#define __DRIVERS_DEVFREQ_SCMI_QCOM_MEMLAT_CONFIG_H__

/*
 * Memlat Effective Frequency Calculation Method
 * CPUCP_EFFECTIVE_FREQ_METHOD_0 - Uses CPU Cycles and CONST Cycles to calculate
 * CPUCP_EFFECTIVE_FREQ_METHOD_1 - Uses CPU Cycles and time period
 */
#define CPUCP_EFFECTIVE_FREQ_CALC_METHOD_0	0
#define CPUCP_EFFECTIVE_FREQ_CALC_METHOD_1	1

#define EV_CPU_CYCLES		0
#define EV_CNT_CYCLES		1
#define EV_INST_RETIRED		2
#define EV_STALL_BACKEND_MEM	3
#define EV_L2_D_RFILL		5
#define INVALID_IDX		0xff

#define MEMLAT_ALGO_STR		0x4D454D4C4154ULL /* MEMLAT */

struct scmi_qcom_map_table {
	unsigned int cpu_freq;
	unsigned int mem_freq;
};

struct scmi_qcom_opp_data {
	unsigned long freq;
	unsigned int level;
};

struct scmi_qcom_memory_range {
	unsigned int min_freq;
	unsigned int max_freq;
};

enum common_ev_idx {
	INST_IDX,
	CYC_IDX,
	CONST_CYC_IDX,
	FE_STALL_IDX,
	BE_STALL_IDX,
	NUM_COMMON_EVS
};

enum grp_ev_idx {
	MISS_IDX,
	WB_IDX,
	ACC_IDX,
	NUM_GRP_EVS
};

/*
 * CPUCP firmware identifies memory groups by a small integer (the hw_type
 * carried in node_msg / scalar_param_msg / map_param_msg / ev_map_msg). The
 * encoding is shared between the cfg tables below and scmi_qcom_devfreq_get_cur_freq()
 * which special-cases DDR_QOS as a level-based bus rather than a frequency-scaled one.
 */
enum scmi_qcom_memlat_hw_type {
	MEMLAT_HW_DDR			= 0,
	MEMLAT_HW_LLCC			= 1,
	MEMLAT_HW_DDR_QOS_COMPUTE	= 2,
};

struct scmi_qcom_monitor_cfg {
	const struct scmi_qcom_map_table *table;
	const char *name;
	u32 be_stall_floor;
	u32 cpu_mask;
	u32 ipm_ceil;
	int table_len;
};

struct scmi_qcom_memory_cfg {
	const struct scmi_qcom_monitor_cfg *monitor_cfg;
	const struct scmi_qcom_opp_data *mem_table;
	struct scmi_qcom_memory_range memory_range;
	const u32 *grp_ev;
	const char *name;
	u32 memory_type;
	int monitor_cnt;
	int num_opps;
};

struct scmi_qcom_memlat_cfg_data {
	const struct scmi_qcom_memory_cfg *memory_cfg;
	const u32 *common_ev;
	u32 cpucp_freq_method;
	u32 cpucp_sample_ms;
	int memory_cnt;
};

static const u32 glymur_common_ev[NUM_COMMON_EVS] = {
	[INST_IDX]      = EV_INST_RETIRED,
	[CYC_IDX]       = EV_CPU_CYCLES,
	[CONST_CYC_IDX] = EV_CNT_CYCLES,
	[FE_STALL_IDX]  = INVALID_IDX,
	[BE_STALL_IDX]  = EV_STALL_BACKEND_MEM,
};

static const u32 glymur_ddr_grp_ev[NUM_GRP_EVS] = {
	[MISS_IDX] = EV_L2_D_RFILL,
	[WB_IDX]   = INVALID_IDX,
	[ACC_IDX]  = INVALID_IDX,
};

static const u32 glymur_llcc_grp_ev[NUM_GRP_EVS] = {
	[MISS_IDX] = EV_L2_D_RFILL,
	[WB_IDX]   = INVALID_IDX,
	[ACC_IDX]  = INVALID_IDX,
};

static const u32 glymur_ddr_qos_grp_ev[NUM_GRP_EVS] = {
	[MISS_IDX] = EV_L2_D_RFILL,
	[WB_IDX]   = INVALID_IDX,
	[ACC_IDX]  = INVALID_IDX,
};

static const u32 hamoa_common_ev[NUM_COMMON_EVS] = {
	[INST_IDX]      = EV_INST_RETIRED,
	[CYC_IDX]       = EV_CPU_CYCLES,
	[CONST_CYC_IDX] = EV_CNT_CYCLES,
	[FE_STALL_IDX]  = INVALID_IDX,
	[BE_STALL_IDX]  = EV_STALL_BACKEND_MEM,
};

static const u32 hamoa_ddr_grp_ev[NUM_GRP_EVS] = {
	[MISS_IDX] = EV_L2_D_RFILL,
	[WB_IDX]   = INVALID_IDX,
	[ACC_IDX]  = INVALID_IDX,
};

static const u32 hamoa_llcc_grp_ev[NUM_GRP_EVS] = {
	[MISS_IDX] = EV_L2_D_RFILL,
	[WB_IDX]   = INVALID_IDX,
	[ACC_IDX]  = INVALID_IDX,
};

static const u32 hamoa_ddr_qos_grp_ev[NUM_GRP_EVS] = {
	[MISS_IDX] = EV_L2_D_RFILL,
	[WB_IDX]   = INVALID_IDX,
	[ACC_IDX]  = INVALID_IDX,
};

static const struct scmi_qcom_opp_data glymur_llcc_table[] = {
	{ .freq = 315000000 },
	{ .freq = 479000000 },
	{ .freq = 545000000 },
	{ .freq = 725000000 },
	{ .freq = 840000000 },
	{ .freq = 959000000 },
	{ .freq = 1090000000 },
	{ .freq = 1211000000 },
};

static const struct scmi_qcom_opp_data hamoa_llcc_table[] = {
	{ .freq = 300000000 },
	{ .freq = 466000000 },
	{ .freq = 600000000 },
	{ .freq = 806000000 },
	{ .freq = 933000000 },
	{ .freq = 1066000000 },
};

static const struct scmi_qcom_opp_data glymur_ddr_table[] = {
	{ .freq = 200000000 },
	{ .freq = 547000000 },
	{ .freq = 1353000000 },
	{ .freq = 1555000000 },
	{ .freq = 1708000000 },
	{ .freq = 2092000000 },
	{ .freq = 2736000000 },
	{ .freq = 3187000000 },
	{ .freq = 3686000000 },
	{ .freq = 4224000000 },
	{ .freq = 4761000000 },
};

static const struct scmi_qcom_opp_data hamoa_ddr_table[] = {
	{ .freq = 200000000 },
	{ .freq = 547000000 },
	{ .freq = 768000000 },
	{ .freq = 1555000000 },
	{ .freq = 1708000000 },
	{ .freq = 2092000000 },
	{ .freq = 2736000000 },
	{ .freq = 3187000000 },
	{ .freq = 3686000000 },
	{ .freq = 4224000000 },
};

/*
 * DDR_QOS is a level-based bus (0 = nominal, 1 = boost), not a
 * frequency-scaled one. The OPP entries below use synthetic frequencies
 * (1 / 100) purely as distinct devfreq keys so trans_stat can show
 * level transitions. scmi_qcom_devfreq_get_cur_freq() maps the firmware
 * level back to the matching key.
 */
static const struct scmi_qcom_opp_data glymur_ddr_qos_table[] = {
	{ .freq = 1, .level = 0 },
	{ .freq = 100, .level = 1 },
};

static const struct scmi_qcom_memory_cfg glymur_memory_cfg[] = {
	{
		.memory_type = MEMLAT_HW_DDR,
		.name = "ddr",
		.mem_table = glymur_ddr_table,
		.num_opps = ARRAY_SIZE(glymur_ddr_table),
		.grp_ev = glymur_ddr_grp_ev,
		.monitor_cnt = 4,
		.memory_range = { .min_freq = 547000, .max_freq = 4761000},
		.monitor_cfg = (const struct scmi_qcom_monitor_cfg[]) {
			{
				.name = "mon_0",
				.cpu_mask = 0x3f,
				.ipm_ceil = 60000000,
				.be_stall_floor = 1,
				.table_len = 8,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 960, .mem_freq = 547000 },
					{ .cpu_freq = 1133, .mem_freq = 1353000 },
					{ .cpu_freq = 1594, .mem_freq = 1555000 },
					{ .cpu_freq = 1920, .mem_freq = 1708000 },
					{ .cpu_freq = 2228, .mem_freq = 2736000 },
					{ .cpu_freq = 2362, .mem_freq = 3187000 },
					{ .cpu_freq = 2650, .mem_freq = 3686000 },
					{ .cpu_freq = 2938, .mem_freq = 4761000 },
				}
			},
			{
				.name = "mon_1",
				.cpu_mask = 0xfc0,
				.ipm_ceil = 60000000,
				.be_stall_floor = 1,
				.table_len = 8,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 356, .mem_freq = 547000 },
					{ .cpu_freq = 1018, .mem_freq = 1353000 },
					{ .cpu_freq = 1536, .mem_freq = 1555000 },
					{ .cpu_freq = 1748, .mem_freq = 1708000 },
					{ .cpu_freq = 2324, .mem_freq = 2736000 },
					{ .cpu_freq = 2496, .mem_freq = 3187000 },
					{ .cpu_freq = 2900, .mem_freq = 3686000 },
					{ .cpu_freq = 3514, .mem_freq = 4761000 },
				}
			},
			{
				.name = "mon_2",
				.cpu_mask = 0x3f000,
				.ipm_ceil = 60000000,
				.be_stall_floor = 1,
				.table_len = 8,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 356, .mem_freq = 547000 },
					{ .cpu_freq = 1018, .mem_freq = 1353000 },
					{ .cpu_freq = 1536, .mem_freq = 1555000 },
					{ .cpu_freq = 1748, .mem_freq = 1708000 },
					{ .cpu_freq = 2324, .mem_freq = 2736000 },
					{ .cpu_freq = 2496, .mem_freq = 3187000 },
					{ .cpu_freq = 2900, .mem_freq = 3686000 },
					{ .cpu_freq = 3514, .mem_freq = 4761000 },
				}
			},
			{
				.name = "mon_3",
				.cpu_mask = 0x3ffff,
				.table_len = 4,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 2823, .mem_freq = 547000 },
					{ .cpu_freq = 3034, .mem_freq = 1555000 },
					{ .cpu_freq = 3226, .mem_freq = 1708000 },
					{ .cpu_freq = 5012, .mem_freq = 2092000 },
				}
			},
		},
	},
	{
		.memory_type = MEMLAT_HW_LLCC,
		.name = "llcc",
		.mem_table = glymur_llcc_table,
		.num_opps = ARRAY_SIZE(glymur_llcc_table),
		.grp_ev = glymur_llcc_grp_ev,
		.monitor_cnt = 3,
		.memory_range = { .min_freq = 315000, .max_freq = 1211000},
		.monitor_cfg = (const struct scmi_qcom_monitor_cfg[]) {
			{
				.name = "mon_0",
				.cpu_mask = 0x3f,
				.ipm_ceil = 60000000,
				.be_stall_floor = 1,
				.table_len = 7,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 960, .mem_freq = 315000 },
					{ .cpu_freq = 1113, .mem_freq = 479000 },
					{ .cpu_freq = 1594, .mem_freq = 545000 },
					{ .cpu_freq = 1920, .mem_freq = 725000 },
					{ .cpu_freq = 2362, .mem_freq = 840000 },
					{ .cpu_freq = 2650, .mem_freq = 959000 },
					{ .cpu_freq = 2938, .mem_freq = 1211000 },
				}
			},
			{
				.name = "mon_1",
				.cpu_mask = 0xfc0,
				.ipm_ceil = 60000000,
				.be_stall_floor = 1,
				.table_len = 7,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 356, .mem_freq = 315000 },
					{ .cpu_freq = 1018, .mem_freq = 479000 },
					{ .cpu_freq = 1536, .mem_freq = 545000 },
					{ .cpu_freq = 1748, .mem_freq = 725000 },
					{ .cpu_freq = 2496, .mem_freq = 840000 },
					{ .cpu_freq = 2900, .mem_freq = 959000 },
					{ .cpu_freq = 3514, .mem_freq = 1211000 },
				}
			},
			{
				.name = "mon_2",
				.cpu_mask = 0x3f000,
				.ipm_ceil = 60000000,
				.be_stall_floor = 1,
				.table_len = 7,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 356, .mem_freq = 315000 },
					{ .cpu_freq = 1018, .mem_freq = 479000 },
					{ .cpu_freq = 1536, .mem_freq = 545000 },
					{ .cpu_freq = 1748, .mem_freq = 725000 },
					{ .cpu_freq = 2496, .mem_freq = 840000 },
					{ .cpu_freq = 2900, .mem_freq = 959000 },
					{ .cpu_freq = 3514, .mem_freq = 1211000 },
				}
			},
		},
	},
	{
		.memory_type = MEMLAT_HW_DDR_QOS_COMPUTE,
		.name = "ddr-qos",
		.monitor_cnt = 3,
		.mem_table = glymur_ddr_qos_table,
		.num_opps = ARRAY_SIZE(glymur_ddr_qos_table),
		.grp_ev = glymur_ddr_qos_grp_ev,
		.memory_range = { .min_freq = 0, .max_freq = 1},
		.monitor_cfg = (const struct scmi_qcom_monitor_cfg[]) {
			{
				.name = "mon_0",
				.cpu_mask = 0x3f,
				.ipm_ceil = 80000000,
				.be_stall_floor = 1,
				.table_len = 2,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 2362, .mem_freq = 0 },
					{ .cpu_freq = 2938, .mem_freq = 1 },
				}
			},
			{
				.name = "mon_1",
				.cpu_mask = 0xfc0,
				.ipm_ceil = 80000000,
				.be_stall_floor = 1,
				.table_len = 2,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 2496, .mem_freq = 0 },
					{ .cpu_freq = 3514, .mem_freq = 1 },
				}
			},
			{
				.name = "mon_2",
				.cpu_mask = 0x3f000,
				.ipm_ceil = 80000000,
				.be_stall_floor = 1,
				.table_len = 2,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 2496, .mem_freq = 0 },
					{ .cpu_freq = 3514, .mem_freq = 1 },
				}
			},
		},
	},
};

static const struct scmi_qcom_memory_cfg hamoa_memory_cfg[] = {
	{
		.memory_type = MEMLAT_HW_DDR,
		.name = "ddr",
		.mem_table = hamoa_ddr_table,
		.num_opps = ARRAY_SIZE(hamoa_ddr_table),
		.grp_ev = hamoa_ddr_grp_ev,
		.monitor_cnt = 4,
		.memory_range = { .min_freq = 200000, .max_freq = 4224000},
		.monitor_cfg = (const struct scmi_qcom_monitor_cfg[]) {
			{
				.name = "mon_0",
				.cpu_mask = 0xf,
				.ipm_ceil = 20000000,
				.be_stall_floor = 1,
				.table_len = 6,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 999, .mem_freq = 547000 },
					{ .cpu_freq = 1440, .mem_freq = 768000 },
					{ .cpu_freq = 1671, .mem_freq = 1555000 },
					{ .cpu_freq = 2189, .mem_freq = 2092000 },
					{ .cpu_freq = 2516, .mem_freq = 3187000 },
					{ .cpu_freq = 3860, .mem_freq = 4224000 },
				}
			},
			{
				.name = "mon_1",
				.cpu_mask = 0xf0,
				.ipm_ceil = 20000000,
				.be_stall_floor = 1,
				.table_len = 6,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 999, .mem_freq = 547000 },
					{ .cpu_freq = 1440, .mem_freq = 768000 },
					{ .cpu_freq = 1671, .mem_freq = 1555000 },
					{ .cpu_freq = 2189, .mem_freq = 2092000 },
					{ .cpu_freq = 2516, .mem_freq = 3187000 },
					{ .cpu_freq = 3860, .mem_freq = 4224000 },
				}
			},
			{
				.name = "mon_2",
				.cpu_mask = 0xf00,
				.ipm_ceil = 20000000,
				.be_stall_floor = 1,
				.table_len = 6,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 999, .mem_freq = 547000 },
					{ .cpu_freq = 1440, .mem_freq = 768000 },
					{ .cpu_freq = 1671, .mem_freq = 1555000 },
					{ .cpu_freq = 2189, .mem_freq = 2092000 },
					{ .cpu_freq = 2516, .mem_freq = 3187000 },
					{ .cpu_freq = 3860, .mem_freq = 4224000 },
				}
			},
			{
				.name = "mon_3",
				.cpu_mask = 0xfff,
				.table_len = 4,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 1440, .mem_freq = 547000 },
					{ .cpu_freq = 2189, .mem_freq = 768000 },
					{ .cpu_freq = 2516, .mem_freq = 1555000 },
					{ .cpu_freq = 3860, .mem_freq = 2092000 },
				}
			},
		},
	},
	{
		.memory_type = MEMLAT_HW_LLCC,
		.name = "llcc",
		.mem_table = hamoa_llcc_table,
		.num_opps = ARRAY_SIZE(hamoa_llcc_table),
		.grp_ev = hamoa_llcc_grp_ev,
		.monitor_cnt = 3,
		.memory_range = { .min_freq = 300000, .max_freq = 1066000},
		.monitor_cfg = (const struct scmi_qcom_monitor_cfg[]) {
			{
				.name = "mon_0",
				.cpu_mask = 0xf,
				.ipm_ceil = 20000000,
				.be_stall_floor = 1,
				.table_len = 6,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 999, .mem_freq = 300000 },
					{ .cpu_freq = 1440, .mem_freq = 466000 },
					{ .cpu_freq = 1671, .mem_freq = 600000 },
					{ .cpu_freq = 2189, .mem_freq = 806000 },
					{ .cpu_freq = 2516, .mem_freq = 933000 },
					{ .cpu_freq = 3860, .mem_freq = 1066000 },
				}
			},
			{
				.name = "mon_1",
				.cpu_mask = 0xf0,
				.ipm_ceil = 20000000,
				.be_stall_floor = 1,
				.table_len = 6,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 999, .mem_freq = 300000 },
					{ .cpu_freq = 1440, .mem_freq = 466000 },
					{ .cpu_freq = 1671, .mem_freq = 600000 },
					{ .cpu_freq = 2189, .mem_freq = 806000 },
					{ .cpu_freq = 2516, .mem_freq = 933000 },
					{ .cpu_freq = 3860, .mem_freq = 1066000 },
				}
			},
			{
				.name = "mon_2",
				.cpu_mask = 0xf00,
				.ipm_ceil = 20000000,
				.be_stall_floor = 1,
				.table_len = 6,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 999, .mem_freq = 300000 },
					{ .cpu_freq = 1440, .mem_freq = 466000 },
					{ .cpu_freq = 1671, .mem_freq = 600000 },
					{ .cpu_freq = 2189, .mem_freq = 806000 },
					{ .cpu_freq = 2516, .mem_freq = 933000 },
					{ .cpu_freq = 3860, .mem_freq = 1066000 },
				}
			},
		},
	},
	{
		.memory_type = MEMLAT_HW_DDR_QOS_COMPUTE,
		.name = "ddr-qos",
		.monitor_cnt = 3,
		.mem_table = glymur_ddr_qos_table,
		.num_opps = ARRAY_SIZE(glymur_ddr_qos_table),
		.grp_ev = hamoa_ddr_qos_grp_ev,
		.memory_range = { .min_freq = 0, .max_freq = 1},
		.monitor_cfg = (const struct scmi_qcom_monitor_cfg[]) {
			{
				.name = "mon_0",
				.cpu_mask = 0xf,
				.ipm_ceil = 20000000,
				.be_stall_floor = 1,
				.table_len = 2,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 2189, .mem_freq = 0 },
					{ .cpu_freq = 3860, .mem_freq = 1 },
				}
			},
			{
				.name = "mon_1",
				.cpu_mask = 0xf0,
				.ipm_ceil = 20000000,
				.be_stall_floor = 1,
				.table_len = 2,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 2189, .mem_freq = 0 },
					{ .cpu_freq = 3860, .mem_freq = 1 },
				}
			},
			{
				.name = "mon_2",
				.cpu_mask = 0xf00,
				.ipm_ceil = 20000000,
				.be_stall_floor = 1,
				.table_len = 2,
				.table = (const struct scmi_qcom_map_table[]) {
					{ .cpu_freq = 2189, .mem_freq = 0 },
					{ .cpu_freq = 3860, .mem_freq = 1 },
				}
			},
		},
	},
};

static const struct scmi_qcom_memlat_cfg_data glymur_memlat_data = {
	.memory_cfg = glymur_memory_cfg,
	.common_ev = glymur_common_ev,
	.cpucp_freq_method = CPUCP_EFFECTIVE_FREQ_CALC_METHOD_1,
	.cpucp_sample_ms = 4,
	.memory_cnt = ARRAY_SIZE(glymur_memory_cfg),
};

static const struct scmi_qcom_memlat_cfg_data hamoa_memlat_data = {
	.memory_cfg = hamoa_memory_cfg,
	.common_ev = hamoa_common_ev,
	.cpucp_freq_method = CPUCP_EFFECTIVE_FREQ_CALC_METHOD_1,
	.cpucp_sample_ms = 4,
	.memory_cnt = ARRAY_SIZE(hamoa_memory_cfg),
};

#endif
