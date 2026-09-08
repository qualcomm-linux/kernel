// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 *
 * Description: CoreSight Replicator driver
 */

#include <linux/acpi.h>
#include <linux/amba/bus.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/coresight.h>

#include "coresight-priv.h"

#define REPLICATOR_IDFILTER0		0x000
#define REPLICATOR_IDFILTER1		0x004

DEFINE_CORESIGHT_DEVLIST(replicator_devs, "replicator");
static LIST_HEAD(replicator_delay_probe);
static enum cpuhp_state hp_online;
static DEFINE_SPINLOCK(delay_lock);

/**
 * struct replicator_drvdata - specifics associated to a replicator component
 * @base:	memory mapped base address for this component. Also indicates
 *		whether this one is programmable or not.
 * @atclk:	optional clock for the core parts of the replicator.
 * @pclk:	APB clock if present, otherwise NULL
 * @csdev:	component vitals needed by the framework
 * @spinlock:	serialize enable/disable operations.
 * @check_idfilter_val: check if the context is lost upon clock removal.
 * @cpumask:	CPU mask representing the CPUs related to this replicator.
 * @dev:	pointer to the device associated with this replicator.
 * @link:	link to the delay_probed list.
 */
struct replicator_drvdata {
	void __iomem		*base;
	struct clk		*atclk;
	struct clk		*pclk;
	struct coresight_device	*csdev;
	raw_spinlock_t		spinlock;
	bool			check_idfilter_val;
	struct cpumask		*cpumask;
	struct device		*dev;
	struct list_head	link;
};

struct replicator_smp_arg {
	struct replicator_drvdata *drvdata;
	int outport;
	u32 offset;
	int rc;
};

static void replicator_clear_self_claim_tag(struct replicator_drvdata *drvdata)
{
	struct csdev_access access = CSDEV_ACCESS_IOMEM(drvdata->base);

	coresight_clear_self_claim_tag(&access);
}

static int replicator_claim_device_unlocked(struct replicator_drvdata *drvdata)
{
	struct coresight_device *csdev = drvdata->csdev;
	struct csdev_access access = CSDEV_ACCESS_IOMEM(drvdata->base);
	u32 claim_tag;

	if (csdev)
		return coresight_claim_device_unlocked(csdev);

	writel_relaxed(CORESIGHT_CLAIM_SELF_HOSTED, drvdata->base + CORESIGHT_CLAIMSET);

	claim_tag = readl_relaxed(drvdata->base + CORESIGHT_CLAIMCLR);
	if (claim_tag != CORESIGHT_CLAIM_SELF_HOSTED) {
		coresight_clear_self_claim_tag_unlocked(&access);
		return -EBUSY;
	}

	return 0;
}

static void replicator_disclaim_device_unlocked(struct replicator_drvdata *drvdata)
{
	struct coresight_device *csdev = drvdata->csdev;
	struct csdev_access access = CSDEV_ACCESS_IOMEM(drvdata->base);

	if (csdev)
		return coresight_disclaim_device_unlocked(csdev);

	coresight_clear_self_claim_tag_unlocked(&access);
}

static void dynamic_replicator_reset(struct replicator_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	if (!replicator_claim_device_unlocked(drvdata)) {
		writel_relaxed(0xff, drvdata->base + REPLICATOR_IDFILTER0);
		writel_relaxed(0xff, drvdata->base + REPLICATOR_IDFILTER1);
		replicator_disclaim_device_unlocked(drvdata);
	}

	CS_LOCK(drvdata->base);
}

/*
 * replicator_reset : Reset the replicator configuration to sane values.
 */
static void replicator_reset(struct replicator_drvdata *drvdata)
{
	if (drvdata->base)
		dynamic_replicator_reset(drvdata);
}

