// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/coresight.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/uaccess.h>

#include "coresight-ctcu.h"
#include "coresight-priv.h"
#include "coresight-tmc.h"

static irqreturn_t byte_cntr_handler(int irq, void *data)
{
	struct ctcu_byte_cntr *byte_cntr_data = data;

	atomic_inc(&byte_cntr_data->irq_cnt);
	wake_up(&byte_cntr_data->wq);

	return IRQ_HANDLED;
}

static void ctcu_cfg_byte_cntr_reg(struct ctcu_drvdata *drvdata, u32 val,
				   u32 offset)
{
	/* A one value for IRQCTRL register represents 8 bytes */
	ctcu_program_register(drvdata, val / 8, offset);
}

static struct ctcu_byte_cntr *ctcu_get_byte_cntr(struct coresight_device *ctcu,
						 struct coresight_device *etr)
{
	struct ctcu_drvdata *drvdata = dev_get_drvdata(ctcu->dev.parent);
	int port;

	port = coresight_get_in_port(etr, ctcu);
	if (port < 0 || port > 1)
		return NULL;

	return &drvdata->byte_cntr_data[port];
}

static bool ctcu_byte_cntr_switch_buffer(struct tmc_drvdata *etr_drvdata,
					 struct ctcu_byte_cntr *byte_cntr_data)
{
	struct etr_buf_node *nd, *next, *curr_node = NULL, *picked_node = NULL;
	struct etr_buf *curr_buf = etr_drvdata->sysfs_buf;
	bool found_free_buf = false;

	if (WARN_ON(!etr_drvdata || !byte_cntr_data))
		return false;

	/* Stop the ETR before initiating the switch */
	if (coresight_get_mode(etr_drvdata->csdev) != CS_MODE_DISABLED)
		tmc_etr_enable_disable_hw(etr_drvdata, false);

	list_for_each_entry_safe(nd, next, &etr_drvdata->etr_buf_list, link) {
		/* curr_buf is free for next round */
		if (nd->sysfs_buf == curr_buf) {
			nd->is_free = true;
			curr_node = nd;
		} else if (!found_free_buf && nd->is_free) {
			picked_node = nd;
			found_free_buf = true;
		}
	}

	if (found_free_buf) {
		curr_node->pos = 0;
		curr_node->reading = true;
		byte_cntr_data->buf_node = curr_node;
		etr_drvdata->sysfs_buf = picked_node->sysfs_buf;
		etr_drvdata->etr_buf = picked_node->sysfs_buf;
		picked_node->is_free = false;
		/* Reset irq_cnt for next etr_buf */
		atomic_set(&byte_cntr_data->irq_cnt, 0);
		/* Restart the ETR once a free buffer is available */
		if (coresight_get_mode(etr_drvdata->csdev) != CS_MODE_DISABLED)
			tmc_etr_enable_disable_hw(etr_drvdata, true);
	}

	return found_free_buf;
}

/*
 * ctcu_byte_cntr_get_data() - reads data from the deactivated and filled buffer.
 * The byte-cntr reading work reads data from the deactivated and filled buffer.
 * The read operation waits for a buffer to become available, either filled or
 * upon timeout, and then reads trace data from the synced buffer.
 */
