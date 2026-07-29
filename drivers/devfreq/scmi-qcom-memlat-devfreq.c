// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/cpu.h>
#include <linux/devfreq.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/scmi_protocol.h>
#include <linux/scmi_qcom_protocol.h>
#include <linux/units.h>

#define MAX_NAME_LEN				20
#define MAX_MAP_ENTRIES				10

#include "scmi-qcom-memlat-cfg.h"

/**
 * enum scmi_memlat_protocol_cmd - parameter_ids supported by the "MEMLAT" algo_str hosted
 *                                 by the Qualcomm Generic Vendor Protocol on the SCMI controller.
 *
 * MEMLAT (Memory Latency) monitors the counters to detect memory latency bound workloads
 * and scales the frequency/levels of the memory buses accordingly.
 *
 * @MEMLAT_SET_MEM_GROUP: initializes the frequency/level scaling functions for the memory bus.
 * @MEMLAT_SET_MONITOR: configures the monitor to work on a specific memory bus.
 * @MEMLAT_SET_COMMON_EV_MAP: set up common counters used to monitor the cpu frequency.
 * @MEMLAT_SET_GRP_EV_MAP: set up any specific counters used to monitor the memory bus.
 * @MEMLAT_IPM_CEIL: set the IPM (Instruction Per Misses) ceiling per monitor.
 * @MEMLAT_BE_STALL_FLOOR: set the back-end stall floor per monitor.
 * @MEMLAT_SAMPLE_MS: set the sampling period for all the monitors.
 * @MEMLAT_MON_FREQ_MAP: setup the cpufreq to memfreq map.
 * @MEMLAT_SET_MIN_FREQ: set the min frequency of the memory bus.
 * @MEMLAT_SET_MAX_FREQ: set the max frequency of the memory bus.
 * @MEMLAT_GET_CUR_FREQ: query the current frequency/level of the memory bus.
 * @MEMLAT_START_TIMER: start all the monitors with the requested sampling period.
 * @MEMLAT_STOP_TIMER: stop all the running monitors.
 * @MEMLAT_SET_EFFECTIVE_FREQ_METHOD: set the method used to determine cpu frequency.
 */
enum scmi_memlat_protocol_cmd {
	MEMLAT_SET_MEM_GROUP = 16,
	MEMLAT_SET_MONITOR,
	MEMLAT_SET_COMMON_EV_MAP,
	MEMLAT_SET_GRP_EV_MAP,
	MEMLAT_IPM_CEIL = 23,
	MEMLAT_BE_STALL_FLOOR = 25,
	MEMLAT_SAMPLE_MS = 31,
	MEMLAT_MON_FREQ_MAP,
	MEMLAT_SET_MIN_FREQ,
	MEMLAT_SET_MAX_FREQ,
	MEMLAT_GET_CUR_FREQ,
	MEMLAT_START_TIMER = 36,
	MEMLAT_STOP_TIMER,
	MEMLAT_SET_EFFECTIVE_FREQ_METHOD = 39,
};

struct cpucp_map_table {
	__le16 v1;
	__le16 v2;
};

struct map_param_msg {
	__le32 hw_type;
	__le32 mon_idx;
	__le32 nr_rows;
	struct cpucp_map_table tbl[MAX_MAP_ENTRIES];
} __packed;

struct node_msg {
	__le32 cpumask;
	__le32 hw_type;
	__le32 mon_type;
	__le32 mon_idx;
	char mon_name[MAX_NAME_LEN];
};

struct scalar_param_msg {
	__le32 hw_type;
	__le32 mon_idx;
	__le32 val;
};

struct ev_map_msg {
	__le32 num_evs;
	__le32 hw_type;
	__le32 cid[NUM_COMMON_EVS];
};

struct scmi_qcom_memlat_map {
	unsigned int cpufreq_mhz;
	unsigned int memfreq_khz;
};

struct scmi_qcom_monitor_info {
	struct scmi_qcom_memlat_map *freq_map;
	char name[MAX_NAME_LEN];
	u32 mon_idx;
	u32 mon_type;
	u32 ipm_ceil;
	u32 be_stall_floor;
	u32 mask;
	u32 freq_map_len;
};

