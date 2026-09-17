// SPDX-License-Identifier: GPL-2.0+
/*
 * PCIe bandwidth controller
 *
 * Author: Alexandru Gagniuc <mr.nuke.me@gmail.com>
 *
 * Copyright (C) 2019 Dell Inc
 * Copyright (C) 2023-2024 Intel Corporation
 *
 * The PCIe bandwidth controller provides a way to alter PCIe Link Speeds
 * and notify the operating system when the Link Width or Speed changes. The
 * notification capability is required for all Root Ports and Downstream
 * Ports supporting Link Width wider than x1 and/or multiple Link Speeds.
 *
 * This service port driver hooks into the Bandwidth Notification interrupt
 * watching for changes or links becoming degraded in operation. It updates
 * the cached Current Link Speed that is exposed to user space through sysfs.
 */

#define dev_fmt(fmt) "bwctrl: " fmt

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/devfreq.h>
#include <linux/errno.h>
#include <linux/find.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pci-bwctrl.h>
#include <linux/pm_opp.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "../pci.h"
#include "portdrv.h"

/**
 * struct pcie_bwctrl_data - PCIe bandwidth controller
 * @set_speed_mutex:	Serializes link speed changes
 * @cdev:		Thermal cooling device associated with the port
 * @devfreq:		On-demand Link Speed scaling devfreq device, if enabled
 * @profile:		devfreq profile backing @devfreq
 * @ondemand_data:	simple_ondemand governor tuning backing @devfreq
 * @freq_table:		Link Speeds usable as devfreq frequencies
 * @nr_freqs:		Number of valid entries in @freq_table (and matching OPPs)
 * @activity_lock:	Serializes @bytes_in_flight/@bytes_completed_total/@window_start_t
 * @bytes_in_flight:	Sum of currently-outstanding downstream request sizes
 * @bytes_completed_total:	Cumulative bytes completed since registration, never reset
 * @window_start_t:	Start of the current polling window
 * @last_agg_total:	Snapshot of the aggregated total as of the last poll
 * @last_bytes:		Bytes completed in the most recently closed window (for logging)
 * @last_elapsed_us:	Duration of the most recently closed window (for logging)
 * @last_capacity_mbps:	Link capacity during the most recently closed window (for logging)
 * @last_change_t:	Time of the most recent Link Speed change, for the dwell-time guard
 * @held_for_optin:	Set once "holding, not opted in" has been logged, until it clears
 */
struct pcie_bwctrl_data {
	struct mutex set_speed_mutex;
	struct thermal_cooling_device *cdev;
#ifdef CONFIG_PCIE_BW_ONDEMAND
	struct devfreq *devfreq;
	struct devfreq_dev_profile profile;
	struct devfreq_simple_ondemand_data ondemand_data;
	unsigned long freq_table[BITS_PER_TYPE(u8)];
	unsigned int nr_freqs;

	spinlock_t activity_lock;
	s64 bytes_in_flight;
	u64 bytes_completed_total;
	ktime_t window_start_t;
	u64 last_agg_total;

	u64 last_bytes;
	u64 last_elapsed_us;
	u32 last_capacity_mbps;
	ktime_t last_change_t;
	bool held_for_optin;
#endif
};

/* Prevent port removal during Link Speed changes. */
static DECLARE_RWSEM(pcie_bwctrl_setspeed_rwsem);

static bool pcie_valid_speed(enum pci_bus_speed speed)
{
	return (speed >= PCIE_SPEED_2_5GT) && (speed <= PCIE_SPEED_64_0GT);
}

static u16 pci_bus_speed2lnkctl2(enum pci_bus_speed speed)
{
	static const u8 speed_conv[] = {
		[PCIE_SPEED_2_5GT] = PCI_EXP_LNKCTL2_TLS_2_5GT,
		[PCIE_SPEED_5_0GT] = PCI_EXP_LNKCTL2_TLS_5_0GT,
		[PCIE_SPEED_8_0GT] = PCI_EXP_LNKCTL2_TLS_8_0GT,
		[PCIE_SPEED_16_0GT] = PCI_EXP_LNKCTL2_TLS_16_0GT,
		[PCIE_SPEED_32_0GT] = PCI_EXP_LNKCTL2_TLS_32_0GT,
		[PCIE_SPEED_64_0GT] = PCI_EXP_LNKCTL2_TLS_64_0GT,
	};

	if (WARN_ON_ONCE(!pcie_valid_speed(speed)))
		return 0;

	return speed_conv[speed];
}

