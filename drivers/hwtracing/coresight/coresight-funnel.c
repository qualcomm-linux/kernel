// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
 *
 * Description: CoreSight Funnel driver
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/coresight.h>
#include <linux/amba/bus.h>
#include <linux/clk.h>

#include "coresight-priv.h"

#define FUNNEL_FUNCTL		0x000
#define FUNNEL_PRICTL		0x004

#define FUNNEL_HOLDTIME_MASK	0xf00
#define FUNNEL_HOLDTIME_SHFT	0x8
#define FUNNEL_HOLDTIME		(0x7 << FUNNEL_HOLDTIME_SHFT)
#define FUNNEL_ENSx_MASK	0xff

DEFINE_CORESIGHT_DEVLIST(funnel_devs, "funnel");
static LIST_HEAD(funnel_delay_probe);
static enum cpuhp_state hp_online;
static DEFINE_SPINLOCK(delay_lock);

/**
 * struct funnel_drvdata - specifics associated to a funnel component
 * @base:	memory mapped base address for this component.
 * @atclk:	optional clock for the core parts of the funnel.
 * @pclk:	APB clock if present, otherwise NULL
 * @csdev:	component vitals needed by the framework.
 * @priority:	port selection order.
 * @spinlock:	serialize enable/disable operations.
 * @cpumask:	CPU mask representing the CPUs related to this funnel.
 * @dev:	pointer to the device associated with this funnel.
 * @link:	list node for adding this funnel to the delayed probe list.
 */
struct funnel_drvdata {
	void __iomem		*base;
	struct clk		*atclk;
	struct clk		*pclk;
	struct coresight_device	*csdev;
	unsigned long		priority;
	raw_spinlock_t		spinlock;
	struct cpumask		*cpumask;
	struct device		*dev;
	struct list_head	link;
};

struct funnel_smp_arg {
	struct funnel_drvdata *drvdata;
	int port;
	int rc;
};

static int dynamic_funnel_enable_hw(struct funnel_drvdata *drvdata, int port)
{
	u32 functl;
	int rc = 0;
	struct coresight_device *csdev = drvdata->csdev;

	CS_UNLOCK(drvdata->base);

	functl = readl_relaxed(drvdata->base + FUNNEL_FUNCTL);
	/* Claim the device only when we enable the first slave */
	if (!(functl & FUNNEL_ENSx_MASK)) {
		rc = coresight_claim_device_unlocked(csdev);
		if (rc)
			goto done;
	}

	functl &= ~FUNNEL_HOLDTIME_MASK;
	functl |= FUNNEL_HOLDTIME;
	functl |= (1 << port);
	writel_relaxed(functl, drvdata->base + FUNNEL_FUNCTL);
	writel_relaxed(drvdata->priority, drvdata->base + FUNNEL_PRICTL);
done:
	CS_LOCK(drvdata->base);
	return rc;
}

static void funnel_enable_hw_smp_call(void *info)
{
	struct funnel_smp_arg *arg = info;

	arg->rc = dynamic_funnel_enable_hw(arg->drvdata, arg->port);
}

static int funnel_enable_hw(struct funnel_drvdata *drvdata, int port)
{
	int cpu, ret;
	struct funnel_smp_arg arg = { 0 };

	if (!drvdata->cpumask)
		return dynamic_funnel_enable_hw(drvdata, port);

	arg.drvdata = drvdata;
	arg.port = port;

	for_each_cpu(cpu, drvdata->cpumask) {
		ret = smp_call_function_single(cpu,
					       funnel_enable_hw_smp_call, &arg, 1);
		if (!ret)
			return arg.rc;
	}
	return ret;
}

static int funnel_enable(struct coresight_device *csdev,
			 struct coresight_connection *in,
			 struct coresight_connection *out,
			 enum cs_mode mode)
{
	int rc = 0;
	struct funnel_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	unsigned long flags;
	bool first_enable = false;

	raw_spin_lock_irqsave(&drvdata->spinlock, flags);

	if (in->dest_refcnt == 0)
		first_enable = true;
	else
		in->dest_refcnt++;

