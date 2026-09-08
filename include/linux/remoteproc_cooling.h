/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Remote Processor Cooling Device
 *
 * Copyright (c) 2025, Qualcomm Innovation Center
 */

#ifndef __REMOTEPROC_COOLING_H__
#define __REMOTEPROC_COOLING_H__

#include <linux/thermal.h>

struct device;
struct device_node;

struct remoteproc_cooling_ops {
	int (*get_max_level)(void *devdata, unsigned long *level);
	int (*get_cur_level)(void *devdata, unsigned long *level);
	int (*set_cur_level)(void *devdata, unsigned long level);
};

struct remoteproc_cdev;

#ifdef CONFIG_REMOTEPROC_THERMAL

struct remoteproc_cdev *
remoteproc_cooling_register(struct device_node *np,
			    const char *name,
			     const struct remoteproc_cooling_ops *ops,
			     void *devdata);

void remoteproc_cooling_unregister(struct remoteproc_cdev *rproc_cdev);

#else /* !CONFIG_REMOTEPROC_THERMAL */

static inline struct remoteproc_cdev *
remoteproc_cooling_register(struct device_node *np,
			    const char *name,
			     const struct remoteproc_cooling_ops *ops,
			     void *devdata)
{
	return ERR_PTR(-EINVAL);
}

static inline void
remoteproc_cooling_unregister(struct remoteproc_cdev *rproc_cdev)
{
}

#endif /* CONFIG_REMOTEPROC_THERMAL */

#endif /* __REMOTEPROC_COOLING_H__ */