static inline u16 pcie_supported_speeds2target_speed(u8 supported_speeds)
{
	return __fls(supported_speeds);
}

/**
 * pcie_bwctrl_select_speed - Select Target Link Speed
 * @port:	PCIe Port
 * @speed_req:	Requested PCIe Link Speed
 *
 * Select Target Link Speed by take into account Supported Link Speeds of
 * both the Root Port and the Endpoint.
 *
 * Return: Target Link Speed (1=2.5GT/s, 2=5GT/s, 3=8GT/s, etc.)
 */
static u16 pcie_bwctrl_select_speed(struct pci_dev *port, enum pci_bus_speed speed_req)
{
	struct pci_bus *bus = port->subordinate;
	u8 desired_speeds, supported_speeds;
	struct pci_dev *dev;

	desired_speeds = GENMASK(pci_bus_speed2lnkctl2(speed_req),
				 __fls(PCI_EXP_LNKCAP2_SLS_2_5GB));

	supported_speeds = port->supported_speeds;
	if (bus) {
		down_read(&pci_bus_sem);
		dev = list_first_entry_or_null(&bus->devices, struct pci_dev, bus_list);
		if (dev)
			supported_speeds &= dev->supported_speeds;
		up_read(&pci_bus_sem);
	}
	if (!supported_speeds)
		supported_speeds = PCI_EXP_LNKCAP2_SLS_2_5GB;

	return pcie_supported_speeds2target_speed(supported_speeds & desired_speeds);
}

static int pcie_bwctrl_change_speed(struct pci_dev *port, u16 target_speed, bool use_lt)
{
	int ret;

	ret = pcie_capability_clear_and_set_word(port, PCI_EXP_LNKCTL2,
						 PCI_EXP_LNKCTL2_TLS, target_speed);
	if (ret != PCIBIOS_SUCCESSFUL)
		return pcibios_err_to_errno(ret);

	return pcie_retrain_link(port, use_lt);
}

/**
 * pcie_set_target_speed - Set downstream Link Speed for PCIe Port
 * @port:	PCIe Port
 * @speed_req:	Requested PCIe Link Speed
 * @use_lt:	Wait for the LT or DLLLA bit to detect the end of link training
 *
 * Attempt to set PCIe Port Link Speed to @speed_req. @speed_req may be
 * adjusted downwards to the best speed supported by both the Port and PCIe
 * Device underneath it.
 *
 * Return:
 * * 0		- on success
 * * -EINVAL	- @speed_req is not a PCIe Link Speed
 * * -ENODEV	- @port is not controllable
 * * -ETIMEDOUT	- changing Link Speed took too long
 * * -EAGAIN	- Link Speed was changed but @speed_req was not achieved
 */
int pcie_set_target_speed(struct pci_dev *port, enum pci_bus_speed speed_req,
			  bool use_lt)
{
	struct pci_bus *bus = port->subordinate;
	u16 target_speed;
	int ret;

	if (WARN_ON_ONCE(!pcie_valid_speed(speed_req)))
		return -EINVAL;

	if (bus && bus->cur_bus_speed == speed_req)
		return 0;

	target_speed = pcie_bwctrl_select_speed(port, speed_req);

	scoped_guard(rwsem_read, &pcie_bwctrl_setspeed_rwsem) {
		struct pcie_bwctrl_data *data = port->link_bwctrl;

		/*
		 * port->link_bwctrl is NULL during initial scan when called
		 * e.g. from the Target Speed quirk.
		 */
		if (data)
			mutex_lock(&data->set_speed_mutex);

		ret = pcie_bwctrl_change_speed(port, target_speed, use_lt);

		if (data)
			mutex_unlock(&data->set_speed_mutex);
	}

	/*
	 * Despite setting higher speed into the Target Link Speed, empty
	 * bus won't train to 5GT+ speeds.
	 */
	if (!ret && bus && bus->cur_bus_speed != speed_req &&
	    !list_empty(&bus->devices))
		ret = -EAGAIN;

	return ret;
}
EXPORT_SYMBOL_GPL(pcie_set_target_speed);