	if (mode == CS_MODE_PERF) {
		if (first_enable) {
			if (drvdata->cpumask &&
			    !cpumask_test_cpu(smp_processor_id(), drvdata->cpumask)) {
				raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);
				return -EINVAL;
			}

			if (drvdata->base)
				rc = dynamic_funnel_enable_hw(drvdata, in->dest_port);
			if (!rc)
				in->dest_refcnt++;
		}
		raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return rc;
	}

	raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);

	if (first_enable) {
		if (drvdata->base)
			rc = funnel_enable_hw(drvdata, in->dest_port);
		if (!rc) {
			in->dest_refcnt++;
			dev_dbg(&csdev->dev, "FUNNEL inport %d enabled\n",
				in->dest_port);
		}
	}

	return rc;
}

static void dynamic_funnel_disable_hw(struct funnel_drvdata *drvdata,
				      int inport)
{
	u32 functl;
	struct coresight_device *csdev = drvdata->csdev;

	CS_UNLOCK(drvdata->base);

	functl = readl_relaxed(drvdata->base + FUNNEL_FUNCTL);
	functl &= ~(1 << inport);
	writel_relaxed(functl, drvdata->base + FUNNEL_FUNCTL);

	/* Disclaim the device if none of the slaves are now active */
	if (!(functl & FUNNEL_ENSx_MASK))
		coresight_disclaim_device_unlocked(csdev);

	CS_LOCK(drvdata->base);
}

static void funnel_disable(struct coresight_device *csdev,
			   struct coresight_connection *in,
			   struct coresight_connection *out)
{
	struct funnel_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	unsigned long flags;
	bool last_disable = false;

	raw_spin_lock_irqsave(&drvdata->spinlock, flags);
	if (--in->dest_refcnt == 0) {
		if (drvdata->base)
			dynamic_funnel_disable_hw(drvdata, in->dest_port);
		last_disable = true;
	}

	raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);

	if (last_disable)
		dev_dbg(&csdev->dev, "FUNNEL inport %d disabled\n",
			in->dest_port);
}

static const struct coresight_ops_link funnel_link_ops = {
	.enable		= funnel_enable,
	.disable	= funnel_disable,
};

static const struct coresight_ops funnel_cs_ops = {
	.link_ops	= &funnel_link_ops,
};

static ssize_t priority_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct funnel_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val = drvdata->priority;

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t priority_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct funnel_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->priority = val;
	return size;
}
static DEVICE_ATTR_RW(priority);

static u32 get_funnel_ctrl_hw(struct funnel_drvdata *drvdata)
{
	u32 functl;

	CS_UNLOCK(drvdata->base);
	functl = readl_relaxed(drvdata->base + FUNNEL_FUNCTL);
	CS_LOCK(drvdata->base);

	return functl;
}

static void get_funnel_ctrl_smp_call(void *info)
{
	struct funnel_smp_arg *arg = info;

	arg->rc = get_funnel_ctrl_hw(arg->drvdata);
}

static ssize_t funnel_ctrl_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	u32 val;
	int cpu, ret;
	struct funnel_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct funnel_smp_arg arg = { 0 };

	pm_runtime_get_sync(dev->parent);
	if (!drvdata->cpumask) {
		val = get_funnel_ctrl_hw(drvdata);
	} else {
		arg.drvdata = drvdata;
		for_each_cpu(cpu, drvdata->cpumask) {
			ret = smp_call_function_single(cpu,
						       get_funnel_ctrl_smp_call, &arg, 1);
			if (!ret)
				break;
		}
		if (!ret) {
			val =  arg.rc;
		} else {
			pm_runtime_put(dev->parent);
			return ret;
		}
	}

	pm_runtime_put(dev->parent);

	return sprintf(buf, "%#x\n", val);
}
static DEVICE_ATTR_RO(funnel_ctrl);

static struct attribute *coresight_funnel_attrs[] = {
	&dev_attr_funnel_ctrl.attr,
	&dev_attr_priority.attr,
	NULL,
};
ATTRIBUTE_GROUPS(coresight_funnel);

