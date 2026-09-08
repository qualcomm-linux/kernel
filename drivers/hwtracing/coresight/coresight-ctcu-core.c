// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/coresight.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/sizes.h>

#include "coresight-ctcu.h"
#include "coresight-priv.h"

DEFINE_CORESIGHT_DEVLIST(ctcu_devs, "ctcu");

#define ctcu_writel(drvdata, val, offset)	__raw_writel((val), drvdata->base + offset)
#define ctcu_readl(drvdata, offset)		__raw_readl(drvdata->base + offset)

/*
 * The TMC Coresight Control Unit utilizes four ATID registers to control the data
 * filter function based on the trace ID for each TMC ETR sink. The length of each
 * ATID register is 32 bits. Therefore, an ETR device has a 128-bit long field
 * in CTCU. Each trace ID is represented by one bit in that filed.
 * e.g. ETR0ATID0 layout, set bit 5 for traceid 5
 *                                           bit5
 * ------------------------------------------------------
 * |   |28|   |24|   |20|   |16|   |12|   |8|  1|4|   |0|
 * ------------------------------------------------------
 *
 * e.g. ETR0:
 * 127                     0 from ATID_offset for ETR0ATID0
 * -------------------------
 * |ATID3|ATID2|ATID1|ATID0|
 */
#define CTCU_ATID_REG_OFFSET(traceid, atid_offset) \
		((traceid / 32) * 4 + atid_offset)

#define CTCU_ATID_REG_BIT(traceid)	(traceid % 32)
#define CTCU_ATID_REG_SIZE		0x10
#define CTCU_ETR0_IRQCTRL               0x6c
#define CTCU_ETR1_IRQCTRL               0x70
#define CTCU_ETR0_ATID0			0xf8
#define CTCU_ETR1_ATID0			0x108

static const struct ctcu_etr_config sa8775p_etr_cfgs[] = {
	{
		.atid_offset		= CTCU_ETR0_ATID0,
		.irq_ctrl_offset	= CTCU_ETR0_IRQCTRL,
		.irq_name		= "etr0",
		.port_num		= 0,
	},
	{
		.atid_offset		= CTCU_ETR1_ATID0,
		.irq_ctrl_offset	= CTCU_ETR1_IRQCTRL,
		.irq_name		= "etr1",
		.port_num		= 1,
	},
};

static const struct ctcu_config sa8775p_cfgs = {
	.etr_cfgs	= sa8775p_etr_cfgs,
	.num_etr_config	= ARRAY_SIZE(sa8775p_etr_cfgs),
};

void ctcu_program_register(struct ctcu_drvdata *drvdata, u32 val, u32 offset)
{
	CS_UNLOCK(drvdata->base);
	ctcu_writel(drvdata, val, offset);
	CS_LOCK(drvdata->base);
}

static ssize_t irq_threshold_show(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	struct ctcu_drvdata *drvdata = dev_get_drvdata(dev->parent);
	int i, len = 0;

	for (i = 0; i < ETR_MAX_NUM; i++) {
		if (drvdata->byte_cntr_data[i].irq_ctrl_offset)
			len += scnprintf(buf + len, PAGE_SIZE - len, "%u ",
					 drvdata->byte_cntr_data[i].thresh_val);
	}

	len += scnprintf(buf + len, PAGE_SIZE - len, "\n");

	return len;
}

/* Program a valid value into IRQCTRL register will enable byte-cntr interrupt */
static ssize_t irq_threshold_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct ctcu_drvdata *drvdata = dev_get_drvdata(dev->parent);
	u32 thresh_vals[ETR_MAX_NUM] = { 0 };
	int num, i;

	num = sscanf(buf, "%i %i", &thresh_vals[0], &thresh_vals[1]);
	if (num <= 0 || num > ETR_MAX_NUM)
		return -EINVAL;

	/* Threshold 0 disables the interruption. */
	guard(raw_spinlock_irqsave)(&drvdata->spin_lock);
	for (i = 0; i < num; i++) {
		/* A small threshold will result in a large number of interruptions */
		if (thresh_vals[i] && thresh_vals[i] < SZ_4K)
			return -EINVAL;

		if (drvdata->byte_cntr_data[i].irq_ctrl_offset)
			drvdata->byte_cntr_data[i].thresh_val = thresh_vals[i];
	}

	return size;
}
static DEVICE_ATTR_RW(irq_threshold);