#ifdef CONFIG_PCIE_BW_ONDEMAND
static enum pci_bus_speed pcie_bwctrl_cur_speed(struct pci_dev *port)
{
	struct pci_bus *bus = port->subordinate;

	return bus ? bus->cur_bus_speed : PCI_SPEED_UNKNOWN;
}

/*
 * Current negotiated link capacity in Mb/s (encoded bandwidth, i.e. link
 * signaling rate reduced by 128b/130b or 8b/10b encoding overhead, times
 * negotiated width) -- the real achievable throughput ceiling at the link's
 * current speed, not the raw signaling rate.
 */
static u32 pcie_bwctrl_link_capacity_mbps(struct pci_dev *port)
{
	enum pci_bus_speed speed = pcie_bwctrl_cur_speed(port);
	u16 lnksta;
	u32 width;
	int ret;

	ret = pcie_capability_read_word(port, PCI_EXP_LNKSTA, &lnksta);
	if (ret != PCIBIOS_SUCCESSFUL) {
		pci_err(port, "bwctrl: failed to read LNKSTA for capacity calc: %d\n",
			ret);
		return 0;
	}

	width = FIELD_GET(PCI_EXP_LNKSTA_NLW, lnksta);

	return width * PCIE_SPEED2MBS_ENC(speed);
}

/*
 * Running totals gathered while walking a subtree: @completed_total sums
 * cumulative completed bytes (see pcie_bwctrl_note_activity()), and
 * @in_flight sums bytes currently outstanding (submitted, not yet
 * completed) across every descendant bridge.
 */
struct pcie_bwctrl_agg {
	u64 completed_total;
	s64 in_flight;
};

/*
 * Sum bytes_completed_total and bytes_in_flight across every bwctrl-tracked
 * bridge found beneath the walked bus. Each endpoint's traffic is credited
 * exactly once, at its immediate upstream bridge (see
 * pcie_bwctrl_note_activity()), so summing across the whole subtree --
 * however deep -- picks up that credit exactly once without needing
 * per-child bookkeeping at every intermediate level.
 */
static int pcie_bwctrl_agg_cb(struct pci_dev *dev, void *userdata)
{
	struct pcie_bwctrl_agg *agg = userdata;
	struct pcie_bwctrl_data *cdata;

	if (!pci_is_bridge(dev))
		return 0;

	cdata = READ_ONCE(dev->link_bwctrl);
	if (!cdata)
		return 0;

	scoped_guard(spinlock_irqsave, &cdata->activity_lock) {
		agg->completed_total += cdata->bytes_completed_total;
		agg->in_flight += cdata->bytes_in_flight;
	}

	return 0;
}

/*
 * Aggregate bytes completed and bytes in flight for @port itself plus
 * every descendant bridge reachable from @port's subordinate bus. Runs
 * from devfreq_monitor()'s workqueue context (holding devfreq->lock, a
 * mutex), so it is safe to sleep here -- pci_walk_bus() takes
 * down_read(&pci_bus_sem).
 */
static void pcie_bwctrl_aggregate(struct pci_dev *port, struct pcie_bwctrl_data *data,
				  struct pcie_bwctrl_agg *agg)
{
	scoped_guard(spinlock_irqsave, &data->activity_lock) {
		agg->completed_total = data->bytes_completed_total;
		agg->in_flight = data->bytes_in_flight;
	}

	if (port->subordinate)
		pci_walk_bus(port->subordinate, pcie_bwctrl_agg_cb, agg);
}

