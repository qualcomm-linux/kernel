// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_clk.h>
#include <linux/pm_clock.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include "clk-regmap.h"

#define LPASS_RATE_GEN_CTRL		0xd000
#define LPASS_RATE_GEN_COUNTER_0	0xd004
#define LPASS_RATE_GEN_DELAY		0xd010

#define LPASS_RATE_GEN_MAX_REG		LPASS_RATE_GEN_DELAY

#define LPASS_RG_CTRL_EN		BIT(0)

struct lpass_va_csr_data {
	u32 counter_0;
	u32 delay;
};

static const struct lpass_va_csr_data hawi_csr_data = {
	.counter_0 = 0x960,
	.delay = 0x16,
};

static const struct regmap_config lpass_rate_gen_regmap_config = {
	.name = "lpass_rate_gen",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = LPASS_RATE_GEN_MAX_REG,
	.cache_type = REGCACHE_MAPLE,
};

struct lpass_va_csr {
	struct device *dev;
	struct clk_regmap hb;
};

#define to_lpass_va_csr(_hw) \
	container_of(to_clk_regmap(_hw), struct lpass_va_csr, hb)

static int lpass_va_csr_hb_prepare(struct clk_hw *hw)
{
	struct lpass_va_csr *csr = to_lpass_va_csr(hw);

	return pm_runtime_resume_and_get(csr->dev);
}

static void lpass_va_csr_hb_unprepare(struct clk_hw *hw)
{
	struct lpass_va_csr *csr = to_lpass_va_csr(hw);

	pm_runtime_put_sync(csr->dev);
}

static const struct clk_ops lpass_va_csr_hb_ops = {
	.prepare = lpass_va_csr_hb_prepare,
	.unprepare = lpass_va_csr_hb_unprepare,
	.enable = clk_enable_regmap,
	.disable = clk_disable_regmap,
};

static int lpass_va_csr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct lpass_va_csr_data *data = of_device_get_match_data(dev);
	struct lpass_va_csr *csr;
	struct clk_init_data init = {
		.name = "lpass_heartbeat_pulse",
		.ops = &lpass_va_csr_hb_ops,
	};
	struct regmap *regmap;
	void __iomem *base;
	int ret;

	csr = devm_kzalloc(dev, sizeof(*csr), GFP_KERNEL);
	if (!csr)
		return -ENOMEM;

	csr->dev = dev;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base,
				       &lpass_rate_gen_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "failed to init regmap\n");

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable PM runtime\n");

	ret = devm_pm_clk_create(dev);
	if (ret)
		return ret;

	ret = of_pm_clk_add_clks(dev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get vote clocks\n");

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	ret = regmap_write(regmap, LPASS_RATE_GEN_COUNTER_0, data->counter_0);
	if (ret) {
		pm_runtime_put_sync(dev);
		return ret;
	}

	ret = regmap_write(regmap, LPASS_RATE_GEN_DELAY, data->delay);
	if (ret) {
		pm_runtime_put_sync(dev);
		return ret;
	}

	csr->hb.regmap = regmap;
	csr->hb.enable_reg = LPASS_RATE_GEN_CTRL;
	csr->hb.enable_mask = LPASS_RG_CTRL_EN;
	csr->hb.hw.init = &init;

	ret = devm_clk_register_regmap(dev, &csr->hb);
	if (ret) {
		pm_runtime_put_sync(dev);
		return ret;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, &csr->hb.hw);
	if (ret) {
		pm_runtime_put_sync(dev);
		return ret;
	}

	pm_runtime_put_sync(dev);

	return 0;
}

static const struct of_device_id lpass_va_csr_dt_match[] = {
	{ .compatible = "qcom,hawi-lpass-va-csr", .data = &hawi_csr_data },
	{}
};
MODULE_DEVICE_TABLE(of, lpass_va_csr_dt_match);

static const struct dev_pm_ops lpass_va_csr_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static struct platform_driver lpass_va_csr_driver = {
	.driver = {
		.name = "qcom-lpass-va-csr",
		.of_match_table = lpass_va_csr_dt_match,
		.pm = pm_ptr(&lpass_va_csr_pm_ops),
	},
	.probe = lpass_va_csr_probe,
};

module_platform_driver(lpass_va_csr_driver);

MODULE_DESCRIPTION("Qualcomm LPASS VA CSR heartbeat pulse clock provider");
MODULE_LICENSE("GPL");