struct scmi_qcom_memory_info {
	struct scmi_qcom_monitor_info **monitor;
	u32 hw_type;
	int monitor_cnt;
	u32 min_freq;
	u32 max_freq;
	struct devfreq_dev_profile profile;
	struct devfreq *devfreq;
	struct platform_device *pdev;
	struct scmi_protocol_handle *ph;
	const struct qcom_generic_ext_ops *ops;
};

struct scmi_qcom_memlat_info {
	struct scmi_protocol_handle *ph;
	const struct qcom_generic_ext_ops *ops;
	const struct scmi_qcom_memlat_cfg_data *cfg_data;
	struct scmi_qcom_memory_info **memory;
	u32 cpucp_freq_method;
	u32 cpucp_sample_ms;
	int memory_cnt;
};

static int configure_cpucp_common_events(struct scmi_qcom_memlat_info *info,
					 const struct scmi_qcom_memlat_cfg_data *cfg_data)
{
	const struct qcom_generic_ext_ops *ops = info->ops;
	struct ev_map_msg msg = {};
	int i;

	msg.num_evs = cpu_to_le32(NUM_COMMON_EVS);
	/* Common events apply to all groups; INVALID_IDX flags "no specific group". */
	msg.hw_type = cpu_to_le32(INVALID_IDX);
	for (i = 0; i < NUM_COMMON_EVS; i++)
		msg.cid[i] = cpu_to_le32(cfg_data->common_ev[i]);

	return ops->set_param(info->ph, &msg, sizeof(msg), MEMLAT_ALGO_STR,
			      MEMLAT_SET_COMMON_EV_MAP);
}

static int configure_cpucp_grp(struct device *dev, struct scmi_qcom_memlat_info *info,
			       const struct scmi_qcom_memlat_cfg_data *cfg_data,
			       int memory_index)
{
	const u32 *grp_ev = cfg_data->memory_cfg[memory_index].grp_ev;
	struct scmi_qcom_memory_info *memory = info->memory[memory_index];
	const struct qcom_generic_ext_ops *ops = info->ops;
	struct ev_map_msg ev_msg = {};
	struct node_msg msg = {};
	int ret;
	int i;

	msg.cpumask = cpu_to_le32(*cpumask_bits(cpu_possible_mask));
	msg.hw_type = cpu_to_le32(memory->hw_type);
	msg.mon_type = 0;
	msg.mon_idx = 0;
	ret = ops->set_param(info->ph, &msg, sizeof(msg), MEMLAT_ALGO_STR, MEMLAT_SET_MEM_GROUP);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to configure mem type %d\n",
				     memory->hw_type);

	ev_msg.num_evs = cpu_to_le32(NUM_GRP_EVS);
	ev_msg.hw_type = cpu_to_le32(memory->hw_type);
	for (i = 0; i < NUM_GRP_EVS; i++)
		ev_msg.cid[i] = cpu_to_le32(grp_ev[i]);

	ret = ops->set_param(info->ph, &ev_msg, sizeof(ev_msg), MEMLAT_ALGO_STR,
			     MEMLAT_SET_GRP_EV_MAP);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to configure event map for mem type %d\n",
				     memory->hw_type);

	return ret;
}