static struct attribute *ctcu_attrs[] = {
	&dev_attr_irq_threshold.attr,
	NULL,
};

static struct attribute_group ctcu_attr_grp = {
	.attrs = ctcu_attrs,
};

static const struct attribute_group *ctcu_attr_grps[] = {
	&ctcu_attr_grp,
	NULL,
};

static void ctcu_program_atid_register(struct ctcu_drvdata *drvdata, u32 reg_offset,
				       u8 bit, bool enable)
{
	u32 val;

	CS_UNLOCK(drvdata->base);
	val = ctcu_readl(drvdata, reg_offset);
	if (enable)
		val |= BIT(bit);
	else
		val &= ~BIT(bit);

	ctcu_writel(drvdata, val, reg_offset);
	CS_LOCK(drvdata->base);
}

/*
 * __ctcu_set_etr_traceid: Set bit in the ATID register based on trace ID when enable is true.
 * Reset the bit of the ATID register based on trace ID when enable is false.
 *
 * @csdev:	coresight_device of CTCU.
 * @traceid:	trace ID of the source tracer.
 * @port_num:	port number connected to TMC ETR sink.
 * @enable:	True for set bit and false for reset bit.
 *
 * Returns 0 indicates success. Non-zero result means failure.
 */
static int __ctcu_set_etr_traceid(struct coresight_device *csdev, u8 traceid, int port_num,
				  bool enable)
{
	struct ctcu_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	u32 atid_offset, reg_offset;
	u8 refcnt, bit;

	atid_offset = drvdata->atid_offset[port_num];
	if (atid_offset == 0)
		return -EINVAL;

	bit = CTCU_ATID_REG_BIT(traceid);
	reg_offset = CTCU_ATID_REG_OFFSET(traceid, atid_offset);
	if (reg_offset - atid_offset > CTCU_ATID_REG_SIZE)
		return -EINVAL;

	guard(raw_spinlock_irqsave)(&drvdata->spin_lock);
	refcnt = drvdata->traceid_refcnt[port_num][traceid];
	/* Only program the atid register when the refcnt value is 1 or 0 */
	if ((enable && !refcnt++) || (!enable && !--refcnt))
		ctcu_program_atid_register(drvdata, reg_offset, bit, enable);

	drvdata->traceid_refcnt[port_num][traceid] = refcnt;

	return 0;
}

static int ctcu_set_etr_traceid(struct coresight_device *csdev, struct coresight_path *path,
				bool enable)
{
	struct coresight_device *sink = coresight_get_sink(path);
	u8 traceid = path->trace_id;
	int port_num;

	if ((sink == NULL) || !IS_VALID_CS_TRACE_ID(traceid)) {
		dev_err(&csdev->dev, "Invalid sink device or trace ID\n");
		return -EINVAL;
	}

	port_num = coresight_get_in_port_dest(sink, csdev);
	if (port_num < 0)
		return -EINVAL;

	dev_dbg(&csdev->dev, "traceid is %d\n", traceid);

	return __ctcu_set_etr_traceid(csdev, traceid, port_num, enable);
}

static int ctcu_enable(struct coresight_device *csdev, enum cs_mode mode, void *data)
{
	struct coresight_path *path = (struct coresight_path *)data;

	ctcu_byte_cntr_start(csdev, path);

	return ctcu_set_etr_traceid(csdev, path, true);
}

static int ctcu_disable(struct coresight_device *csdev, void *data)
{
	struct coresight_path *path = (struct coresight_path *)data;

	ctcu_byte_cntr_stop(csdev, path);

	return ctcu_set_etr_traceid(csdev, path, false);
}

