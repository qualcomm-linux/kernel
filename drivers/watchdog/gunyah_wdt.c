// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/suspend.h>
#include <linux/watchdog.h>

#define GUNYAH_WDT_SMCCC_CALL_VAL(func_id) \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32,\
			   ARM_SMCCC_OWNER_VENDOR_HYP, func_id)

/* SMCCC function IDs for watchdog operations */
#define GUNYAH_WDT_CONTROL   GUNYAH_WDT_SMCCC_CALL_VAL(0x0005)
#define GUNYAH_WDT_STATUS    GUNYAH_WDT_SMCCC_CALL_VAL(0x0006)
#define GUNYAH_WDT_PING      GUNYAH_WDT_SMCCC_CALL_VAL(0x0007)
#define GUNYAH_WDT_SET_TIME  GUNYAH_WDT_SMCCC_CALL_VAL(0x0008)

/*
 * Control values for GUNYAH_WDT_CONTROL.
 * Bit 0 is used to enable or disable the watchdog. If this bit is set,
 * then the watchdog is enabled and vice versa.
 * Bit 1 should always be set to 1 as this bit is reserved in Gunyah and
 * it's expected to be 1.
 */
#define WDT_CTRL_ENABLE  (BIT(1) | BIT(0))
#define WDT_CTRL_DISABLE BIT(1)

enum gunyah_error {
	GUNYAH_ERROR_OK				= 0,
	GUNYAH_ERROR_UNIMPLEMENTED		= -1,
	GUNYAH_ERROR_RETRY			= -2,

	GUNYAH_ERROR_ARG_INVAL			= 1,
	GUNYAH_ERROR_ARG_SIZE			= 2,
	GUNYAH_ERROR_ARG_ALIGN			= 3,

	GUNYAH_ERROR_NOMEM			= 10,

	GUNYAH_ERROR_ADDR_OVFL			= 20,
	GUNYAH_ERROR_ADDR_UNFL			= 21,
	GUNYAH_ERROR_ADDR_INVAL			= 22,

	GUNYAH_ERROR_DENIED			= 30,
	GUNYAH_ERROR_BUSY			= 31,
	GUNYAH_ERROR_IDLE			= 32,

	GUNYAH_ERROR_IRQ_BOUND			= 40,
	GUNYAH_ERROR_IRQ_UNBOUND		= 41,

	GUNYAH_ERROR_CSPACE_CAP_NULL		= 50,
	GUNYAH_ERROR_CSPACE_CAP_REVOKED		= 51,
	GUNYAH_ERROR_CSPACE_WRONG_OBJ_TYPE	= 52,
	GUNYAH_ERROR_CSPACE_INSUF_RIGHTS	= 53,
	GUNYAH_ERROR_CSPACE_FULL		= 54,

	GUNYAH_ERROR_MSGQUEUE_EMPTY		= 60,
	GUNYAH_ERROR_MSGQUEUE_FULL		= 61,
};

/**
 * gunyah_error_remap() - Remap Gunyah hypervisor errors into a Linux error code
 * @gunyah_error: Gunyah hypercall return value
 */
static inline int gunyah_error_remap(enum gunyah_error gunyah_error)
{
	switch (gunyah_error) {
	case GUNYAH_ERROR_OK:
		return 0;
	case GUNYAH_ERROR_NOMEM:
		return -ENOMEM;
	case GUNYAH_ERROR_DENIED:
	case GUNYAH_ERROR_CSPACE_CAP_NULL:
	case GUNYAH_ERROR_CSPACE_CAP_REVOKED:
	case GUNYAH_ERROR_CSPACE_WRONG_OBJ_TYPE:
	case GUNYAH_ERROR_CSPACE_INSUF_RIGHTS:
	case GUNYAH_ERROR_CSPACE_FULL:
		return -EACCES;
	case GUNYAH_ERROR_BUSY:
	case GUNYAH_ERROR_IDLE:
		return -EBUSY;
	case GUNYAH_ERROR_IRQ_BOUND:
	case GUNYAH_ERROR_IRQ_UNBOUND:
	case GUNYAH_ERROR_MSGQUEUE_FULL:
	case GUNYAH_ERROR_MSGQUEUE_EMPTY:
		return -EIO;
	case GUNYAH_ERROR_UNIMPLEMENTED:
	case GUNYAH_ERROR_RETRY:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
}

static int gunyah_wdt_call(unsigned long func_id, unsigned long arg1,
			   unsigned long arg2, struct arm_smccc_res *res)
{
	arm_smccc_1_1_smc(func_id, arg1, arg2, res);
	return gunyah_error_remap(res->a0);
}

static int gunyah_wdt_start(struct watchdog_device *wdd)
{
	struct arm_smccc_res res;
	unsigned int timeout_ms;
	int ret;

	ret = gunyah_wdt_call(GUNYAH_WDT_CONTROL, WDT_CTRL_DISABLE, 0, &res);
	if (ret && watchdog_active(wdd)) {
		pr_err("%s: Failed to stop gunyah wdt %d\n", __func__, ret);
		return ret;
	}

	timeout_ms = wdd->timeout * 1000;
	ret = gunyah_wdt_call(GUNYAH_WDT_SET_TIME,
			      timeout_ms, timeout_ms, &res);
	if (ret) {
		pr_err("%s: Failed to set timeout for gunyah wdt %d\n",
		       __func__,  ret);
		return ret;
	}

	ret = gunyah_wdt_call(GUNYAH_WDT_CONTROL, WDT_CTRL_ENABLE, 0, &res);
	if (ret)
		pr_err("%s: Failed to start gunyah wdt %d\n", __func__, ret);

	return ret;
}

static int gunyah_wdt_stop(struct watchdog_device *wdd)
{
	struct arm_smccc_res res;

	return gunyah_wdt_call(GUNYAH_WDT_CONTROL, WDT_CTRL_DISABLE, 0, &res);
}

static int gunyah_wdt_ping(struct watchdog_device *wdd)
{
	struct arm_smccc_res res;

	return gunyah_wdt_call(GUNYAH_WDT_PING, 0, 0, &res);
}

static int gunyah_wdt_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout_sec)
{
	wdd->timeout = timeout_sec;

	if (watchdog_active(wdd))
		return gunyah_wdt_start(wdd);

	return 0;
}

