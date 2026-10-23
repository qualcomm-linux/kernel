// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/iopoll.h>
#include <linux/pm_opp.h>
#include <linux/reset.h>

#include "iris_instance.h"
#include "iris_vpu_common.h"
#include "iris_vpu_register_defines.h"

/*
 * iris5 specific register defines.
 * Many base offsets are shared with prior generations:
 *   WRAPPER_BASE_OFFS = 0x000B0000
 *   AON_BASE_OFFS     = 0x000E0000
 *   AON_MVP_NOC_RESET = 0x0001F000
 *   CPU_BASE_OFFS     = 0x000A0000
 */

#define WRAPPER_MVP_NOC_LPI_CONTROL		(WRAPPER_BASE_OFFS + 0x110)
#define WRAPPER_MVP_NOC_LPI_STATUS		(WRAPPER_BASE_OFFS + 0x114)
#define WRAPPER_MVP_NOC_CX_LPI_CONTROL		(WRAPPER_BASE_OFFS + 0x118)
#define WRAPPER_MVP_NOC_CX_LPI_STATUS		(WRAPPER_BASE_OFFS + 0x11C)

#define AON_WRAPPER_MVP_NOC_ARCG_CONTROL	(AON_BASE_OFFS + 0x10)

#define AON_WRAPPER_MVP_VIDEO_CTL_NOC_LPI_CONTROL	(AON_BASE_OFFS + 0x2C)
#define AON_WRAPPER_MVP_VIDEO_CTL_NOC_LPI_STATUS	(AON_BASE_OFFS + 0x30)

#define CPU_NOC_BASE_OFFS			0x000D0000
#define CPU_NOC_ERRORLOGGER_MAINCTL_LOW		(CPU_NOC_BASE_OFFS + 0x08)
#define CPU_NOC_SBM_FAULTINEN0_LOW		(CPU_NOC_BASE_OFFS + 0x240)

#define NOC_BASE_OFFS				0x00010000
#define NOC_ERL_ERRORLOGGER_MAINCTL_LOW		(NOC_BASE_OFFS + 0xA008)
#define NOC_ERL_ERRORLOGGER_ERRCLR_LOW		(NOC_BASE_OFFS + 0xA018)
#define NOC_SBM_FAULTINEN0_LOW			(NOC_BASE_OFFS + 0x7040)

#define NOC5_RESET_VPP0_ONLY_DISABLED		0x36010E
#define NOC5_RESET_VPP1_DISABLED		0x35000F
#define NOC5_RESET_ALL_VPP_ENABLED		0x37010F

#define NOC_LPI_PD_QREQ				BIT(0)
#define NOC_SBM_FAULTINEN0_PORT0		BIT(0)
#define NOC_ERRORLOGGER_MAINCTL_FAULTEN		BIT(0)
#define NOC_ERL_ERRORLOGGER_ERRCLR		BIT(0)
#define AON_MVP_NOC_ARCG_OVERRIDE		BIT(0)

#define WRAPPER_INTR_MASK_SS_CPU_NOC		BIT(6)
#define WRAPPER_INTR_MASK_SS_NOC		BIT(5)

static bool iris_vpu5x_hw_power_collapsed(struct iris_core *core)
{
	u32 value = readl(core->reg_base + WRAPPER_CORE_POWER_STATUS);

	return !(value & CORE_PWR_ON);
}

