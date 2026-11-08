// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/platform_device.h>

#define ROLLOVER_VAL				999999999
#define NSEC_SHIFT				32
#define PULSE_WIDTH_MASK			GENMASK(7, 6)
#define DRIFT_MAX_RES				0x1FFFF
#define DRIFT_JUMP_SWALLOW			BIT(31)
#define DRIFT_PERIOD_MASK			GENMASK(28, 12)
#define DRIFT_SUBPERIOD_MASK			GENMASK(11, 0)
#define DRIFT_INTERVAL_US			10
#define DRIFT_MIN_TIMEOUT_US			5000

#define DRIFT_TIMEOUT_US(period, subperiod, cntr_res)			\
	max_t(u64, div_u64((u64)(period) * (subperiod) * (cntr_res), NSEC_PER_USEC), \
	      DRIFT_MIN_TIMEOUT_US)

/* Dynamic (continuous) drift correction, used on Nord/Shikra */
#define DRIFT_STATUS				BIT(1)
#define DRIFT_DYNAMIC_EN			BIT(2)
#define DYNAMIC_DRIFT_SUBPERIOD_MASK		GENMASK(27, 0)
#define DYNAMIC_DRIFT_MIN_SUBPERIOD		6
#define DYNAMIC_DRIFT_INCR_VAL			8
#define DYNAMIC_DRIFT_POLL_US			10
#define DYNAMIC_DRIFT_DISABLE_TIMEOUT_US	1000

struct qcom_tsc_regs {
	u32 control_cntcr;
	u32 control_cntcv_lo;
	u32 control_cntcv_hi;
	u32 drift_correct_incr_val;
	u32 drift_correct_duration;
	u32 drift_correct_cmd;
	u32 drift_correct_duration_2;
	u32 drift_correct_cmd_2;
	u32 rollover_val;
	u32 offset_lo;
	u32 offset_hi;
	u32 read_cntcv_lo;
	u32 read_cntcv_hi;
};

/**
 * struct qcom_tsc_soc_data - SoC specific TSC hardware capabilities
 *
 * @rollover: indicates the counter uses sec/nsec split
 * @offset_correction: hardware supports one-shot offset correction
 * @dynamic_drift_correction: hardware supports continuous drift correction,
 *                            instead of a one-shot jump/swallow correction
 * @pulse_width: valid pulse width in cycles for drift correction
 * @cntr_res: TSC counter resolution in nsec
 * @reg_layout: register layout for this SoC/version
 */
struct qcom_tsc_soc_data {
	bool rollover;
	bool offset_correction;
	bool dynamic_drift_correction;
	int pulse_width;
	int cntr_res;
	const struct qcom_tsc_regs *reg_layout;
};

struct qcom_tsc {
	struct device *dev;
	void __iomem *base;
	const struct qcom_tsc_soc_data *soc;
	struct clk *ahb_clk;
	struct clk *cntr_clk;
	bool clocks_enabled;
	struct ptp_clock *ptp;
	struct ptp_clock_info ptp_info;
	/* Mutex to protect operations from being interrupted */
	struct mutex lock;
};

static bool qcom_tsc_is_enabled(struct qcom_tsc *tsc)
{
	const struct qcom_tsc_regs *regs = tsc->soc->reg_layout;

	return readl_relaxed(tsc->base + regs->control_cntcr) & BIT(0);
}

static int qcom_tsc_read_time(struct qcom_tsc *tsc, struct timespec64 *ts)
{
	const struct qcom_tsc_regs *regs = tsc->soc->reg_layout;
	u32 hi, lo;

	if (!qcom_tsc_is_enabled(tsc))
		return -EINVAL;

	hi = readl_relaxed(tsc->base + regs->read_cntcv_hi);
	lo = readl_relaxed(tsc->base + regs->read_cntcv_lo);

	if (tsc->soc->rollover) {
		ts->tv_sec = hi;
		ts->tv_nsec = lo;
	} else {
		u64 ns = ((u64)hi << NSEC_SHIFT) | lo;

		*ts = ns_to_timespec64(ns);
	}

	return 0;
}