static int dynamic_replicator_enable(struct replicator_drvdata *drvdata,
				     int inport, int outport)
{
	int rc = 0;
	u32 id0val, id1val;
	struct coresight_device *csdev = drvdata->csdev;

	CS_UNLOCK(drvdata->base);

	id0val = readl_relaxed(drvdata->base + REPLICATOR_IDFILTER0);
	id1val = readl_relaxed(drvdata->base + REPLICATOR_IDFILTER1);

	/*
	 * Some replicator designs lose context when AMBA clocks are removed,
	 * so have a check for this.
	 */
	if (drvdata->check_idfilter_val && id0val == 0x0 && id1val == 0x0)
		id0val = id1val = 0xff;

	if (id0val == 0xff && id1val == 0xff)
		rc = coresight_claim_device_unlocked(csdev);

	if (!rc) {
		switch (outport) {
		case 0:
			id0val = 0x0;
			break;
		case 1:
			id1val = 0x0;
			break;
		default:
			WARN_ON(1);
			rc = -EINVAL;
		}
	}

	/* Ensure that the outport is enabled. */
	if (!rc) {
		writel_relaxed(id0val, drvdata->base + REPLICATOR_IDFILTER0);
		writel_relaxed(id1val, drvdata->base + REPLICATOR_IDFILTER1);
	}

	CS_LOCK(drvdata->base);

	return rc;
}

static void replicator_enable_hw_smp_call(void *info)
{
	struct replicator_smp_arg *arg = info;

	arg->rc = dynamic_replicator_enable(arg->drvdata, 0, arg->outport);
}

static int replicator_enable_hw(struct replicator_drvdata *drvdata,
				int inport, int outport)
{
	int cpu, ret;
	struct replicator_smp_arg arg = { 0 };

	if (!drvdata->cpumask)
		return dynamic_replicator_enable(drvdata, 0, outport);

	arg.drvdata = drvdata;
	arg.outport = outport;

	for_each_cpu(cpu, drvdata->cpumask) {
		ret = smp_call_function_single(cpu, replicator_enable_hw_smp_call, &arg, 1);
		if (!ret)
			return arg.rc;
	}

	return ret;
}

static int replicator_enable(struct coresight_device *csdev,
			     struct coresight_connection *in,
			     struct coresight_connection *out,
			     enum cs_mode mode)
{
	int rc = 0;
	struct replicator_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	unsigned long flags;
	bool first_enable = false;

	raw_spin_lock_irqsave(&drvdata->spinlock, flags);

	if (out->src_refcnt == 0)
		first_enable = true;
	else
		out->src_refcnt++;

	if (mode == CS_MODE_PERF) {
		if (first_enable) {
			if (drvdata->cpumask &&
			    !cpumask_test_cpu(smp_processor_id(), drvdata->cpumask)) {
				raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);
				return -EINVAL;
			}

			if (drvdata->base)
				rc = dynamic_replicator_enable(drvdata, in->dest_port,
							       out->src_port);
			if (!rc)
				out->src_refcnt++;
		}
		raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return rc;
	}

	raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);

	if (first_enable) {
		if (drvdata->base)
			rc = replicator_enable_hw(drvdata, in->dest_port,
						  out->src_port);
		if (!rc) {
			out->src_refcnt++;
			dev_dbg(&csdev->dev, "REPLICATOR enabled\n");
			return rc;
		}
	}

	return rc;
}

static void dynamic_replicator_disable(struct replicator_drvdata *drvdata,
				       int inport, int outport)
{
	u32 reg;
	struct coresight_device *csdev = drvdata->csdev;

	switch (outport) {
	case 0:
		reg = REPLICATOR_IDFILTER0;
		break;
	case 1:
		reg = REPLICATOR_IDFILTER1;
		break;
	default:
		WARN_ON(1);
		return;
	}

	CS_UNLOCK(drvdata->base);

	/* disable the flow of ATB data through port */
	writel_relaxed(0xff, drvdata->base + reg);

	if ((readl_relaxed(drvdata->base + REPLICATOR_IDFILTER0) == 0xff) &&
	    (readl_relaxed(drvdata->base + REPLICATOR_IDFILTER1) == 0xff))
		coresight_disclaim_device_unlocked(csdev);
	CS_LOCK(drvdata->base);
}