static unsigned int gunyah_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct arm_smccc_res res;
	unsigned int seconds_since_last_ping;
	int ret;

	ret = gunyah_wdt_call(GUNYAH_WDT_STATUS, 0, 0, &res);
	if (ret)
		return 0;

	seconds_since_last_ping = res.a2 / 1000;
	if (seconds_since_last_ping > wdd->timeout)
		return 0;

	return wdd->timeout - seconds_since_last_ping;
}

static int gunyah_wdt_restart(struct watchdog_device *wdd,
			      unsigned long action, void *data)
{
	struct arm_smccc_res res;

	/* Set timeout to 1ms and send a ping */
	gunyah_wdt_call(GUNYAH_WDT_CONTROL, WDT_CTRL_ENABLE, 0, &res);
	gunyah_wdt_call(GUNYAH_WDT_SET_TIME, 1, 1, &res);
	gunyah_wdt_call(GUNYAH_WDT_PING, 0, 0, &res);

	/* Wait to make sure reset occurs */
	mdelay(100);

	return 0;
}

static const struct watchdog_info gunyah_wdt_info = {
	.identity = "Gunyah Watchdog",
	.firmware_version = 0,
	.options = WDIOF_SETTIMEOUT
		 | WDIOF_KEEPALIVEPING
		 | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops gunyah_wdt_ops = {
	.owner = THIS_MODULE,
	.start = gunyah_wdt_start,
	.stop = gunyah_wdt_stop,
	.ping = gunyah_wdt_ping,
	.set_timeout = gunyah_wdt_set_timeout,
	.get_timeleft = gunyah_wdt_get_timeleft,
	.restart = gunyah_wdt_restart
};

static int gunyah_wdt_pm_notifier(struct notifier_block *nb, unsigned long mode,
				  void *data)
{
	struct watchdog_device *wdd;
	int ret = 0;

	wdd = container_of(nb, struct watchdog_device, pm_nb);

	if (!watchdog_active(wdd))
		return NOTIFY_DONE;

	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
	case PM_SUSPEND_PREPARE:
		gunyah_wdt_ping(wdd);
		ret = gunyah_wdt_stop(wdd);
		if (ret)
			return NOTIFY_BAD;
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		ret = gunyah_wdt_start(wdd);
		if (ret)
			return NOTIFY_BAD;
		gunyah_wdt_ping(wdd);
		break;
	}

	return NOTIFY_DONE;
}

static int __init gunyah_wdt_init(void)
{
	struct arm_smccc_res res;
	struct watchdog_device *wdd;
	struct device_node *np;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "qcom,kpss-wdt");
	if (np) {
		of_node_put(np);
		return -ENODEV;
	}

	np = of_find_compatible_node(NULL, NULL, "arm,sbsa-gwdt");
	if (np) {
		of_node_put(np);
		return -ENODEV;
	}

	ret = gunyah_wdt_call(GUNYAH_WDT_STATUS, 0, 0, &res);
	if (ret)
		return -ENODEV;

	wdd = kzalloc(sizeof(*wdd), GFP_KERNEL);
	if (!wdd)
		return -ENOMEM;

	wdd->info = &gunyah_wdt_info;
	wdd->ops = &gunyah_wdt_ops;

	/*
	 * Although Gunyah expects 16-bit unsigned int values as timeout values
	 * in milliseconds, values above 0x8000 are reserved. This limits the
	 * max timeout value to 32 seconds.
	 */
	wdd->max_timeout = 32; /* seconds */
	wdd->min_timeout = 1; /* seconds */
	wdd->timeout = wdd->max_timeout;

	gunyah_wdt_stop(wdd);

	watchdog_set_restart_priority(wdd, 0);

	wdd->pm_nb.notifier_call = gunyah_wdt_pm_notifier;
	ret = register_pm_notifier(&wdd->pm_nb);
	if (ret)
		pr_warn("Failed to register pm handler: %d\n", ret);

	ret = watchdog_register_device(wdd);
	if (ret) {
		pr_err("Failed to register watchdog device: %d\n", ret);
		unregister_pm_notifier(&wdd->pm_nb);
		kfree(wdd);
		return ret;
	}

	pr_debug("Gunyah watchdog registered\n");
	return 0;
}

module_init(gunyah_wdt_init);

MODULE_DESCRIPTION("Gunyah Watchdog Driver");
MODULE_LICENSE("GPL");