static int configure_cpucp_mon(struct device *dev, struct scmi_qcom_memlat_info *info,
			       int memory_index, int monitor_index)
{
	const struct qcom_generic_ext_ops *ops = info->ops;
	struct scmi_qcom_memory_info *memory = info->memory[memory_index];
	struct scmi_qcom_monitor_info *monitor = memory->monitor[monitor_index];
	struct scalar_param_msg scalar_msg = {};
	struct map_param_msg map_msg = {};
	struct node_msg msg = {};
	int ret;
	int i;

	msg.cpumask = cpu_to_le32(monitor->mask);
	msg.hw_type = cpu_to_le32(memory->hw_type);
	msg.mon_type = cpu_to_le32(monitor->mon_type);
	msg.mon_idx = cpu_to_le32(monitor->mon_idx);
	strscpy(msg.mon_name, monitor->name, sizeof(msg.mon_name));
	ret = ops->set_param(info->ph, &msg, sizeof(msg), MEMLAT_ALGO_STR, MEMLAT_SET_MONITOR);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to configure monitor %s\n",
				     monitor->name);

	scalar_msg.hw_type = cpu_to_le32(memory->hw_type);
	scalar_msg.mon_idx = cpu_to_le32(monitor->mon_idx);
	scalar_msg.val = cpu_to_le32(monitor->ipm_ceil);
	ret = ops->set_param(info->ph, &scalar_msg, sizeof(scalar_msg), MEMLAT_ALGO_STR,
			     MEMLAT_IPM_CEIL);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to set ipm ceil for %s\n",
				     monitor->name);

	scalar_msg.hw_type = cpu_to_le32(memory->hw_type);
	scalar_msg.mon_idx = cpu_to_le32(monitor->mon_idx);
	scalar_msg.val = cpu_to_le32(monitor->be_stall_floor);
	ret = ops->set_param(info->ph, &scalar_msg, sizeof(scalar_msg), MEMLAT_ALGO_STR,
			     MEMLAT_BE_STALL_FLOOR);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to set be_stall_floor for %s\n",
				     monitor->name);

	map_msg.hw_type = cpu_to_le32(memory->hw_type);
	map_msg.mon_idx = cpu_to_le32(monitor->mon_idx);
	map_msg.nr_rows = cpu_to_le32(monitor->freq_map_len);
	for (i = 0; i < monitor->freq_map_len; i++) {
		map_msg.tbl[i].v1 = cpu_to_le16(monitor->freq_map[i].cpufreq_mhz);

		/*
		 * Wire format v2 is u16 in MHz; convert from kHz. For DDR_QOS
		 * the table holds level indices (0 / 1) rather than real
		 * frequencies, so pass them through unchanged.
		 */
		if (monitor->freq_map[i].memfreq_khz > 1)
			map_msg.tbl[i].v2 = cpu_to_le16(monitor->freq_map[i].memfreq_khz / 1000);
		else
			map_msg.tbl[i].v2 = cpu_to_le16(monitor->freq_map[i].memfreq_khz);
	}
	ret = ops->set_param(info->ph, &map_msg, sizeof(map_msg), MEMLAT_ALGO_STR,
			     MEMLAT_MON_FREQ_MAP);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to configure freq_map for %s\n",
				     monitor->name);

	scalar_msg.hw_type = cpu_to_le32(memory->hw_type);
	scalar_msg.mon_idx = cpu_to_le32(monitor->mon_idx);
	scalar_msg.val = cpu_to_le32(memory->min_freq);
	ret = ops->set_param(info->ph, &scalar_msg, sizeof(scalar_msg), MEMLAT_ALGO_STR,
			     MEMLAT_SET_MIN_FREQ);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to set min_freq for %s\n",
				     monitor->name);

	scalar_msg.hw_type = cpu_to_le32(memory->hw_type);
	scalar_msg.mon_idx = cpu_to_le32(monitor->mon_idx);
	scalar_msg.val = cpu_to_le32(memory->max_freq);
	ret = ops->set_param(info->ph, &scalar_msg, sizeof(scalar_msg), MEMLAT_ALGO_STR,
			     MEMLAT_SET_MAX_FREQ);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to set max_freq for %s\n", monitor->name);

	return ret;
}

static int scmi_qcom_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct scmi_qcom_memory_info *memory = dev_get_drvdata(dev);
	const struct qcom_generic_ext_ops *ops = memory->ops;
	struct scalar_param_msg scalar_msg = {};
	u32 max_freq_khz = 0;
	__le32 cur_freq;
	int ret, i;

	/*
	 * MEMLAT_GET_CUR_FREQ returns target_freq for a single (hw_type,
	 * mon_idx) tuple. The bus's actual voted frequency is the max across
	 * all configured monitors in the group, so query each one and pick
	 * the highest vote.
	 */
	for (i = 0; i < memory->monitor_cnt; i++) {
		scalar_msg.hw_type = cpu_to_le32(memory->hw_type);
		scalar_msg.mon_idx = cpu_to_le32(memory->monitor[i]->mon_idx);
		scalar_msg.val = 0;

		ret = ops->get_param(memory->ph, &scalar_msg, sizeof(scalar_msg),
				     MEMLAT_ALGO_STR, MEMLAT_GET_CUR_FREQ,
				     sizeof(cur_freq));
		if (ret < 0) {
			dev_err(dev, "failed to get current frequency for %s\n",
				memory->monitor[i]->name);
			return ret;
		}

		/* qcom_scmi_common_xfer() returns the response into the same tx buffer. */
		memcpy(&cur_freq, &scalar_msg, sizeof(cur_freq));
		if (le32_to_cpu(cur_freq) > max_freq_khz)
			max_freq_khz = le32_to_cpu(cur_freq);
	}

	/*
	 * Frequency-scaled buses (DDR/LLCC) report cur_freq in kHz; convert
	 * to Hz to match the devfreq OPP table units. Level-based buses
	 * (e.g. DDR_QOS_COMPUTE) configure max_freq == 1 because the firmware
	 * reports a 0/1 level rather than a frequency, and the matching
	 * synthetic OPP keys (1 / 100) live in glymur_ddr_qos_table.
	 */
	if (memory->max_freq > 1)
		*freq = max_freq_khz * 1000UL;
	else
		*freq = max_freq_khz ? 100 : 1;

	return 0;
}

