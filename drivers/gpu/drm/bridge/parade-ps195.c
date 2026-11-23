// SPDX-License-Identifier: GPL-2.0-only
/*
 * Parade PS195 DisplayPort to HDMI protocol converter
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>

/* Enable the supplies in this order and disable them in reverse. */
static const char * const ps195_supply_names[] = {
	"vdd09",
	"vdd12",
	"vdd33",
};

struct ps195 {
	struct drm_bridge bridge;
	struct device *dev;
	struct regulator_bulk_data supplies[ARRAY_SIZE(ps195_supply_names)];
	struct gpio_desc *reset_gpio;
	bool powered;
};

static void ps195_disable_supplies(struct ps195 *ps195, unsigned int count)
{
	int ret;

	while (count) {
		count--;
		ret = regulator_disable(ps195->supplies[count].consumer);
		if (ret)
			dev_err(ps195->dev, "Failed to disable %s: %d\n",
				ps195->supplies[count].supply, ret);

		/* Allow discharge before disabling the next rail. */
		if (count)
			usleep_range(1000, 2000);
	}
}

static void ps195_power_off(struct ps195 *ps195)
{
	if (!ps195->powered)
		return;

	gpiod_set_value_cansleep(ps195->reset_gpio, 1);
	ps195_disable_supplies(ps195, ARRAY_SIZE(ps195->supplies));
	ps195->powered = false;
}

static int ps195_power_on(struct ps195 *ps195)
{
	unsigned int i;
	int ret;

	if (ps195->powered)
		return 0;

	gpiod_set_value_cansleep(ps195->reset_gpio, 1);
	for (i = 0; i < ARRAY_SIZE(ps195->supplies); i++) {
		ret = regulator_enable(ps195->supplies[i].consumer);
		if (ret) {
			dev_err(ps195->dev, "Failed to enable %s: %d\n",
				ps195->supplies[i].supply, ret);
			goto err_disable;
		}

		/* Allow the rail to settle before continuing. */
		usleep_range(2000, 3000);
	}

	gpiod_set_value_cansleep(ps195->reset_gpio, 0);
	ps195->powered = true;

	return 0;

err_disable:
	ps195_disable_supplies(ps195, i);
	return ret;
}

static void ps195_power_off_action(void *data)
{
	ps195_power_off(data);
}

static int ps195_suspend(struct device *dev)
{
	ps195_power_off(dev_get_drvdata(dev));
	return 0;
}

static int ps195_resume(struct device *dev)
{
	return ps195_power_on(dev_get_drvdata(dev));
}

static const struct dev_pm_ops ps195_pm_ops = {
	/* Keep AUX and HPD available until the display controller suspends. */
	LATE_SYSTEM_SLEEP_PM_OPS(ps195_suspend, ps195_resume)
};

static int ps195_bridge_attach(struct drm_bridge *bridge,
			       struct drm_encoder *encoder,
			       enum drm_bridge_attach_flags flags)
{
	return drm_bridge_attach(encoder, bridge->next_bridge, bridge, flags);
}

static const struct drm_bridge_funcs ps195_bridge_funcs = {
	.atomic_create_state = drm_atomic_helper_bridge_create_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.attach = ps195_bridge_attach,
};

static int ps195_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ps195 *ps195;
	unsigned int i;
	int ret;

	ps195 = devm_drm_bridge_alloc(dev, struct ps195, bridge,
				      &ps195_bridge_funcs);
	if (IS_ERR(ps195))
		return PTR_ERR(ps195);

	ps195->dev = dev;
	platform_set_drvdata(pdev, ps195);

	ps195->bridge.next_bridge = of_drm_get_bridge_by_endpoint(dev->of_node, 1, -1);
	if (IS_ERR(ps195->bridge.next_bridge))
		return dev_err_probe(dev, PTR_ERR(ps195->bridge.next_bridge),
				     "Failed to get output bridge\n");

	for (i = 0; i < ARRAY_SIZE(ps195->supplies); i++)
		ps195->supplies[i].supply = ps195_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ps195->supplies),
				      ps195->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get supplies\n");

	ps195->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ps195->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ps195->reset_gpio),
				     "Failed to get reset GPIO\n");

	ret = devm_add_action_or_reset(dev, ps195_power_off_action, ps195);
	if (ret)
		return ret;

	/*
	 * Firmware drives DP HPD and services AUX, including EDID reads,
	 * before a modeset. Keep the bridge powered while the system is
	 * awake, including when no monitor is connected.
	 */
	ret = ps195_power_on(ps195);
	if (ret)
		return ret;

	ps195->bridge.of_node = dev->of_node;
	ps195->bridge.type = DRM_MODE_CONNECTOR_HDMIA;

	return devm_drm_bridge_add(dev, &ps195->bridge);
}

static const struct of_device_id ps195_of_match[] = {
	{ .compatible = "parade,ps195" },
	{ }
};
MODULE_DEVICE_TABLE(of, ps195_of_match);

static struct platform_driver ps195_driver = {
	.probe = ps195_probe,
	.driver = {
		.name = "parade-ps195",
		.of_match_table = ps195_of_match,
		.pm = pm_sleep_ptr(&ps195_pm_ops),
	},
};
module_platform_driver(ps195_driver);

MODULE_AUTHOR("Mahadevan P <mahadevan.p@oss.qualcomm.com>");
MODULE_DESCRIPTION("Parade PS195 DisplayPort to HDMI bridge driver");
MODULE_LICENSE("GPL");
