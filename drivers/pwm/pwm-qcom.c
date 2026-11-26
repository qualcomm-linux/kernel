// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

/*
 * mul_u64_add_u64_div_u64() computes (a * b + c) / d with a 128-bit
 * intermediate upstream. It is not available on this kernel base; all
 * operands used here fit in 64 bits, so a div64_u64() of the sum is exact.
 */
static inline u64 mul_u64_add_u64_div_u64(u64 a, u64 b, u64 c, u64 d)
{
	return div64_u64(a * b + c, d);
}

/* Offsets */
#define PWM_TOPCTL0			0x0

/* offsets per frame */
#define PWM_CTL0			0x0
#define PWM_CTL1			0x4
#define PWM_CTL2			0x8
#define PWM_CYC_CFG			0xc
#define PWM_UPDATE			0x10
#define PWM_RESET			0x18

#define PWM_FRAME_POLARITY_BIT		BIT(0)
#define PWM_FRAME_ROLLOVER_CNT_BIT	BIT(4)
#define PWM_FRAME_RESET_BIT		BIT(0)

/* All per-frame 16-bit value fields (CYC_CFG, CTL1, CTL2). */
#define PWM_FRAME_VAL_MASK		GENMASK(15, 0)

/*
 * struct qcom_pwm_priv_data - per-compatible hardware description
 * @max_channels: number of frames supported by this hardware variant.
 * @pwm_reset_support: whether PWM_RESET is implemented, allowing the period
 *	to be changed after it has already been set once.
 * @pwm_cnt_rollover: whether PWM_FRAME_ROLLOVER_CNT_BIT is implemented,
 *	needed so period changes don't get stuck waiting on the counter.
 */
struct qcom_pwm_priv_data {
	unsigned int max_channels;
	bool pwm_reset_support;
	bool pwm_cnt_rollover;
};

/*
 * struct qcom_pwm_frames - Information regarding per PWM frame
 * @frame_id: Id number associated with each frame.
 * @polarity: Current polarity of the particular frame.
 * @regmap: regmap covering this frame's own register block. Each frame now
 *	owns its own "reg" range in DT, so registers are accessed at their
 *	plain offsets within this regmap instead of as an offset into a
 *	shared parent regmap.
 * @current_period_ns: Current period of the particular frame.
 * @current_duty_ns: Current duty cycle of the particular frame.
 * @current_freq: Current frequency of frame.
 * @freq_set: This bool flag is responsible for setting period once per frame.
 * @frame_lock: mutex lock per frame.
 * @cnt_rollover_en: This bool flag is used to set rollover bit per frame.
 */
struct qcom_pwm_frames {
	u32	frame_id;
	u32	polarity;
	struct regmap *regmap;
	u64	current_period_ns;
	u64	current_duty_ns;
	unsigned long current_freq;
	bool	is_enabled;
	bool	freq_set;
	struct mutex frame_lock; /* PWM per frame lock */
	bool cnt_rollover_en;
};

/*
 * struct qcom_pwm_chip - Information regarding per PWM block
 * @regmap: regmap of the shared PWM_TOPCTL0 enable register.
 * @frames: structure for all frames of each PWM block.
 * @priv_data: per-compatible hardware description.
 * @ahb: AHB clock providing access to the PWM block.
 * @core: shared clock providing the PWM frame rate.
 * @lock: mutex lock per chip (enable/disable path).
 * @num_frames: number of frames in each PWM block.
 */
struct qcom_pwm_chip {
	struct regmap		*regmap;
	struct qcom_pwm_frames	*frames;
	const struct qcom_pwm_priv_data	*priv_data;
	struct clk		*ahb;
	struct clk		*core;
	/* This lock to be used for Enable/Disable as it is per PWM channel */
	struct mutex		lock;
	u32			num_frames;
};

static inline struct qcom_pwm_chip *to_qcom_pwm_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int qcom_pwm_clks_enable(struct qcom_pwm_chip *chip)
{
	int ret;

	ret = clk_prepare_enable(chip->ahb);
	if (ret)
		return ret;

	ret = clk_prepare_enable(chip->core);
	if (ret)
		clk_disable_unprepare(chip->ahb);

	return ret;
}

static void qcom_pwm_clks_disable(struct qcom_pwm_chip *chip)
{
	clk_disable_unprepare(chip->core);
	clk_disable_unprepare(chip->ahb);
}

