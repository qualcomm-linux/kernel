/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __REBOOT_MODE_H__
#define __REBOOT_MODE_H__

#include <linux/fwnode.h>
#include <linux/mutex.h>
#include <linux/types.h>

struct reboot_mode_driver {
	struct device *dev;
	struct device *reboot_dev;
	const char *driver_name;
	struct list_head head;
	int (*write)(struct reboot_mode_driver *reboot, u64 magic);
	struct notifier_block reboot_notifier;
	/*Protects access to reboot mode list*/
	struct mutex rb_lock;
};

int reboot_mode_register(struct reboot_mode_driver *reboot, struct fwnode_handle *fwnode);
int reboot_mode_unregister(struct reboot_mode_driver *reboot);
int devm_reboot_mode_register(struct device *dev,
			      struct reboot_mode_driver *reboot);
void devm_reboot_mode_unregister(struct device *dev,
				 struct reboot_mode_driver *reboot);

#endif
