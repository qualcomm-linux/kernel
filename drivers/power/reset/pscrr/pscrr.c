// SPDX-License-Identifier: GPL-2.0
/*
 * pscrr.c - Core Power State Change Reason Recording
 *
 * PSCRR records why the last power state change (shutdown/reboot) happened.
 * Reasons come from providers: hardware reset-cause registers (PMIC, SoC reset
 * controller, watchdog), persistent recorders (NVMEM/RTC scratch), the
 * bootloader's device-tree /chosen/reset-source (a built-in provider here), or
 * test stubs. Each provider gets a directory under /sys/kernel/pscrr/ and reports
 * the full set of reasons it observed - the picture is deliberately not
 * collapsed to a single "winning" cause, since resets are often multi-causal.
 *
 * Sysfs (per provider, under /sys/kernel/pscrr/providerN/):
 *   name               ro  human label of the provider
 *   device             symlink to the backing device (if any)
 *   reason             the provider's reason set, as tokens; writable (record
 *                      one reason) when the provider supports it
 *   caps               ro  non-default capabilities ("writable"); empty for a
 *                      read-only, single-slot provider
 *   supported_reasons  ro  reasons this provider can report or record
 *   record_policy      recorders only: keep the "first" or "last" reason
 *                      recorded in a power cycle
 *
 * The kernel keeps the first (root cause) and last power-state-change reason
 * (get_psc_first_reason()/get_psc_reason(), set by the thermal, regulator and
 * hw_protection paths). At reboot each recorder is given the first or the last
 * reason according to its record policy.
 *
 * Copyright (C) 2025 Pengutronix, Oleksij Rempel <o.rempel@pengutronix.de>
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "PSCRR: " fmt

#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/pscrr.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>

static struct kobject *pscrr_root;
static DEFINE_MUTEX(pscrr_lock);
static LIST_HEAD(pscrr_dirs);
static DEFINE_IDA(pscrr_ida);

/*
 * Record policy: when several reasons are recorded in one power cycle, keep the
 * first (root cause) or overwrite with the last. Global, tunable via sysfs.
 */
enum pscrr_record_policy {
	PSCRR_RECORD_FIRST,
	PSCRR_RECORD_LAST,
};

/*
 * Per-provider sysfs directory. Core-owned and self-freeing on kobject_put(),
 * so its lifetime is decoupled from the caller-owned struct pscrr_provider.
 *
 * The sysfs show/store callbacks below dereference ->provider without
 * pscrr_lock: kobject_del() in pscrr_provider_unregister() blocks until
 * every in-flight callback on this kobject returns before the directory
 * (and thus ->provider) can be torn down, so a callback that is running
 * always sees a live provider.
 */
struct pscrr_provider_dir {
	struct kobject kobj;
	struct pscrr_provider *provider;
	struct list_head node;
	int id;
	enum pscrr_record_policy policy;	/* single-slot recorder: keep first/last */
	bool recorded;		/* a reason was recorded this power cycle */
};

static inline struct pscrr_provider_dir *to_pscrr_dir(struct kobject *kobj)
{
	return container_of(kobj, struct pscrr_provider_dir, kobj);
}

/*----------------------------------------------------------------------*/
/* Per-provider attributes */
/*----------------------------------------------------------------------*/

static ssize_t name_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "%s\n", to_pscrr_dir(kobj)->provider->name);
}

static struct kobj_attribute pscrr_name_attr = __ATTR_RO(name);

static int pscrr_parse_reason(const char *buf, enum psc_reason *out)
{
	unsigned int val;

	/* Accept either a decimal index or a reason token. */
	if (!kstrtouint(buf, 0, &val)) {
		if (val >= PSCR_REASON_COUNT)
			return -ERANGE;
		*out = val;
		return 0;
	}

	return psc_reason_from_token(buf, out);
}

