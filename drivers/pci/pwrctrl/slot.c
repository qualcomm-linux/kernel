// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pci-pwrctrl.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

struct pci_pwrctrl_slot {
	struct pci_pwrctrl pwrctrl;
	struct regulator_bulk_data *supplies;
	int num_supplies;
	struct clk *clk;
};

static int pci_pwrctrl_slot_power_on(struct pci_pwrctrl *pwrctrl)
{
	struct pci_pwrctrl_slot *slot = container_of(pwrctrl,
					    struct pci_pwrctrl_slot, pwrctrl);
	int ret;

	ret = regulator_bulk_enable(slot->num_supplies, slot->supplies);
	if (ret < 0) {
		dev_err(slot->pwrctrl.dev, "Failed to enable slot regulators\n");
		return ret;
	}

	return clk_prepare_enable(slot->clk);
}

static int pci_pwrctrl_slot_power_off(struct pci_pwrctrl *pwrctrl)
{
	struct pci_pwrctrl_slot *slot = container_of(pwrctrl,
					    struct pci_pwrctrl_slot, pwrctrl);

	regulator_bulk_disable(slot->num_supplies, slot->supplies);
	clk_disable_unprepare(slot->clk);

	return 0;
}

static void devm_pci_pwrctrl_slot_release(void *data)
{
	struct pci_pwrctrl_slot *slot = data;

	pci_pwrctrl_slot_power_off(&slot->pwrctrl);
	regulator_bulk_free(slot->num_supplies, slot->supplies);
}

static int pci_pwrctrl_slot_probe(struct platform_device *pdev)
{
	struct pci_pwrctrl_slot *slot;
	struct device *dev = &pdev->dev;
	int ret;

	slot = devm_kzalloc(dev, sizeof(*slot), GFP_KERNEL);
	if (!slot)
		return -ENOMEM;

	ret = of_regulator_bulk_get_all(dev, dev_of_node(dev),
					&slot->supplies);
	if (ret < 0) {
		dev_err_probe(dev, ret, "Failed to get slot regulators\n");
		return ret;
	}

	slot->num_supplies = ret;

	ret = devm_add_action_or_reset(dev, devm_pci_pwrctrl_slot_release,
				       slot);
	if (ret)
		return ret;

	slot->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(slot->clk))
		return dev_err_probe(dev, PTR_ERR(slot->clk),
				     "Failed to enable slot clock\n");

	slot->pwrctrl.power_on = pci_pwrctrl_slot_power_on;
	slot->pwrctrl.power_off = pci_pwrctrl_slot_power_off;

	pci_pwrctrl_init(&slot->pwrctrl, dev);

	ret = devm_pci_pwrctrl_device_set_ready(dev, &slot->pwrctrl);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register pwrctrl driver\n");

	return 0;
}

static const struct of_device_id pci_pwrctrl_slot_of_match[] = {
	{
		.compatible = "pciclass,0604",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pci_pwrctrl_slot_of_match);

static struct platform_driver pci_pwrctrl_slot_driver = {
	.driver = {
		.name = "pci-pwrctrl-slot",
		.of_match_table = pci_pwrctrl_slot_of_match,
	},
	.probe = pci_pwrctrl_slot_probe,
};
module_platform_driver(pci_pwrctrl_slot_driver);

MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("Generic PCI Power Control driver for PCI Slots");
MODULE_LICENSE("GPL");
