// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe link speed switch driver
 *
 * Adjusts the PCIe link rate of NVMe devices dynamically based on the I/O
 * workload: the link is downgraded to a configurable minimum rate during
 * periods of low activity to save power, and upgraded to the maximum
 * supported rate as soon as the workload requires it.
 *
 * Copyright (c) 2026 Hygon Information Technology Co., Ltd.
 * Copyright (c) 2026 Kylin Software Co., Ltd.
 *
 */

#include <linux/blk-mq.h>
#include <linux/math64.h>
#include <linux/pci.h>
#include <linux/percpu.h>
#include <linux/processor.h>

#include "nvme.h"

/*
 * PCIe link bandwidth in KB/s for each generation (rows, Gen1..Gen5) and
 * lane width (columns, x1..x16).  The table is used to derive the I/O
 * threshold exceed/below which the link can be upgraded/downgraded.
 */
static const u32 nvme_speed_table[][5] = {
	{  312,  625, 1250, 2500, 5000 },   /* Gen1 */
	{  625, 1250, 2500, 5000, 10000 },  /* Gen2 */
	{ 1000, 2000, 4000, 8000, 16000 },  /* Gen3 */
	{ 2000, 4000, 8000, 16000, 32000 }, /* Gen4 */
	{ 4000, 8000, 16000, 32000, 64000 },/* Gen5 */
};

static int nvme_bandwidth_index(enum pcie_link_width width)
{
	switch (width) {
	case PCIE_LNK_X1:
		return 0;
	case PCIE_LNK_X2:
		return 1;
	case PCIE_LNK_X4:
		return 2;
	case PCIE_LNK_X8:
		return 3;
	case PCIE_LNK_X16:
		return 4;
	default:
		return -EINVAL;
	}
}

/*
 * Read the maximum supported link rate of the upstream port from the Link
 * Capability register.
 */
static int nvme_get_max_link_speed(struct pci_dev *pdev)
{
	u16 linkcap;

	pcie_capability_read_word(pdev, PCI_EXP_LNKCAP, &linkcap);

	return linkcap & PCI_EXP_LNKCAP_SLS;
}

static enum pci_bus_speed nvme_speed_to_bus_speed(u8 gen)
{
	return gen + PCIE_SPEED_2_5GT - 1;
}

/*
 * Work item that actually changes the link rate.  The queues are frozen
 * before retraining to quiesce I/O and resumed once the switch completed.
 */
static void nvme_speed_switch_work(struct work_struct *work)
{
	struct nvme_speed_switch *sw = container_of(work, struct nvme_speed_switch, work);
	struct nvme_ctrl *ctrl = container_of(sw, struct nvme_ctrl, speed_switch);
	struct pci_dev *pdev = to_pci_dev(ctrl->dev);
	struct pci_dev *bridge = pdev->bus->self;
	u8 cur_speed = READ_ONCE(sw->cur_speed);
	int ret;

	if (nvme_ctrl_state(ctrl) != NVME_CTRL_LIVE)
		return;

	/* Freeze I/O to avoid timeouts during link retraining. */
	nvme_start_freeze(ctrl);
	nvme_wait_freeze(ctrl);

	ret = pcie_set_target_speed(bridge, nvme_speed_to_bus_speed(sw->target_speed),
				    true);
	if (ret) {
		dev_warn(ctrl->device,
			 "failed to set target rate Gen%u (%d), trying rollback to Gen%u\n",
			 sw->target_speed, ret, cur_speed);
		ret = pcie_set_target_speed(bridge, nvme_speed_to_bus_speed(cur_speed),
					    true);
		if (ret) {
			dev_err(ctrl->device,
				"rollback to Gen%u failed (%d), disabling speed switch\n",
				cur_speed, ret);
			WRITE_ONCE(sw->enabled, false);
			nvme_unfreeze(ctrl);
			return;
		}
	} else {
		WRITE_ONCE(sw->cur_speed, sw->target_speed);
	}

	nvme_unfreeze(ctrl);
	dev_dbg(ctrl->device, "link rate changed to Gen%u\n",
		READ_ONCE(sw->cur_speed));
}

/*
 * Compute the I/O threshold (in KB) for a monitoring window.
 *
 * The threshold is derived from the following constraint: for the link
 * speed to be upgraded, the transfer time of the data at the minimum
 * speed must be greater than the sum of the transfer time at maximum
 * speed and the link retraining time (100us):
 *
 *	data / min_bw >= data / max_bw + 100us
 *
 * which yields data <= min_bw * max_bw * 0.1ms / (max_bw - min_bw).
 * The bandwidths are taken from the link capability of the upstream port
 * and the link width capability of the device.
 */
static u32 nvme_calc_speed_threshold(struct nvme_speed_switch *sw,
				     enum pcie_link_width max_width)
{
	struct nvme_ctrl *ctrl = container_of(sw, struct nvme_ctrl, speed_switch);
	int bw_idx, min_gen = sw->min_speed, max_gen = sw->max_speed;
	u64 min_bw, max_bw, x, y;
	u32 threshold;