static const struct coresight_ops_helper ctcu_helper_ops = {
	.enable = ctcu_enable,
	.disable = ctcu_disable,
};

static const struct coresight_ops ctcu_ops = {
	.helper_ops = &ctcu_helper_ops,
};

static int ctcu_probe(struct platform_device *pdev)
{
	const struct ctcu_etr_config *etr_cfg;
	struct coresight_platform_data *pdata;
	struct coresight_desc desc = { 0 };
	struct device *dev = &pdev->dev;
	const struct ctcu_config *cfgs;
	struct ctcu_drvdata *drvdata;
	void __iomem *base;
	int i, ret;

	desc.name = coresight_alloc_device_name(&ctcu_devs, dev);
	if (!desc.name)
		return -ENOMEM;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	dev->platform_data = pdata;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ret = coresight_get_enable_clocks(dev, &drvdata->apb_clk, NULL);
	if (ret)
		return ret;

	cfgs = of_device_get_match_data(dev);
	if (cfgs) {
		if (cfgs->num_etr_config <= ETR_MAX_NUM) {
			for (i = 0; i < cfgs->num_etr_config; i++) {
				etr_cfg = &cfgs->etr_cfgs[i];
				drvdata->atid_offset[i] = etr_cfg->atid_offset;
				drvdata->byte_cntr_data[i].irq_name = etr_cfg->irq_name;
				drvdata->byte_cntr_data[i].irq_ctrl_offset =
					etr_cfg->irq_ctrl_offset;
			}
			ctcu_byte_cntr_init(dev, drvdata, cfgs->num_etr_config);
		}
	}

	drvdata->base = base;
	drvdata->dev = dev;
	platform_set_drvdata(pdev, drvdata);

	desc.type = CORESIGHT_DEV_TYPE_HELPER;
	desc.subtype.helper_subtype = CORESIGHT_DEV_SUBTYPE_HELPER_CTCU;
	desc.pdata = pdata;
	desc.dev = dev;
	desc.groups = ctcu_attr_grps;
	desc.ops = &ctcu_ops;
	desc.access = CSDEV_ACCESS_IOMEM(base);

	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);

	return 0;
}

static void ctcu_remove(struct platform_device *pdev)
{
	struct ctcu_drvdata *drvdata = platform_get_drvdata(pdev);

	coresight_unregister(drvdata->csdev);
}

static int ctcu_platform_probe(struct platform_device *pdev)
{
	int ret;

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = ctcu_probe(pdev);
	pm_runtime_put(&pdev->dev);
	if (ret)
		pm_runtime_disable(&pdev->dev);

	return ret;
}

static void ctcu_platform_remove(struct platform_device *pdev)
{
	struct ctcu_drvdata *drvdata = platform_get_drvdata(pdev);

	if (WARN_ON(!drvdata))
		return;

	ctcu_remove(pdev);
	pm_runtime_disable(&pdev->dev);
}

#ifdef CONFIG_PM
static int ctcu_runtime_suspend(struct device *dev)
{
	struct ctcu_drvdata *drvdata = dev_get_drvdata(dev);

	clk_disable_unprepare(drvdata->apb_clk);

	return 0;
}

static int ctcu_runtime_resume(struct device *dev)
{
	struct ctcu_drvdata *drvdata = dev_get_drvdata(dev);

	return clk_prepare_enable(drvdata->apb_clk);
}
#endif

static const struct dev_pm_ops ctcu_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(ctcu_runtime_suspend, ctcu_runtime_resume, NULL)
};

static const struct of_device_id ctcu_match[] = {
	{.compatible = "qcom,sa8775p-ctcu", .data = &sa8775p_cfgs},
	{}
};

static struct platform_driver ctcu_driver = {
	.probe          = ctcu_platform_probe,
	.remove         = ctcu_platform_remove,
	.driver         = {
		.name   = "coresight-ctcu",
		.of_match_table = ctcu_match,
		.pm	= &ctcu_dev_pm_ops,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(ctcu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CoreSight TMC Control Unit driver");