static int qcom_pwm_calc_frequency(struct qcom_pwm_chip *chip,
				   u64 period_ns, unsigned long rate, u32 hw_idx)
{
	struct regmap *frame_regmap = chip->frames[hw_idx].regmap;
	u64 cycles;
	int ret;

	/*
	 * PWM client can set the period only once if the HW version does
	 * not support reset functionality.
	 */
	if (chip->frames[hw_idx].freq_set && !chip->priv_data->pwm_reset_support)
		return 0;

	/*
	 * Number of core-clock cycles in one PWM period. Round down so the
	 * implemented period never exceeds the requested one, as the PWM API
	 * requires. PWM_CYC_CFG holds (cycles - 1) in its low 16 bits, so
	 * cycles is bounded to [2, 65536], i.e. an output frequency from
	 * rate / 2 down to rate / 65536.
	 */
	cycles = mul_u64_u64_div_u64(period_ns, rate, NSEC_PER_SEC);
	if (cycles < 2 || cycles > 65536) {
		dev_dbg(regmap_get_device(frame_regmap),
			"period %llu ns out of range for rate %lu Hz\n",
			period_ns, rate);
		return -ERANGE;
	}

	ret = regmap_update_bits(frame_regmap, PWM_CYC_CFG,
				 PWM_FRAME_VAL_MASK, cycles - 1);
	if (ret)
		return ret;

	chip->frames[hw_idx].current_freq = div64_ul(rate, cycles);
	chip->frames[hw_idx].freq_set = true;
	chip->frames[hw_idx].current_period_ns = period_ns;

	return 0;
}

static int qcom_pwm_get_state(struct pwm_chip *pwm_chip, struct pwm_device *pwm,
			      struct pwm_state *state)
{
	struct qcom_pwm_chip *chip = to_qcom_pwm_chip(pwm_chip);
	struct qcom_pwm_frames *frame = &chip->frames[pwm->hwpwm];
	unsigned long rate;
	u32 topctl, ctl0, ctl1, cyc_cfg, cycles;
	int ret;

	if (!frame->regmap)
		return -ENODEV;

	/* Both clocks are needed for register access. */
	ret = qcom_pwm_clks_enable(chip);
	if (ret)
		return ret;

	rate = clk_get_rate(chip->core);
	if (!rate) {
		ret = -EINVAL;
		goto err_clks;
	}

	mutex_lock(&frame->frame_lock);

	ret = regmap_read(chip->regmap, PWM_TOPCTL0, &topctl);
	if (ret)
		goto out;

	ret = regmap_read(frame->regmap, PWM_CTL0, &ctl0);
	if (ret)
		goto out;

	ret = regmap_read(frame->regmap, PWM_CTL1, &ctl1);
	if (ret)
		goto out;

	ret = regmap_read(frame->regmap, PWM_CYC_CFG, &cyc_cfg);
	if (ret)
		goto out;

	cycles = (cyc_cfg & PWM_FRAME_VAL_MASK) + 1;
	state->enabled = !!(topctl & BIT(frame->frame_id));
	state->polarity = (ctl0 & PWM_FRAME_POLARITY_BIT) ?
			  PWM_POLARITY_INVERSED : PWM_POLARITY_NORMAL;
	/*
	 * Round up so re-applying the returned state reproduces the same
	 * register values, since qcom_pwm_config() rounds the period and duty
	 * cycle down.
	 */
	state->period = DIV_ROUND_UP_ULL((u64)cycles * NSEC_PER_SEC, rate);
	state->duty_cycle = DIV_ROUND_UP_ULL((u64)(ctl1 & PWM_FRAME_VAL_MASK) *
					     NSEC_PER_SEC, rate);
out:
	mutex_unlock(&frame->frame_lock);
err_clks:
	qcom_pwm_clks_disable(chip);

	return ret;
}