	if (min_gen < 1 || min_gen > 5 || max_gen < 1 || max_gen > 5) {
		dev_warn(ctrl->device,
			 "invalid link rate range for threshold: min=Gen%d, max=Gen%d\n",
			 min_gen, max_gen);
		return 0;
	}

	bw_idx = nvme_bandwidth_index(max_width);
	if (bw_idx < 0) {
		dev_warn(ctrl->device,
			 "unsupported link width (%d) for threshold calculation\n",
			 max_width);
		return 0;
	}

	min_bw = nvme_speed_table[min_gen - 1][bw_idx];
	max_bw = nvme_speed_table[max_gen - 1][bw_idx];
	if (min_bw >= max_bw) {
		dev_warn(ctrl->device,
			 "invalid bandwidth for threshold: min=%llu KB/s, max=%llu KB/s\n",
			 min_bw, max_bw);
		return 0;
	}

	x = min_bw * max_bw;
	y = 10ULL * (max_bw - min_bw);
	threshold = div64_u64(x + (y >> 1), y);

	dev_dbg(ctrl->device,
		"I/O threshold: %u KB (min=Gen%d, max=Gen%d, width=x%d)\n",
		threshold, min_gen, max_gen, max_width);

	return threshold;
}

/* Called on the I/O submission path to accumulate the transferred bytes. */
void nvme_update_io_stats(struct nvme_ctrl *ctrl, struct request *req)
{
	struct nvme_speed_switch *sw = &ctrl->speed_switch;
	struct nvme_speed_switch_stats *stat;
	enum req_op op = req_op(req);
	unsigned int bytes = blk_rq_bytes(req);

	if (!READ_ONCE(sw->enabled))
		return;

	if (op != REQ_OP_READ && op != REQ_OP_WRITE)
		return;

	if (!sw->stats) {
		dev_dbg(ctrl->device, "I/O statistics not allocated, skip accounting\n");
		return;
	}

	stat = get_cpu_ptr(sw->stats);
	if (op == REQ_OP_READ)
		stat->read_bytes += bytes;
	else
		stat->write_bytes += bytes;
	put_cpu_ptr(sw->stats);

	if (atomic_cmpxchg(&sw->timer_active, NVME_SPEED_TIMER_INACTIVE,
			   NVME_SPEED_TIMER_ACTIVE) != NVME_SPEED_TIMER_INACTIVE)
		return;

	/* First I/O of the activity period: compute the threshold and arm the timer. */
	WRITE_ONCE(sw->threshold, nvme_calc_speed_threshold(sw,
					pcie_get_width_cap(to_pci_dev(ctrl->dev))));
	if (!READ_ONCE(sw->threshold)) {
		dev_warn(ctrl->device,
			 "failed to compute speed switch threshold, keeping current rate\n");
		atomic_set(&sw->timer_active, NVME_SPEED_TIMER_INACTIVE);
		return;
	}
	mod_timer(&sw->timer, jiffies + msecs_to_jiffies(READ_ONCE(sw->monitor_interval)));
}
EXPORT_SYMBOL_GPL(nvme_update_io_stats);

static void nvme_clear_io_stats(struct nvme_speed_switch *sw)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct nvme_speed_switch_stats *stat = per_cpu_ptr(sw->stats, cpu);

		stat->read_bytes = 0;
		stat->write_bytes = 0;
	}
}

/*
 * Aggregate the per-CPU I/O counters of the last monitoring window and
 * decide the target rate.  Hysteresis counters are used to avoid
 * excessive rate switching when the workload fluctuates around the
 * threshold: the rate is only upgraded after the threshold was exceeded
 * for a number of consecutive windows, and only downgraded after it
 * stayed below the threshold for a number of consecutive windows.  When
 * no I/O was observed for 10 seconds the monitoring is stopped.
 */
static int nvme_check_io_and_decide_speed(struct nvme_speed_switch *sw,
					  bool *io_activity)
{
	struct nvme_ctrl *ctrl = container_of(sw, struct nvme_ctrl, speed_switch);
	u64 read_bytes = 0, write_bytes = 0;
	unsigned long read_kb, write_kb;
	int cpu;

	for_each_possible_cpu(cpu) {
		struct nvme_speed_switch_stats *stat = per_cpu_ptr(sw->stats, cpu);

		read_bytes += stat->read_bytes;
		write_bytes += stat->write_bytes;
		stat->read_bytes = 0;
		stat->write_bytes = 0;
	}

	if (read_bytes || write_bytes) {
		*io_activity = true;
		sw->idle_cnt = 0;
	} else {
		sw->idle_cnt++;
	}

	/* Stop monitoring after 10 seconds of inactivity (100 windows of 100ms). */
	if (sw->idle_cnt >= 100)
		*io_activity = false;

	read_kb = read_bytes / 1024;
	write_kb = write_bytes / 1024;
	dev_dbg(ctrl->device,
		"I/O in window: read=%lu KB, write=%lu KB, threshold=%u KB\n",
		read_kb, write_kb, READ_ONCE(sw->threshold));

	if (read_kb >= sw->threshold || write_kb >= sw->threshold) {
		sw->up_cnt++;
		sw->down_cnt = 0;
	} else {
		sw->down_cnt++;
		sw->up_cnt = 0;
	}

	if (sw->up_cnt > READ_ONCE(sw->up_threshold)) {
		sw->up_cnt = 0;
		return sw->max_speed;
	}

	if (sw->down_cnt > READ_ONCE(sw->down_threshold) || !*io_activity) {
		sw->down_cnt = 0;
		return sw->min_speed;
	}

	return READ_ONCE(sw->cur_speed);
}

