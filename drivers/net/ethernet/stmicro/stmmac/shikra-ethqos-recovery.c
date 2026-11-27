// SPDX-License-Identifier: GPL-2.0-only
/*
 * Shikra ETHQOS thin recovery driver
 *
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 *
 * Restores the Ethernet MAC clocks, the GCC reset and the PHY reset after
 * qcom-ethqos has been unbound from a qcom,shikra-ethqos device (EMAC0 or
 * EMAC1), so the MAC can be handed to vfio-platform and driven by the DPDK
 * stmmac PMD.
 *
 * Unbinding qcom-ethqos leaves the PHY held in reset and the MAC clocks off,
 * after which binding it again fails with -ENODEV on the PHY. Binding this
 * driver by hand undoes that.
 *
 * On remove the hardware is left running unless cleanup_on_remove is set:
 * devm only releases the clock, GPIO and reset handles, it does not change
 * the hardware state.
 *
 * This driver has no of_match_table on purpose: qcom,shikra-ethqos belongs to
 * qcom-ethqos, and a second match would let this driver be autoloaded and
 * claim the device at boot. It is bound by hand: load the module, write its
 * name to the device's driver_override, then write the device to bind.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/string.h>

#define SHIKRA_PHY_RESET_ASSERT_US	10000U
#define SHIKRA_PHY_RESET_DEASSERT_US	50000U
#define SHIKRA_PHY_RESET_SLACK_US	5000U

/*
 * The DT also lists "axi", but it is the same GCC clock as "stmmaceth", so
 * it is not enabled separately.
 */
static const char * const shikra_ethqos_clk_names[] = {
	"stmmaceth",
	"pclk",
	"ptp_ref",
	"rgmii",
	"axi-noc",
	"pcie-tile-axi-noc",
};

#define SHIKRA_ETHQOS_NUM_CLKS	ARRAY_SIZE(shikra_ethqos_clk_names)

/**
 * struct shikra_ethqos_recovery - per-device driver state
 * @dev:            platform device's struct device
 * @rst:            optional GCC reset control (DT: resets property)
 * @clks:           Ethernet MAC clocks, named by shikra_ethqos_clk_names
 * @phy_reset_gpio: PHY reset GPIO (DT: reset-gpios in the phy-handle node)
 * @phy_supply:     PHY 2.5 V supply (DT: vdda-2p5-supply in the phy-handle node)
 */
struct shikra_ethqos_recovery {
	struct device		*dev;
	struct reset_control	*rst;
	struct clk_bulk_data	clks[SHIKRA_ETHQOS_NUM_CLKS];
	struct gpio_desc	*phy_reset_gpio;
	struct regulator	*phy_supply;
};

static bool cleanup_on_remove;
module_param(cleanup_on_remove, bool, 0644);
MODULE_PARM_DESC(cleanup_on_remove,
		 "Tear down hardware on remove (default: false = keep running for DPDK handoff)");

static unsigned int speed;
module_param(speed, uint, 0644);
MODULE_PARM_DESC(speed,
		 "RGMII link speed in Mbit/s (10, 100 or 1000) to set the RGMII clock for, 0 leaves the clock unchanged (default: 0)");

/*
 * Drop our PHY supply reference and turn the supply off. Only called when
 * cleanup_on_remove is set, or on a failed probe.
 */
static void shikra_ethqos_release_phy_supply(struct shikra_ethqos_recovery *priv)
{
	if (!priv->phy_supply)
		return;

	regulator_disable(priv->phy_supply);
	regulator_put(priv->phy_supply);
	priv->phy_supply = NULL;
}

static int shikra_ethqos_phy_reset(struct shikra_ethqos_recovery *priv)
{
	struct device *dev = priv->dev;
	u32 assert_us = SHIKRA_PHY_RESET_ASSERT_US;
	u32 deassert_us = SHIKRA_PHY_RESET_DEASSERT_US;
	struct device_node *phy_np;
	struct gpio_desc *gpio;
	int ret;

	/* reset-gpios and the reset timings are in the PHY node, not the MAC node */
	phy_np = of_parse_phandle(dev->of_node, "phy-handle", 0);
	if (!phy_np)
		return dev_err_probe(dev, -ENODEV, "no phy-handle in DT\n");

	of_property_read_u32(phy_np, "reset-assert-us", &assert_us);
	of_property_read_u32(phy_np, "reset-deassert-us", &deassert_us);

	/*
	 * The PHY's 2.5 V supply is a fixed regulator. qcom-ethqos's consumer
	 * reference keeps it on while it is bound, and unbinding drops the last
	 * reference, so the regulator turns off and the PHY stops answering MDIO.
	 * Take a reference of our own and enable the supply before the reset
	 * pulse. The reference is kept on remove unless cleanup_on_remove is set.
	 */
	priv->phy_supply = of_regulator_get_optional(dev, phy_np, "vdda-2p5");
	if (IS_ERR(priv->phy_supply)) {
		of_node_put(phy_np);
		return dev_err_probe(dev, PTR_ERR(priv->phy_supply),
				     "failed to get PHY supply\n");
	}

	if (priv->phy_supply) {
		ret = regulator_enable(priv->phy_supply);
		if (ret) {
			regulator_put(priv->phy_supply);
			priv->phy_supply = NULL;
			of_node_put(phy_np);
			return dev_err_probe(dev, ret, "failed to enable PHY supply\n");
		}
	}

	gpio = devm_fwnode_gpiod_get(dev, of_fwnode_handle(phy_np), "reset",
				     GPIOD_OUT_HIGH, "phy-reset");
	of_node_put(phy_np);
	if (IS_ERR(gpio)) {
		shikra_ethqos_release_phy_supply(priv);
		return dev_err_probe(dev, PTR_ERR(gpio),
				     "failed to get PHY reset GPIO\n");
	}

	priv->phy_reset_gpio = gpio;

	/* The GPIO is GPIO_ACTIVE_LOW: logical 1 holds the PHY in reset */
	gpiod_set_value_cansleep(gpio, 1);
	usleep_range(assert_us, assert_us + SHIKRA_PHY_RESET_SLACK_US);
	gpiod_set_value_cansleep(gpio, 0);
	usleep_range(deassert_us, deassert_us + SHIKRA_PHY_RESET_SLACK_US);

	return 0;
}

