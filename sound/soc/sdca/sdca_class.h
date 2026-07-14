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
struct sdw_port_config;
struct sdw_slave;
struct sdca_function_data;
struct snd_pcm_substream;
struct snd_soc_dai;

/**
 * struct sdca_class_hw_ops - optional device-specific hardware callbacks
 * @hw_init: called during probe to enable supplies, toggle reset GPIO, etc.
 * @prepare: called from the DAI prepare op; may be NULL
 * @get_aux_port_configs: called from class_function_sdw_add_peripheral() to
 *             let a codec whose SDCA topology carries more than one dataport
 *             per DAI (e.g. WCD9378 HPH: DP6 audio + DP7 envelope) contribute
 *             the extra ports in the same sdw_stream_add_slave() call as the
 *             DAI's main port.  Fill up to @max entries in @configs and
 *             return the number filled, or negative on error.  Zero means
 *             no aux ports for this DAI.
 * @get_function_data: called when no DisCo/ACPI firmware node is available
 *             (e.g. DT/ARM platforms) to supply pre-populated static function
 *             data in place of sdca_parse_function(); may be NULL
 * @pde_pre_pmu: called before DAPM writes REQUESTED_PS=PS0; use to
 *             prepare device state that must be valid before the PDE
 *             sequencer runs (e.g. IT_USAGE); may be NULL
 * @pde_post_pmu: called after DAPM writes REQUESTED_PS=PS0 and before
 *             ACTUAL_PS polling begins; use to commit pending register
 *             writes (e.g. FUNCTION_ACTION) on devices that require an
 *             explicit commit trigger for PDE power-up; may be NULL
 * @set_jack:  called from class_function_set_jack() after IRQ handlers
 *             are registered; use to enable hardware detection (e.g. MBHC
 *             comparator) that must not fire earlier; may be NULL
 *
 * Pass a pointer to this struct via the driver_data field of sdw_device_id.
 */
struct sdca_class_hw_ops {
	int  (*hw_init)(struct sdw_slave *slave);
	int  (*prepare)(struct sdw_slave *slave,
			struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai);
	int  (*get_aux_port_configs)(struct sdw_slave *slave,
				     struct snd_soc_dai *dai,
				     struct sdw_port_config *configs, int max);
	struct sdca_function_data *(*get_function_data)(void);
	int  (*pde_pre_pmu)(struct sdw_slave *slave, struct regmap *regmap,
			    unsigned int function_id, unsigned int entity_id);
	int  (*pde_post_pmu)(struct sdw_slave *slave, struct regmap *regmap,
			     unsigned int function_id, unsigned int entity_id);
	int  (*set_jack)(struct sdw_slave *slave, struct regmap *dev_regmap);
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
