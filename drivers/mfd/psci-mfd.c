// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/of.h>
#include <linux/string.h>

static const struct mfd_cell psci_cells[] = {
	{
		.name = "psci-cpuidle-domain",
	},
	{
		.name = "psci-reboot-mode",
	},
};

static int psci_mfd_match_pdev_name(struct device *dev, const void *data)
{
	struct platform_device *pdev;

	if (dev->bus != &platform_bus_type)
		return 0;

	pdev = to_platform_device(dev);

	return !strcmp(pdev->name, data);
}

static void psci_mfd_bind_reboot_mode_node(struct platform_device *pdev)
{
	struct device_node *np;
	struct device *child;

	if (!pdev->dev.of_node)
		return;

	np = of_get_child_by_name(pdev->dev.of_node, "reboot-mode");
	if (!np)
		return;

	child = device_find_child(&pdev->dev, "psci-reboot-mode",
				  psci_mfd_match_pdev_name);
	if (!child) {
		dev_dbg(&pdev->dev, "psci-reboot-mode child not found\n");
		of_node_put(np);
		return;
	}

	device_set_node(child, of_fwnode_handle(np));
	put_device(child);
	of_node_put(np);
}

static int psci_mfd_probe(struct platform_device *pdev)
{
	int ret;

	ret = devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_AUTO, psci_cells,
				   ARRAY_SIZE(psci_cells), NULL, 0, NULL);
	if (ret)
		goto out;

	psci_mfd_bind_reboot_mode_node(pdev);

out:
	return ret;
}

static const struct of_device_id psci_mfd_of_match[] = {
	{ .compatible = "arm,psci-1.0" },
	{ }
};
MODULE_DEVICE_TABLE(of, psci_mfd_of_match);

static struct platform_driver psci_mfd_driver = {
	.probe = psci_mfd_probe,
	.driver = {
		.name = "psci-mfd",
		.of_match_table = psci_mfd_of_match,
	},
};

static int __init psci_mfd_init(void)
{
	return platform_driver_register(&psci_mfd_driver);
}

core_initcall(psci_mfd_init);