static int qcom_tsc_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct qcom_tsc *tsc = container_of(ptp, struct qcom_tsc, ptp_info);
	int ret;

	mutex_lock(&tsc->lock);

	if (!tsc->clocks_enabled) {
		mutex_unlock(&tsc->lock);
		return -EINVAL;
	}

	ret = qcom_tsc_read_time(tsc, ts);
	mutex_unlock(&tsc->lock);

	return ret;
}

static void qcom_tsc_split_ts(struct qcom_tsc *tsc, struct timespec64 ts, u32 *hi, u32 *lo)
{
	if (tsc->soc->rollover) {
		*hi = ts.tv_sec;
		*lo = ts.tv_nsec;
	} else {
		u64 ns = timespec64_to_ns(&ts);

		*lo = lower_32_bits(ns);
		*hi = upper_32_bits(ns);
	}
}

static void qcom_tsc_clk_disable(void *data)
{
	clk_disable_unprepare(data);
}

static int qcom_tsc_enable_clocks(struct qcom_tsc *tsc)
{
	int ret;

	if (tsc->clocks_enabled)
		return 0;

	ret = clk_prepare_enable(tsc->ahb_clk);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(tsc->dev, qcom_tsc_clk_disable, tsc->ahb_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(tsc->cntr_clk);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(tsc->dev, qcom_tsc_clk_disable, tsc->cntr_clk);
	if (ret)
		return ret;

	tsc->clocks_enabled = true;

	return 0;
}

static int qcom_tsc_update_counter(struct qcom_tsc *tsc, struct timespec64 ts)
{
	const struct qcom_tsc_regs *regs = tsc->soc->reg_layout;
	u32 regval, hi, lo;

	qcom_tsc_split_ts(tsc, ts, &hi, &lo);

	writel_relaxed(lo, tsc->base + regs->control_cntcv_lo);
	writel_relaxed(hi, tsc->base + regs->control_cntcv_hi);

	regval = readl_relaxed(tsc->base + regs->control_cntcr);
	regval |= BIT(0);
	writel_relaxed(regval, tsc->base + regs->control_cntcr);

	return 0;
}

static int qcom_tsc_update_offset(struct qcom_tsc *tsc, const struct timespec64 ts)
{
	const struct qcom_tsc_regs *regs = tsc->soc->reg_layout;
	struct timespec64 tod;
	u32 regval, hi, lo;
	int ret;

	if (tsc->soc->offset_correction) {
		qcom_tsc_split_ts(tsc, ts, &hi, &lo);

		writel_relaxed(lo, tsc->base + regs->offset_lo);
		writel_relaxed(hi, tsc->base + regs->offset_hi);

		return 0;
	}

	/*
	 * No hardware offset correction support: disable and reload the
	 * counter with the corrected time, as recommended by the hardware
	 * design team.
	 */
	ret = qcom_tsc_read_time(tsc, &tod);
	if (ret)
		return ret;

	regval = readl_relaxed(tsc->base + regs->control_cntcr);
	regval &= ~BIT(0);
	writel_relaxed(regval, tsc->base + regs->control_cntcr);

	return qcom_tsc_update_counter(tsc, timespec64_add(tod, ts));
}

static int qcom_tsc_settime(struct ptp_clock_info *ptp, const struct timespec64 *ts)
{
	struct qcom_tsc *tsc = container_of(ptp, struct qcom_tsc, ptp_info);
	const struct qcom_tsc_regs *regs = tsc->soc->reg_layout;
	struct timespec64 tod;
	u32 regval;
	int ret;

	mutex_lock(&tsc->lock);

	ret = qcom_tsc_enable_clocks(tsc);
	if (ret) {
		mutex_unlock(&tsc->lock);
		return ret;
	}

	if (qcom_tsc_is_enabled(tsc)) {
		qcom_tsc_read_time(tsc, &tod);
		ret = qcom_tsc_update_offset(tsc, timespec64_sub(*ts, tod));
		mutex_unlock(&tsc->lock);
		return ret;
	}

	/* Configure and enable the TSC counter */
	regval = readl_relaxed(tsc->base + regs->control_cntcr);
	regval &= ~PULSE_WIDTH_MASK;
	regval |= FIELD_PREP(PULSE_WIDTH_MASK, tsc->soc->pulse_width);
	writel_relaxed(regval, tsc->base + regs->control_cntcr);

	if (tsc->soc->rollover)
		writel_relaxed(ROLLOVER_VAL, tsc->base + regs->rollover_val);

	ret = qcom_tsc_update_counter(tsc, *ts);
	mutex_unlock(&tsc->lock);

	return ret;
}

static int qcom_tsc_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct qcom_tsc *tsc = container_of(ptp, struct qcom_tsc, ptp_info);
	int ret;

	mutex_lock(&tsc->lock);

	if (!tsc->clocks_enabled) {
		mutex_unlock(&tsc->lock);
		return -EINVAL;
	}

	ret = qcom_tsc_update_offset(tsc, ns_to_timespec64(delta));
	mutex_unlock(&tsc->lock);

	return ret;
}