static int iris_vpu5x_genpd_set_hwmode(struct iris_core *core, bool hw_mode, u32 efuse_value)
{
	int ret;

	ret = iris_genpd_set_hwmode(core->vcodec, hw_mode);
	if (ret)
		return ret;

	if (!(efuse_value & DISABLE_VIDEO_VPP0_BIT)) {
		ret = iris_genpd_set_hwmode(core->vcodec_vpp0, hw_mode);
		if (ret)
			goto restore_hw_domain_mode;
	}

	if (!(efuse_value & DISABLE_VIDEO_VPP1_BIT)) {
		ret = iris_genpd_set_hwmode(core->vcodec_vpp1, hw_mode);
		if (ret)
			goto restore_vpp0_domain_mode;
	}

	if (!(efuse_value & DISABLE_VIDEO_APV_BIT)) {
		ret = iris_genpd_set_hwmode(core->apv, hw_mode);
		if (ret)
			goto restore_vpp1_domain_mode;
	}

	ret = iris_genpd_set_hwmode(core->mm_int, hw_mode);
	if (ret)
		goto restore_apv_domain_mode;

	ret = iris_genpd_set_hwmode(core->cx_int, hw_mode);
	if (ret)
		goto restore_mm_int_domain_mode;

	return 0;

restore_mm_int_domain_mode:
	iris_genpd_set_hwmode(core->mm_int, !hw_mode);
restore_apv_domain_mode:
	if (!(efuse_value & DISABLE_VIDEO_APV_BIT))
		iris_genpd_set_hwmode(core->apv, !hw_mode);
restore_vpp1_domain_mode:
	if (!(efuse_value & DISABLE_VIDEO_VPP1_BIT))
		iris_genpd_set_hwmode(core->vcodec_vpp1, !hw_mode);
restore_vpp0_domain_mode:
	if (!(efuse_value & DISABLE_VIDEO_VPP0_BIT))
		iris_genpd_set_hwmode(core->vcodec_vpp0, !hw_mode);
restore_hw_domain_mode:
	iris_genpd_set_hwmode(core->vcodec, !hw_mode);

	return ret;
}

static int iris_vpu5x_set_hwmode(struct iris_core *core)
{
	u32 efuse_value = readl(core->reg_base + WRAPPER_EFUSE_MONITOR);

	if (!(efuse_value & DISABLE_VIDEO_APV_BIT))
		iris_vpu_ahb_sync_reset_apv(core);

	iris_vpu_ahb_sync_reset_hardware(core);

	return iris_vpu5x_genpd_set_hwmode(core, true, efuse_value);
}

/*
 * Perform an LPI power-down handshake on a single NOC interface.
 *
 * Sets QREQ and polls for the status DONE bit. On a busy response the request
 * is toggled and, when @errclr_offs is non-zero, the NOC error-log clear
 * register at that offset is written between retries to unblock the fabric.
 * Pass @errclr_offs = 0 for interfaces that do not require error clearing.
 */
static void iris_vpu5x_noc_lpi_handshake(struct iris_core *core,
					 u32 ctrl_offs, u32 status_offs,
					 u32 busy_mask, u32 errclr_offs,
					 const char *name)
{
	bool handshake_done, handshake_busy;
	u32 value, count = 0;
	int ret;

	do {
		if (errclr_offs) {
			value = readl(core->reg_base + errclr_offs);
			value |= NOC_ERL_ERRORLOGGER_ERRCLR;
			writel(value, core->reg_base + errclr_offs);
		}

		value = readl(core->reg_base + ctrl_offs);
		value |= NOC_LPI_PD_QREQ;
		writel(value, core->reg_base + ctrl_offs);
		usleep_range(10, 20);

		value = readl(core->reg_base + status_offs);
		handshake_done = value & NOC_LPI_STATUS_DONE;
		handshake_busy = value & busy_mask;

		if (handshake_done || !handshake_busy)
			break;

		value = readl(core->reg_base + ctrl_offs);
		value &= ~NOC_LPI_PD_QREQ;
		writel(value, core->reg_base + ctrl_offs);
		usleep_range(10, 20);

	} while (++count < 1000);

	if (!handshake_done && handshake_busy)
		dev_err(core->dev, "%s NOC LPI handshake timeout\n", name);

	ret = readl_poll_timeout(core->reg_base + status_offs,
				 value, value & NOC_LPI_STATUS_DONE, 200, 2000);
	if (ret)
		dev_err(core->dev, "%s NOC LPI status timeout\n", name);

	value = readl(core->reg_base + ctrl_offs);
	value &= ~NOC_LPI_PD_QREQ;
	writel(value, core->reg_base + ctrl_offs);
}

static int iris_vpu5x_power_on_cx_int(struct iris_core *core)
{
	u32 value;
	int ret;

	ret = iris_enable_power_domain_and_clocks(core, core->cx_int);
	if (ret)
		return ret;

	value = readl(core->reg_base + WRAPPER_MVP_NOC_CX_LPI_CONTROL);
	value &= ~NOC_LPI_PD_QREQ;
	writel(value, core->reg_base + WRAPPER_MVP_NOC_CX_LPI_CONTROL);

	readl_poll_timeout(core->reg_base + WRAPPER_MVP_NOC_CX_LPI_STATUS,
			   value, !(value & NOC_LPI_STATUS_DONE), 200, 2000);

	return 0;
}