static int pcie_bwctrl_devfreq_get_status(struct device *dev,
					  struct devfreq_dev_status *stat)
{
	struct pci_dev *port = to_pci_dev(dev);
	struct pcie_bwctrl_data *data = port->link_bwctrl;
	struct pcie_bwctrl_agg agg;
	u64 elapsed_us, bytes, capacity_bytes, credited_in_flight;
	u32 capacity_mbps;
	ktime_t curr_t;

	memset(stat, 0, sizeof(*stat));

	if (!data)
		return -ENODEV;

	pcie_bwctrl_aggregate(port, data, &agg);

	scoped_guard(spinlock_irqsave, &data->activity_lock) {
		curr_t = ktime_get();
		if (!data->window_start_t) {
			data->window_start_t = curr_t;
			data->last_agg_total = agg.completed_total;
			return 0;
		}

		elapsed_us = ktime_us_delta(curr_t, data->window_start_t);
		bytes = agg.completed_total - data->last_agg_total;

		data->window_start_t = curr_t;
		data->last_agg_total = agg.completed_total;
	}

	capacity_mbps = pcie_bwctrl_link_capacity_mbps(port);

	stat->current_frequency = pcie_bwctrl_cur_speed(port);
	stat->total_time = (u64)capacity_mbps * elapsed_us;

	/*
	 * A request that stays outstanding across an entire poll window
	 * (e.g. a large transfer that takes longer than polling_ms to
	 * complete) contributes nothing to @bytes above until it finally
	 * completes -- which both hides the link being busy while it's
	 * still in flight, and produces an oversized spike in whichever
	 * window the completion happens to land in. Credit each window
	 * with however much of the link's own capacity is still
	 * outstanding, capped at that window's real capacity so this can
	 * never claim more throughput than physically possible.
	 */
	capacity_bytes = stat->total_time / 8;
	credited_in_flight = min_t(u64, max_t(s64, agg.in_flight, 0), capacity_bytes);

	stat->busy_time = min_t(u64, (bytes + credited_in_flight) * 8, stat->total_time);

	scoped_guard(spinlock_irqsave, &data->activity_lock) {
		data->last_bytes = bytes;
		data->last_elapsed_us = elapsed_us;
		data->last_capacity_mbps = capacity_mbps;
	}

	return 0;
}

/*
 * Minimum time a newly-reached Link Speed must be held before scaling back
 * down. Sustained throughput that sits between two adjacent speeds'
 * capacities (over-saturating the lower one, under-using the higher one)
 * otherwise makes simple_ondemand flip-flop every poll, and each flip costs
 * a real link retrain. Scaling up is never held back by this guard, since
 * that would risk starving a real burst.
 */
#define PCIE_BWCTRL_DOWNSCALE_DWELL_MS	1000

/*
 * Look for any endpoint beneath the walked bus that has not opted into
 * bwctrl bandwidth scaling. Bridges are skipped -- only leaf (non-bridge)
 * devices are required to opt in, since a bridge itself has no traffic of
 * its own to gate on. Returning non-zero stops the walk early.
 */
static int pcie_bwctrl_optin_cb(struct pci_dev *dev, void *userdata)
{
	if (pci_is_bridge(dev))
		return 0;

	return dev->bwctrl_participate ? 0 : 1;
}

/*
 * True only if every endpoint beneath @port's subordinate bus has called
 * pcie_bwctrl_register(). A port with no subordinate bus, or an empty one,
 * trivially has nothing that could withhold consent.
 */
static bool pcie_bwctrl_all_opted_in(struct pci_dev *port)
{
	int not_opted_in = 0;

	if (port->subordinate)
		pci_walk_bus(port->subordinate, pcie_bwctrl_optin_cb, &not_opted_in);

	return !not_opted_in;
}

/*
 * Clamp a governor-requested scale-DOWN to at most one freq_table step
 * below the current speed, so idle-driven decisions retrain one generation
 * at a time rather than dropping straight to the slowest supported speed.
 * Scale-up is intentionally left unclamped: simple_ondemand's up-scale
 * requests DEVFREQ_MAX_FREQ once busy_time crosses upthreshold, and real
 * sustained traffic should reach a speed that can actually accommodate it
 * immediately, not climb through intermediate generations first.
 */
