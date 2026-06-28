/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _QCOM_TGU_H
#define _QCOM_TGU_H

/* Register addresses */
#define TGU_CONTROL		0x0000
#define TGU_LAR		0xfb0
#define TGU_UNLOCK_OFFSET	0xc5acce55
#define TGU_DEVID		0xfc8
#define CORESIGHT_DEVID2	0xfc0

#define TGU_DEVID_SENSE_INPUT(devid_val) \
	((int)FIELD_GET(GENMASK(17, 10), devid_val))
#define TGU_DEVID_STEPS(devid_val) \
	((int)FIELD_GET(GENMASK(6, 3), devid_val))
#define TGU_DEVID_CONDITIONS(devid_val) \
	((int)FIELD_GET(GENMASK(2, 0), devid_val))
#define TGU_DEVID2_TIMER0(devid_val)	\
	((int)FIELD_GET(GENMASK(23, 18), devid_val))
#define TGU_DEVID2_TIMER1(devid_val)	\
	((int)FIELD_GET(GENMASK(17, 13), devid_val))
#define TGU_DEVID2_COUNTER0(devid_val)	\
	((int)FIELD_GET(GENMASK(11, 6), devid_val))
#define TGU_DEVID2_COUNTER1(devid_val)	\
	((int)FIELD_GET(GENMASK(5, 0), devid_val))


#define TGU_BITS_PER_SIGNAL 4
#define LENGTH_REGISTER 32

/*
 *  TGU configuration space                              Step configuration
 *  offset table                                         space layout
 * x-------------------------x$                          x-------------x$
 * |                         |$                          |             |$
 * |                         |                           |   reserve   |$
 * |                         |                           |             |$
 * |coresight management     |                           |-------------|base+n*0x1D8+0x1F4$
 * |     registers           |                     |---> |priority[3]  |$
 * |                         |                     |     |-------------|base+n*0x1D8+0x194$
 * |                         |                     |     |priority[2]  |$
 * |-------------------------|                     |     |-------------|base+n*0x1D8+0x134$
 * |                         |                     |     |priority[1]  |$
 * |         step[7]         |                     |     |-------------|base+n*0x1D8+0xD4$
 * |-------------------------|->base+0x40+7*0x1D8  |     |priority[0]  |$
 * |                         |                     |     |-------------|base+n*0x1D8+0x74$
 * |         ...             |                     |     |  condition  |$
 * |                         |                     |     |   select    |$
 * |-------------------------|->base+0x40+1*0x1D8  |     |-------------|base+n*0x1D8+0x60$
 * |                         |                     |     |  condition  |$
 * |         step[0]         |-------------------->      |   decode    |$
 * |-------------------------|-> base+0x40               |-------------|base+n*0x1D8+0x50$
 * |                         |                           |             |$
 * | Control and status space|                           |Timer/Counter|$
 * |        space            |                           |             |$
 * x-------------------------x->base                     x-------------x base+n*0x1D8+0x40$
 *
 */
#define STEP_OFFSET 0x1D8
#define PRIORITY_START_OFFSET 0x0074
#define CONDITION_DECODE_OFFSET 0x0050
#define CONDITION_SELECT_OFFSET 0x0060
#define TIMER_START_OFFSET 0x0040
#define COUNTER_START_OFFSET 0x0048
#define PRIORITY_OFFSET 0x60
#define REG_OFFSET 0x4

/* Calculate compare step addresses */
#define PRIORITY_REG_STEP(step, priority, reg)\
	(PRIORITY_START_OFFSET + PRIORITY_OFFSET * priority +\
	 REG_OFFSET * reg + STEP_OFFSET * step)

#define CONDITION_DECODE_STEP(step, decode) \
	(CONDITION_DECODE_OFFSET + REG_OFFSET * decode + STEP_OFFSET * step)

#define CONDITION_SELECT_STEP(step, select) \
	(CONDITION_SELECT_OFFSET + REG_OFFSET * select + STEP_OFFSET * step)

#define TIMER_COMPARE_STEP(step, timer) \
	(TIMER_START_OFFSET + REG_OFFSET * timer + STEP_OFFSET * step)

#define COUNTER_COMPARE_STEP(step, counter) \
	(COUNTER_START_OFFSET + REG_OFFSET * counter + STEP_OFFSET * step)

#define tgu_dataset_rw(name, step_index, type, reg_num)                  \
	(&((struct tgu_attribute[]){ {                                   \
		__ATTR(name, 0644, tgu_dataset_show, tgu_dataset_store), \
		step_index,                                              \
		type,                                                    \
		reg_num,                                                 \
	} })[0].attr.attr)

#define STEP_PRIORITY(step_index, reg_num, priority)                     \
	tgu_dataset_rw(reg##reg_num, step_index, TGU_PRIORITY##priority, \
			reg_num)