static void funnel_clear_self_claim_tag(struct funnel_drvdata *drvdata)
{
	struct csdev_access access = CSDEV_ACCESS_IOMEM(drvdata->base);

	coresight_clear_self_claim_tag(&access);
}

static void funnel_init_on_cpu(void *info)
{
	struct funnel_drvdata *drvdata = info;

	funnel_clear_self_claim_tag(drvdata);
}

static int funnel_add_coresight_dev(struct device *dev)
{
	struct coresight_desc desc = { 0 };
	struct funnel_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata->base) {
		desc.groups = coresight_funnel_groups;
		desc.access = CSDEV_ACCESS_IOMEM(drvdata->base);
	}

	desc.name = coresight_alloc_device_name(&funnel_devs, dev);
	if (!desc.name)
		return -ENOMEM;

	desc.type = CORESIGHT_DEV_TYPE_LINK;
	desc.subtype.link_subtype = CORESIGHT_DEV_SUBTYPE_LINK_MERG;
	desc.ops = &funnel_cs_ops;
	desc.pdata = dev->platform_data;
	desc.dev = dev;

	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);
	return 0;
}

static struct cpumask *funnel_get_cpumask(struct device *dev)
{
	struct generic_pm_domain *pd;

	pd = pd_to_genpd(dev->pm_domain);
	if (pd)
		return pd->cpus;

	return NULL;
}

static int funnel_probe(struct device *dev, struct resource *res)
{
	void __iomem *base;
	struct coresight_platform_data *pdata = NULL;
	struct funnel_drvdata *drvdata;
	int cpu, ret;

	if (is_of_node(dev_fwnode(dev)) &&
	    of_device_is_compatible(dev->of_node, "arm,coresight-funnel"))
		dev_warn_once(dev, "Uses OBSOLETE CoreSight funnel binding\n");

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	ret = coresight_get_enable_clocks(dev, &drvdata->pclk, &drvdata->atclk);
	if (ret)
		return ret;

	/*
	 * Map the device base for dynamic-funnel, which has been
	 * validated by AMBA core.
	 */
	if (res) {
		base = devm_ioremap_resource(dev, res);
		if (IS_ERR(base))
			return PTR_ERR(base);
		drvdata->base = base;
	}

	dev_set_drvdata(dev, drvdata);

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);

	dev->platform_data = pdata;

	raw_spin_lock_init(&drvdata->spinlock);

	if (is_of_node(dev_fwnode(dev)) &&
	    of_device_is_compatible(dev->of_node, "arm,coresight-cpu-funnel")) {
		drvdata->cpumask = funnel_get_cpumask(dev);
		if (!drvdata->cpumask)
			return -EINVAL;
		drvdata->dev = dev;
		cpus_read_lock();
		for_each_cpu(cpu, drvdata->cpumask) {
			ret = smp_call_function_single(cpu,
						       funnel_init_on_cpu, drvdata, 1);
			if (!ret)
				break;
		}

		if (ret) {
			scoped_guard(spinlock,  &delay_lock)
				list_add(&drvdata->link, &funnel_delay_probe);
			cpus_read_unlock();
			return 0;
		}

		cpus_read_unlock();
	} else if (res) {
		funnel_clear_self_claim_tag(drvdata);
	}

	return funnel_add_coresight_dev(dev);
}

static int funnel_remove(struct device *dev)
{
	struct funnel_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata->csdev) {
		coresight_unregister(drvdata->csdev);
	} else {
		scoped_guard(spinlock,  &delay_lock)
			list_del(&drvdata->link);
	}
	return 0;
}

#ifdef CONFIG_PM
static int funnel_runtime_suspend(struct device *dev)
{
	struct funnel_drvdata *drvdata = dev_get_drvdata(dev);

	clk_disable_unprepare(drvdata->atclk);
	clk_disable_unprepare(drvdata->pclk);

	return 0;
}

static int funnel_runtime_resume(struct device *dev)
{
	struct funnel_drvdata *drvdata = dev_get_drvdata(dev);
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

static const struct dev_pm_ops funnel_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(funnel_runtime_suspend, funnel_runtime_resume, NULL)
};

