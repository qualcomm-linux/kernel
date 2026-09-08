// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 */

#define pr_fmt(fmt)	"reboot-mode: " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/reboot-mode.h>
#include <linux/slab.h>

#define PREFIX "mode-"

struct mode_info {
	const char *mode;
	u64 magic;
	struct list_head list;
};

static struct class *rb_class;

static u64 get_reboot_mode_magic(struct reboot_mode_driver *reboot, const char *cmd)
{
	const char *normal = "normal";
	struct mode_info *info;
	char cmd_[110];

	if (!cmd)
		cmd = normal;

	scoped_guard(mutex, &reboot->rb_lock) {
		list_for_each_entry(info, &reboot->head, list)
			if (!strcmp(info->mode, cmd))
				return info->magic;
	}

	/* try to match again, replacing characters impossible in DT */
	if (strscpy(cmd_, cmd, sizeof(cmd_)) == -E2BIG)
		return 0;

	strreplace(cmd_, ' ', '-');
	strreplace(cmd_, ',', '-');
	strreplace(cmd_, '/', '-');

	scoped_guard(mutex, &reboot->rb_lock) {
		list_for_each_entry(info, &reboot->head, list)
			if (!strcmp(info->mode, cmd_))
				return info->magic;
	}

	return 0;
}

static int reboot_mode_notify(struct notifier_block *this,
			      unsigned long mode, void *cmd)
{
	struct reboot_mode_driver *reboot;
	u64 magic;

	reboot = container_of(this, struct reboot_mode_driver, reboot_notifier);
	magic = get_reboot_mode_magic(reboot, cmd);
	if (magic)
		reboot->write(reboot, magic);

	return NOTIFY_DONE;
}

static void release_reboot_mode_device(struct device *dev, void *res);

static ssize_t reboot_modes_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct reboot_mode_driver **devres_reboot;
	struct reboot_mode_driver *reboot;
	struct mode_info *info;
	ssize_t size = 0;

	devres_reboot = devres_find(dev, release_reboot_mode_device, NULL, NULL);
	if (!devres_reboot || !(*devres_reboot))
		return -ENODATA;

	reboot = *devres_reboot;
	scoped_guard(mutex, &reboot->rb_lock) {
		list_for_each_entry(info, &reboot->head, list)
			size += sysfs_emit_at(buf, size, "%s ", info->mode);
	}

	if (size) {
		size += sysfs_emit_at(buf, size - 1, "\n");
		return size;
	}

	return -ENODATA;
}
static DEVICE_ATTR_RO(reboot_modes);

static void release_reboot_mode_device(struct device *dev, void *res)
{
	struct reboot_mode_driver *reboot = *(struct reboot_mode_driver **)res;
	struct mode_info *info;
	struct mode_info *next;

	unregister_reboot_notifier(&reboot->reboot_notifier);

	scoped_guard(mutex, &reboot->rb_lock) {
		list_for_each_entry_safe(info, next, &reboot->head, list) {
			list_del(&info->list);
			kfree_const(info->mode);
			kfree(info);
		}
	}

	device_remove_file(reboot->reboot_dev, &dev_attr_reboot_modes);
}

static int create_reboot_mode_device(struct reboot_mode_driver *reboot)
{
	struct reboot_mode_driver **dr;
	int ret = 0;

	if (!rb_class) {
		rb_class = class_create("reboot-mode");
		if (IS_ERR(rb_class))
			return PTR_ERR(rb_class);
	}

	reboot->reboot_dev = device_create(rb_class, NULL, 0, NULL, reboot->driver_name);
	if (IS_ERR(reboot->reboot_dev))
		return PTR_ERR(reboot->reboot_dev);

	ret = device_create_file(reboot->reboot_dev, &dev_attr_reboot_modes);
	if (ret)
		goto create_file_err;

	dr = devres_alloc(release_reboot_mode_device, sizeof(*dr), GFP_KERNEL);
	if (!dr) {
		ret = -ENOMEM;
		goto devres_alloc_error;
	}

	*dr = reboot;
	devres_add(reboot->reboot_dev, dr);
	return ret;

devres_alloc_error:
	device_remove_file(reboot->reboot_dev, &dev_attr_reboot_modes);
create_file_err:
	device_unregister(reboot->reboot_dev);
	return ret;
}

