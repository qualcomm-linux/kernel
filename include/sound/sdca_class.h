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
struct dev_pm_ops;
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
 *           DT/non-DisCo platforms.  Must leave @function->desc alone.
 *           Return 0 on success or a negative errno.  May be NULL.
 */
struct sdca_class_hw_ops {
	int (*hw_init)(struct sdw_slave *slave);
	int (*populate_function)(struct sdw_slave *slave,
				 struct sdca_function_data *function);
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