#define STEP_DECODE(step_index, reg_num) \
	tgu_dataset_rw(reg##reg_num, step_index, TGU_CONDITION_DECODE, reg_num)
#define STEP_SELECT(step_index, reg_num) \
	tgu_dataset_rw(reg##reg_num, step_index, TGU_CONDITION_SELECT, reg_num)
#define STEP_TIMER(step_index, reg_num) \
	tgu_dataset_rw(reg##reg_num, step_index, TGU_TIMER, reg_num)
#define STEP_COUNTER(step_index, reg_num) \
	tgu_dataset_rw(reg##reg_num, step_index, TGU_COUNTER, reg_num)

#define STEP_PRIORITY_LIST(step_index, priority) \
	{STEP_PRIORITY(step_index, 0, priority), \
	 STEP_PRIORITY(step_index, 1, priority),  \
	 STEP_PRIORITY(step_index, 2, priority),	 \
	 STEP_PRIORITY(step_index, 3, priority),  \
	 STEP_PRIORITY(step_index, 4, priority),  \
	 STEP_PRIORITY(step_index, 5, priority),  \
	 STEP_PRIORITY(step_index, 6, priority),  \
	 STEP_PRIORITY(step_index, 7, priority),  \
	 STEP_PRIORITY(step_index, 8, priority),  \
	 STEP_PRIORITY(step_index, 9, priority),  \
	 STEP_PRIORITY(step_index, 10, priority), \
	 STEP_PRIORITY(step_index, 11, priority), \
	 STEP_PRIORITY(step_index, 12, priority), \
	 STEP_PRIORITY(step_index, 13, priority), \
	 STEP_PRIORITY(step_index, 14, priority), \
	 STEP_PRIORITY(step_index, 15, priority), \
	 STEP_PRIORITY(step_index, 16, priority), \
	 STEP_PRIORITY(step_index, 17, priority), \
	 NULL                   \
	}

#define STEP_DECODE_LIST(n) \
	{STEP_DECODE(n, 0), \
	 STEP_DECODE(n, 1), \
	 STEP_DECODE(n, 2), \
	 STEP_DECODE(n, 3), \
	 NULL               \
	}

#define STEP_SELECT_LIST(n) \
	{STEP_SELECT(n, 0), \
	 STEP_SELECT(n, 1), \
	 STEP_SELECT(n, 2), \
	 STEP_SELECT(n, 3), \
	 STEP_SELECT(n, 4), \
	 NULL               \
	}

#define STEP_TIMER_LIST(n) \
	{STEP_TIMER(n, 0), \
	 STEP_TIMER(n, 1), \
	 NULL              \
	}

#define STEP_COUNTER_LIST(n) \
	{STEP_COUNTER(n, 0), \
	 STEP_COUNTER(n, 1), \
	 NULL                \
	}

#define PRIORITY_ATTRIBUTE_GROUP_INIT(step, priority)\
	(&(const struct attribute_group){\
		.attrs = (struct attribute*[])STEP_PRIORITY_LIST(step, priority),\
		.is_visible = tgu_node_visible,\
		.name = "step" #step "_priority" #priority \
	})

#define CONDITION_DECODE_ATTRIBUTE_GROUP_INIT(step)\
	(&(const struct attribute_group){\
		.attrs = (struct attribute*[])STEP_DECODE_LIST(step),\
		.is_visible = tgu_node_visible,\
		.name = "step" #step "_condition_decode" \
	})

#define CONDITION_SELECT_ATTRIBUTE_GROUP_INIT(step)\
	(&(const struct attribute_group){\
		.attrs = (struct attribute*[])STEP_SELECT_LIST(step),\
		.is_visible = tgu_node_visible,\
		.name = "step" #step "_condition_select" \
	})

#define TIMER_ATTRIBUTE_GROUP_INIT(step)\
	(&(const struct attribute_group){\
		.attrs = (struct attribute*[])STEP_TIMER_LIST(step),\
		.is_visible = tgu_node_visible,\
		.name = "step" #step "_timer" \
	})

#define COUNTER_ATTRIBUTE_GROUP_INIT(step)\
	(&(const struct attribute_group){\
		.attrs = (struct attribute*[])STEP_COUNTER_LIST(step),\
		.is_visible = tgu_node_visible,\
		.name = "step" #step "_counter" \
	})

enum operation_index {
	TGU_PRIORITY0,
	TGU_PRIORITY1,
	TGU_PRIORITY2,
	TGU_PRIORITY3,
	TGU_CONDITION_DECODE,
	TGU_CONDITION_SELECT,
	TGU_TIMER,
	TGU_COUNTER,
};

/* Maximum priority that TGU supports */
#define MAX_PRIORITY 4

struct tgu_attribute {
	struct device_attribute attr;
	u32 step_index;
	enum operation_index operation_index;
	u32 reg_num;
};

struct value_table {
	unsigned int *priority;
	unsigned int *condition_decode;
	unsigned int *condition_select;
	unsigned int *timer;
	unsigned int *counter;
};

static inline void TGU_LOCK(void __iomem *addr)
{
	do {
		/* Wait for things to settle */
		mb();
		writel_relaxed(0x0, addr + TGU_LAR);
	} while (0);
}

static inline void TGU_UNLOCK(void __iomem *addr)
{
	do {
		writel_relaxed(TGU_UNLOCK_OFFSET, addr + TGU_LAR);
		/* Make sure everyone has seen this */
		mb();
	} while (0);
}

/**
 * struct tgu_drvdata - Data structure for a TGU (Trigger Generator Unit)
 * @base: Memory-mapped base address of the TGU device
 * @dev: Pointer to the associated device structure
 * @lock: Spinlock for handling concurrent access to private data
 * @enabled: Flag indicating whether the TGU device is enabled
 * @value_table: Store given value based on relevant parameters
 * @num_reg: Maximum number of registers
 * @num_step: Maximum step size
 * @num_condition_decode: Maximum number of condition_decode
 * @num_condition_select: Maximum number of condition_select
 * @num_timer: Maximum number of timers
 * @num_counter: Maximum number of counters
 *
 * This structure defines the data associated with a TGU device,
 * including its base address, device pointers, clock, spinlock for
 * synchronization, trigger data pointers, maximum limits for various
 * trigger-related parameters, and enable status.
 */
struct tgu_drvdata {
	void __iomem *base;
	struct device *dev;
	spinlock_t lock;
	bool enabled;
	struct value_table *value_table;
	int num_reg;
	int num_step;
	int num_condition_decode;
	int num_condition_select;
	int num_timer;
	int num_counter;
};

#endif