static int qcom_tsc_drift_correction(struct qcom_tsc *tsc, long scaled_ppm)
{
	const struct qcom_tsc_soc_data *soc = tsc->soc;
	const struct qcom_tsc_regs *regs = soc->reg_layout;
	long ppb = scaled_ppm_to_ppb(scaled_ppm);
	u64 period, incval = 0;
	u32 regval = 0, subperiod;

	if (ppb == 0)
		return 0;

	if (ppb < 0) {
		ppb = -ppb;
		regval &= ~DRIFT_JUMP_SWALLOW;

		period = div_u64(ppb, soc->cntr_res);
		if (period >= DRIFT_MAX_RES)
			period = DRIFT_MAX_RES;
	} else {
		regval |= DRIFT_JUMP_SWALLOW;

		period = div_u64(ppb, soc->cntr_res);
		if (period >= DRIFT_MAX_RES) {
			period = DRIFT_MAX_RES;
			incval = soc->cntr_res * 2;
		} else if (ppb < soc->cntr_res) {
			incval = soc->cntr_res + ppb;
			period = 1;
		} else {
			incval = soc->cntr_res * 2;
		}
	}

	subperiod = soc->pulse_width * 2;
	regval |= FIELD_PREP(DRIFT_SUBPERIOD_MASK, subperiod);
	regval |= FIELD_PREP(DRIFT_PERIOD_MASK, period);

	/* Update jump/swallow, period, subperiod */
	writel_relaxed(regval, tsc->base + regs->drift_correct_duration);

	writel_relaxed(incval, tsc->base + regs->drift_correct_incr_val);

	/* Trigger the drift correction */
	regval = readl_relaxed(tsc->base + regs->drift_correct_cmd);
	regval |= BIT(0);
	writel_relaxed(regval, tsc->base + regs->drift_correct_cmd);

	return readl_poll_timeout(tsc->base + regs->drift_correct_cmd, regval,
				  !(regval & BIT(1)), DRIFT_INTERVAL_US,
				  DRIFT_TIMEOUT_US(period, subperiod, soc->cntr_res));
}

static bool qcom_tsc_dynamic_drift_type_matches(u32 duration, bool neg_adj)
{
	return !(duration & DRIFT_JUMP_SWALLOW) == neg_adj;
}

static void qcom_tsc_set_dynamic_drift_enable(struct qcom_tsc *tsc, u32 cmd, bool enable)
{
	const struct qcom_tsc_regs *regs = tsc->soc->reg_layout;

	if (enable)
		cmd |= DRIFT_DYNAMIC_EN;
	else
		cmd &= ~DRIFT_DYNAMIC_EN;

	writel_relaxed(cmd, tsc->base + regs->drift_correct_cmd);
}

static int qcom_tsc_disable_dynamic_drift(struct qcom_tsc *tsc, u32 cmd)
{
	const struct qcom_tsc_regs *regs = tsc->soc->reg_layout;

	qcom_tsc_set_dynamic_drift_enable(tsc, cmd, false);

	return readl_poll_timeout(tsc->base + regs->drift_correct_cmd, cmd,
				  !(cmd & DRIFT_STATUS), DYNAMIC_DRIFT_POLL_US,
				  DYNAMIC_DRIFT_DISABLE_TIMEOUT_US);
}

