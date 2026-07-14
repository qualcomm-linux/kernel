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

#include <linux/mutex.h>
#include <linux/workqueue.h>

struct device;
struct regmap;
struct sdw_slave;
struct sdca_function_data;

/**
 * struct sdca_class_hw_ops - optional device-specific hardware callbacks
 * @hw_init: called during probe to enable supplies, toggle reset GPIO, etc.
 * @get_function_data: called when no DisCo/ACPI firmware node is available
 *             (e.g. DT/ARM platforms) to supply pre-populated static function
 *             data in place of sdca_parse_function(); may be NULL
 *
 * Pass a pointer to this struct via the driver_data field of sdw_device_id.
 */
struct sdca_class_hw_ops {
	int  (*hw_init)(struct sdw_slave *slave);
	struct sdca_function_data *(*get_function_data)(void);
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

#endif /* __SDCA_CLASS_H__ */