static void iris_vpu5x_power_off_cx_int(struct iris_core *core)
{
	iris_vpu5x_noc_lpi_handshake(core,
				     WRAPPER_MVP_NOC_CX_LPI_CONTROL,
				     WRAPPER_MVP_NOC_CX_LPI_STATUS,
				     NOC_LPI_STATUS_DENY | NOC_LPI_STATUS_ACTIVE,
				     NOC_ERL_ERRORLOGGER_ERRCLR_LOW, "CX");

	iris_disable_power_domain_and_clocks(core, core->cx_int);
}

static int iris_vpu5x_power_on_mm_int(struct iris_core *core)
{
	u32 value;
	int ret;

	ret = iris_enable_power_domain_and_clocks(core, core->mm_int);
	if (ret)
		return ret;

	value = readl(core->reg_base + WRAPPER_MVP_NOC_LPI_CONTROL);
	value &= ~NOC_LPI_PD_QREQ;
	writel(value, core->reg_base + WRAPPER_MVP_NOC_LPI_CONTROL);

	readl_poll_timeout(core->reg_base + WRAPPER_MVP_NOC_LPI_STATUS,
			   value, !(value & NOC_LPI_STATUS_DONE), 200, 2000);

	return 0;
}

static void iris_vpu5x_power_off_mm_int(struct iris_core *core)
{
	u32 value;

	value = readl(core->reg_base + WRAPPER_MVP_NOC_LPI_CONTROL);
	value &= ~NOC_LPI_PD_QREQ;
	writel(value, core->reg_base + WRAPPER_MVP_NOC_LPI_CONTROL);

	value = readl(core->reg_base + WRAPPER_MVP_NOC_CX_LPI_CONTROL);
	value &= ~NOC_LPI_PD_QREQ;
	writel(value, core->reg_base + WRAPPER_MVP_NOC_CX_LPI_CONTROL);

	iris_vpu5x_noc_lpi_handshake(core,
				     WRAPPER_MVP_NOC_LPI_CONTROL,
				     WRAPPER_MVP_NOC_LPI_STATUS,
				     NOC_LPI_STATUS_DENY | NOC_LPI_STATUS_ACTIVE,
				     NOC_ERL_ERRORLOGGER_ERRCLR_LOW, "MM");

	iris_disable_power_domain_and_clocks(core, core->mm_int);
}

static int iris_vpu5x_power_on_hardware(struct iris_core *core)
{
	u32 efuse_value = readl(core->reg_base + WRAPPER_EFUSE_MONITOR);
	int ret;

	ret = iris_vpu5x_power_on_cx_int(core);
	if (ret)
		return ret;

	ret = iris_vpu5x_power_on_mm_int(core);
	if (ret)
		goto err_disable_cx_int;

	ret = iris_enable_power_domain_and_clocks(core, core->vcodec);
	if (ret)
		goto err_disable_mm_int;

	if (!(efuse_value & DISABLE_VIDEO_VPP0_BIT)) {
		ret = iris_enable_power_domain_and_clocks(core, core->vcodec_vpp0);
		if (ret)
			goto err_disable_hw_power;
	}

	if (!(efuse_value & DISABLE_VIDEO_VPP1_BIT)) {
		ret = iris_enable_power_domain_and_clocks(core, core->vcodec_vpp1);
		if (ret)
			goto err_disable_vpp0_power;
	}

	if (!(efuse_value & DISABLE_VIDEO_APV_BIT)) {
		ret = iris_vpu_power_on_apv(core);
		if (ret)
			goto err_disable_vpp1_power;
	}

	return 0;

err_disable_vpp1_power:
	if (!(efuse_value & DISABLE_VIDEO_VPP1_BIT))
		iris_disable_power_domain_and_clocks(core, core->vcodec_vpp1);
err_disable_vpp0_power:
	if (!(efuse_value & DISABLE_VIDEO_VPP0_BIT))
		iris_disable_power_domain_and_clocks(core, core->vcodec_vpp0);
err_disable_hw_power:
	iris_disable_power_domain_and_clocks(core, core->vcodec);
err_disable_mm_int:
	iris_vpu5x_power_off_mm_int(core);
err_disable_cx_int:
	iris_vpu5x_power_off_cx_int(core);

	return ret;
}

