// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bits.h>
#include <linux/iopoll.h>
#include <linux/reset.h>

#include "iris_instance.h"
#include "iris_resources.h"
#include "iris_vpu_common.h"

#include "iris_vpu_register_defines.h"

static int iris_vpu2_init_cb_devs(struct iris_core *core)
{
	struct device *dev;

	dev = iris_create_cb_dev(core, "video-firmware");
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	core->fw_dev = dev;

	return 0;
}

static void iris_vpu2_deinit_cb_devs(struct iris_core *core)
{
	if (core->fw_dev)
		platform_device_unregister(to_platform_device(core->fw_dev));
}

const struct vpu_ops iris_vpu2_ops = {
	.power_off_hw = iris_vpu_power_off_hw,
	.power_on_hw = iris_vpu_power_on_hw,
	.power_off_controller = iris_vpu_power_off_controller,
	.power_on_controller = iris_vpu_power_on_controller,
	.calc_freq = iris_vpu2_calculate_frequency,
	.set_hwmode = iris_vpu_set_hwmode,
	.set_preset_registers = iris_vpu_set_preset_registers,
	.interrupt_init = iris_vpu_interrupt_init,
	.init_cb_devs = iris_vpu2_init_cb_devs,
	.deinit_cb_devs = iris_vpu2_deinit_cb_devs,
};