static void replicator_disable(struct coresight_device *csdev,
			       struct coresight_connection *in,
			       struct coresight_connection *out)
{
	struct replicator_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	unsigned long flags;
	bool last_disable = false;

	raw_spin_lock_irqsave(&drvdata->spinlock, flags);
	if (--out->src_refcnt == 0) {
		if (drvdata->base)
			dynamic_replicator_disable(drvdata, in->dest_port,
						   out->src_port);
		last_disable = true;
	}

	raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);

	if (last_disable)
		dev_dbg(&csdev->dev, "REPLICATOR disabled\n");
}

static const struct coresight_ops_link replicator_link_ops = {
	.enable		= replicator_enable,
	.disable	= replicator_disable,
};

static const struct coresight_ops replicator_cs_ops = {
	.link_ops	= &replicator_link_ops,
};

static void replicator_read_register_smp_call(void *info)
{
	struct replicator_smp_arg *arg = info;

	arg->rc = readl_relaxed(arg->drvdata->base + arg->offset);
}

static ssize_t coresight_replicator_reg32_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct replicator_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cs_off_attribute *cs_attr = container_of(attr, struct cs_off_attribute, attr);
	unsigned long flags;
	struct replicator_smp_arg arg = { 0 };
	u32 val;
	int ret, cpu;

	pm_runtime_get_sync(dev->parent);

	if (!drvdata->cpumask) {
		raw_spin_lock_irqsave(&drvdata->spinlock, flags);
		val = readl_relaxed(drvdata->base + cs_attr->off);
		raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);

	} else {
		arg.drvdata = drvdata;
		arg.offset = cs_attr->off;
		for_each_cpu(cpu, drvdata->cpumask) {
			ret = smp_call_function_single(cpu,
						       replicator_read_register_smp_call,
						       &arg, 1);
			if (!ret)
				break;
		}
		if (!ret) {
			val = arg.rc;
		} else {
			pm_runtime_put_sync(dev->parent);
			return ret;
		}
	}

	pm_runtime_put_sync(dev->parent);

	return sysfs_emit(buf, "0x%x\n", val);
}

#define coresight_replicator_reg32(name, offset)				\
	(&((struct cs_off_attribute[]) {				\
	   {								\
		__ATTR(name, 0444, coresight_replicator_reg32_show, NULL),	\
		offset							\
	   }								\
	})[0].attr.attr)

static struct attribute *replicator_mgmt_attrs[] = {
	coresight_replicator_reg32(idfilter0, REPLICATOR_IDFILTER0),
	coresight_replicator_reg32(idfilter1, REPLICATOR_IDFILTER1),
	NULL,
};

static const struct attribute_group replicator_mgmt_group = {
	.attrs = replicator_mgmt_attrs,
	.name = "mgmt",
};

static const struct attribute_group *replicator_groups[] = {
	&replicator_mgmt_group,
	NULL,
};

static int replicator_add_coresight_dev(struct device *dev)
{
	struct coresight_desc desc = { 0 };
	struct replicator_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata->base) {
		desc.groups = replicator_groups;
		desc.access = CSDEV_ACCESS_IOMEM(drvdata->base);
	}

	desc.name = coresight_alloc_device_name(&replicator_devs, dev);
	if (!desc.name)
		return -ENOMEM;

	desc.type = CORESIGHT_DEV_TYPE_LINK;
	desc.subtype.link_subtype = CORESIGHT_DEV_SUBTYPE_LINK_SPLIT;
	desc.ops = &replicator_cs_ops;
	desc.pdata = dev->platform_data;
	desc.dev = dev;

	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);

	return 0;
}

static void replicator_init_hw(struct replicator_drvdata *drvdata)
{
	replicator_clear_self_claim_tag(drvdata);
	replicator_reset(drvdata);
}

