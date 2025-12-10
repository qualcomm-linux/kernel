/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CORESIGHT_CTCU_H
#define _CORESIGHT_CTCU_H

#include <linux/time.h>
#include "coresight-trace-id.h"
#include "coresight-tmc.h"

/* Maximum number of supported ETR devices for a single CTCU. */
#define ETR_MAX_NUM	2

#define BYTE_CNTR_TIMEOUT	(5 * HZ)

/**
 * struct ctcu_etr_config
 * @atid_offset:	offset to the ATID0 Register.
 * @port_num:		in-port number of the CTCU device that connected to ETR.
 * @irq_ctrl_offset:    offset to the BYTECNTRVAL register.
 * @irq_name:           IRQ name in dt node.
 */
struct ctcu_etr_config {
	const u32 atid_offset;
	const u32 port_num;
	const u32 irq_ctrl_offset;
	const char *irq_name;
};

struct ctcu_config {
	const struct ctcu_etr_config *etr_cfgs;
	int num_etr_config;
};

/**
 * struct ctcu_byte_cntr
 * @enable:		indicates that byte_cntr function is enabled or not.
 * @reading:		indicates that byte-cntr reading is started.
 * @reading_buf:	indicates that byte-cntr is reading data from the buffer.
 * @thresh_val:		threshold to trigger a interruption.
 * @total_size:		total size of transferred data.
 * @irq:		allocated number of the IRQ.
 * @irq_cnt:		IRQ count number for triggered interruptions.
 * @wq:			waitqueue for reading data from ETR buffer.
 * @spin_lock:		spinlock of byte_cntr_data.
 * @irq_ctrl_offset:	offset to the BYTECNTVAL Register.
 * @irq_name:		IRQ name defined in DT.
 */
struct ctcu_byte_cntr {
	bool			enable;
	bool                    reading;
	bool			reading_buf;
	u32			thresh_val;
	u64			total_size;
	int			irq;
	atomic_t		irq_cnt;
	wait_queue_head_t	wq;
	raw_spinlock_t		spin_lock;
	u32			irq_ctrl_offset;
	const char		*irq_name;
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
	const struct sysfs_read_ops	*byte_cntr_sysfs_read_ops;
};

void ctcu_program_register(struct ctcu_drvdata *drvdata, u32 val, u32 offset);

/* Byte-cntr functions */
void ctcu_byte_cntr_start(struct coresight_device *csdev, struct coresight_path *path);
void ctcu_byte_cntr_stop(struct coresight_device *csdev, struct coresight_path *path);
void ctcu_byte_cntr_init(struct device *dev, struct ctcu_drvdata *drvdata, int port_num);

#endif