/*
 * The RGMII clock rate follows the link speed. The DPDK PMD runs in user space and
 * cannot change a GCC clock, so set it here the way ethqos_update_link_clk() does:
 * with phy-mode rgmii-id the MAC adds no delay and the clock is 250, 25 and 2.5 MHz
 * for 1000, 100 and 10 Mbit/s. With any other RGMII mode the rate is doubled.
 */
static int shikra_ethqos_set_rgmii_rate(struct shikra_ethqos_recovery *priv)
{
	struct device *dev = priv->dev;
	phy_interface_t mode;
	long rate;
	int i, ret;

	if (!speed)
		return 0;

	rate = rgmii_clock(speed);
	if (rate < 0)
		return dev_err_probe(dev, -EINVAL,
				     "invalid speed %u, use 10, 100 or 1000\n", speed);

	if (of_get_phy_mode(dev->of_node, &mode))
		mode = PHY_INTERFACE_MODE_RGMII_ID;
	if (mode != PHY_INTERFACE_MODE_RGMII_ID || speed == SPEED_1000)
		rate *= 2;

	for (i = 0; i < SHIKRA_ETHQOS_NUM_CLKS; i++) {
		if (strcmp(priv->clks[i].id, "rgmii"))
			continue;

		ret = clk_set_rate(priv->clks[i].clk, rate);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to set the RGMII clock to %ld Hz\n", rate);
		dev_info(dev, "RGMII clock set to %ld Hz for %u Mbit/s\n", rate, speed);
		return 0;
	}

	return dev_err_probe(dev, -ENODEV, "no rgmii clock\n");
}

static int shikra_ethqos_recovery_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct shikra_ethqos_recovery *priv;
	int i, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	platform_set_drvdata(pdev, priv);

	/* Step 1: deassert the GCC reset (optional) */
	priv->rst = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(priv->rst))
		return dev_err_probe(dev, PTR_ERR(priv->rst),
				     "failed to get GCC reset\n");

	ret = reset_control_deassert(priv->rst);
	if (ret)
		return dev_err_probe(dev, ret, "failed to deassert GCC reset\n");

	/* Step 2: enable the Ethernet MAC clocks */
	for (i = 0; i < SHIKRA_ETHQOS_NUM_CLKS; i++)
		priv->clks[i].id = shikra_ethqos_clk_names[i];

	ret = devm_clk_bulk_get(dev, SHIKRA_ETHQOS_NUM_CLKS, priv->clks);
	if (ret) {
		dev_err_probe(dev, ret, "failed to get clocks\n");
		goto err_assert_reset;
	}

	ret = clk_bulk_prepare_enable(SHIKRA_ETHQOS_NUM_CLKS, priv->clks);
	if (ret) {
		dev_err(dev, "failed to enable clocks: %d\n", ret);
		goto err_assert_reset;
	}

	/* Step 2b: RGMII clock rate for the requested link speed (optional) */
	ret = shikra_ethqos_set_rgmii_rate(priv);
	if (ret)
		goto err_disable_clks;

	/* Step 3: pulse the PHY reset */
	ret = shikra_ethqos_phy_reset(priv);
	if (ret)
		goto err_disable_clks;

	dev_info(dev, "MAC clocks and PHY reset restored\n");
	return 0;

err_disable_clks:
	shikra_ethqos_release_phy_supply(priv);
	clk_bulk_disable_unprepare(SHIKRA_ETHQOS_NUM_CLKS, priv->clks);
err_assert_reset:
	reset_control_assert(priv->rst);
	return ret;
}

static void shikra_ethqos_recovery_remove(struct platform_device *pdev)
{
	struct shikra_ethqos_recovery *priv = platform_get_drvdata(pdev);

	/* Default: leave the hardware running for the vfio-platform handoff */
	if (!cleanup_on_remove)
		return;

	gpiod_set_value_cansleep(priv->phy_reset_gpio, 1);
	clk_bulk_disable_unprepare(SHIKRA_ETHQOS_NUM_CLKS, priv->clks);
	reset_control_assert(priv->rst);
	shikra_ethqos_release_phy_supply(priv);
}

static struct platform_driver shikra_ethqos_recovery_driver = {
	.probe		= shikra_ethqos_recovery_probe,
	.remove		= shikra_ethqos_recovery_remove,
	.driver		= {
		.name		= "shikra-ethqos-recovery",
	},
};
module_platform_driver(shikra_ethqos_recovery_driver);

MODULE_DESCRIPTION("Shikra ETHQOS thin recovery driver for DPDK/vfio handoff");
MODULE_AUTHOR("Qualcomm Technologies, Inc.");
MODULE_LICENSE("GPL");
