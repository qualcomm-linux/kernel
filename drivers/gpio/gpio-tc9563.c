// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 by RISCstar Solutions Corporation.  All rights reserved.
 */

/*
 * The Toshiba TC9563 implements a PCIe Gen 3 switch that connects an
 * upstream x4 port to two downstream PCIe x2 ports. It incorporates
 * an internal endpoint on a internal PCIe port that implements two
 * Synopsys XGMAC Ethernet interfaces.
 *
 * 37 GPIOs are also implemented by an embedded GPIO controller.  Three
 * registers control the first 32 GPIOs (other than 20 and 21, which are
 * reserved). Three other registers control GPIOs 32 through 36. GPIOs
 * 22-24, 27-28, 31, and 34 are treated as "input only".
 *
 */

#include <linux/auxiliary_bus.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/soc/qcom/tc9563.h>

/*
 * There are two sets of registers, each representing (up to) 32 GPIOs with a
 * stride of 4 bytes (IN1 is 4 bytes past IN0, EN1 is 4 bytes past EN0, etc.).
 */
#define TC9563_GPIO_COUNT		37
#define TC9563_GPIO_PER_REG		32
#define TC9563_GPIO_REG_STRIDE		4

static int tc9563_gpio_init_valid_mask(struct gpio_chip *gc,
				       unsigned long *valid_mask,
				       unsigned int ngpios)
{
	/* GPIOs 20 and 21 are reserved */
	bitmap_fill(valid_mask, ngpios);
	bitmap_clear(valid_mask, 20, 2);

	return 0;
}

static int tc9563_gpio_probe(struct auxiliary_device *adev,
			     const struct auxiliary_device_id *id)
{
	struct gpio_regmap_config config = {
		.parent = &adev->dev,
		.ngpio = TC9563_GPIO_COUNT,
		.reg_stride = TC9563_GPIO_REG_STRIDE,
		.ngpio_per_reg = TC9563_GPIO_PER_REG,
		.reg_dat_base = GPIO_REGMAP_ADDR(TC9563_GPIO_IN0_OFFSET),
		.reg_set_base = GPIO_REGMAP_ADDR(TC9563_GPIO_OUT0_OFFSET),
		.reg_dir_in_base = GPIO_REGMAP_ADDR(TC9563_GPIO_EN0_OFFSET),
		.init_valid_mask = tc9563_gpio_init_valid_mask,
	};

	config.regmap = dev_get_platdata(&adev->dev);
	if (!config.regmap)
		return -EINVAL;

	/*
	 * GPIOs 22, 23, 24, 27, 28, 31, and 34 are input-only, but this
	 * kernel's gpio-regmap core only supports a single, chip-wide
	 * fixed_direction_output bitmap, with no way to leave the
	 * remaining GPIOs under reg_dir_in_base control. Rely on
	 * reg_dir_in_base for direction on all valid GPIOs instead;
	 * the input-only lines simply never get switched to output.
	 */

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(&adev->dev, &config));
};

static const struct auxiliary_device_id tc9563_gpio_ids[] = {
	{ "pci_pwrctrl_tc9563." TC9563_GPIO_DEV_NAME },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(auxiliary, tc9563_gpio_ids);

static struct auxiliary_driver tc9563_gpio_driver = {
	.name		= TC9563_GPIO_DEV_NAME,
	.probe		= tc9563_gpio_probe,
	.id_table	= tc9563_gpio_ids,
};
module_auxiliary_driver(tc9563_gpio_driver);

MODULE_AUTHOR("Alex Elder <elder@riscstar.com>");
MODULE_AUTHOR("Daniel Thompson <daniel@riscstar.com>");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@oss.qualcomm.com>");
MODULE_DESCRIPTION("Toshiba TC9563 GPIO Driver");
MODULE_LICENSE("GPL");