static ssize_t tmc_byte_cntr_get_data(struct tmc_drvdata *etr_drvdata, loff_t pos,
				      size_t len, char **bufpp)
{
	struct coresight_device *ctcu = tmc_etr_get_ctcu_device(etr_drvdata);
	struct device *dev = &etr_drvdata->csdev->dev;
	struct ctcu_byte_cntr *byte_cntr_data;
	struct etr_buf *sysfs_buf;
	atomic_t *irq_cnt;
	ssize_t actual;
	int ret;

	byte_cntr_data = ctcu_get_byte_cntr(ctcu, etr_drvdata->csdev);
	if (!byte_cntr_data || !byte_cntr_data->irq_enabled)
		return -EINVAL;

	irq_cnt = &byte_cntr_data->irq_cnt;

wait_buffer:
	if (!byte_cntr_data->buf_node) {
		ret = wait_event_interruptible_timeout(byte_cntr_data->wq,
				(atomic_read(irq_cnt) >= MAX_IRQ_CNT - 1) ||
				!byte_cntr_data->enable,
				BYTE_CNTR_TIMEOUT);
		if (ret < 0)
			return ret;
		/*
		 * The current etr_buf is almost full or timeout is triggered,
		 * so switch the buffer and mark the switched buffer as reading.
		 */
		if (byte_cntr_data->enable) {
			if (!ctcu_byte_cntr_switch_buffer(etr_drvdata, byte_cntr_data)) {
				dev_err(dev, "Switch buffer failed for the byte-cntr\n");
				return -ENOMEM;
			}
		} else {
			/* Exit byte-cntr reading */
			return 0;
		}
	}

	/* Check the status of current etr_buf */
	if (atomic_read(irq_cnt) >= MAX_IRQ_CNT)
		dev_warn(dev, "Data overwrite happened\n");

	pos = byte_cntr_data->buf_node->pos;
	sysfs_buf = byte_cntr_data->buf_node->sysfs_buf;
	actual = tmc_etr_read_sysfs_buf(sysfs_buf, pos, len, bufpp);
	if (actual <= 0) {
		/* Reset buf_node upon reading is finished or failed */
		byte_cntr_data->buf_node->reading = false;
		byte_cntr_data->buf_node = NULL;

		/*
		 * Nothing in the buffer, waiting for the next buffer
		 * to be filled.
		 */
		if (actual == 0)
			goto wait_buffer;
	}

	return actual;
}

static int tmc_read_prepare_byte_cntr(struct tmc_drvdata *etr_drvdata)
{
	struct coresight_device *ctcu = tmc_etr_get_ctcu_device(etr_drvdata);
	struct ctcu_byte_cntr *byte_cntr_data;
	unsigned long flags;
	int ret = 0;

	/* byte-cntr is operating with SYSFS mode being enabled only */
	if (coresight_get_mode(etr_drvdata->csdev) != CS_MODE_SYSFS)
		return -EINVAL;

	byte_cntr_data = ctcu_get_byte_cntr(ctcu, etr_drvdata->csdev);
	if (!byte_cntr_data || !byte_cntr_data->irq_enabled)
		return -EINVAL;

	raw_spin_lock_irqsave(&byte_cntr_data->spin_lock, flags);
	if (byte_cntr_data->reading) {
		raw_spin_unlock_irqrestore(&byte_cntr_data->spin_lock, flags);
		return -EBUSY;
	}

	byte_cntr_data->reading = true;
	raw_spin_unlock_irqrestore(&byte_cntr_data->spin_lock, flags);
	/* Setup an available etr_buf_list for byte-cntr */
	ret = tmc_create_etr_buf_list(etr_drvdata, 2);
	if (ret) {
		byte_cntr_data->reading = false;
		return ret;
	}

	guard(raw_spinlock_irqsave)(&byte_cntr_data->spin_lock);
	atomic_set(&byte_cntr_data->irq_cnt, 0);
	/*
	 * Configure the byte-cntr register to enable IRQ. The configured
	 * size is 5% of the buffer_size.
	 */
	ctcu_cfg_byte_cntr_reg(byte_cntr_data->ctcu_drvdata,
			       etr_drvdata->size / MAX_IRQ_CNT,
			       byte_cntr_data->irq_ctrl_offset);
	enable_irq_wake(byte_cntr_data->irq);
	byte_cntr_data->buf_node = NULL;

	return 0;
}