static enum pci_bus_speed pcie_bwctrl_clamp_step(struct pcie_bwctrl_data *data,
						 enum pci_bus_speed cur_speed,
						 enum pci_bus_speed speed_req)
{
	int cur_idx = -1, req_idx = -1;

	if (speed_req >= cur_speed)
		return speed_req;

	for (unsigned int i = 0; i < data->nr_freqs; i++) {
		if (data->freq_table[i] == cur_speed)
			cur_idx = i;
		if (data->freq_table[i] == speed_req)
			req_idx = i;
	}

	/* Have not observed the current speed in the table; do not clamp. */
	if (cur_idx < 0)
		return speed_req;

	if (req_idx >= 0 && req_idx < cur_idx - 1)
		return data->freq_table[cur_idx - 1];

	return speed_req;
}

static int pcie_bwctrl_devfreq_target(struct device *dev, unsigned long *freq,
				      u32 flags)
{
	struct pci_dev *port = to_pci_dev(dev);
	struct pcie_bwctrl_data *data = port->link_bwctrl;
	enum pci_bus_speed speed_req = (enum pci_bus_speed)*freq;
	enum pci_bus_speed cur_speed = pcie_bwctrl_cur_speed(port);
	int ret = 0;

	if (data)
		speed_req = pcie_bwctrl_clamp_step(data, cur_speed, speed_req);

	if (data && speed_req < cur_speed) {
		s64 since_change_ms;

		scoped_guard(spinlock_irqsave, &data->activity_lock)
			since_change_ms = data->last_change_t ?
				ktime_ms_delta(ktime_get(), data->last_change_t) :
				S64_MAX;

		if (since_change_ms < PCIE_BWCTRL_DOWNSCALE_DWELL_MS)
			speed_req = cur_speed;
	}

	if (speed_req != cur_speed && data && !pcie_bwctrl_all_opted_in(port)) {
		if (!data->held_for_optin) {
			pci_dbg(port,
				"bwctrl: holding at %u, not every endpoint has opted in\n",
				cur_speed);
			data->held_for_optin = true;
		}
		speed_req = cur_speed;
	} else if (data) {
		data->held_for_optin = false;
	}

	if (speed_req != cur_speed) {
		u64 last_bytes = 0, last_elapsed_us = 0;
		u32 last_capacity_mbps = 0;

		if (data) {
			scoped_guard(spinlock_irqsave, &data->activity_lock) {
				last_bytes = data->last_bytes;
				last_elapsed_us = data->last_elapsed_us;
				last_capacity_mbps = data->last_capacity_mbps;
			}
		}

		pci_dbg(port,
			"bwctrl: speed change %u -> %u (last window: %llu bytes / %llu us, capacity %u Mb/s)\n",
			cur_speed, speed_req, last_bytes, last_elapsed_us,
			last_capacity_mbps);

		ret = pcie_set_target_speed(port, speed_req, true);
		if (ret && ret != -EAGAIN)
			pci_err(port, "failed to set link speed to %u: %d\n",
				speed_req, ret);
		if (ret == -EAGAIN)
			ret = 0;

		if (data) {
			scoped_guard(spinlock_irqsave, &data->activity_lock)
				data->last_change_t = ktime_get();
		}
	}

	*freq = pcie_bwctrl_cur_speed(port);

	return ret;
}

static int pcie_bwctrl_devfreq_get_cur_freq(struct device *dev,
					    unsigned long *freq)
{
	*freq = pcie_bwctrl_cur_speed(to_pci_dev(dev));

	return 0;
}

/**
 * pcie_bwctrl_note_activity - Report I/O byte transitions to bwctrl
 * @pdev:	Downstream PCIe device (e.g. an NVMe controller's own pci_dev)
 * @bytes:	Positive request size at submit, negative (matching) size at completion
 *
 * May be called from IRQ/atomic context (e.g. NVMe completion handlers), so
 * this must not sleep: unlike pcie_set_target_speed(), it does not take
 * pcie_bwctrl_setspeed_rwsem. @port->link_bwctrl is only ever cleared (never
 * freed) while holding that rwsem for writing, and @data itself is
 * devm-allocated against the bwctrl service device, so a lock-free read here
 * is safe -- the worst case is observing a stale non-NULL @data pointer
 * whose ->devfreq is already NULL, which the check below turns into a no-op.
 *
 * See the kdoc in <linux/pci-bwctrl.h> for the calling convention.
 */
