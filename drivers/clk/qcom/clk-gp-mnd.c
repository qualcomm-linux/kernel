// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/rational.h>
#include <linux/regmap.h>

/*
 * PDM GP_MND clock divider register offsets.
 *
 * The hardware computes:
 *   Fout = Fin * (M / N)
 *
 * with duty cycle controlled by D, where M < D < (N - M).
 *
 * Register encoding:
 *   MDIV  = M
 *   NDIV  = ~(N - M)  [1's complement of (N - M), masked to N_REG_WIDTH bits]
 *   DUTY  = D
 */
#define GP_MND_MDIV_REG		0x0
#define GP_MND_NDIV_REG		0x4
#define GP_MND_DUTY_REG		0x8

#define GP_MND_M_WIDTH		9
#define GP_MND_N_WIDTH		13

#define GP_MND_MAX_M		GENMASK(GP_MND_M_WIDTH - 1, 0)
#define GP_MND_MAX_N		GENMASK(GP_MND_N_WIDTH - 1, 0)

/**
 * struct clk_gp_mnd - GP_MND fractional clock divider
 * @dev:		device for runtime PM
 * @regmap:		register map for the PDM block
 * @hw:			handle between common and hardware-specific interfaces
 * @m_val:		M value (numerator)
 * @n_val:		N value (period)
 */
struct clk_gp_mnd {
	struct device		*dev;
	struct regmap		*regmap;
	struct clk_hw		hw;
	unsigned int		m_val;
	unsigned int		n_val;
};

#define to_clk_gp_mnd(_hw) container_of(_hw, struct clk_gp_mnd, hw)

static int gp_mnd_clk_runtime_resume(struct clk_gp_mnd *gp)
{
	return pm_runtime_resume_and_get(gp->dev);
}

static void gp_mnd_clk_runtime_put(struct clk_gp_mnd *gp)
{
	pm_runtime_put(gp->dev);
}

static int gp_mnd_clk_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	unsigned long m = 0, n = 0;

	rational_best_approximation(req->rate, req->best_parent_rate,
				    (unsigned long)GP_MND_MAX_M,
				    (unsigned long)GP_MND_MAX_N,
				    &m, &n);
	if (!m || !n)
		return -EINVAL;

	if (n <= 2 * m + 1)
		return -EINVAL;

	req->rate = DIV_ROUND_CLOSEST_ULL((u64)req->best_parent_rate * m, n);

	return 0;
}

static int gp_mnd_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct clk_gp_mnd *gp = to_clk_gp_mnd(hw);
	unsigned long m = 0, n = 0;
	unsigned int d_val, n_val;
	int ret;

	rational_best_approximation(rate, parent_rate,
				    (unsigned long)GP_MND_MAX_M,
				    (unsigned long)GP_MND_MAX_N,
				    &m, &n);
	if (!m || !n)
		return -EINVAL;

	if (n <= 2 * m + 1)
		return -EINVAL;

	ret = gp_mnd_clk_runtime_resume(gp);
	if (ret)
		return ret;

	ret = regmap_write(gp->regmap, GP_MND_MDIV_REG, m);
	if (ret)
		goto err_runtime_put;

	/* N divider holds the 1's complement of (N - M), N_WIDTH bits wide */
	n_val = ~(n - m) & GP_MND_MAX_N;
	ret = regmap_write(gp->regmap, GP_MND_NDIV_REG, n_val);
	if (ret)
		goto err_runtime_put;

	/* Program the closest-to-50% duty cycle. */
	d_val = n / 2;
	ret = regmap_write(gp->regmap, GP_MND_DUTY_REG, d_val);
	if (ret)
		goto err_runtime_put;

	gp->m_val = m;
	gp->n_val = n;

err_runtime_put:
	gp_mnd_clk_runtime_put(gp);

	return ret;
}

static unsigned long gp_mnd_clk_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct clk_gp_mnd *gp = to_clk_gp_mnd(hw);
	unsigned int m_val, n_val;
	int ret;

	ret = gp_mnd_clk_runtime_resume(gp);
	if (ret)
		return 0;

	ret = regmap_read(gp->regmap, GP_MND_MDIV_REG, &m_val);
	if (ret)
		goto out_runtime_put;

	m_val &= GP_MND_MAX_M;

	ret = regmap_read(gp->regmap, GP_MND_NDIV_REG, &n_val);
	if (ret)
		goto out_runtime_put;

	/* Reverse the 1's complement encoding: N = ~NDIV_REG + M */
	n_val = (~n_val & GP_MND_MAX_N) + m_val;

out_runtime_put:
	gp_mnd_clk_runtime_put(gp);

	if (ret)
		return 0;

	/*
	 * At power-on the divider registers read back as zero (M = 0), which
	 * the 1's complement encoding would turn into a bogus N = 8191. Treat
	 * an unconfigured divider as rate 0 and leave the cached M/N cleared so
	 * the 50% duty-cycle default path in get_duty_cycle() stays valid.
	 */
	if (!m_val || !n_val) {
		gp->m_val = 0;
		gp->n_val = 0;
		return 0;
	}

	gp->m_val = m_val;
	gp->n_val = n_val;

	return DIV_ROUND_CLOSEST_ULL((u64)parent_rate * m_val, n_val);
}