static ssize_t reason_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	struct pscrr_provider *p = to_pscrr_dir(kobj)->provider;
	DECLARE_BITMAP(reasons, PSCR_REASON_COUNT);
	ssize_t len = 0;
	int bit, ret;

	bitmap_zero(reasons, PSCR_REASON_COUNT);

	ret = p->ops->read_reasons(p, reasons);
	if (ret)
		return ret;

	for_each_set_bit(bit, reasons, PSCR_REASON_COUNT)
		len += sysfs_emit_at(buf, len, "%s%s", len ? " " : "",
				     psc_reason_to_token(bit));
	len += sysfs_emit_at(buf, len, "\n");

	return len;
}

/* Record @reason into @dir honouring the global record policy. */
static int pscrr_do_record(struct pscrr_provider_dir *dir, enum psc_reason reason)
{
	struct pscrr_provider *p = dir->provider;
	int ret;

	if (!p->ops->write_reason)
		return -EOPNOTSUPP;

	/*
	 * PSCR_UNKNOWN clears the slot and releases the latch, regardless of
	 * policy, so a reason recorded afterwards is taken again.
	 */
	if (reason == PSCR_UNKNOWN) {
		ret = p->ops->write_reason(p, reason);
		if (ret)
			return ret;

		dir->recorded = false;
		return 0;
	}

	/* Reject reasons the provider does not advertise (NULL == all). */
	if (p->supported_reasons && !test_bit(reason, p->supported_reasons))
		return -EINVAL;

	/* "first" policy: keep the first reason recorded this power cycle. */
	if (dir->policy == PSCRR_RECORD_FIRST && dir->recorded)
		return 0;

	ret = p->ops->write_reason(p, reason);
	if (ret)
		return ret;

	dir->recorded = true;

	return 0;
}

static ssize_t reason_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	struct pscrr_provider_dir *dir = to_pscrr_dir(kobj);
	enum psc_reason reason;
	int ret;

	if (!dir->provider->ops->write_reason)
		return -EOPNOTSUPP;

	ret = pscrr_parse_reason(buf, &reason);
	if (ret)
		return ret;

	/* Serialise the record state against concurrent stores and the notifier. */
	scoped_guard(mutex, &pscrr_lock)
		ret = pscrr_do_record(dir, reason);
	if (ret)
		return ret;

	return count;
}

static struct kobj_attribute pscrr_reason_attr =
	__ATTR(reason, 0644, reason_show, reason_store);

static ssize_t caps_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct pscrr_provider *p = to_pscrr_dir(kobj)->provider;
	ssize_t len = 0;

	/* Readable and single-slot are the defaults and not listed. */
	if (p->ops->write_reason)
		len += sysfs_emit_at(buf, len, "writable");

	return len + sysfs_emit_at(buf, len, "\n");
}

static struct kobj_attribute pscrr_caps_attr = __ATTR_RO(caps);

static ssize_t supported_reasons_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	struct pscrr_provider *p = to_pscrr_dir(kobj)->provider;
	const unsigned long *sup = p->supported_reasons;
	ssize_t len = 0;
	int i;

	for (i = 0; i < PSCR_REASON_COUNT; i++) {
		if (sup && !test_bit(i, sup))	/* NULL means all */
			continue;
		len += sysfs_emit_at(buf, len, "%s%s", len ? " " : "",
				     psc_reason_to_token(i));
	}

	return len + sysfs_emit_at(buf, len, "\n");
}

static struct kobj_attribute pscrr_supported_attr =
	__ATTR(supported_reasons, 0444, supported_reasons_show, NULL);

static ssize_t record_policy_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct pscrr_provider_dir *dir = to_pscrr_dir(kobj);

	return sysfs_emit(buf, "%s\n",
			  READ_ONCE(dir->policy) == PSCRR_RECORD_FIRST ?
			  "first" : "last");
}

static ssize_t record_policy_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	struct pscrr_provider_dir *dir = to_pscrr_dir(kobj);

	guard(mutex)(&pscrr_lock);

	if (sysfs_streq(buf, "first"))
		WRITE_ONCE(dir->policy, PSCRR_RECORD_FIRST);
	else if (sysfs_streq(buf, "last"))
		WRITE_ONCE(dir->policy, PSCRR_RECORD_LAST);
	else
		return -EINVAL;

	return count;
}

static struct kobj_attribute pscrr_record_policy_attr =
	__ATTR(record_policy, 0644, record_policy_show, record_policy_store);