static void scmi_qcom_memlat_unwind(struct scmi_qcom_memlat_info *info, int count)
{
	for (int i = 0; i < count; i++) {
		struct scmi_qcom_memory_info *memory = info->memory[i];

		if (IS_ERR_OR_NULL(memory) || IS_ERR_OR_NULL(memory->pdev))
			continue;

		dev_pm_opp_remove_all_dynamic(&memory->pdev->dev);
		platform_device_unregister(memory->pdev);
	}
}

static int scmi_qcom_memlat_configure_events(struct scmi_device *sdev,
					     struct scmi_qcom_memlat_info *info)
{
	const struct qcom_generic_ext_ops *ops = info->ops;
	struct scmi_protocol_handle *ph = info->ph;
	__le32 sample_ms, freq_method;
	int i, j, ret;

	/* Configure common events ids */
	ret = configure_cpucp_common_events(info, info->cfg_data);
	if (ret < 0)
		return dev_err_probe(&sdev->dev, ret, "failed to configure common events\n");

	for (i = 0; i < info->memory_cnt; i++) {
		/* Configure per group parameters */
		ret = configure_cpucp_grp(&sdev->dev, info, info->cfg_data, i);
		if (ret < 0)
			return ret;

		for (j = 0; j < info->memory[i]->monitor_cnt; j++) {
			/* Configure per monitor parameters */
			ret = configure_cpucp_mon(&sdev->dev, info, i, j);
			if (ret < 0)
				return ret;
		}
	}

	/* Set loop sampling time */
	sample_ms = cpu_to_le32(info->cpucp_sample_ms);
	ret = ops->set_param(ph, &sample_ms, sizeof(sample_ms),
			     MEMLAT_ALGO_STR, MEMLAT_SAMPLE_MS);
	if (ret < 0)
		return dev_err_probe(&sdev->dev, ret, "failed to set sample_ms\n");

	/* Set the effective cpu frequency calculation method */
	freq_method = cpu_to_le32(info->cpucp_freq_method);
	ret = ops->set_param(ph, &freq_method, sizeof(freq_method),
			     MEMLAT_ALGO_STR, MEMLAT_SET_EFFECTIVE_FREQ_METHOD);
	if (ret < 0)
		return dev_err_probe(&sdev->dev, ret,
				     "failed to set effective frequency calc method\n");

	/* Start sampling and voting timer */
	ret = ops->start_activity(ph, NULL, 0, MEMLAT_ALGO_STR, MEMLAT_START_TIMER);
	if (ret < 0)
		return dev_err_probe(&sdev->dev, ret, "failed to start memory group timer\n");

	for (i = 0; i < info->memory_cnt; i++) {
		struct scmi_qcom_memory_info *memory = info->memory[i];
		struct platform_device *pdev = memory->pdev;
		struct devfreq_dev_profile *profile = &memory->profile;

		/* sampling time should be double the devfreq observing time */
		profile->polling_ms = max(1U, info->cpucp_sample_ms / 2);
		profile->get_cur_freq = scmi_qcom_devfreq_get_cur_freq;
		profile->initial_freq = memory->min_freq > 1 ?
					(memory->min_freq * 1000UL) : memory->min_freq;

		platform_set_drvdata(pdev, memory);

		memory->devfreq = devm_devfreq_add_device(&pdev->dev, profile,
							  DEVFREQ_GOV_REMOTE, NULL);
		if (IS_ERR(memory->devfreq)) {
			dev_err(&sdev->dev, "failed to add devfreq device\n");
			/* Stop sampling and voting timer */
			ret = ops->stop_activity(ph, NULL, 0, MEMLAT_ALGO_STR, MEMLAT_STOP_TIMER);
			if (ret < 0)
				dev_err_probe(&sdev->dev, ret,
					      "failed to stop memory group timer\n");
			return PTR_ERR(memory->devfreq);
		}
	}