static int gp_mnd_clk_get_duty_cycle(struct clk_hw *hw, struct clk_duty *duty)
{
	struct clk_gp_mnd *gp = to_clk_gp_mnd(hw);
	unsigned int d_val;
	int ret;

	if (!gp->n_val) {
		duty->num = 1;
		duty->den = 2;
		return 0;
	}

	ret = gp_mnd_clk_runtime_resume(gp);
	if (ret)
		return ret;

	ret = regmap_read(gp->regmap, GP_MND_DUTY_REG, &d_val);

	gp_mnd_clk_runtime_put(gp);

	if (ret)
		return ret;

	if (d_val >= gp->n_val)
		return -EINVAL;

	/* DUTY stores the low-phase count; report the high-phase duty cycle. */
	duty->num = gp->n_val - d_val;
	duty->den = gp->n_val;

	return 0;
}

static int gp_mnd_clk_set_duty_cycle(struct clk_hw *hw, struct clk_duty *duty)
{
	struct clk_gp_mnd *gp = to_clk_gp_mnd(hw);
	unsigned int d_val;
	int ret;

	if (!gp->n_val || !gp->m_val || !duty->den || duty->num > duty->den)
		return -EINVAL;

	/* D = (1 - duty) * N, giving the low-phase count */
	d_val = DIV_ROUND_UP_ULL((u64)(duty->den - duty->num) * gp->n_val, duty->den);

	/* Hardware constraint: M < D < (N - M) */
	if (d_val <= gp->m_val || d_val >= (gp->n_val - gp->m_val))
		return -EINVAL;

	ret = gp_mnd_clk_runtime_resume(gp);
	if (ret)
		return ret;

	ret = regmap_write(gp->regmap, GP_MND_DUTY_REG, d_val);

	gp_mnd_clk_runtime_put(gp);

	return ret;
}

static const struct clk_ops clk_gp_mnd_ops = {
	.determine_rate	= gp_mnd_clk_determine_rate,
	.set_rate	= gp_mnd_clk_set_rate,
	.recalc_rate	= gp_mnd_clk_recalc_rate,
	.get_duty_cycle	= gp_mnd_clk_get_duty_cycle,
	.set_duty_cycle	= gp_mnd_clk_set_duty_cycle,
};

static const struct regmap_config gp_mnd_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.fast_io	= true,
};

static int clk_gp_mnd_probe(struct platform_device *pdev)
{
	struct clk_parent_data parent_data = { .index = 0 };
	struct clk_init_data init = {
		.name		= "gp_mn_clk",
		.ops		= &clk_gp_mnd_ops,
		.parent_data	= &parent_data,
		.num_parents	= 1,
		.flags		= CLK_GET_RATE_NOCACHE,
	};
	struct device *dev = &pdev->dev;
	struct clk_gp_mnd *gp;
	struct clk *clk;
	void __iomem *base;
	int ret;

	gp = devm_kzalloc(dev, sizeof(*gp), GFP_KERNEL);
	if (!gp)
		return -ENOMEM;

	gp->dev = dev;

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = devm_pm_clk_create(dev);
	if (ret)
		return ret;

	ret = pm_clk_add(dev, "ahb");
	if (ret)
		return dev_err_probe(dev, ret, "failed to get ahb clock\n");

	clk = devm_clk_get(dev, "pdm");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	/* Set default rate if not already configured */
	if (!clk_get_rate(clk)) {
		ret = clk_set_rate(clk, 19200000);
		if (ret)
			dev_warn(dev, "failed to set default pdm clock rate\n");
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base),
				     "failed to map PDM registers\n");

	gp->regmap = devm_regmap_init_mmio(dev, base, &gp_mnd_regmap_config);
	if (IS_ERR(gp->regmap))
		return dev_err_probe(dev, PTR_ERR(gp->regmap),
				     "failed to init regmap\n");

	gp->hw.init = &init;

	ret = devm_clk_hw_register(dev, &gp->hw);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register gp_mnd clock\n");

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, &gp->hw);
}

static const struct of_device_id clk_gp_mnd_match_table[] = {
	{ .compatible = "qcom,sc7280-gp-mnd" },
	{ }
};
MODULE_DEVICE_TABLE(of, clk_gp_mnd_match_table);

static const struct dev_pm_ops clk_gp_mnd_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static struct platform_driver clk_gp_mnd_driver = {
	.probe  = clk_gp_mnd_probe,
	.driver = {
		.name		= "qcom-clk-gp-mnd",
		.of_match_table	= clk_gp_mnd_match_table,
		.pm		= pm_ptr(&clk_gp_mnd_pm_ops),
	},
};
module_platform_driver(clk_gp_mnd_driver);

MODULE_DESCRIPTION("Qualcomm PDM GP_MND clock divider driver");
MODULE_LICENSE("GPL");
