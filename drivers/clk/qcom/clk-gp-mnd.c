// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
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
 * @pdm_ahb_clk:	AHB bus clock required for register access
 * @regmap:		register map for the PDM block
 * @hw:			handle between common and hardware-specific interfaces
 * @m_val:		M value (numerator)
 * @n_val:		N value (period)
 */
struct clk_gp_mnd {
	struct clk		*pdm_ahb_clk;
	struct regmap		*regmap;
	struct clk_hw		hw;
	unsigned int		m_val;
	unsigned int		n_val;
};

#define to_clk_gp_mnd(_hw) container_of(_hw, struct clk_gp_mnd, hw)

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

	/* N = 2M + 1 leaves no valid D satisfying M < D < (N - M) */
	if (n == 2 * m + 1)
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

	/*
	 * When N = 2M + 1 the valid D range [M+1, M] is empty; no duty
	 * cycle can satisfy M < D < (N - M).  Reject before touching hw.
	 */
	if (n == 2 * m + 1)
		return -EINVAL;

	ret = clk_prepare_enable(gp->pdm_ahb_clk);
	if (ret)
		return ret;

	ret = regmap_write(gp->regmap, GP_MND_MDIV_REG, m);
	if (ret)
		goto err_unprepare;

	/* N divider holds the 1's complement of (N - M), N_WIDTH bits wide */
	n_val = ~(n - m) & GP_MND_MAX_N;
	ret = regmap_write(gp->regmap, GP_MND_NDIV_REG, n_val);
	if (ret)
		goto err_unprepare;

	/* Program the closest-to-50% duty cycle. */
	d_val = n / 2;
	ret = regmap_write(gp->regmap, GP_MND_DUTY_REG, d_val);
	if (ret)
		goto err_unprepare;

	gp->m_val = m;
	gp->n_val = n;

err_unprepare:
	clk_disable_unprepare(gp->pdm_ahb_clk);

	return ret;
}

static unsigned long gp_mnd_clk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_gp_mnd *gp = to_clk_gp_mnd(hw);
	unsigned int m_val, n_val;
	int ret;

	ret = clk_prepare_enable(gp->pdm_ahb_clk);
	if (ret)
		return 0;

	ret = regmap_read(gp->regmap, GP_MND_MDIV_REG, &m_val);
	if (ret)
		goto out_unprepare;

	m_val &= GP_MND_MAX_M;

	ret = regmap_read(gp->regmap, GP_MND_NDIV_REG, &n_val);
	if (ret)
		goto out_unprepare;

	/* Reverse the 1's complement encoding: N = ~NDIV_REG + M */
	n_val = (~n_val & GP_MND_MAX_N) + m_val;

out_unprepare:
	clk_disable_unprepare(gp->pdm_ahb_clk);

	if (ret)
		return 0;

	if (!n_val)
		return 0;

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

	ret = clk_prepare_enable(gp->pdm_ahb_clk);
	if (ret)
		return ret;

	ret = regmap_read(gp->regmap, GP_MND_DUTY_REG, &d_val);

	clk_disable_unprepare(gp->pdm_ahb_clk);

	if (ret)
		return ret;

	duty->num = d_val;
	duty->den = gp->n_val;

	return 0;
}

static int gp_mnd_clk_set_duty_cycle(struct clk_hw *hw, struct clk_duty *duty)
{
	struct clk_gp_mnd *gp = to_clk_gp_mnd(hw);
	unsigned int d_val;
	int ret;

	if (!gp->n_val || !gp->m_val)
		return -EINVAL;

	/* D = (1 - duty) * N, giving the low-phase count */
	d_val = DIV_ROUND_UP((u64)(duty->den - duty->num) * gp->n_val, duty->den);

	/* Hardware constraint: M < D < (N - M) */
	if (d_val <= gp->m_val || d_val >= (gp->n_val - gp->m_val))
		return -EINVAL;

	ret = clk_prepare_enable(gp->pdm_ahb_clk);
	if (ret)
		return ret;

	ret = regmap_write(gp->regmap, GP_MND_DUTY_REG, d_val);

	clk_disable_unprepare(gp->pdm_ahb_clk);

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
		.ops		= &clk_gp_mnd_ops,
		.parent_data	= &parent_data,
		.num_parents	= 1,
		.flags		= CLK_GET_RATE_NOCACHE,
	};
	struct device *dev = &pdev->dev;
	struct clk_gp_mnd *gp;
	struct clk *clk;
	struct pinctrl *pin;
	struct pinctrl_state *pin_default_state;
	void __iomem *base;
	int ret;

	gp = devm_kzalloc(dev, sizeof(*gp), GFP_KERNEL);
	if (!gp)
		return -ENOMEM;

	gp->pdm_ahb_clk = devm_clk_get(dev, "ahb_clk");
	if (IS_ERR(gp->pdm_ahb_clk))
		return dev_err_probe(dev, PTR_ERR(gp->pdm_ahb_clk),
				     "failed to get ahb_clk\n");

	clk = devm_clk_get(dev, "pdm_clk");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	/* Set default rate if not already configured */
	if (!clk_get_rate(clk)) {
		ret = clk_set_rate(clk, 19200000);
		if (ret)
			dev_warn(dev, "failed to set default pdm_clk rate\n");
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base),
				     "failed to map PDM registers\n");

	gp->regmap = devm_regmap_init_mmio(dev, base, &gp_mnd_regmap_config);
	if (IS_ERR(gp->regmap))
		return dev_err_probe(dev, PTR_ERR(gp->regmap),
				     "failed to init regmap\n");

	ret = of_property_read_string_index(dev->of_node,
					    "clock-output-names", 0,
					    &init.name);
	if (ret)
		return dev_err_probe(dev, ret, "missing clock-output-names\n");

	gp->hw.init = &init;

	pin = devm_pinctrl_get(dev);
	if (IS_ERR(pin))
		return dev_err_probe(dev, PTR_ERR(pin), "missing pinctrl device\n");

	pin_default_state = pinctrl_lookup_state(pin, "active");
	if (IS_ERR(pin_default_state))
		return dev_err_probe(dev, PTR_ERR(pin_default_state),
				     "missing pinctrl default state\n");

	ret = pinctrl_select_state(pin, pin_default_state);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to select pinctrl default state\n");

	ret = devm_clk_hw_register(dev, &gp->hw);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register gp_mnd clock\n");

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, &gp->hw);
}

static const struct of_device_id clk_gp_mnd_match_table[] = {
	{ .compatible = "qcom,clk-gp-mnd" },
	{ }
};
MODULE_DEVICE_TABLE(of, clk_gp_mnd_match_table);

static struct platform_driver clk_gp_mnd_driver = {
	.probe  = clk_gp_mnd_probe,
	.driver = {
		.name		= "qcom-clk-gp-mnd",
		.of_match_table	= clk_gp_mnd_match_table,
	},
};
module_platform_driver(clk_gp_mnd_driver);

MODULE_DESCRIPTION("Qualcomm PDM GP_MND clock divider driver");
MODULE_LICENSE("GPL");