static struct attribute *pscrr_dir_attrs[] = {
	&pscrr_name_attr.attr,
	&pscrr_reason_attr.attr,
	&pscrr_caps_attr.attr,
	&pscrr_supported_attr.attr,
	&pscrr_record_policy_attr.attr,
	NULL,
};

static umode_t pscrr_dir_is_visible(struct kobject *kobj, struct attribute *attr,
				    int n)
{
	struct pscrr_provider *p = to_pscrr_dir(kobj)->provider;

	/* A provider without write_reason() exposes reason read-only. */
	if (attr == &pscrr_reason_attr.attr && !p->ops->write_reason)
		return 0444;

	/* record_policy only applies to a (single-slot) recorder. */
	if (attr == &pscrr_record_policy_attr.attr && !p->ops->write_reason)
		return 0;

	return attr->mode;
}

static const struct attribute_group pscrr_dir_group = {
	.attrs		= pscrr_dir_attrs,
	.is_visible	= pscrr_dir_is_visible,
};

static const struct attribute_group *pscrr_dir_groups[] = {
	&pscrr_dir_group,
	NULL,
};

static void pscrr_dir_release(struct kobject *kobj)
{
	kfree(to_pscrr_dir(kobj));
}

static const struct kobj_type pscrr_dir_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.release	= pscrr_dir_release,
	.default_groups	= pscrr_dir_groups,
};

/*----------------------------------------------------------------------*/
/* Provider registration */
/*----------------------------------------------------------------------*/

/**
 * pscrr_provider_register - register a power state change reason provider
 * @p: caller-owned provider description
 *
 * Creates /sys/kernel/pscrr/providerN/ with "name" and "reason" attributes
 * and, when @p->dev is set, a "device" symlink. @p->reason is writable when
 * @p provides write_reason(). The provider must outlive the matching
 * pscrr_provider_unregister() call.
 *
 * Return: 0 on success or a negative errno.
 */
int pscrr_provider_register(struct pscrr_provider *p)
{
	struct pscrr_provider_dir *dir;
	int ret;

	if (!p || !p->name || !p->ops || !p->ops->read_reasons)
		return -EINVAL;

	/*
	 * pscrr_root is set once at core init and cleared at core exit;
	 * neither transition is serialised against this function by
	 * pscrr_lock, so check it up front rather than under the lock below.
	 */
	if (!pscrr_root)
		return -ENODEV;

	dir = kzalloc_obj(*dir);
	if (!dir)
		return -ENOMEM;

	dir->provider = p;
	dir->policy = PSCRR_RECORD_FIRST;

	/* ida_alloc() and the kobject/sysfs calls below are individually thread-safe. */
	dir->id = ida_alloc(&pscrr_ida, GFP_KERNEL);
	if (dir->id < 0) {
		ret = dir->id;
		kfree(dir);
		return ret;
	}

	ret = kobject_init_and_add(&dir->kobj, &pscrr_dir_ktype, pscrr_root,
				   "provider%d", dir->id);
	if (ret) {
		/*
		 * kobject_init_and_add() failed: per its contract only
		 * kobject_put() may follow, no kobject_del().
		 */
		ida_free(&pscrr_ida, dir->id);
		kobject_put(&dir->kobj);
		return ret;
	}

	if (p->dev) {
		ret = sysfs_create_link(&dir->kobj, &p->dev->kobj, "device");
		if (ret)
			goto err_del;
	}

	/* pscrr_lock only serialises pscrr_dirs against concurrent (un)registration. */
	scoped_guard(mutex, &pscrr_lock)
		list_add_tail(&dir->node, &pscrr_dirs);

	return 0;

err_del:
	kobject_del(&dir->kobj);
	ida_free(&pscrr_ida, dir->id);
	kobject_put(&dir->kobj);
	return ret;
}
EXPORT_SYMBOL_GPL(pscrr_provider_register);

/**
 * pscrr_provider_unregister - remove a previously registered provider
 * @p: the provider passed to pscrr_provider_register()
 */
