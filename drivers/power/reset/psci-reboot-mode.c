// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/device/faux.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/psci.h>
#include <linux/reboot.h>
#include <linux/reboot-mode.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/module.h>

/*
 * Predefined reboot-modes are defined as per the values
 * of enum reboot_mode defined in the kernel: reboot.c.
 */
static struct mode_info psci_resets[] = {
	{ .mode = "warm", .magic = REBOOT_WARM},
	{ .mode = "soft", .magic = REBOOT_SOFT},
	{ .mode = "cold", .magic = REBOOT_COLD},
};

static void psci_reboot_mode_set_predefined_modes(struct reboot_mode_driver *reboot)
{
	INIT_LIST_HEAD(&reboot->predefined_modes);
	for (u32 i = 0; i < ARRAY_SIZE(psci_resets); i++) {
		/* Prepare the magic with arg1 as 0 and arg2 as per pre-defined mode */
		psci_resets[i].magic = REBOOT_MODE_MAGIC(0, psci_resets[i].magic);
		INIT_LIST_HEAD(&psci_resets[i].list);
		list_add_tail(&psci_resets[i].list, &reboot->predefined_modes);
	}
}

/*
 * arg1 is reset_type(Low 32 bit of magic).
 * arg2 is cookie(High 32 bit of magic).
 * If reset_type is 0, cookie will be used to decide the reset command.
 */
static int psci_reboot_mode_write(struct reboot_mode_driver *reboot, u64 magic)
{
	u32 reset_type = REBOOT_MODE_ARG1(magic);
	u32 cookie = REBOOT_MODE_ARG2(magic);

	pr_err("DEBUG: PSCI write called");
	pr_err("DEBUG: PSCI write called");
	pr_err("DEBUG: PSCI write called");
	if (reset_type == 0) {
		if (cookie == REBOOT_WARM || cookie == REBOOT_SOFT)
			psci_set_reset_cmd(true, 0, 0);
		else
			psci_set_reset_cmd(false, 0, 0);
	} else {
		psci_set_reset_cmd(true, reset_type, cookie);
	}

	return NOTIFY_DONE;
}

static int psci_reboot_mode_probe(struct platform_device *pdev)
{
	struct reboot_mode_driver *reboot;
	int ret;

	reboot = devm_kzalloc(&pdev->dev, sizeof(*reboot), GFP_KERNEL);
	if (!reboot)
		return -ENOMEM;

	psci_reboot_mode_set_predefined_modes(reboot);
	reboot->write = psci_reboot_mode_write;
	reboot->dev = &pdev->dev;

	ret = devm_reboot_mode_register(&pdev->dev, reboot);
	if (ret) {
		dev_err_probe(&pdev->dev, ret, "devm_reboot_mode_register failed %d\n", ret);
		return ret;
	}

	return 0;
}

static struct platform_driver psci_reboot_mode_driver = {
	.probe  = psci_reboot_mode_probe,
	.driver = {
		.name	= "psci-reboot-mode",
	},
};

module_platform_driver(psci_reboot_mode_driver);