/**
 * reboot_mode_register - register a reboot mode driver
 * @reboot: reboot mode driver
 * @fwnode: Firmware node with reboot-mode configuration
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int reboot_mode_register(struct reboot_mode_driver *reboot, struct fwnode_handle *fwnode)
{
	struct mode_info *info;
	struct device_node *np;
	struct property *prop;
	size_t len = strlen(PREFIX);
	u32 magic_arg1;
	u32 magic_arg2;
	int ret;

	if (!fwnode || !reboot->driver_name)
		return -EINVAL;

	np = to_of_node(fwnode);
	if (!np)
		return -EINVAL;

	ret = create_reboot_mode_device(reboot);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&reboot->head);

	mutex_init(&reboot->rb_lock);

	scoped_guard(mutex, &reboot->rb_lock) {
		for_each_property_of_node(np, prop) {
			if (strncmp(prop->name, PREFIX, len))
				continue;

			info = kzalloc(sizeof(*info), GFP_KERNEL);
			if (!info) {
				ret = -ENOMEM;
				goto error;
			}

			if (of_property_read_u32(np, prop->name, &magic_arg1)) {
				pr_err("reboot mode without magic number\n");
				kfree(info);
				continue;
			}

			if (of_property_read_u32_index(np, prop->name, 1, &magic_arg2))
				magic_arg2 = 0;

			info->magic = magic_arg2;
			info->magic = (info->magic << 32) | magic_arg1;

			info->mode = kstrdup_const(prop->name + len, GFP_KERNEL);
			if (!info->mode) {
				ret =  -ENOMEM;
				kfree(info);
				goto error;
			} else if (info->mode[0] == '\0') {
				kfree_const(info->mode);
				kfree(info);
				ret = -EINVAL;
				pr_err("invalid mode name(%s): too short!\n", prop->name);
				goto error;
			}

			list_add_tail(&info->list, &reboot->head);
		}
	}

	reboot->reboot_notifier.notifier_call = reboot_mode_notify;
	register_reboot_notifier(&reboot->reboot_notifier);

	return 0;

error:
	device_unregister(reboot->reboot_dev);
	return ret;
}
EXPORT_SYMBOL_GPL(reboot_mode_register);

/**
 * reboot_mode_unregister - unregister a reboot mode driver
 * @reboot: reboot mode driver
 */
int reboot_mode_unregister(struct reboot_mode_driver *reboot)
{
	device_unregister(reboot->reboot_dev);
	return 0;
}
EXPORT_SYMBOL_GPL(reboot_mode_unregister);

static void devm_reboot_mode_release(struct device *dev, void *res)
{
	struct reboot_mode_driver *reboot = *(struct reboot_mode_driver **)res;

	device_unregister(reboot->reboot_dev);
}

/**
 * devm_reboot_mode_register() - resource managed reboot_mode_register()
 * @dev: device to associate this resource with
 * @reboot: reboot mode driver
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int devm_reboot_mode_register(struct device *dev,
			      struct reboot_mode_driver *reboot)
{
	struct reboot_mode_driver **dr;
	int rc;

	if (!reboot->dev || !reboot->dev->of_node)
		return -EINVAL;

	dr = devres_alloc(devm_reboot_mode_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	reboot->driver_name = reboot->dev->driver->name;
	rc = reboot_mode_register(reboot, of_fwnode_handle(reboot->dev->of_node));
	if (rc) {
		devres_free(dr);
		return rc;
	}

	*dr = reboot;
	devres_add(dev, dr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_reboot_mode_register);

static int devm_reboot_mode_match(struct device *dev, void *res, void *data)
{
	struct reboot_mode_driver **p = res;

	if (WARN_ON(!p || !*p))
		return 0;

	return *p == data;
}

/**
 * devm_reboot_mode_unregister() - resource managed reboot_mode_unregister()
 * @dev: device to associate this resource with
 * @reboot: reboot mode driver
 */
void devm_reboot_mode_unregister(struct device *dev,
				 struct reboot_mode_driver *reboot)
{
	WARN_ON(devres_release(dev,
			       devm_reboot_mode_release,
			       devm_reboot_mode_match, reboot));
}
EXPORT_SYMBOL_GPL(devm_reboot_mode_unregister);

MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_DESCRIPTION("System reboot mode core library");
MODULE_LICENSE("GPL v2");
