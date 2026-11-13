/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 *
 * Copyright (C) 2025 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef __SDCA_CLASS_H__
#define __SDCA_CLASS_H__

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

struct device;
struct regmap;
struct sdw_slave;
struct sdca_function_data;

/**
 * struct sdca_class_hw_ops - optional codec hardware callbacks
 * @hw_init: enable supplies, toggle reset, etc.  Runs from sdca_class_probe()
 *           before the class regmap is created and before the slave is
 *           ATTACHED; callers needing bus I/O must sdw_slave_wait_for_init()
 *           first.
 * @populate_function: fill @function (entities, clusters, init_table, ...)
 *           from static tables in place of sdca_parse_function() on
 *           DT/non-DisCo platforms.  Called once per SDCA function with
 *           @dev pointing at that function's device.  Must leave
 *           @function->desc alone.  Pure data source; performs no bus
 *           I/O.  Return 0 on success or a negative errno.  May be NULL.
 * @set_jack: called from the SDCA class function driver's ASoC .set_jack,
 *           after the jack has been registered with sdca_jack_set_jack();
 *           use to enable hardware detection (e.g. arm the MBHC comparator)
 *           that must not be turned on any earlier, since doing so before
 *           the machine driver has finished registering the jack can raise
 *           a detection interrupt that blocks the deferred card probe.
 *           May be NULL.
 * @ge_mode_detected: called from sdca_jack_process() every time a GE
 *           DETECTED_MODE interrupt fires, unlike @set_jack which only
 *           runs once at jack registration; use to re-arm hardware
 *           detection logic that must be reconfigured after every
 *           transition (e.g. toggling a mechanical comparator's
 *           polarity-select bit so the next opposite-direction transition
 *           is still seen).  May be NULL.
 */
struct sdca_class_hw_ops {
	int (*hw_init)(struct sdw_slave *slave);
	int (*populate_function)(struct device *dev,
				 struct sdca_function_data *function);
	int (*set_jack)(struct sdw_slave *slave, struct regmap *dev_regmap);
	int (*ge_mode_detected)(struct sdw_slave *slave, struct regmap *dev_regmap);
};

struct sdca_class_drv {
	struct device *dev;
	struct regmap *dev_regmap;
	struct sdw_slave *sdw;

	struct sdca_interrupt_info *irq_info;

	const struct sdca_class_hw_ops *hw_ops;

	struct mutex regmap_lock;
	/* Serialise function initialisations */
	struct mutex init_lock;
	struct work_struct boot_work;
};

/* Library helpers used by codec-specific SDCA SoundWire drivers. */
int sdca_class_read_prop(struct sdw_slave *sdw);
int sdca_class_probe(struct sdw_slave *sdw,
		     struct sdca_class_drv *drv,
		     const struct sdca_class_hw_ops *hw_ops);
void sdca_class_remove(struct sdca_class_drv *drv);

/*
 * PM helpers.  Codec drivers embed sdca_class_drv in their own priv,
 * own dev_set_drvdata(), and compose these into their own dev_pm_ops:
 *
 *	static int wcd_runtime_suspend(struct device *dev) {
 *		struct wcd_priv *priv = dev_get_drvdata(dev);
 *		return sdca_class_runtime_suspend(&priv->class);
 *	}
 *
 * The built-in class_sdw_driver in sdca_class.c uses sdca_class_pm_ops
 * directly because it stashes the sdca_class_drv in drvdata itself.
 */
int sdca_class_runtime_suspend(struct sdca_class_drv *drv);
int sdca_class_runtime_resume(struct sdca_class_drv *drv);
int sdca_class_system_suspend(struct sdca_class_drv *drv);
int sdca_class_system_resume(struct sdca_class_drv *drv);
extern const struct dev_pm_ops sdca_class_pm_ops;

#endif /* __SDCA_CLASS_H__ */
