/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_PLATFORM_KAANAPALI_H__
#define __IRIS_PLATFORM_KAANAPALI_H__

extern const char *const iris_kaanapali_clk_reset_table[4];
extern const struct iris_power_domain_data iris_kaanapali_ctrl_data;
extern const struct iris_power_domain_data iris_kaanapali_vcodec_data[1];
extern const struct iris_power_domain_data iris_kaanapali_vpp0_data;
extern const struct iris_power_domain_data iris_kaanapali_vpp1_data;
extern const struct iris_power_domain_data iris_kaanapali_apv_data;
extern const char *const iris_kaanapali_opp_clk_table[5];
extern struct tz_cp_config iris_kaanapali_tz_cp_config[2];

#endif /* __IRIS_PLATFORM_KAANAPALI_H__ */