void pcie_bwctrl_note_activity(struct pci_dev *pdev, s64 bytes)
{
	struct pci_dev *port = pci_upstream_bridge(pdev);
	struct pcie_bwctrl_data *data;

	if (!port)
		return;

	data = READ_ONCE(port->link_bwctrl);
	if (!data)
		return;

	guard(spinlock_irqsave)(&data->activity_lock);

	if (!data->devfreq)
		return;

	data->bytes_in_flight += bytes;

	if (bytes < 0)
		data->bytes_completed_total += -bytes;
}
EXPORT_SYMBOL_GPL(pcie_bwctrl_note_activity);

/**
 * pcie_bwctrl_register - Opt an endpoint into bwctrl bandwidth scaling
 * @pdev:	Downstream PCIe device (e.g. an NVMe controller's own pci_dev)
 *
 * See the kdoc in <linux/pci-bwctrl.h> for the calling convention.
 */
void pcie_bwctrl_register(struct pci_dev *pdev)
{
	pdev->bwctrl_participate = 1;
}
EXPORT_SYMBOL_GPL(pcie_bwctrl_register);

static void pcie_bwctrl_devfreq_init(struct pci_dev *port, struct pcie_bwctrl_data *data)
{
	unsigned long bit, supported_speeds = port->supported_speeds;
	unsigned int n = 0, added = 0;
	int ret;

	if (hweight8(supported_speeds) <= 1) {
		pci_err(port, "bwctrl devfreq: only one supported speed (0x%02lx), skipping\n",
			supported_speeds);
		return;
	}

	/*
	 * supported_speeds is a PCIe Supported Link Speeds Vector: bit N
	 * (N >= 1) set means PCIE_SPEED_2_5GT + (N - 1) is supported.
	 */
	for_each_set_bit(bit, &supported_speeds, BITS_PER_TYPE(u8)) {
		if (bit == 0)
			continue;
		data->freq_table[n++] = PCIE_SPEED_2_5GT + (bit - 1);
	}

	pci_err(port, "bwctrl devfreq: supported_speeds=0x%02lx, %u freq_table entries\n",
		supported_speeds, n);

	/*
	 * devfreq_add_device() resolves min/max scaling frequency through
	 * the OPP framework regardless of profile->freq_table, so an OPP
	 * must exist for every entry or registration fails with -EINVAL.
	 */
	for (added = 0; added < n; added++) {
		ret = dev_pm_opp_add(&port->dev, data->freq_table[added], 0);
		if (ret) {
			pci_err(port, "failed to add OPP for speed %lu: %d\n",
				data->freq_table[added], ret);
			goto err_remove_opps;
		}
		pci_err(port, "bwctrl devfreq: added OPP for speed %lu\n",
			data->freq_table[added]);
	}

	data->ondemand_data.upthreshold = 80;
	data->ondemand_data.downdifferential = 5;

	data->profile.polling_ms = 100;
	data->profile.timer = DEVFREQ_TIMER_DELAYED;
	data->profile.target = pcie_bwctrl_devfreq_target;
	data->profile.get_dev_status = pcie_bwctrl_devfreq_get_status;
	data->profile.get_cur_freq = pcie_bwctrl_devfreq_get_cur_freq;
	data->profile.freq_table = data->freq_table;
	data->profile.max_state = n;
	data->profile.initial_freq = pcie_bwctrl_cur_speed(port);

	pci_err(port, "bwctrl devfreq: registering, initial_freq=%u\n",
		data->profile.initial_freq);

	data->devfreq = devfreq_add_device(&port->dev, &data->profile,
					   DEVFREQ_GOV_SIMPLE_ONDEMAND,
					   &data->ondemand_data);
	if (IS_ERR(data->devfreq)) {
		pci_err(port, "failed to register bwctrl devfreq: %ld\n",
			PTR_ERR(data->devfreq));
		data->devfreq = NULL;
		goto err_remove_opps;
	}

	data->nr_freqs = n;
	pci_err(port, "bwctrl devfreq: registered successfully\n");

	return;

err_remove_opps:
	for (unsigned int i = 0; i < added; i++)
		dev_pm_opp_remove(&port->dev, data->freq_table[i]);
}