static int tmc_read_unprepare_byte_cntr(struct tmc_drvdata *etr_drvdata)
{
	struct coresight_device *ctcu = tmc_etr_get_ctcu_device(etr_drvdata);
	struct ctcu_byte_cntr *byte_cntr_data;

	byte_cntr_data = ctcu_get_byte_cntr(ctcu, etr_drvdata->csdev);
	if (!byte_cntr_data || !byte_cntr_data->irq_enabled)
		return -EINVAL;

	tmc_clean_etr_buf_list(etr_drvdata);
	scoped_guard(raw_spinlock_irqsave, &byte_cntr_data->spin_lock) {
		/* Configure the byte-cntr register to disable IRQ */
		ctcu_cfg_byte_cntr_reg(byte_cntr_data->ctcu_drvdata, 0,
				       byte_cntr_data->irq_ctrl_offset);
		disable_irq_wake(byte_cntr_data->irq);
		byte_cntr_data->buf_node = NULL;
		byte_cntr_data->reading = false;
	}
	wake_up(&byte_cntr_data->wq);

	return 0;
}

const struct tmc_sysfs_ops byte_cntr_sysfs_ops = {
	.read_prepare	= tmc_read_prepare_byte_cntr,
	.read_unprepare	= tmc_read_unprepare_byte_cntr,
	.get_trace_data	= tmc_byte_cntr_get_data,
};

/* Start the byte-cntr function when the path is enabled. */
void ctcu_byte_cntr_start(struct coresight_device *csdev, struct coresight_path *path)
{
	struct coresight_device *sink = coresight_get_sink(path);
	struct ctcu_byte_cntr *byte_cntr_data;

	byte_cntr_data = ctcu_get_byte_cntr(csdev, sink);
	if (!byte_cntr_data)
		return;

	/* Don't start byte-cntr function when irq_enabled is not set. */
	if (!byte_cntr_data->irq_enabled || byte_cntr_data->enable)
		return;

	guard(raw_spinlock_irqsave)(&byte_cntr_data->spin_lock);
	byte_cntr_data->enable = true;
}

/* Stop the byte-cntr function when the path is disabled. */
void ctcu_byte_cntr_stop(struct coresight_device *csdev, struct coresight_path *path)
{
	struct coresight_device *sink = coresight_get_sink(path);
	struct ctcu_byte_cntr *byte_cntr_data;

	if (coresight_get_mode(sink) == CS_MODE_SYSFS)
		return;

	byte_cntr_data = ctcu_get_byte_cntr(csdev, sink);
	if (!byte_cntr_data)
		return;

	guard(raw_spinlock_irqsave)(&byte_cntr_data->spin_lock);
	byte_cntr_data->enable = false;
}

void ctcu_byte_cntr_init(struct device *dev, struct ctcu_drvdata *drvdata, int etr_num)
{
	struct ctcu_byte_cntr *byte_cntr_data;
	struct device_node *nd = dev->of_node;
	int irq_num, ret, i, irq_registered = 0;

	for (i = 0; i < etr_num; i++) {
		byte_cntr_data = &drvdata->byte_cntr_data[i];
		irq_num = of_irq_get(nd, i);
		if (irq_num < 0) {
			dev_err(dev, "Failed to get IRQ from DT for port%d\n", i);
			continue;
		}

		ret = devm_request_irq(dev, irq_num, byte_cntr_handler,
				       IRQF_TRIGGER_RISING | IRQF_SHARED,
				       dev_name(dev), byte_cntr_data);
		if (ret) {
			dev_err(dev, "Failed to register IRQ for port%d\n", i);
			continue;
		}

		byte_cntr_data->irq = irq_num;
		byte_cntr_data->ctcu_drvdata = drvdata;
		init_waitqueue_head(&byte_cntr_data->wq);
		raw_spin_lock_init(&byte_cntr_data->spin_lock);
		irq_registered++;
	}

	if (irq_registered)
		tmc_etr_set_byte_cntr_sysfs_ops(&byte_cntr_sysfs_ops);
}