static void iris_vpu5x_power_off_hardware(struct iris_core *core)
{
	u32 efuse_value = readl(core->reg_base + WRAPPER_EFUSE_MONITOR);
	u32 noc_reset_mask;
	u32 value;
	int ret;

	iris_vpu5x_genpd_set_hwmode(core, false, efuse_value);

	if (!(efuse_value & DISABLE_VIDEO_APV_BIT))
		iris_vpu_power_off_apv(core);

	if (iris_vpu5x_hw_power_collapsed(core))
		goto disable_clocks_and_power;

	value = readl(core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);
	if (value & CORE_CLK_HALT)
		writel(CORE_CLK_RUN, core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);

	readl_poll_timeout(core->reg_base + VCODEC_SS_IDLE_STATUSN,
			   value, (value & VPU_IDLE_BITS) == VPU_IDLE_BITS, 2000, 20000);

	if (efuse_value & DISABLE_VIDEO_VPP0_BIT)
		noc_reset_mask = NOC5_RESET_VPP0_ONLY_DISABLED;
	else if (efuse_value & DISABLE_VIDEO_VPP1_BIT)
		noc_reset_mask = NOC5_RESET_VPP1_DISABLED;
	else
		noc_reset_mask = NOC5_RESET_ALL_VPP_ENABLED;

	writel(noc_reset_mask, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_REQ);
	ret = readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_RESET_ACK,
				 value, (value & noc_reset_mask) == noc_reset_mask, 200, 2000);
	if (ret)
		dev_err(core->dev, "MVP NOC reset ack timeout\n");

	writel(noc_reset_mask, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_SYNCRST);
	writel(0x0, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_SYNCRST);
	writel(0x0, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_REQ);

	ret = readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_RESET_ACK,
				 value, value == 0x0, 200, 2000);
	if (ret)
		dev_err(core->dev, "MVP NOC reset deassert timeout\n");

	writel(CORE_BRIDGE_SW_RESET | CORE_BRIDGE_HW_RESET_DISABLE,
	       core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(CORE_BRIDGE_HW_RESET_DISABLE, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(0x0, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);

disable_clocks_and_power:
	if (!(efuse_value & DISABLE_VIDEO_VPP1_BIT))
		iris_disable_power_domain_and_clocks(core, core->vcodec_vpp1);

	if (!(efuse_value & DISABLE_VIDEO_VPP0_BIT))
		iris_disable_power_domain_and_clocks(core, core->vcodec_vpp0);

	iris_disable_power_domain_and_clocks(core, core->vcodec);
	iris_vpu5x_power_off_mm_int(core);
	iris_vpu5x_power_off_cx_int(core);
}

static int iris_vpu5x_power_on_controller(struct iris_core *core)
{
	u32 mask_val;
	int ret;

	ret = iris_enable_power_domain_and_clocks(core, core->ctrl);
	if (ret)
		return ret;

	mask_val = readl(core->reg_base + CPU_NOC_SBM_FAULTINEN0_LOW);
	mask_val |= NOC_SBM_FAULTINEN0_PORT0;
	writel(mask_val, core->reg_base + CPU_NOC_SBM_FAULTINEN0_LOW);

	mask_val = readl(core->reg_base + CPU_NOC_ERRORLOGGER_MAINCTL_LOW);
	mask_val |= NOC_ERRORLOGGER_MAINCTL_FAULTEN;
	writel(mask_val, core->reg_base + CPU_NOC_ERRORLOGGER_MAINCTL_LOW);

	mask_val = readl(core->reg_base + WRAPPER_INTR_MASK);
	mask_val &= ~WRAPPER_INTR_MASK_SS_CPU_NOC;
	writel(mask_val, core->reg_base + WRAPPER_INTR_MASK);

	mask_val = readl(core->reg_base + NOC_SBM_FAULTINEN0_LOW);
	mask_val |= NOC_SBM_FAULTINEN0_PORT0;
	writel(mask_val, core->reg_base + NOC_SBM_FAULTINEN0_LOW);

	mask_val = readl(core->reg_base + NOC_ERL_ERRORLOGGER_MAINCTL_LOW);
	mask_val |= NOC_ERRORLOGGER_MAINCTL_FAULTEN;
	writel(mask_val, core->reg_base + NOC_ERL_ERRORLOGGER_MAINCTL_LOW);

	mask_val = readl(core->reg_base + WRAPPER_INTR_MASK);
	mask_val &= ~WRAPPER_INTR_MASK_SS_NOC;
	writel(mask_val, core->reg_base + WRAPPER_INTR_MASK);

	return 0;
}

static int iris_vpu5x_power_off_controller(struct iris_core *core)
{
	u32 ctrl_rst_tbl_size = core->iris_platform_data->controller_rst_tbl_size;
	u32 value;

	writel(MSK_SIGNAL_FROM_TENSILICA | MSK_CORE_POWER_ON,
	       core->reg_base + CPU_CS_X2RPMH);

	iris_vpu5x_noc_lpi_handshake(core,
				     WRAPPER_IRIS_CPU_NOC_LPI_CONTROL,
				     WRAPPER_IRIS_CPU_NOC_LPI_STATUS,
				     NOC_LPI_STATUS_DENY, 0, "CPU");

	iris_vpu5x_noc_lpi_handshake(core,
				     AON_WRAPPER_MVP_VIDEO_CTL_NOC_LPI_CONTROL,
				     AON_WRAPPER_MVP_VIDEO_CTL_NOC_LPI_STATUS,
				     NOC_LPI_STATUS_DENY | NOC_LPI_STATUS_ACTIVE, 0, "Video CTL");

	writel(0x0, core->reg_base + WRAPPER_DEBUG_BRIDGE_LPI_CONTROL);
	readl_poll_timeout(core->reg_base + WRAPPER_DEBUG_BRIDGE_LPI_STATUS,
			   value, value == 0x0, 200, 2000);

	value = readl(core->reg_base + AON_WRAPPER_MVP_NOC_ARCG_CONTROL);
	value |= AON_MVP_NOC_ARCG_OVERRIDE;
	writel(value, core->reg_base + AON_WRAPPER_MVP_NOC_ARCG_CONTROL);

	iris_disable_power_domain_and_clocks(core, core->ctrl);

	reset_control_bulk_assert(ctrl_rst_tbl_size, core->controller_resets);
	usleep_range(400, 500);
	reset_control_bulk_deassert(ctrl_rst_tbl_size, core->controller_resets);

	return 0;
}

static int iris_vpu5x_init_cb_devs(struct iris_core *core)
{
	struct device *dev;

	dev = iris_create_cb_dev(core, "non-pixel");
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	core->np_dev = dev;

	dev = iris_create_cb_dev(core, "pixel");
	if (IS_ERR(dev))
		goto unreg_np_dev;

	core->p_dev = dev;

	dev = iris_create_cb_dev(core, "video-firmware");
	if (IS_ERR(dev))
		goto unreg_p_dev;

	core->fw_dev = dev;

	return 0;

unreg_p_dev:
	if (core->p_dev)
		platform_device_unregister(to_platform_device(core->p_dev));
	core->p_dev = NULL;
unreg_np_dev:
	if (core->np_dev)
		platform_device_unregister(to_platform_device(core->np_dev));
	core->np_dev = NULL;

	return PTR_ERR(dev);
}

static void iris_vpu5x_deinit_cb_devs(struct iris_core *core)
{
	if (core->fw_dev)
		platform_device_unregister(to_platform_device(core->fw_dev));
	if (core->p_dev)
		platform_device_unregister(to_platform_device(core->p_dev));
	if (core->np_dev)
		platform_device_unregister(to_platform_device(core->np_dev));
}

const struct vpu_ops iris_vpu5x_ops = {
	.power_off_hw		  = iris_vpu5x_power_off_hardware,
	.power_on_hw		  = iris_vpu5x_power_on_hardware,
	.power_off_controller	  = iris_vpu5x_power_off_controller,
	.power_on_controller	  = iris_vpu5x_power_on_controller,
	.program_bootup_registers = iris_vpu35_vpu4x_program_bootup_registers,
	.calc_freq		  = iris_vpu3x_vpu4x_calculate_frequency,
	.set_hwmode		  = iris_vpu5x_set_hwmode,
	.interrupt_init		  = iris_vpu_interrupt_init,
	.set_preset_registers	  = iris_vpu_set_preset_registers,
	.init_cb_devs		  = iris_vpu5x_init_cb_devs,
	.deinit_cb_devs		  = iris_vpu5x_deinit_cb_devs,
};