void pscrr_provider_unregister(struct pscrr_provider *p)
{
	struct pscrr_provider_dir *dir = NULL, *iter;

	scoped_guard(mutex, &pscrr_lock) {
		list_for_each_entry(iter, &pscrr_dirs, node) {
			if (iter->provider == p) {
				dir = iter;
				list_del(&dir->node);
				break;
			}
		}
	}

	if (!dir)
		return;

	/*
	 * Tear the sysfs directory down outside pscrr_lock: kobject_del()
	 * drains in-flight reason/record_policy stores, which take pscrr_lock,
	 * so holding it here would deadlock. Release the id only once the
	 * directory is gone, so a concurrent register cannot reuse it and
	 * collide on the providerN name.
	 */
	if (p->dev)
		sysfs_remove_link(&dir->kobj, "device");
	kobject_del(&dir->kobj);
	ida_free(&pscrr_ida, dir->id);
	kobject_put(&dir->kobj);
}
EXPORT_SYMBOL_GPL(pscrr_provider_unregister);

static void pscrr_provider_devm_release(void *p)
{
	pscrr_provider_unregister(p);
}

/**
 * devm_pscrr_provider_register - device-managed reason provider registration
 * @dev: device the provider belongs to; also backs the "device" symlink
 * @name: human-readable provider label
 * @ops: provider callback table; read_reasons() is required, write_reason() is
 *	 optional and makes the provider a recorder
 * @supported_reasons: bitmap of the reasons the provider supports, or NULL
 *	 for all; set before the provider is exposed in sysfs
 * @priv: provider private data, handed back to the @ops callbacks
 *
 * Allocates and registers a struct pscrr_provider and schedules its
 * unregistration when @dev is unbound, so the caller keeps no reference to it.
 *
 * Return: the registered provider on success or an ERR_PTR() on failure. When
 * CONFIG_PSCRR is disabled the call resolves to a stub returning NULL, so
 * callers need no IS_ENABLED() guard.
 */
struct pscrr_provider *
devm_pscrr_provider_register(struct device *dev, const char *name,
			     const struct pscrr_provider_ops *ops,
			     const unsigned long *supported_reasons, void *priv)
{
	struct pscrr_provider *p;
	int ret;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	p->name = name;
	p->dev = dev;
	p->ops = ops;
	p->supported_reasons = supported_reasons;
	p->priv = priv;

	ret = pscrr_provider_register(p);
	if (ret)
		return ERR_PTR(ret);

	ret = devm_add_action_or_reset(dev, pscrr_provider_devm_release, p);
	if (ret)
		return ERR_PTR(ret);

	return p;
}
EXPORT_SYMBOL_GPL(devm_pscrr_provider_register);

/*----------------------------------------------------------------------*/
/* Record path: reboot notifier writes the current reason to recorders */
/*----------------------------------------------------------------------*/

/*
 * Record the current power-state-change reason into every provider, giving
 * each the first (root cause) or the last reason according to its record
 * policy. The kernel keeps both, so "first" is meaningful even when later
 * events overwrite the last reason. The caller holds pscrr_lock, or runs where
 * the provider list is stable (panic).
 */
static void pscrr_record_current(void)
{
	enum psc_reason first = get_psc_first_reason();
	enum psc_reason last = get_psc_reason();
	struct pscrr_provider_dir *dir;

	list_for_each_entry(dir, &pscrr_dirs, node)
		pscrr_do_record(dir, dir->policy == PSCRR_RECORD_FIRST ?
					     first : last);
}

static int pscrr_reboot_notifier(struct notifier_block *nb,
				 unsigned long action, void *unused)
{
	guard(mutex)(&pscrr_lock);

	/*
	 * A reboot, halt or power-off that reaches here with no more specific
	 * reason is software-initiated by definition. Record it as such rather
	 * than leaving it unattributed; a real cause set earlier (thermal,
	 * under-voltage, ...) is already latched and left untouched.
	 */
	if (get_psc_reason() == PSCR_UNKNOWN)
		set_psc_reason(PSCR_SOFTWARE);

	pscrr_record_current();

	return NOTIFY_DONE;
}

static struct notifier_block pscrr_reboot_nb = {
	.notifier_call = pscrr_reboot_notifier,
};

