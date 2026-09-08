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
	struct ctcu_byte_cntr *byte_cntr_data = (struct ctcu_byte_cntr *)data;

	atomic_inc(&byte_cntr_data->irq_cnt);
	wake_up(&byte_cntr_data->wq);

	return IRQ_HANDLED;
}

static void ctcu_reset_sysfs_buf(struct tmc_drvdata *drvdata)
{
	u32 sts;

	CS_UNLOCK(drvdata->base);
	tmc_write_rrp(drvdata, drvdata->sysfs_buf->hwaddr);
	tmc_write_rwp(drvdata, drvdata->sysfs_buf->hwaddr);
	sts = readl_relaxed(drvdata->base + TMC_STS) & ~TMC_STS_FULL;
	writel_relaxed(sts, drvdata->base + TMC_STS);
	CS_LOCK(drvdata->base);
}

static void ctcu_cfg_byte_cntr_reg(struct tmc_drvdata *drvdata, u32 val, u32 offset)
{
	struct ctcu_drvdata *ctcu_drvdata;
	struct coresight_device *helper;

	helper = coresight_get_helper(drvdata->csdev, CORESIGHT_DEV_SUBTYPE_HELPER_CTCU);
	if (!helper)
		return;

	ctcu_drvdata = dev_get_drvdata(helper->dev.parent);
	/* A one value for IRQCTRL register represents 8 bytes */
	ctcu_program_register(ctcu_drvdata, val / 8, offset);
}

static struct ctcu_byte_cntr *ctcu_get_byte_cntr_data(struct tmc_drvdata *drvdata)
{
	struct ctcu_byte_cntr *byte_cntr_data;
	struct ctcu_drvdata *ctcu_drvdata;
	struct coresight_device *helper;
	int port;

	helper = coresight_get_helper(drvdata->csdev, CORESIGHT_DEV_SUBTYPE_HELPER_CTCU);
	if (!helper)
		return NULL;

	port = coresight_get_in_port_dest(drvdata->csdev, helper);
	if (port < 0)
		return NULL;

	ctcu_drvdata = dev_get_drvdata(helper->dev.parent);
	byte_cntr_data = &ctcu_drvdata->byte_cntr_data[port];
	return byte_cntr_data;
}

static bool ctcu_byte_cntr_switch_buffer(struct tmc_drvdata *drvdata,
					 struct ctcu_byte_cntr *byte_cntr_data)
{
	struct etr_buf_node *nd, *next, *curr_node, *picked_node;
	struct etr_buf *curr_buf = drvdata->sysfs_buf;
	bool found_free_buf = false;

	if (WARN_ON(!drvdata || !byte_cntr_data))
		return found_free_buf;

	/* Stop the ETR before we start the switch */
	if (coresight_get_mode(drvdata->csdev) != CS_MODE_DISABLED)
		tmc_etr_enable_disable_hw(drvdata, false);

	list_for_each_entry_safe(nd, next, &drvdata->etr_buf_list, node) {
		/* curr_buf is free for next round */
		if (nd->sysfs_buf == curr_buf) {
			nd->is_free = true;
			curr_node = nd;
		}

		if (!found_free_buf && nd->is_free && nd->sysfs_buf != curr_buf) {
			picked_node = nd;
			found_free_buf = true;
		}
	}

	if (found_free_buf) {
		curr_node->pos = 0;
		drvdata->reading_node = curr_node;
		drvdata->sysfs_buf = picked_node->sysfs_buf;
		drvdata->etr_buf = picked_node->sysfs_buf;
		picked_node->is_free = false;
		/* Reset irq_cnt for next etr_buf */
		atomic_set(&byte_cntr_data->irq_cnt, 0);
		/* Reset rrp and rwp when the system has switched the buffer*/
		ctcu_reset_sysfs_buf(drvdata);
		/* Restart the ETR when we find a free buffer */
		if (coresight_get_mode(drvdata->csdev) != CS_MODE_DISABLED)
			tmc_etr_enable_disable_hw(drvdata, true);
	}

	return found_free_buf;
}

/*
 * ctcu_byte_cntr_get_data() - reads data from the deactivated and filled buffer.
 * The byte-cntr reading work reads data from the deactivated and filled buffer.
 * The read operation waits for a buffer to become available, either filled or
 * upon timeout, and then reads trace data from the synced buffer.
 */