static void nvme_speed_switch_timer_fn(struct timer_list *t)
{
	struct nvme_speed_switch *sw = container_of(t, struct nvme_speed_switch, timer);
	bool io_activity = true;

	if (!READ_ONCE(sw->enabled)) {
		atomic_set(&sw->timer_active, NVME_SPEED_TIMER_INACTIVE);
		return;
	}

	sw->target_speed = nvme_check_io_and_decide_speed(sw, &io_activity);
	if (sw->target_speed != READ_ONCE(sw->cur_speed))
		schedule_work(&sw->work);

	if (!io_activity) {
		atomic_set(&sw->timer_active, NVME_SPEED_TIMER_INACTIVE);
		return;
	}
	mod_timer(t, jiffies + msecs_to_jiffies(READ_ONCE(sw->monitor_interval)));
}

static void nvme_speed_switch_params_init(struct nvme_speed_switch *sw)
{
	sw->monitor_interval = 100;
	sw->min_speed = PCI_EXP_LNKSTA_CLS_2_5GB;
	/* Upgrade immediately once the threshold is exceeded. */
	sw->up_threshold = 0;
	/* Downgrade after 10 consecutive below-threshold windows. */
	sw->down_threshold = 10;
}

void nvme_speed_switch_init(struct nvme_ctrl *ctrl)
{
	struct nvme_speed_switch *sw = &ctrl->speed_switch;
	struct pci_dev *pdev;
	int max_speed, ret;

	if (!dev_is_pci(ctrl->dev)) {
		dev_err(ctrl->device, "link rate switching requires a PCI device\n");
		return;
	}

	pdev = to_pci_dev(ctrl->dev);
	if (!pdev->bus->self) {
		dev_err(ctrl->device,
			"link rate switching requires a PCIe upstream port\n");
		return;
	}

	nvme_speed_switch_params_init(sw);

	max_speed = nvme_get_max_link_speed(pdev->bus->self);
	if (max_speed <= sw->min_speed) {
		dev_err(ctrl->device,
			"invalid link rate range: min=Gen%u, max=Gen%d\n",
			sw->min_speed, max_speed);
		return;
	}
	sw->max_speed = max_speed;

	sw->stats = alloc_percpu(struct nvme_speed_switch_stats);
	if (!sw->stats) {
		dev_err(ctrl->device, "failed to allocate per-CPU I/O statistics\n");
		return;
	}
	nvme_clear_io_stats(sw);

	WRITE_ONCE(sw->cur_speed, sw->max_speed);
	WRITE_ONCE(sw->threshold, nvme_calc_speed_threshold(sw, pcie_get_width_cap(pdev)));
	if (!READ_ONCE(sw->threshold)) {
		dev_err(ctrl->device, "failed to compute speed switch threshold\n");
		free_percpu(sw->stats);
		sw->stats = NULL;
		return;
	}

	INIT_WORK(&sw->work, nvme_speed_switch_work);
	timer_setup(&sw->timer, nvme_speed_switch_timer_fn, 0);
	atomic_set(&sw->timer_active, NVME_SPEED_TIMER_INACTIVE);
	WRITE_ONCE(sw->enabled, true);

	ret = sysfs_create_group(&ctrl->device->kobj, &nvme_speed_attr_group);
	if (ret) {
		dev_err(ctrl->device,
			"failed to create link rate switching sysfs group (%d)\n",
			ret);
		timer_delete_sync(&sw->timer);
		free_percpu(sw->stats);
		sw->stats = NULL;
		WRITE_ONCE(sw->enabled, false);
		return;
	}
	sw->initialized = true;

	dev_info(ctrl->device,
		 "NVMe link rate switching initialized (min Gen%u, max Gen%d)\n",
		 sw->min_speed, sw->max_speed);
}

void nvme_speed_switch_start(struct nvme_ctrl *ctrl)
{
	struct nvme_speed_switch *sw = &ctrl->speed_switch;

	if (!READ_ONCE(sw->enabled))
		return;

	atomic_set(&sw->timer_active, NVME_SPEED_TIMER_ACTIVE);
	mod_timer(&sw->timer, jiffies + msecs_to_jiffies(READ_ONCE(sw->monitor_interval)));
}

void nvme_speed_switch_exit(struct nvme_ctrl *ctrl)
{
	struct nvme_speed_switch *sw = &ctrl->speed_switch;

	if (!sw->initialized)
		return;

	sw->initialized = false;
	WRITE_ONCE(sw->enabled, false);
	timer_delete_sync(&sw->timer);
	cancel_work_sync(&sw->work);
	sysfs_remove_group(&ctrl->device->kobj, &nvme_speed_attr_group);

	free_percpu(sw->stats);
	sw->stats = NULL;
}