static void replicator_init_on_cpu(void *info)
{
	struct replicator_drvdata *drvdata = info;

	replicator_init_hw(drvdata);
}

static struct cpumask *replicator_get_cpumask(struct device *dev)
{
	struct generic_pm_domain *pd;

	pd = pd_to_genpd(dev->pm_domain);
	if (pd)
		return pd->cpus;

	return NULL;
}

static int replicator_probe(struct device *dev, struct resource *res)
{
	struct coresight_platform_data *pdata = NULL;
	struct replicator_drvdata *drvdata;
	void __iomem *base;
	int cpu, ret;

	if (is_of_node(dev_fwnode(dev)) &&
	    of_device_is_compatible(dev->of_node, "arm,coresight-replicator"))
		dev_warn_once(dev,
			      "Uses OBSOLETE CoreSight replicator binding\n");

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	ret = coresight_get_enable_clocks(dev, &drvdata->pclk, &drvdata->atclk);
	if (ret)
		return ret;

	/*
	 * Map the device base for dynamic-replicator, which has been
	 * validated by AMBA core
	 */
	if (res) {
		base = devm_ioremap_resource(dev, res);
		if (IS_ERR(base))
			return PTR_ERR(base);
		drvdata->base = base;
	}

	if (fwnode_property_present(dev_fwnode(dev),
				    "qcom,replicator-loses-context"))
		drvdata->check_idfilter_val = true;

	dev_set_drvdata(dev, drvdata);

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	dev->platform_data = pdata;

	raw_spin_lock_init(&drvdata->spinlock);

	if (is_of_node(dev_fwnode(dev)) &&
	    of_device_is_compatible(dev->of_node, "arm,coresight-cpu-replicator")) {
		drvdata->cpumask = replicator_get_cpumask(dev);
		if (!drvdata->cpumask)
			return -EINVAL;
		drvdata->dev = dev;
		cpus_read_lock();
		for_each_cpu(cpu, drvdata->cpumask) {
			ret = smp_call_function_single(cpu,
						       replicator_init_on_cpu, drvdata, 1);
			if (!ret)
				break;
		}

		if (ret) {
			scoped_guard(spinlock,  &delay_lock)
				list_add(&drvdata->link, &replicator_delay_probe);
			cpus_read_unlock();
			return 0;
		}

		cpus_read_unlock();
	} else if (res) {
		replicator_init_hw(drvdata);
	}

	ret = replicator_add_coresight_dev(dev);

	return ret;
}

static int replicator_remove(struct device *dev)
{
	struct replicator_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata->csdev) {
		coresight_unregister(drvdata->csdev);
	} else {
		scoped_guard(spinlock,  &delay_lock)
			list_del(&drvdata->link);
	}

	return 0;
}

static int replicator_platform_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int ret;

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = replicator_probe(&pdev->dev, res);
	pm_runtime_put(&pdev->dev);
	if (ret)
		pm_runtime_disable(&pdev->dev);

	return ret;
}