static ssize_t ctcu_byte_cntr_get_data(struct tmc_drvdata *drvdata, loff_t pos,
				       size_t len, char **bufpp)
{
	struct etr_buf *sysfs_buf = drvdata->sysfs_buf;
	struct device *dev = &drvdata->csdev->dev;
	ssize_t actual, size = sysfs_buf->size;
	struct ctcu_byte_cntr *byte_cntr_data;
	size_t thresh_val;
	atomic_t *irq_cnt;
	int ret;

	byte_cntr_data = ctcu_get_byte_cntr_data(drvdata);
	if (!byte_cntr_data)
		return -EINVAL;

	thresh_val = byte_cntr_data->thresh_val;
	irq_cnt = &byte_cntr_data->irq_cnt;

wait_buffer:
	if (!byte_cntr_data->reading_buf) {
		ret = wait_event_interruptible_timeout(byte_cntr_data->wq,
				((atomic_read(irq_cnt) + 1) * thresh_val >= size) ||
				!byte_cntr_data->enable,
				BYTE_CNTR_TIMEOUT);
		if (ret < 0)
			return ret;
		/*
		 * The current etr_buf is almost full or timeout is triggered,
		 * so switch the buffer and mark the switched buffer as reading.
		 */
		if (byte_cntr_data->enable) {
			if (!ctcu_byte_cntr_switch_buffer(drvdata, byte_cntr_data)) {
				dev_err(dev, "Switch buffer failed for byte-cntr\n");
				return -EINVAL;
			}

			byte_cntr_data->reading_buf = true;
		} else {
			/*
			 * TMC-ETR has been disabled, so directly reads data from
			 * the drvdata->sysfs_buf.
			 */
			actual = drvdata->sysfs_ops->get_trace_data(drvdata, pos, len, bufpp);
			if (actual > 0) {
				byte_cntr_data->total_size += actual;
				return actual;
			}

			/* Exit byte-cntr reading */
			return 0;
		}
	}

	/* Check the status of current etr_buf*/
	if ((atomic_read(irq_cnt) + 1) * thresh_val >= size)
		/*
		 * Unlikely to find a free buffer to switch, so just disable
		 * the ETR for a while.
		 */
		if (!ctcu_byte_cntr_switch_buffer(drvdata, byte_cntr_data))
			dev_warn(dev, "No available buffer to store data, disable ETR\n");

	pos = drvdata->reading_node->pos;
	actual = drvdata->sysfs_ops->get_trace_data(drvdata, pos, len, bufpp);
	if (actual <= 0) {
		/* Reset flags upon reading is finished or failed */
		byte_cntr_data->reading_buf = false;
		drvdata->reading_node = NULL;

		/*
		 * Nothing in the buffer, waiting for the next buffer
		 * to be filled.
		 */
		if (actual == 0)
			goto wait_buffer;
	} else
		byte_cntr_data->total_size += actual;

	return actual;
}

static int ctcu_read_prepare_byte_cntr(struct tmc_drvdata *drvdata)
{
	struct ctcu_byte_cntr *byte_cntr_data;
	unsigned long flags;
	int ret = 0;

	/* config types are set a boot time and never change */
	if (WARN_ON_ONCE(drvdata->config_type != TMC_CONFIG_TYPE_ETR))
		return -EINVAL;

	/*
	 * Byte counter reading should start only after the TMC-ETR has been
	 * enabled, which implies that the sysfs_buf has already been setup
	 * in drvdata.
	 */
	if (!drvdata->sysfs_buf)
		return -EINVAL;

	byte_cntr_data = ctcu_get_byte_cntr_data(drvdata);
	if (!byte_cntr_data)
		return -EINVAL;

	/*
	 * The threshold value must not exceed the buffer size.
	 * A margin should be maintained between the two values to account
	 * for the time gap between the interrupt and buffer switching.
	 */
	if (byte_cntr_data->thresh_val + SZ_16K >= drvdata->size) {
		dev_err(&drvdata->csdev->dev, "The threshold value is too large\n");
		return -EINVAL;
	}

	raw_spin_lock_irqsave(&drvdata->spinlock, flags);
	if (byte_cntr_data->reading) {
		ret = -EBUSY;
		goto out_unlock;
	}

	byte_cntr_data->reading = true;
	raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);
	/* Setup an available etr_buf_list for byte-cntr */
	ret = tmc_create_etr_buf_list(drvdata, 2);
	if (ret)
		goto out;

	raw_spin_lock_irqsave(&drvdata->spinlock, flags);
	atomic_set(&byte_cntr_data->irq_cnt, 0);
	/* Configure the byte-cntr register to enable IRQ */
	ctcu_cfg_byte_cntr_reg(drvdata, byte_cntr_data->thresh_val,
			       byte_cntr_data->irq_ctrl_offset);
	enable_irq_wake(byte_cntr_data->irq);
	byte_cntr_data->total_size = 0;