static void pcie_bwctrl_devfreq_remove(struct pci_dev *port, struct pcie_bwctrl_data *data)
{
	struct devfreq *devfreq;
	unsigned int nr_freqs;

	scoped_guard(spinlock_irqsave, &data->activity_lock) {
		devfreq = data->devfreq;
		data->devfreq = NULL;
		nr_freqs = data->nr_freqs;
		data->nr_freqs = 0;
	}

	if (!devfreq)
		return;

	pci_err(port, "bwctrl devfreq: removing\n");
	devfreq_remove_device(devfreq);

	for (unsigned int i = 0; i < nr_freqs; i++)
		dev_pm_opp_remove(&port->dev, data->freq_table[i]);
}

static void pcie_bwctrl_devfreq_suspend(struct pcie_bwctrl_data *data)
{
	if (data->devfreq)
		devfreq_suspend_device(data->devfreq);
}

static void pcie_bwctrl_devfreq_resume(struct pcie_bwctrl_data *data)
{
	if (data->devfreq)
		devfreq_resume_device(data->devfreq);
}
#else
static void pcie_bwctrl_devfreq_init(struct pci_dev *port, struct pcie_bwctrl_data *data)
{
}

static void pcie_bwctrl_devfreq_remove(struct pci_dev *port, struct pcie_bwctrl_data *data)
{
}

static void pcie_bwctrl_devfreq_suspend(struct pcie_bwctrl_data *data)
{
}

static void pcie_bwctrl_devfreq_resume(struct pcie_bwctrl_data *data)
{
}
#endif /* CONFIG_PCIE_BW_ONDEMAND */

static void pcie_bwnotif_enable(struct pcie_device *srv)
{
	struct pci_dev *port = srv->port;
	u16 link_status;
	int ret;

	/* Note if LBMS has been seen so far */
	ret = pcie_capability_read_word(port, PCI_EXP_LNKSTA, &link_status);
	if (ret == PCIBIOS_SUCCESSFUL && link_status & PCI_EXP_LNKSTA_LBMS)
		set_bit(PCI_LINK_LBMS_SEEN, &port->priv_flags);

	pcie_capability_set_word(port, PCI_EXP_LNKCTL,
				 PCI_EXP_LNKCTL_LBMIE | PCI_EXP_LNKCTL_LABIE);
	pcie_capability_write_word(port, PCI_EXP_LNKSTA,
				   PCI_EXP_LNKSTA_LBMS | PCI_EXP_LNKSTA_LABS);

	/*
	 * Update after enabling notifications & clearing status bits ensures
	 * link speed is up to date.
	 */
	pcie_update_link_speed(port->subordinate, PCIE_BWCTRL_ENABLE);
}

static void pcie_bwnotif_disable(struct pci_dev *port)
{
	pcie_capability_clear_word(port, PCI_EXP_LNKCTL,
				   PCI_EXP_LNKCTL_LBMIE | PCI_EXP_LNKCTL_LABIE);
}

static irqreturn_t pcie_bwnotif_irq(int irq, void *context)
{
	struct pcie_device *srv = context;
	struct pci_dev *port = srv->port;
	u16 link_status, events;
	int ret;

	ret = pcie_capability_read_word(port, PCI_EXP_LNKSTA, &link_status);
	if (ret != PCIBIOS_SUCCESSFUL)
		return IRQ_NONE;

	events = link_status & (PCI_EXP_LNKSTA_LBMS | PCI_EXP_LNKSTA_LABS);
	if (!events)
		return IRQ_NONE;

	if (events & PCI_EXP_LNKSTA_LBMS)
		set_bit(PCI_LINK_LBMS_SEEN, &port->priv_flags);

	pcie_capability_write_word(port, PCI_EXP_LNKSTA, events);

	/*
	 * Interrupts will not be triggered from any further Link Speed
	 * change until LBMS is cleared by the write. Therefore, re-read the
	 * speed (inside pcie_update_link_speed()) after LBMS has been
	 * cleared to avoid missing link speed changes.
	 */
	pcie_update_link_speed(port->subordinate, PCIE_BWCTRL_IRQ);

	return IRQ_HANDLED;
}