/*----------------------------------------------------------------------*/
/* Built-in provider: device-tree /chosen/reset-source                  */
/*----------------------------------------------------------------------*/

/*
 * Bootloaders such as barebox record the SoC reset cause in the standard
 * device-tree /chosen/reset-source property. When it is present, surface it as
 * a read-only, device-less provider so the bootloader's view of the last reset
 * shows up next to any hardware or software providers - the framework just
 * reads the property already there, with no dedicated node or new binding.
 */
static const struct {
	const char *name;
	enum psc_reason reason;
} pscrr_reset_source_map[] = {
	{ "POR",      PSCR_POWER_ON },
	{ "RST",      PSCR_SOFTWARE },
	{ "WDG",      PSCR_WATCHDOG },
	{ "THERM",    PSCR_OVER_TEMPERATURE },
	{ "EXT",      PSCR_EXTERNAL },
	{ "BROWNOUT", PSCR_UNDER_VOLTAGE },
};

static const unsigned long
pscrr_reset_source_supported[BITS_TO_LONGS(PSCR_REASON_COUNT)] = {
	BIT(PSCR_UNDER_VOLTAGE) | BIT(PSCR_OVER_TEMPERATURE) |
	BIT(PSCR_POWER_ON) | BIT(PSCR_WATCHDOG) | BIT(PSCR_SOFTWARE) |
	BIT(PSCR_EXTERNAL),
};

/* Parsed once at init; read back by the provider's read_reasons(). */
static enum psc_reason pscrr_reset_source_reason = PSCR_UNKNOWN;

static int pscrr_reset_source_read(struct pscrr_provider *p,
				   unsigned long *reasons)
{
	set_bit(pscrr_reset_source_reason, reasons);

	return 0;
}

static const struct pscrr_provider_ops pscrr_reset_source_ops = {
	.read_reasons = pscrr_reset_source_read,
};

static struct pscrr_provider pscrr_reset_source_provider = {
	.name			= "reset-source",
	.ops			= &pscrr_reset_source_ops,
	.supported_reasons	= pscrr_reset_source_supported,
};

static void __init pscrr_register_reset_source(void)
{
	const char *name;
	int i, ret;

	if (!IS_ENABLED(CONFIG_OF) || !of_chosen)
		return;

	if (of_property_read_string(of_chosen, "reset-source", &name))
		return;

	for (i = 0; i < ARRAY_SIZE(pscrr_reset_source_map); i++)
		if (!strcmp(name, pscrr_reset_source_map[i].name)) {
			pscrr_reset_source_reason = pscrr_reset_source_map[i].reason;
			break;
		}

	ret = pscrr_provider_register(&pscrr_reset_source_provider);
	if (ret)
		pr_warn("failed to register the reset-source provider: %d\n", ret);
}

/*----------------------------------------------------------------------*/
/* Module init/exit */
/*----------------------------------------------------------------------*/

static int __init pscrr_core_init(void)
{
	int ret;

	pscrr_root = kobject_create_and_add("pscrr", kernel_kobj);
	if (!pscrr_root)
		return -ENOMEM;

	ret = register_reboot_notifier(&pscrr_reboot_nb);
	if (ret) {
		kobject_put(pscrr_root);
		pscrr_root = NULL;
		return ret;
	}

	pscrr_register_reset_source();

	return 0;
}

static void __exit pscrr_core_exit(void)
{
	pscrr_provider_unregister(&pscrr_reset_source_provider);
	unregister_reboot_notifier(&pscrr_reboot_nb);
	kobject_put(pscrr_root);
	pscrr_root = NULL;
	ida_destroy(&pscrr_ida);
}

/* Bring the core up before device drivers probe and register providers. */
subsys_initcall(pscrr_core_init);
module_exit(pscrr_core_exit);

MODULE_AUTHOR("Oleksij Rempel <o.rempel@pengutronix.de>");
MODULE_AUTHOR("Faruque Ansari <faruque.ansari@oss.qualcomm.com>");
MODULE_DESCRIPTION("Power State Change Reason Recording (PSCRR) core");
MODULE_LICENSE("GPL");