static int qcom_pwm_config(struct qcom_pwm_chip *chip,
			   u32 hw_idx, u64 duty_ns, u64 period_ns, int polarity)
{
	struct regmap *frame_regmap = chip->frames[hw_idx].regmap;
	u64 ctl1, duty_percent;
	unsigned long rate;
	u64 current_period = period_ns;
	bool clks_enabled = false;
	int ret;
	u32 cyc_cfg;

	mutex_lock(&chip->frames[hw_idx].frame_lock);

	if (!chip->frames[hw_idx].is_enabled) {
		ret = qcom_pwm_clks_enable(chip);
		if (ret)
			goto err_unlock;
		clks_enabled = true;
	}

	rate = clk_get_rate(chip->core);
	if (!rate) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Set the counter rollover enable bit, so that counter doesn't get stuck
	 * in period change configuration.
	 */
	if (chip->priv_data->pwm_cnt_rollover && !chip->frames[hw_idx].cnt_rollover_en) {
		ret = regmap_update_bits(frame_regmap, PWM_CTL0,
					 PWM_FRAME_ROLLOVER_CNT_BIT,
					 PWM_FRAME_ROLLOVER_CNT_BIT);
		if (ret)
			goto out;

		chip->frames[hw_idx].cnt_rollover_en = true;
	}

	if (chip->frames[hw_idx].freq_set &&
	    chip->frames[hw_idx].current_period_ns != period_ns) {
		if (chip->priv_data->pwm_reset_support) {
			ret = regmap_update_bits(frame_regmap, PWM_RESET,
						 PWM_FRAME_RESET_BIT,
						 PWM_FRAME_RESET_BIT);
			if (ret)
				goto out;
		} else {
			ret = -EINVAL;
			goto out;
		}
	}

	ret = qcom_pwm_calc_frequency(chip, current_period, rate, hw_idx);
	if (ret)
		goto out;

	if (chip->frames[hw_idx].polarity != polarity) {
		ret = regmap_update_bits(frame_regmap, PWM_CTL0,
					 PWM_FRAME_POLARITY_BIT, polarity);
		if (ret)
			goto out;

		chip->frames[hw_idx].polarity = polarity;
	}

	ctl1 = DIV_ROUND_CLOSEST(rate, chip->frames[hw_idx].current_freq);
	duty_percent = mul_u64_add_u64_div_u64(duty_ns, 100, current_period / 2, current_period);
	ctl1 = mul_u64_add_u64_div_u64(ctl1, duty_percent, 50, 100);

	ret = regmap_read(frame_regmap, PWM_CYC_CFG, &cyc_cfg);
	if (ret)
		goto out;

	if ((ctl1 > cyc_cfg || ctl1 == 0) && duty_ns != 0) {
		dev_err(regmap_get_device(frame_regmap),
			"duty cycle out of valid range\n");
		ret = -ERANGE;
		goto out;
	}

	ret = regmap_update_bits(frame_regmap, PWM_CTL2, PWM_FRAME_VAL_MASK, 0);
	if (ret)
		goto out;

	ret = regmap_update_bits(frame_regmap, PWM_CTL1, PWM_FRAME_VAL_MASK, ctl1);
	if (ret)
		goto out;

	ret = regmap_update_bits(frame_regmap, PWM_UPDATE, BIT(0), 1);
	if (ret)
		goto out;

	chip->frames[hw_idx].current_duty_ns = duty_ns;
out:
	if (clks_enabled)
		qcom_pwm_clks_disable(chip);
err_unlock:
	mutex_unlock(&chip->frames[hw_idx].frame_lock);

	return ret;
}

static int qcom_pwm_enable(struct qcom_pwm_chip *chip,
			   struct pwm_device *pwm)
{
	struct device *dev = regmap_get_device(chip->regmap);
	int ret;
	u32 val, hw_idx = pwm->hwpwm;

	ret = qcom_pwm_clks_enable(chip);
	if (ret)
		return ret;

	mutex_lock(&chip->lock);

	dev_dbg(dev, "enable: hw_idx=%u frame_id=%u\n",
		hw_idx, chip->frames[hw_idx].frame_id);

	val = BIT(chip->frames[hw_idx].frame_id);
	ret = regmap_update_bits(chip->regmap, PWM_TOPCTL0, val, val);
	if (!ret)
		chip->frames[hw_idx].is_enabled = true;

	mutex_unlock(&chip->lock);

	if (ret)
		goto err_clks;

	return 0;

err_clks:
	qcom_pwm_clks_disable(chip);
	return ret;
}

static int qcom_pwm_disable(struct qcom_pwm_chip *chip,
			    struct pwm_device *pwm)
{
	struct device *dev = regmap_get_device(chip->regmap);
	u32 val, hw_idx = pwm->hwpwm;
	int ret;

	mutex_lock(&chip->lock);

	dev_dbg(dev, "disable: hw_idx=%u\n", hw_idx);

	val = BIT(chip->frames[hw_idx].frame_id);
	ret = regmap_update_bits(chip->regmap, PWM_TOPCTL0, val, 0);
	if (!ret)
		chip->frames[hw_idx].is_enabled = false;

	mutex_unlock(&chip->lock);

	if (ret)
		return ret;

	qcom_pwm_clks_disable(chip);

	return 0;
}