void pcie_reset_lbms(struct pci_dev *port)
{
	clear_bit(PCI_LINK_LBMS_SEEN, &port->priv_flags);
	pcie_capability_write_word(port, PCI_EXP_LNKSTA, PCI_EXP_LNKSTA_LBMS);
}

static int pcie_bwnotif_probe(struct pcie_device *srv)
{
	struct pci_dev *port = srv->port;
	int ret;

	if (port->no_bw_notif)
		return -ENODEV;

	/* Can happen if we run out of bus numbers during enumeration. */
	if (!port->subordinate)
		return -ENODEV;

	struct pcie_bwctrl_data *data = devm_kzalloc(&srv->device,
						     sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = devm_mutex_init(&srv->device, &data->set_speed_mutex);
	if (ret)
		return ret;

#ifdef CONFIG_PCIE_BW_ONDEMAND
	spin_lock_init(&data->activity_lock);
#endif

	scoped_guard(rwsem_write, &pcie_bwctrl_setspeed_rwsem) {
		WRITE_ONCE(port->link_bwctrl, data);

		ret = request_irq(srv->irq, pcie_bwnotif_irq,
				  IRQF_SHARED, "PCIe bwctrl", srv);
		if (ret) {
			WRITE_ONCE(port->link_bwctrl, NULL);
			return ret;
		}

		pcie_bwnotif_enable(srv);
	}

	pci_dbg(port, "enabled with IRQ %d\n", srv->irq);

	/* Don't fail on errors. Don't leave IS_ERR() "pointer" into ->cdev */
	port->link_bwctrl->cdev = pcie_cooling_device_register(port);
	if (IS_ERR(port->link_bwctrl->cdev))
		port->link_bwctrl->cdev = NULL;

	pcie_bwctrl_devfreq_init(port, data);

	return 0;
}

static void pcie_bwnotif_remove(struct pcie_device *srv)
{
	struct pcie_bwctrl_data *data = srv->port->link_bwctrl;

	/*
	 * Stop devfreq polling (synchronously) before anything below tears
	 * down port->link_bwctrl, so no devfreq callback can ever observe a
	 * stale or NULL pcie_bwctrl_data.
	 */
	pcie_bwctrl_devfreq_remove(srv->port, data);

	pcie_cooling_device_unregister(data->cdev);

	scoped_guard(rwsem_write, &pcie_bwctrl_setspeed_rwsem) {
		pcie_bwnotif_disable(srv->port);

		free_irq(srv->irq, srv);

		WRITE_ONCE(srv->port->link_bwctrl, NULL);
	}
}

static int pcie_bwnotif_suspend(struct pcie_device *srv)
{
	pcie_bwctrl_devfreq_suspend(srv->port->link_bwctrl);
	pcie_bwnotif_disable(srv->port);
	return 0;
}

static int pcie_bwnotif_resume(struct pcie_device *srv)
{
	pcie_bwnotif_enable(srv);
	pcie_bwctrl_devfreq_resume(srv->port->link_bwctrl);
	return 0;
}

static struct pcie_port_service_driver pcie_bwctrl_driver = {
	.name		= "pcie_bwctrl",
	.port_type	= PCIE_ANY_PORT,
	.service	= PCIE_PORT_SERVICE_BWCTRL,
	.probe		= pcie_bwnotif_probe,
	.suspend	= pcie_bwnotif_suspend,
	.resume		= pcie_bwnotif_resume,
	.remove		= pcie_bwnotif_remove,
};

int __init pcie_bwctrl_init(void)
{
	return pcie_port_service_register(&pcie_bwctrl_driver);
}
