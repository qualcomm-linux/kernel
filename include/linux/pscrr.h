/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pscrr.h - Public header for Power State Change Reason Recording (PSCRR).
 *
 * Copyright (C) 2025 Pengutronix, Oleksij Rempel <o.rempel@pengutronix.de>
 */

#ifndef __PSCRR_H__
#define __PSCRR_H__

#include <linux/reboot.h>

struct device;
struct pscrr_provider;

/**
 * struct pscrr_provider_ops - Provider callbacks.
 * @read_reasons: Fill @reasons with the complete set this source observed.
 *		@reasons is a bitmap of PSCR_REASON_COUNT bits; set members
 *		with set_bit(PSCR_x, reasons). Return 0 on success or a
 *		negative errno. Required. Driver state is reached via p->priv.
 * @write_reason: Persist @reason. Called from the reboot notifier for
 *		recorder-capable providers so the cause survives the power
 *		cycle. Leave NULL for read-only hardware sources.
 *
 * A const ops table per provider type; extend it with new callbacks without
 * touching the registration signature or existing callers.
 */
struct pscrr_provider_ops {
	int (*read_reasons)(struct pscrr_provider *p, unsigned long *reasons);
	int (*write_reason)(struct pscrr_provider *p, enum psc_reason reason);
};

/**
 * struct pscrr_provider - A source (and optionally recorder) of power state
 *			   change reasons.
 *
 * A provider represents one place the system can learn *why* the last power
 * state change happened: a hardware reset-cause register (PMIC, SoC SRC,
 * watchdog), a persistent recorder (NVMEM/RTC scratch), or a test stub. Each
 * registered provider gets its own directory under /sys/kernel/pscrr/, so the
 * full, un-prioritised picture is visible: several providers - and several
 * reasons within one provider - can be reported simultaneously.
 *
 * @name:	Human-readable label, exported as the "name" attribute. The
 *		directory itself is core-indexed (providerN), so this need not
 *		be unique. Required.
 * @dev:	Backing device. When set it is exported as the "device" symlink
 *		in the provider directory, tying the reason to real hardware.
 *		May be NULL (e.g. for a test provider).
 * @ops:	Provider callbacks. Required.
 * @supported_reasons: Bitmap of the reasons this provider can store or report,
 *		limited e.g. by the storage size. 0 means all reasons.
 * @priv:	Provider private data, passed back through the callbacks.
 *
 * Providers are readable and single-slot by default; only capabilities beyond
 * that (currently: writable) are advertised.
 */
struct pscrr_provider {
	const char *name;
	struct device *dev;
	const struct pscrr_provider_ops *ops;
	const unsigned long *supported_reasons;
	void *priv;
};

#if IS_ENABLED(CONFIG_PSCRR)
int pscrr_provider_register(struct pscrr_provider *p);
void pscrr_provider_unregister(struct pscrr_provider *p);

/**
 * devm_pscrr_provider_register - allocate, fill and register a provider
 * @dev: device the provider belongs to (also the "device" symlink target)
 * @name: provider label
 * @ops: provider callbacks
 * @supported_reasons: bitmap of supported reasons, or NULL for all
 * @priv: driver state passed back through the callbacks
 *
 * The provider is unregistered automatically on device teardown.
 *
 * Return: the provider on success, ERR_PTR() on failure, or NULL when PSCRR
 * is not built (so the caller need not guard the call).
 */
struct pscrr_provider *
devm_pscrr_provider_register(struct device *dev, const char *name,
			     const struct pscrr_provider_ops *ops,
			     const unsigned long *supported_reasons, void *priv);
#else
static inline int pscrr_provider_register(struct pscrr_provider *p)
{
	return -EOPNOTSUPP;
}

static inline void pscrr_provider_unregister(struct pscrr_provider *p)
{
}

static inline struct pscrr_provider *
devm_pscrr_provider_register(struct device *dev, const char *name,
			     const struct pscrr_provider_ops *ops,
			     const unsigned long *supported_reasons, void *priv)
{
	return NULL;
}
#endif

#endif /* __PSCRR_H__ */
