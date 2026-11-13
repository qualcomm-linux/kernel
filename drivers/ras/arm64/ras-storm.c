// SPDX-License-Identifier: GPL-2.0
/*
 * arm64 RAS error storm mitigation.
 *
 * This file plumbs the architecture-independent storm tracker
 * (drivers/ras/storm.c) into the arm64 RAS driver. Storm tracking is
 * per-record while the mitigation is mixed:
 *
 *   - When a record enters storm mode its FHI (fault handling) and CFI
 *     (corrected fault) interrupts are masked via ERR<n>_CTLR.
 *   - The first record entering storm starts the node's poll timer
 *     which drains all currently stormy records.
 *   - When a record leaves storm mode its interrupts are re-enabled and
 *     it is removed from the poll. The timer stops once no
 *     record remains in storm.
 *
 * Note that ERI / UI (uncorrected error reporting) is intentionally
 * left untouched: uncorrected errors must continue to be delivered
 * synchronously and never participate in storm suppression.
 */
#include <linux/bitmap.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/timer.h>

#include "ras.h"

#define INITIAL_CHECK_INTERVAL	(5 * 60) /* 5 minutes */

#define NUM_HISTORY_BITS (sizeof(u64) * BITS_PER_BYTE)

/* How many errors within the history buffer mark the start of a storm. */
#define STORM_BEGIN_THRESHOLD	5

/*
 * How many polls of machine check bank without an error before declaring
 * the storm is over. Since it is tracked by the bitmasks in the history
 * field of struct storm_bank the mask is 30 bits [0 ... 29].
 */
#define STORM_END_POLL_THRESHOLD	29

static void arm64_ras_storm_timer_fn(struct timer_list *t)
{
	struct ras_node *node = timer_container_of(node, t, storm_timer);
	unsigned long iv = msecs_to_jiffies(node->timer_interval);
	int count = 0;

	ras_node_dbg(node, "Stormy bitmap %*pb\n", node->record_count, node->storm_bitmap);
	ras_node_foreach_record(ras_proc_record, node, &count, node->storm_bitmap);

	if (count)
		iv = max(iv / 2, (unsigned long) HZ/100);
	else
		iv = min(iv * 2, INITIAL_CHECK_INTERVAL * HZ);


	node->timer_interval = jiffies_to_msecs(iv);

	if (atomic_read(&node->stormy_count)) {
		ras_node_dbg(node, "next poll at %d ms\n", node->timer_interval);
		mod_timer(&node->storm_timer, jiffies + iv);
	}
}

static void
arm64_ras_storm_reset_record(struct ras_record *record, void *__unused0, bool __unused1)
{
	struct ras_storm_unit *unit = &record->storm;

	unit->in_storm_mode = false;
	unit->history = 0;
	unit->timestamp = 0;
}

void arm64_ras_storm_reset_node(void *data)
{
	struct ras_node *node = data;

	node->begin_threshold = STORM_BEGIN_THRESHOLD;
	node->end_poll_threshold = STORM_END_POLL_THRESHOLD;
	node->timer_interval = INITIAL_CHECK_INTERVAL * MSEC_PER_SEC;

	timer_delete_sync(&node->storm_timer);
	bitmap_set(node->storm_bitmap, 0, node->record_count);

	ras_node_foreach_record(arm64_ras_storm_reset_record, node, NULL, node->record_implemented);
}

static int arm64_ras_storm_do_init(struct ras_node *node)
{
	node->storm_bitmap = devm_bitmap_zalloc(node->dev,
						node->record_count, GFP_KERNEL);
	if (!node->storm_bitmap)
		return -ENOMEM;

	timer_setup(&node->storm_timer, arm64_ras_storm_timer_fn, 0);

	return devm_add_action_or_reset(node->dev,
					arm64_ras_storm_reset_node, node);
}

int arm64_ras_storm_init(struct ras_node *node)
{
	int ret = 0;

	if (!node->record_count)
		return ret;

	/*
	 * Per-CPU (oncore) nodes re-enter this path on every CPU
	 * online transition, so the bitmap is allocated only on the
	 * first call and reused on subsequent re-inits.
	 */
	if (!node->storm_bitmap) {
		ret = arm64_ras_storm_do_init(node);
		if (ret)
			return ret;
	}

	arm64_ras_storm_reset_node(node);
	return ret;
}

/**
 * The function maintains the unit's history bitmap and decides whether
 * the unit should enter or leave storm mode.
 */
static void ras_track_storm(struct ras_storm_unit *unit, bool corrected)
{
	unsigned long now = jiffies, delta;
	unsigned int shift = 1;
	u64 history = 0;

	/*
	 * Check how long it has been since this bank was last checked,
	 * and adjust the amount of "shift" to apply to history.
	 */
	delta = now - unit->timestamp;
	shift = (delta + HZ) / HZ;

	/* If it has been a long time since the last poll, clear history. */
	if (shift < NUM_HISTORY_BITS)
		history = unit->history << shift;

	unit->timestamp = now;

	/* History keeps track of corrected errors. VAL=1 && UC=0 */
	if (corrected)
		history |= 1;

	unit->history = history;
}

void arm64_ras_storm_track_record(struct ras_record *record, u64 err_status)
{
	struct ras_storm_unit *u = &record->storm;
	struct ras_node *node = record->node;

	ras_track_storm(u, err_status & ERR_STATUS_CE);

	if (u->in_storm_mode) {
		/*
		 * Storm ends when no corrected error has been seen for
		 * STORM_END_POLL_THRESHOLD + 1 consecutive polls.
		 */
		if (u->history & GENMASK_ULL(node->end_poll_threshold, 0))
			return;

		u->in_storm_mode = false;
		u->history = 0;
		set_bit(record->index, node->storm_bitmap);

		ras_node_info(node, "%s: exited storm mode\n", record->name);
		ras_enable_irq(record);

		if (atomic_dec_and_test(&record->node->stormy_count))
			timer_delete(&node->storm_timer);
	} else {
		if (hweight64(u->history) < node->begin_threshold)
			return;

		u->in_storm_mode = true;
		clear_bit(record->index, node->storm_bitmap);

		ras_node_info(node, "%s: entered storm mode\n", record->name);
		ras_disable_irq(record);
		/*
		 * If this is the first record on this node to enter storm mode
		 * start polling.
		 */
		if (atomic_inc_return(&record->node->stormy_count) == 1)
			mod_timer(&node->storm_timer,
				  jiffies + msecs_to_jiffies(node->timer_interval));
	}
}
