/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_smsm

#if !defined(__QCOM_SMSM_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __QCOM_SMSM_TRACE_H__

#include <linux/device.h>
#include <linux/tracepoint.h>

/*
 * Fired on every call to smsm_update_bits(), whether or not the state
 * actually changed.  changes == 0 indicates the write was a no-op because
 * the bits were already in the requested state.
 */
TRACE_EVENT(smsm_update_bits,
	TP_PROTO(const struct device *dev, u32 mask, u32 value,
		 u32 orig, u32 val, u32 changes),
	TP_ARGS(dev, mask, value, orig, val, changes),
	TP_STRUCT__entry(
		__string(dev_name,	dev_name(dev))
		__field(u32,		mask)
		__field(u32,		value)
		__field(u32,		orig)
		__field(u32,		val)
		__field(u32,		changes)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__entry->mask    = mask;
		__entry->value   = value;
		__entry->orig    = orig;
		__entry->val     = val;
		__entry->changes = changes;
	),
	TP_printk("%s: mask:0x%08x value:0x%08x 0x%08x->0x%08x changed:0x%08x",
		  __get_str(dev_name),
		  __entry->mask, __entry->value,
		  __entry->orig, __entry->val,
		  __entry->changes)
);

/*
 * Fired for each remote host that is subscribed to the changed bits and
 * is about to receive an IPC kick.
 */
TRACE_EVENT(smsm_ipc_kick,
	TP_PROTO(const struct device *dev, u32 host, u32 subscription),
	TP_ARGS(dev, host, subscription),
	TP_STRUCT__entry(
		__string(dev_name,	dev_name(dev))
		__field(u32,		host)
		__field(u32,		subscription)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__entry->host         = host;
		__entry->subscription = subscription;
	),
	TP_printk("%s: kick host %u subscription:0x%08x",
		  __get_str(dev_name), __entry->host, __entry->subscription)
);

/*
 * Fired at the top of smsm_intr() after reading the remote state and
 * computing the changed bits.
 */
TRACE_EVENT(smsm_intr,
	TP_PROTO(int irq, u32 old_state, u32 new_state, u32 changed),
	TP_ARGS(irq, old_state, new_state, changed),
	TP_STRUCT__entry(
		__field(int,	irq)
		__field(u32,	old_state)
		__field(u32,	new_state)
		__field(u32,	changed)
	),
	TP_fast_assign(
		__entry->irq       = irq;
		__entry->old_state = old_state;
		__entry->new_state = new_state;
		__entry->changed   = changed;
	),
	TP_printk("IRQ %d: 0x%08x->0x%08x changed:0x%08x",
		  __entry->irq,
		  __entry->old_state, __entry->new_state,
		  __entry->changed)
);

/*
 * Fired for each cascaded (per-bit) IRQ that is about to be dispatched
 * to a downstream consumer (e.g. the BAM DMUX pc/pc-ack handler).
 */
TRACE_EVENT(smsm_irq_cascade,
	TP_PROTO(int irq_pin, unsigned int bit, bool rising),
	TP_ARGS(irq_pin, bit, rising),
	TP_STRUCT__entry(
		__field(int,		irq_pin)
		__field(unsigned int,	bit)
		__field(bool,		rising)
	),
	TP_fast_assign(
		__entry->irq_pin = irq_pin;
		__entry->bit     = bit;
		__entry->rising  = rising;
	),
	TP_printk("IRQ %d bit %u %s",
		  __entry->irq_pin, __entry->bit,
		  __entry->rising ? "rising" : "falling")
);

/*
 * Fired when a cascaded per-bit IRQ is masked or unmasked, recording the
 * resulting subscription bitmap written to shared memory.
 */
TRACE_EVENT(smsm_irq_mask,
	TP_PROTO(unsigned long irq, bool mask, u32 subscription),
	TP_ARGS(irq, mask, subscription),
	TP_STRUCT__entry(
		__field(unsigned long,	irq)
		__field(bool,		mask)
		__field(u32,		subscription)
	),
	TP_fast_assign(
		__entry->irq          = irq;
		__entry->mask         = mask;
		__entry->subscription = subscription;
	),
	TP_printk("IRQ %lu %s subscription:0x%08x",
		  __entry->irq,
		  __entry->mask ? "masked" : "unmasked",
		  __entry->subscription)
);

#endif /* __QCOM_SMSM_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-smsm

#include <trace/define_trace.h>