static void qcom_tsc_program_dynamic_drift(struct qcom_tsc *tsc, u32 duration,
					   bool neg_adj, u32 subperiod)
{
	const struct qcom_tsc_regs *regs = tsc->soc->reg_layout;

	if (neg_adj)
		duration &= ~DRIFT_JUMP_SWALLOW;
	else
		duration |= DRIFT_JUMP_SWALLOW;

	writel_relaxed(duration, tsc->base + regs->drift_correct_duration);
	writel_relaxed(DYNAMIC_DRIFT_INCR_VAL, tsc->base + regs->drift_correct_incr_val);
	writel_relaxed(FIELD_PREP(DYNAMIC_DRIFT_SUBPERIOD_MASK, subperiod),
		       tsc->base + regs->drift_correct_duration_2);
}

/*
 * Unlike qcom_tsc_drift_correction()'s one-shot jump/swallow correction,
 * hardware supporting dynamic drift correction keeps the correction
 * continuously active across adjfine() calls. The subperiod can be updated
 * in place while active, as long as the correction direction (jump vs
 * swallow) is unchanged; otherwise the correction must be disabled and
 * reprogrammed before being re-enabled.
 */
static int qcom_tsc_dynamic_drift_correction(struct qcom_tsc *tsc, long scaled_ppm)
{
	const struct qcom_tsc_regs *regs = tsc->soc->reg_layout;
	long ppb = scaled_ppm_to_ppb(scaled_ppm);
	bool neg_adj = ppb < 0;
	u32 duration, cmd, subperiod;
	int ret;

	if (neg_adj)
		ppb = -ppb;

	cmd = readl_relaxed(tsc->base + regs->drift_correct_cmd);

	if (ppb == 0) {
		if (cmd & DRIFT_STATUS)
			return qcom_tsc_disable_dynamic_drift(tsc, cmd);

		return 0;
	}

	subperiod = NSEC_PER_SEC / ppb;
	subperiod = clamp_val(subperiod, DYNAMIC_DRIFT_MIN_SUBPERIOD, DYNAMIC_DRIFT_SUBPERIOD_MASK);

	duration = readl_relaxed(tsc->base + regs->drift_correct_duration);

	if ((cmd & DRIFT_STATUS) && qcom_tsc_dynamic_drift_type_matches(duration, neg_adj)) {
		writel_relaxed(FIELD_PREP(DYNAMIC_DRIFT_SUBPERIOD_MASK, subperiod),
			       tsc->base + regs->drift_correct_duration_2);
		return 0;
	}

	if (cmd & DRIFT_STATUS) {
		ret = qcom_tsc_disable_dynamic_drift(tsc, cmd);
		if (ret)
			return ret;

		cmd = readl_relaxed(tsc->base + regs->drift_correct_cmd);
	}

	qcom_tsc_program_dynamic_drift(tsc, duration, neg_adj, subperiod);
	qcom_tsc_set_dynamic_drift_enable(tsc, cmd, true);

	return 0;
}

static int qcom_tsc_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct qcom_tsc *tsc = container_of(ptp, struct qcom_tsc, ptp_info);
	int ret;

	mutex_lock(&tsc->lock);

	if (!tsc->clocks_enabled) {
		mutex_unlock(&tsc->lock);
		return -EINVAL;
	}

	if (tsc->soc->dynamic_drift_correction)
		ret = qcom_tsc_dynamic_drift_correction(tsc, scaled_ppm);
	else
		ret = qcom_tsc_drift_correction(tsc, scaled_ppm);
	mutex_unlock(&tsc->lock);

	return ret;
}

static struct ptp_clock_info qcom_ptp_info = {
	.owner		= THIS_MODULE,
	.name		= "PTP_QCOM_TSC",
	.max_adj	= 9999999,
	.adjfine	= qcom_tsc_adjfine,
	.adjtime	= qcom_tsc_adjtime,
	.gettime64      = qcom_tsc_gettime,
	.settime64      = qcom_tsc_settime,
};

static const struct qcom_tsc_regs qcom_tsc_layout = {
	.control_cntcr            = 0x0,
	.control_cntcv_lo         = 0x8,
	.control_cntcv_hi         = 0xc,
	.drift_correct_incr_val   = 0x2c,
	.drift_correct_duration   = 0x30,
	.drift_correct_cmd        = 0x34,
	.rollover_val             = 0x3c,
	.offset_lo                = 0x50,
	.offset_hi                = 0x54,
	.read_cntcv_lo            = 0x1000,
	.read_cntcv_hi            = 0x1004,
};