	return 0;
}

static struct scmi_qcom_memlat_map *
scmi_qcom_parse_memlat_map(struct device *dev, const struct scmi_qcom_monitor_cfg *mon_cfg)
{
	struct scmi_qcom_memlat_map *map_table;
	const struct scmi_qcom_map_table *table;

	if (mon_cfg->table_len > MAX_MAP_ENTRIES)
		return ERR_PTR(-EINVAL);

	map_table = devm_kcalloc(dev, mon_cfg->table_len, sizeof(*map_table),
				 GFP_KERNEL);
	if (!map_table)
		return ERR_PTR(-ENOMEM);

	for (int i = 0; i < mon_cfg->table_len; i++) {
		table = &mon_cfg->table[i];

		map_table[i].cpufreq_mhz = table->cpu_freq;
		map_table[i].memfreq_khz = table->mem_freq;
	}

	return map_table;
}

static const struct of_device_id scmi_qcom_memlat_configs[] = {
	{ .compatible = "qcom,glymur", .data = &glymur_memlat_data},
	{ .compatible = "qcom,mahua", .data = &glymur_memlat_data},
	{ .compatible = "qcom,x1e80100", .data = &hamoa_memlat_data},
	{ .compatible = "qcom,x1p42100", .data = &hamoa_memlat_data},
	{ }
};

static int scmi_qcom_memlat_parse_cfg(struct scmi_device *sdev, struct scmi_qcom_memlat_info *info)
{
	const struct scmi_qcom_memlat_cfg_data *cfg_data;
	struct scmi_qcom_monitor_info *monitor;
	struct scmi_qcom_memory_info *memory;
	int ret, i, j;

	cfg_data = of_machine_get_match_data(scmi_qcom_memlat_configs);
	if (!cfg_data) {
		/*
		 * The SCMI generic-ext protocol can bind on Qualcomm SoCs that
		 * do not ship CPUCP memlat. Bail out quietly in that case rather
		 * than printing an error on every such system.
		 */
		dev_dbg(&sdev->dev, "no memlat config data for this platform\n");
		return -ENODEV;
	}

	info->memory = devm_kcalloc(&sdev->dev, cfg_data->memory_cnt,
				    sizeof(*info->memory), GFP_KERNEL);
	if (!info->memory)
		return -ENOMEM;

	for (i = 0; i < cfg_data->memory_cnt; i++) {
		const struct scmi_qcom_memory_cfg *memory_cfg = &cfg_data->memory_cfg[i];
		struct platform_device_info pdevinfo = { 0 };

		pdevinfo.parent = &sdev->dev;
		pdevinfo.name = memory_cfg->name;
		pdevinfo.id = PLATFORM_DEVID_NONE;

		memory = devm_kzalloc(&sdev->dev, sizeof(*memory), GFP_KERNEL);
		if (!memory)
			return -ENOMEM;

		memory->ops = info->ops;
		memory->ph = info->ph;
		memory->hw_type = memory_cfg->memory_type;
		memory->monitor_cnt = memory_cfg->monitor_cnt;
		memory->min_freq = memory_cfg->memory_range.min_freq;
		memory->max_freq = memory_cfg->memory_range.max_freq;

		memory->pdev = platform_device_register_full(&pdevinfo);
		if (IS_ERR(memory->pdev))
			return dev_err_probe(&sdev->dev, PTR_ERR(memory->pdev),
					     "failed to register platform device\n");

		info->memory[i] = memory;
		/* Track progress so probe() can unwind on a later failure. */
		info->memory_cnt = i + 1;

		for (j = 0; j < memory_cfg->num_opps; j++) {
			const struct scmi_qcom_opp_data *table = &memory_cfg->mem_table[j];
			struct platform_device *pdev = memory->pdev;
			struct dev_pm_opp_data data = {};

			data.freq = table->freq;
			data.level = table->level;

			ret = dev_pm_opp_add_dynamic(&pdev->dev, &data);
			if (ret)
				return dev_err_probe(&sdev->dev, ret, "failed to add OPP\n");
		}

		memory->monitor = devm_kcalloc(&sdev->dev, memory_cfg->monitor_cnt,
					       sizeof(*memory->monitor), GFP_KERNEL);
		if (!memory->monitor)
			return -ENOMEM;

		for (j = 0; j < memory_cfg->monitor_cnt; j++) {
			const struct scmi_qcom_monitor_cfg *mon_cfg = &memory_cfg->monitor_cfg[j];

			monitor = devm_kzalloc(&sdev->dev, sizeof(*monitor), GFP_KERNEL);
			if (!monitor)
				return -ENOMEM;

			monitor->ipm_ceil = mon_cfg->ipm_ceil;
			/* mon_type 0 = IPM-based latency monitor; 1 = stall-only (compute) */
			monitor->mon_type = monitor->ipm_ceil ? 0 : 1;
			monitor->be_stall_floor = mon_cfg->be_stall_floor;
			monitor->mask = mon_cfg->cpu_mask;
			monitor->freq_map_len = mon_cfg->table_len;

			monitor->freq_map = scmi_qcom_parse_memlat_map(&sdev->dev, mon_cfg);
			if (IS_ERR(monitor->freq_map))
				return dev_err_probe(&sdev->dev, PTR_ERR(monitor->freq_map),
						     "failed to populate cpufreq-memfreq map\n");

			strscpy(monitor->name, mon_cfg->name, sizeof(monitor->name));
			monitor->mon_idx = j;
			memory->monitor[j] = monitor;
		}
	}

	info->cfg_data = cfg_data;
	info->cpucp_freq_method = cfg_data->cpucp_freq_method;
	info->cpucp_sample_ms = cfg_data->cpucp_sample_ms;

	return 0;
}