static int funnel_platform_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int ret;

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = funnel_probe(&pdev->dev, res);
	pm_runtime_put(&pdev->dev);
	if (ret)
		pm_runtime_disable(&pdev->dev);

	return ret;
}

static void funnel_platform_remove(struct platform_device *pdev)
{
	struct funnel_drvdata *drvdata = dev_get_drvdata(&pdev->dev);

	if (WARN_ON(!drvdata))
		return;

	funnel_remove(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id funnel_match[] = {
	{.compatible = "arm,coresight-static-funnel"},
	{.compatible = "arm,coresight-cpu-funnel"},
	{}
};

MODULE_DEVICE_TABLE(of, funnel_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id funnel_acpi_ids[] = {
	{"ARMHC9FE", 0, 0, 0}, /* ARM Coresight Static Funnel */
	{"ARMHC9FF", 0, 0, 0}, /* ARM CoreSight Dynamic Funnel */
	{},
};

MODULE_DEVICE_TABLE(acpi, funnel_acpi_ids);
#endif

static struct platform_driver funnel_driver = {
	.probe		= funnel_platform_probe,
	.remove		= funnel_platform_remove,
	.driver		= {
		.name   = "coresight-funnel",
		/* THIS_MODULE is taken care of by platform_driver_register() */
		.of_match_table = funnel_match,
		.acpi_match_table = ACPI_PTR(funnel_acpi_ids),
		.pm	= &funnel_dev_pm_ops,
		.suppress_bind_attrs = true,
	},
};

static int dynamic_funnel_probe(struct amba_device *adev,
				const struct amba_id *id)
{
	int ret;

	ret = funnel_probe(&adev->dev, &adev->res);
	if (!ret)
		pm_runtime_put(&adev->dev);

	return ret;
}

static void dynamic_funnel_remove(struct amba_device *adev)
{
	funnel_remove(&adev->dev);
}

static const struct amba_id dynamic_funnel_ids[] = {
	{
		.id     = 0x000bb908,
		.mask   = 0x000fffff,
	},
	{
		/* Coresight SoC-600 */
		.id     = 0x000bb9eb,
		.mask   = 0x000fffff,
	},
	{ 0, 0, NULL },
};

MODULE_DEVICE_TABLE(amba, dynamic_funnel_ids);

static struct amba_driver dynamic_funnel_driver = {
	.drv = {
		.name	= "coresight-dynamic-funnel",
		.pm	= &funnel_dev_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe		= dynamic_funnel_probe,
	.remove		= dynamic_funnel_remove,
	.id_table	= dynamic_funnel_ids,
};

static int funnel_online_cpu(unsigned int cpu)
{
	struct funnel_drvdata *drvdata, *tmp;
	int ret;

	list_for_each_entry_safe(drvdata, tmp, &funnel_delay_probe, link) {
		if (cpumask_test_cpu(cpu, drvdata->cpumask)) {
			scoped_guard(spinlock,  &delay_lock)
				list_del(&drvdata->link);

			ret = pm_runtime_resume_and_get(drvdata->dev);
			if (ret < 0)
				return 0;

			funnel_clear_self_claim_tag(drvdata);
			funnel_add_coresight_dev(drvdata->dev);
			pm_runtime_put(drvdata->dev);
		}
	}
	return 0;
}

static int __init funnel_init(void)
{
	int ret;

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"arm/coresight-funnel:online",
					funnel_online_cpu, NULL);

	if (ret > 0)
		hp_online = ret;
	else
		return ret;

	return coresight_init_driver("funnel", &dynamic_funnel_driver, &funnel_driver,
				     THIS_MODULE);
}

static void __exit funnel_exit(void)
{
	coresight_remove_driver(&dynamic_funnel_driver, &funnel_driver);
	if (hp_online) {
		cpuhp_remove_state_nocalls(hp_online);
		hp_online = 0;
	}
}

module_init(funnel_init);
module_exit(funnel_exit);

MODULE_AUTHOR("Pratik Patel <pratikp@codeaurora.org>");
MODULE_AUTHOR("Mathieu Poirier <mathieu.poirier@linaro.org>");
MODULE_DESCRIPTION("Arm CoreSight Funnel Driver");
MODULE_LICENSE("GPL v2");