static const struct qcom_tsc_regs qcom_tsc_layout_v2 = {
	.control_cntcr            = 0x0,
	.control_cntcv_lo         = 0x8,
	.control_cntcv_hi         = 0xc,
	.drift_correct_incr_val   = 0x2c,
	.drift_correct_duration   = 0x30,
	.drift_correct_duration_2 = 0x34,
	.drift_correct_cmd        = 0x38,
	.drift_correct_cmd_2      = 0x3c,
	.rollover_val             = 0x44,
	.offset_lo                = 0x50,
	.offset_hi                = 0x54,
	.read_cntcv_lo            = 0x1000,
	.read_cntcv_hi            = 0x1004,
};

static const struct qcom_tsc_soc_data qcom_tsc_sa8775p = {
	.reg_layout		= &qcom_tsc_layout,
	.rollover		= false,
	.offset_correction	= false,
	.pulse_width		= 2,
	.cntr_res		= 64,
};

static const struct qcom_tsc_soc_data qcom_tsc_qdu1000 = {
	.reg_layout		= &qcom_tsc_layout,
	.rollover		= true,
	.offset_correction	= true,
	.pulse_width		= 3,
	.cntr_res		= 2,
};

static const struct qcom_tsc_soc_data qcom_tsc_nord = {
	.reg_layout		= &qcom_tsc_layout_v2,
	.rollover		= false,
	.offset_correction	= true,
	.dynamic_drift_correction = true,
	.pulse_width		= 3,
	.cntr_res		= 4,
};

static const struct of_device_id qcom_tsc_of_match[] = {
	{ .compatible = "qcom,sa8775p-tscss", .data = &qcom_tsc_sa8775p },
	{ .compatible = "qcom,qdu1000-tscss", .data = &qcom_tsc_qdu1000 },
	{ .compatible = "qcom,nord-tscss", .data = &qcom_tsc_nord },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_tsc_of_match);

static int qcom_ptp_tsc_probe(struct platform_device *pdev)
{
	const struct qcom_tsc_soc_data *soc;
	struct qcom_tsc *tsc;
	int ret;

	soc = of_device_get_match_data(&pdev->dev);
	if (!soc)
		return -ENODEV;

	tsc = devm_kzalloc(&pdev->dev, sizeof(*tsc), GFP_KERNEL);
	if (!tsc)
		return -ENOMEM;

	tsc->dev = &pdev->dev;
	tsc->soc = soc;

	ret = devm_mutex_init(&pdev->dev, &tsc->lock);
	if (ret)
		return ret;

	tsc->base = devm_platform_ioremap_resource_byname(pdev, "tsc");
	if (IS_ERR(tsc->base))
		return PTR_ERR(tsc->base);

	/* Both clocks are enabled on first use, when the counter is set */
	tsc->ahb_clk = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(tsc->ahb_clk))
		return PTR_ERR(tsc->ahb_clk);

	tsc->cntr_clk = devm_clk_get(&pdev->dev, "cntr");
	if (IS_ERR(tsc->cntr_clk))
		return PTR_ERR(tsc->cntr_clk);

	tsc->ptp_info = qcom_ptp_info;

	tsc->ptp = ptp_clock_register(&tsc->ptp_info, &pdev->dev);
	if (IS_ERR(tsc->ptp))
		return dev_err_probe(&pdev->dev, PTR_ERR(tsc->ptp),
				      "Failed to register ptp clock\n");

	platform_set_drvdata(pdev, tsc);

	return 0;
}

static void qcom_ptp_tsc_remove(struct platform_device *pdev)
{
	struct qcom_tsc *tsc = platform_get_drvdata(pdev);

	if (tsc && tsc->ptp) {
		ptp_clock_unregister(tsc->ptp);
		tsc->ptp = NULL;
	}
}

static struct platform_driver qcom_ptp_tsc_driver = {
	.probe  = qcom_ptp_tsc_probe,
	.remove = qcom_ptp_tsc_remove,
	.driver = {
		.name = "qcom_ptp_tsc",
		.of_match_table = qcom_tsc_of_match,
	},
};
module_platform_driver(qcom_ptp_tsc_driver);

MODULE_DESCRIPTION("PTP QCOM TSC driver");
MODULE_LICENSE("GPL");