static int scmi_qcom_devfreq_memlat_probe(struct scmi_device *sdev)
{
	const struct scmi_handle *handle = sdev->handle;
	const struct qcom_generic_ext_ops *ops;
	struct scmi_qcom_memlat_info *info;
	struct scmi_protocol_handle *ph;
	int ret;

	if (!handle)
		return -ENODEV;

	info = devm_kzalloc(&sdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_QCOM_GENERIC, &ph);
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	info->ops = ops;
	info->ph = ph;

	ret = scmi_qcom_memlat_parse_cfg(sdev, info);
	if (ret) {
		scmi_qcom_memlat_unwind(info, info->memory_cnt);
		return ret;
	}

	ret = scmi_qcom_memlat_configure_events(sdev, info);
	if (ret) {
		scmi_qcom_memlat_unwind(info, info->memory_cnt);
		return ret;
	}

	dev_set_drvdata(&sdev->dev, info);

	return ret;
}

static void scmi_qcom_devfreq_memlat_remove(struct scmi_device *sdev)
{
	struct scmi_qcom_memlat_info *info = dev_get_drvdata(&sdev->dev);
	struct scmi_protocol_handle *ph;
	const struct qcom_generic_ext_ops *ops;
	int ret;

	if (!info)
		return;

	ph = info->ph;
	ops = info->ops;

	ret = ops->stop_activity(ph, NULL, 0, MEMLAT_ALGO_STR, MEMLAT_STOP_TIMER);
	if (ret < 0)
		dev_err(&sdev->dev, "failed to stop memory group timer\n");

	scmi_qcom_memlat_unwind(info, info->memory_cnt);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_QCOM_GENERIC, "qcom-generic-ext" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_qcom_devfreq_memlat_driver = {
	.name		= "scmi-qcom-devfreq-memlat",
	.probe		= scmi_qcom_devfreq_memlat_probe,
	.remove		= scmi_qcom_devfreq_memlat_remove,
	.id_table	= scmi_id_table,
};
module_scmi_driver(scmi_qcom_devfreq_memlat_driver);

MODULE_AUTHOR("Pragnesh Papaniya <pragnesh.papaniya@oss.qualcomm.com>");
MODULE_DESCRIPTION("SCMI QCOM DEVFREQ MEMLAT driver");
MODULE_LICENSE("GPL");