static int qcom_pwm_apply(struct pwm_chip *pwm_chip, struct pwm_device *pwm,
			  const struct pwm_state *state)
{
	struct qcom_pwm_chip *chip = to_qcom_pwm_chip(pwm_chip);
	struct pwm_state curr_state;
	u32 hw_idx = pwm->hwpwm;
	int ret;

	pwm_get_state(pwm, &curr_state);

	if (state->period &&
	    (state->period != curr_state.period ||
	     state->duty_cycle != curr_state.duty_cycle ||
	     state->polarity != curr_state.polarity)) {
		ret = qcom_pwm_config(chip, hw_idx, state->duty_cycle,
				      state->period, state->polarity);
		if (ret) {
			dev_err(pwmchip_parent(pwm_chip),
				"failed to update PWM configuration\n");
			return ret;
		}
	}

	if (state->enabled != curr_state.enabled) {
		if (state->enabled)
			return qcom_pwm_enable(chip, pwm);

		ret = qcom_pwm_disable(chip, pwm);
		if (ret)
			return ret;
	}

	return 0;
}

static int qcom_pwm_request(struct pwm_chip *pwm_chip, struct pwm_device *pwm)
{
	struct qcom_pwm_chip *chip = to_qcom_pwm_chip(pwm_chip);

	if (!chip->frames[pwm->hwpwm].regmap)
		return -ENODEV;

	return 0;
}

/*
 * Reset the software state and the frame's own registers (the same
 * PWM_CTL0/PWM_CTL1/PWM_CYC_CFG registers qcom_pwm_get_state() reads back)
 * to their power-on defaults, and disable an active PWM frame.
 */
static void qcom_pwm_free(struct pwm_chip *pwm_chip, struct pwm_device *pwm)
{
	struct qcom_pwm_chip *chip = to_qcom_pwm_chip(pwm_chip);
	struct qcom_pwm_frames *frame = &chip->frames[pwm->hwpwm];
	bool enabled;
	int ret;

	mutex_lock(&chip->lock);
	enabled = frame->is_enabled;
	mutex_unlock(&chip->lock);

	if (enabled) {
		ret = qcom_pwm_disable(chip, pwm);
		if (ret) {
			dev_err(pwmchip_parent(pwm_chip),
				"failed to disable PWM on free\n");
			return;
		}
	}

	ret = qcom_pwm_clks_enable(chip);
	if (ret) {
		dev_err(pwmchip_parent(pwm_chip),
			"failed to enable clocks on free\n");
		return;
	}

	mutex_lock(&frame->frame_lock);

	ret = regmap_update_bits(frame->regmap, PWM_CTL0,
				 PWM_FRAME_POLARITY_BIT | PWM_FRAME_ROLLOVER_CNT_BIT, 0);
	if (ret)
		goto out;

	ret = regmap_update_bits(frame->regmap, PWM_CTL1, PWM_FRAME_VAL_MASK, 0);
	if (ret)
		goto out;

	ret = regmap_update_bits(frame->regmap, PWM_CTL2, PWM_FRAME_VAL_MASK, 0);
	if (ret)
		goto out;

	ret = regmap_update_bits(frame->regmap, PWM_CYC_CFG, PWM_FRAME_VAL_MASK, 0);
	if (ret)
		goto out;

	ret = regmap_update_bits(frame->regmap, PWM_UPDATE, BIT(0), 1);
	if (ret)
		goto out;

	if (chip->priv_data->pwm_reset_support) {
		ret = regmap_update_bits(frame->regmap, PWM_RESET,
					 PWM_FRAME_RESET_BIT, PWM_FRAME_RESET_BIT);
		if (ret)
			goto out;
	}

	frame->freq_set = false;
	frame->current_period_ns = 0;
	frame->current_duty_ns = 0;
	frame->current_freq = 0;
	frame->polarity = 0;
	frame->cnt_rollover_en = false;
out:
	mutex_unlock(&frame->frame_lock);
	qcom_pwm_clks_disable(chip);

	if (ret)
		dev_err(pwmchip_parent(pwm_chip),
			"failed to reset PWM registers on free\n");
}

static const struct pwm_ops qcom_pwm_ops = {
	.request = qcom_pwm_request,
	.apply = qcom_pwm_apply,
	.free = qcom_pwm_free,
	.get_state = qcom_pwm_get_state,
};

static const struct regmap_config pwm_regmap_config = {
	.reg_bits   = 32,
	.reg_stride = 4,
	.val_bits   = 32,
	.fast_io    = true,
};

static int qcom_pwm_parse_dt(struct platform_device *pdev,
			     struct pwm_chip *pwm_chip,
			     struct qcom_pwm_chip *chip)
{
	struct device *dev = pwmchip_parent(pwm_chip);
	struct device_node *np = pdev->dev.of_node;
	struct device_node *frame_node;
	void __iomem *base;
	unsigned long frame_ids = 0;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	chip->regmap = devm_regmap_init_mmio(dev, base, &pwm_regmap_config);
	if (IS_ERR(chip->regmap))
		return dev_err_probe(dev, PTR_ERR(chip->regmap),
				"failed to initialise regmap\n");