static void replicator_platform_remove(struct platform_device *pdev)
{
	struct replicator_drvdata *drvdata = dev_get_drvdata(&pdev->dev);

	if (WARN_ON(!drvdata))
		return;

	replicator_remove(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

#ifdef CONFIG_PM
static int replicator_runtime_suspend(struct device *dev)
{
	struct replicator_drvdata *drvdata = dev_get_drvdata(dev);

	clk_disable_unprepare(drvdata->atclk);
	clk_disable_unprepare(drvdata->pclk);

	return 0;
}

static int replicator_runtime_resume(struct device *dev)
{
	struct replicator_drvdata *drvdata = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(drvdata->pclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(drvdata->atclk);
	if (ret)
		clk_disable_unprepare(drvdata->pclk);

	return ret;
}
#endif

static const struct dev_pm_ops replicator_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(replicator_runtime_suspend,
			   replicator_runtime_resume, NULL)
};

static const struct of_device_id replicator_match[] = {
	{.compatible = "arm,coresight-replicator"},
	{.compatible = "arm,coresight-static-replicator"},
	{.compatible = "arm,coresight-cpu-replicator"},
	{}
};

MODULE_DEVICE_TABLE(of, replicator_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id replicator_acpi_ids[] = {
	{"ARMHC985", 0, 0, 0}, /* ARM CoreSight Static Replicator */
	{"ARMHC98D", 0, 0, 0}, /* ARM CoreSight Dynamic Replicator */
	{}
};

MODULE_DEVICE_TABLE(acpi, replicator_acpi_ids);
#endif

static struct platform_driver replicator_driver = {
	.probe          = replicator_platform_probe,
	.remove         = replicator_platform_remove,
	.driver         = {
		.name   = "coresight-replicator",
		/* THIS_MODULE is taken care of by platform_driver_register() */
		.of_match_table = of_match_ptr(replicator_match),
		.acpi_match_table = ACPI_PTR(replicator_acpi_ids),
		.pm	= &replicator_dev_pm_ops,
		.suppress_bind_attrs = true,
	},
};

static int dynamic_replicator_probe(struct amba_device *adev,
				    const struct amba_id *id)
{
	int ret;

	ret = replicator_probe(&adev->dev, &adev->res);
	if (!ret)
		pm_runtime_put(&adev->dev);

	return ret;
}

static void dynamic_replicator_remove(struct amba_device *adev)
{
	replicator_remove(&adev->dev);
}

static const struct amba_id dynamic_replicator_ids[] = {
	CS_AMBA_ID(0x000bb909),
	CS_AMBA_ID(0x000bb9ec),		/* Coresight SoC-600 */
	{},
};

MODULE_DEVICE_TABLE(amba, dynamic_replicator_ids);

static struct amba_driver dynamic_replicator_driver = {
	.drv = {
		.name	= "coresight-dynamic-replicator",
		.pm	= &replicator_dev_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe		= dynamic_replicator_probe,
	.remove         = dynamic_replicator_remove,
	.id_table	= dynamic_replicator_ids,
};

static int replicator_online_cpu(unsigned int cpu)
{
	struct replicator_drvdata *drvdata, *tmp;
	int ret;

	spin_lock(&delay_lock);
	list_for_each_entry_safe(drvdata, tmp, &replicator_delay_probe, link) {
		if (cpumask_test_cpu(cpu, drvdata->cpumask)) {
			list_del(&drvdata->link);
			spin_unlock(&delay_lock);
			ret = pm_runtime_resume_and_get(drvdata->dev);
			if (ret < 0)
				return 0;

			replicator_clear_self_claim_tag(drvdata);
			replicator_reset(drvdata);
			replicator_add_coresight_dev(drvdata->dev);
			pm_runtime_put(drvdata->dev);
			spin_lock(&delay_lock);
		}
	}
	spin_unlock(&delay_lock);
	return 0;
}

static int __init replicator_init(void)
{
	int ret;

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"arm/coresight-replicator:online",
					replicator_online_cpu, NULL);

	if (ret > 0)
		hp_online = ret;
	else
		return ret;

	return coresight_init_driver("replicator", &dynamic_replicator_driver, &replicator_driver,
				     THIS_MODULE);
}

static void __exit replicator_exit(void)
{
	coresight_remove_driver(&dynamic_replicator_driver, &replicator_driver);
	if (hp_online) {
		cpuhp_remove_state_nocalls(hp_online);
		hp_online = 0;
	}
}

module_init(replicator_init);
module_exit(replicator_exit);

MODULE_AUTHOR("Pratik Patel <pratikp@codeaurora.org>");
MODULE_AUTHOR("Mathieu Poirier <mathieu.poirier@linaro.org>");
MODULE_DESCRIPTION("Arm CoreSight Replicator Driver");
MODULE_LICENSE("GPL v2");
