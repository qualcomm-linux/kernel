/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_bam_dmux

#if !defined(__QCOM_BAM_DMUX_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __QCOM_BAM_DMUX_TRACE_H__

#include <linux/device.h>
#include <linux/tracepoint.h>

/*
 * Fired at the top of bam_dmux_pc_irq() when the modem toggles its BAM
 * power-control SMSM bit.
 *   old_state: dmux->pc_state before the edge (0 = BAM was off)
 *   new_state: !old_state (what we are transitioning to)
 *
 * Downstream equivalent (BAM_DMUX_LOG):
 *   "bam_dmux_smsm_cb: 0x%08x -> 0x%08x" + "reconnect / disconnect / init"
 */
TRACE_EVENT(bam_dmux_pc,
	TP_PROTO(const struct device *dev, bool old_state, bool new_state),
	TP_ARGS(dev, old_state, new_state),
	TP_STRUCT__entry(
		__string(dev_name,	dev_name(dev))
		__field(bool,		old_state)
		__field(bool,		new_state)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__entry->old_state = old_state;
		__entry->new_state = new_state;
	),
	TP_printk("%s: pc %s -> %s",
		  __get_str(dev_name),
		  __entry->old_state ? "on" : "off",
		  __entry->new_state ? "on" : "off")
);

/*
 * Fired at the end of bam_dmux_power_on() and bam_dmux_power_off() to
 * record whether the BAM DMA channel setup/teardown succeeded.
 *   on:      true = power_on, false = power_off
 *   success: only meaningful when on=true; false means DMA channel
 *            request or RX buffer queueing failed
 *
 * Downstream equivalent:
 *   "reconnect_to_bam: disconnect tx/rx, device reset, sps_connect tx/rx"
 *   "disconnect_to_bam: disconnect tx, disconnect rx, device reset"
 */
TRACE_EVENT(bam_dmux_power,
	TP_PROTO(const struct device *dev, bool on, bool success),
	TP_ARGS(dev, on, success),
	TP_STRUCT__entry(
		__string(dev_name,	dev_name(dev))
		__field(bool,		on)
		__field(bool,		success)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__entry->on      = on;
		__entry->success = success;
	),
	TP_printk("%s: power %s%s",
		  __get_str(dev_name),
		  __entry->on ? "on" : "off",
		  __entry->on ? (__entry->success ? " ok" : " failed") : "")
);

/*
 * Fired at the completion of each step inside bam_dmux_runtime_resume().
 * result=0 means the step passed; negative errno means it failed/timed out.
 *
 * Steps in order:
 *   "prev_ack"  - waited for previous power-down to be acked by modem
 *   "ack"       - voted for power and waited for modem's ack
 *   "bam_up"    - waited for modem to signal BAM is powered (pc_state=true)
 *   "tx_chan"   - requested TX DMA channel
 *
 * Downstream equivalent (BAM_DMUX_LOG):
 *   "ul_wakeup waiting for previous ack"
 *   "ul_wakeup waiting for wakeup ack"
 *   "ul_wakeup waiting completion"
 *   "ul_wakeup complete"
 */
TRACE_EVENT(bam_dmux_resume_step,
	TP_PROTO(const struct device *dev, const char *step, int result),
	TP_ARGS(dev, step, result),
	TP_STRUCT__entry(
		__string(dev_name,	dev_name(dev))
		__string(step,		step)
		__field(int,		result)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__assign_str(step);
		__entry->result = result;
	),
	TP_printk("%s: resume %s: %d",
		  __get_str(dev_name), __get_str(step), __entry->result)
);

/*
 * Fired on every channel open and close event, from both the modem side
 * (CMD_OPEN/CMD_CLOSE) and the local netdev side (ndo_open/ndo_stop).
 *
 * event: "remote_open"  - modem sent CMD_OPEN
 *        "remote_close" - modem sent CMD_CLOSE
 *        "local_open"   - AP opened the netdev (sent CMD_OPEN to modem)
 *        "local_close"  - AP stopped the netdev (sent CMD_CLOSE to modem)
 *
 * Downstream equivalent (BAM_DMUX_LOG / BAM_DMUX_INFO):
 *   "handle_bam_mux_cmd: opening cid N PC enabled"
 *   "msm_bam_dmux_open: opening/opened ch N"
 *   "msm_bam_dmux_close: closing/closed ch N"
 */
TRACE_EVENT(bam_dmux_channel,
	TP_PROTO(const struct device *dev, u8 ch, const char *event),
	TP_ARGS(dev, ch, event),
	TP_STRUCT__entry(
		__string(dev_name,	dev_name(dev))
		__field(u8,		ch)
		__string(event,		event)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__entry->ch = ch;
		__assign_str(event);
	),
	TP_printk("%s: ch %u %s",
		  __get_str(dev_name), __entry->ch, __get_str(event))
);

/*
 * Fired in bam_dmux_rx_callback() for every valid inbound packet after
 * magic and channel validation. For data packets, len is the payload
 * length. For control packets (OPEN/CLOSE), len is 0.
 *
 * Downstream equivalent (BAM_DMUX_INFO via handle_bam_mux_cmd):
 *   "handle_bam_mux_cmd: magic 33fc signal %x cmd %d pad %d ch %d len %d"
 */
TRACE_EVENT(bam_dmux_rx,
	TP_PROTO(const struct device *dev, u8 ch, u8 cmd, u16 len),
	TP_ARGS(dev, ch, cmd, len),
	TP_STRUCT__entry(
		__string(dev_name,	dev_name(dev))
		__field(u8,		ch)
		__field(u8,		cmd)
		__field(u16,		len)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__entry->ch  = ch;
		__entry->cmd = cmd;
		__entry->len = len;
	),
	TP_printk("%s: rx ch %u cmd %u len %u",
		  __get_str(dev_name), __entry->ch, __entry->cmd, __entry->len)
);

#endif /* __QCOM_BAM_DMUX_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-bam-dmux

#include <trace/define_trace.h>
