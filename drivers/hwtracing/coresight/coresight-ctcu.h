/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2026 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _CORESIGHT_CTCU_H
#define _CORESIGHT_CTCU_H

#include <linux/time.h>
#include "coresight-trace-id.h"

/* Maximum number of supported ETR devices for a single CTCU. */
#define ETR_MAX_NUM	2

#define BYTE_CNTR_TIMEOUT	(3 * HZ)
#define MAX_IRQ_CNT		20

/**
 * struct ctcu_etr_config
 * @atid_offset:	offset to the ATID0 Register.
 * @port_num:		in-port number of the CTCU device that connected to ETR.
 * @irq_ctrl_offset:    offset to the BYTECNTRVAL register.
 */
struct ctcu_etr_config {
	const u32 atid_offset;
	const u32 port_num;
	const u32 irq_ctrl_offset;
};

struct ctcu_config {
	const struct ctcu_etr_config *etr_cfgs;
	int num_etr_config;
};

/**
 * struct ctcu_byte_cntr
 * @enable:		indicates that byte_cntr function is enabled or not.
 * @irq_enabled:	indicates that the interruption is enabled.
 * @reading:		indicates that byte_cntr is reading.
 * @irq:		allocated number of the IRQ.
 * @irq_cnt:		IRQ count number of the triggered interruptions.
 * @wq:			waitqueue for reading data from ETR buffer.
 * @spin_lock:		spinlock of the byte_cntr_data.
 * @irq_ctrl_offset:	offset to the BYTECNTVAL Register.
 * @ctcu_drvdata:	drvdata of the CTCU device.
 * @buf_node:		etr_buf_node for reading.
 */
struct ctcu_byte_cntr {
	bool			enable;
	bool			irq_enabled;
	bool			reading;
	int			irq;
	atomic_t		irq_cnt;
	wait_queue_head_t	wq;
	raw_spinlock_t		spin_lock;
	u32			irq_ctrl_offset;
	struct ctcu_drvdata	*ctcu_drvdata;
	struct etr_buf_node	*buf_node;
};

struct ctcu_drvdata {
	void __iomem			*base;
	struct clk			*apb_clk;
	struct device			*dev;
	struct coresight_device		*csdev;
	struct ctcu_byte_cntr		byte_cntr_data[ETR_MAX_NUM];
	raw_spinlock_t			spin_lock;
	u32				atid_offset[ETR_MAX_NUM];
	/* refcnt for each traceid of each sink */
	u8				traceid_refcnt[ETR_MAX_NUM][CORESIGHT_TRACE_ID_RES_TOP];
};

/**
 * struct ctcu_byte_cntr_irq_attribute
 * @attr:	The device attribute.
 * @port:	port number.
 */
struct ctcu_byte_cntr_irq_attribute {
	struct device_attribute	attr;
	u8			port;
};

#define ctcu_byte_cntr_irq_rw(port)					\
	(&((struct ctcu_byte_cntr_irq_attribute[]) {			\
	   {								\
		__ATTR(irq_enabled##port, 0644, irq_enabled_show,	\
		irq_enabled_store),					\
		port,							\
	   }								\
	})[0].attr.attr)

void ctcu_program_register(struct ctcu_drvdata *drvdata, u32 val, u32 offset);

/* Byte-cntr functions */
void ctcu_byte_cntr_start(struct coresight_device *csdev, struct coresight_path *path);
void ctcu_byte_cntr_stop(struct coresight_device *csdev, struct coresight_path *path);
void ctcu_byte_cntr_init(struct device *dev, struct ctcu_drvdata *drvdata, int port_num);

#endif