	chip->ahb = devm_clk_get(dev, "ahb");
	if (IS_ERR(chip->ahb))
		return dev_err_probe(dev, PTR_ERR(chip->ahb),
				"failed to get ahb clock\n");

	chip->core = devm_clk_get(dev, "core");
	if (IS_ERR(chip->core))
		return dev_err_probe(dev, PTR_ERR(chip->core),
				"failed to get core clock\n");

	chip->frames = devm_kcalloc(dev, chip->num_frames,
				    sizeof(*chip->frames), GFP_KERNEL);
	if (!chip->frames)
		return -ENOMEM;

	for_each_available_child_of_node(np, frame_node) {
		u32 n;
		struct resource res;

		if (of_property_read_u32(frame_node, "qcom,frame-index", &n)) {
			of_node_put(frame_node);
			return dev_err_probe(dev, -EINVAL,
					"missing qcom,frame-index property in DT node\n");
		}

		if (n >= chip->priv_data->max_channels) {
			of_node_put(frame_node);
			return dev_err_probe(dev, -EINVAL,
					"invalid qcom,frame-index %u\n", n);
		}

		if (test_and_set_bit(n, &frame_ids)) {
			of_node_put(frame_node);
			return dev_err_probe(dev, -EINVAL,
					"duplicate qcom,frame-index %u\n", n);
		}

		chip->frames[n].frame_id = n;

		ret = of_address_to_resource(frame_node, 0, &res);
		if (ret) {
			of_node_put(frame_node);
			return dev_err_probe(dev, ret,
					"failed to get frame-%u resource\n", n);
		}

		base = devm_ioremap_resource(dev, &res);
		if (IS_ERR(base)) {
			of_node_put(frame_node);
			return PTR_ERR(base);
		}

		chip->frames[n].regmap =
			devm_regmap_init_mmio(dev, base, &pwm_regmap_config);
		if (IS_ERR(chip->frames[n].regmap)) {
			of_node_put(frame_node);
			return dev_err_probe(dev, PTR_ERR(chip->frames[n].regmap),
						 "failed to initialise frame regmap\n");
		}

		mutex_init(&chip->frames[n].frame_lock);
	}

	/* The hardware requires all PWM outputs to be disabled at probe. */
	ret = qcom_pwm_clks_enable(chip);
	if (ret)
		return ret;

	ret = regmap_update_bits(chip->regmap, PWM_TOPCTL0,
				 GENMASK(chip->priv_data->max_channels - 1, 0), 0);

	qcom_pwm_clks_disable(chip);

	return ret;
}

static const struct qcom_pwm_priv_data shikra_pwm_data = {
	.max_channels = 10,
};

static const struct of_device_id qcom_pwm_of_match[] = {
	{ .compatible = "qcom,shikra-pwm", .data = &shikra_pwm_data },
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_pwm_of_match);

static int qcom_pwm_probe(struct platform_device *pdev)
{
	struct pwm_chip *pwm_chip;
	struct qcom_pwm_chip *chip;
	const struct qcom_pwm_priv_data *priv;
	struct device_node *np = pdev->dev.of_node;
	u32 child_count;
	int rc;

	priv = of_device_get_match_data(&pdev->dev);
	if (!priv)
		return -EINVAL;

	/* At least one available frame resource must be described. */
	child_count = of_get_available_child_count(np);
	if (!child_count)
		return dev_err_probe(&pdev->dev, -EINVAL,
				"at least one PWM frame must be described\n");

	pwm_chip = devm_pwmchip_alloc(&pdev->dev, priv->max_channels, sizeof(*chip));
	if (IS_ERR(pwm_chip))
		return PTR_ERR(pwm_chip);

	chip = to_qcom_pwm_chip(pwm_chip);
	chip->priv_data  = priv;
	chip->num_frames = priv->max_channels;
	mutex_init(&chip->lock);

	pwm_chip->ops = &qcom_pwm_ops;

	rc = qcom_pwm_parse_dt(pdev, pwm_chip, chip);
	if (rc)
		return rc;

	rc = devm_pwmchip_add(&pdev->dev, pwm_chip);
	if (rc)
		return rc;

	return 0;
}

static struct platform_driver qcom_pwm_driver = {
	.driver		= {
		.name		= "qcom-pwm",
		.of_match_table	= qcom_pwm_of_match,
	},
	.probe		= qcom_pwm_probe,
};

module_platform_driver(qcom_pwm_driver);

MODULE_DESCRIPTION("Qualcomm PWM driver");
MODULE_LICENSE("GPL");