out_unlock:
	raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);

out:
	return ret;
}

static int ctcu_read_unprepare_byte_cntr(struct tmc_drvdata *drvdata)
{
	struct device *dev = &drvdata->csdev->dev;
	struct ctcu_byte_cntr *byte_cntr_data;
	unsigned long flags;

	byte_cntr_data = ctcu_get_byte_cntr_data(drvdata);
	if (!byte_cntr_data)
		return -EINVAL;

	raw_spin_lock_irqsave(&drvdata->spinlock, flags);
	/* Configure the byte-cntr register to disable IRQ */
	ctcu_cfg_byte_cntr_reg(drvdata, 0, byte_cntr_data->irq_ctrl_offset);
	disable_irq_wake(byte_cntr_data->irq);
	byte_cntr_data->reading = false;
	byte_cntr_data->reading_buf = false;
	drvdata->reading_node = NULL;
	raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);
	dev_dbg(dev, "send data total size:%llu bytes\n", byte_cntr_data->total_size);
	tmc_clean_etr_buf_list(drvdata);

	return 0;
}

static const struct sysfs_read_ops byte_cntr_sysfs_read_ops = {
	.read_prepare	= ctcu_read_prepare_byte_cntr,
	.read_unprepare	= ctcu_read_unprepare_byte_cntr,
	.get_trace_data	= ctcu_byte_cntr_get_data,
};

/* Start the byte-cntr function when the path is enabled. */
void ctcu_byte_cntr_start(struct coresight_device *csdev, struct coresight_path *path)
{
	struct ctcu_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	struct coresight_device *sink = coresight_get_sink(path);
	struct ctcu_byte_cntr *byte_cntr_data;
	int port_num;

	if (!sink)
		return;

	port_num = coresight_get_in_port_dest(sink, csdev);
	if (port_num < 0)
		return;

	byte_cntr_data = &drvdata->byte_cntr_data[port_num];
	/* Don't start byte-cntr function when threshold is not set. */
	if (!byte_cntr_data->thresh_val || byte_cntr_data->enable)
		return;

	guard(raw_spinlock_irqsave)(&byte_cntr_data->spin_lock);
	byte_cntr_data->enable = true;
	byte_cntr_data->reading_buf = false;
}

/* Stop the byte-cntr function when the path is disabled. */
void ctcu_byte_cntr_stop(struct coresight_device *csdev, struct coresight_path *path)
{
	struct ctcu_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	struct coresight_device *sink = coresight_get_sink(path);
	struct ctcu_byte_cntr *byte_cntr_data;
	int port_num;

	if (!sink || coresight_get_mode(sink) == CS_MODE_SYSFS)
		return;

	port_num = coresight_get_in_port_dest(sink, csdev);
	if (port_num < 0)
		return;

	byte_cntr_data = &drvdata->byte_cntr_data[port_num];
	guard(raw_spinlock_irqsave)(&byte_cntr_data->spin_lock);
	byte_cntr_data->enable = false;
}

void ctcu_byte_cntr_init(struct device *dev, struct ctcu_drvdata *drvdata, int etr_num)
{
	struct ctcu_byte_cntr *byte_cntr_data;
	struct device_node *nd = dev->of_node;
	int irq_num, ret, i;

	drvdata->byte_cntr_sysfs_read_ops = &byte_cntr_sysfs_read_ops;
	for (i = 0; i < etr_num; i++) {
		byte_cntr_data = &drvdata->byte_cntr_data[i];
		irq_num = of_irq_get_byname(nd, byte_cntr_data->irq_name);
		if (irq_num < 0) {
			dev_err(dev, "Failed to get IRQ from DT for %s\n",
				byte_cntr_data->irq_name);
			continue;
		}

		ret = devm_request_irq(dev, irq_num, byte_cntr_handler,
				       IRQF_TRIGGER_RISING | IRQF_SHARED,
				       dev_name(dev), byte_cntr_data);
		if (ret) {
			dev_err(dev, "Failed to register IRQ for %s\n",
				byte_cntr_data->irq_name);
			continue;
		}

		byte_cntr_data->irq = irq_num;
		init_waitqueue_head(&byte_cntr_data->wq);
	}
}
