/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_DEBUGFS_H__
#define __IRIS_DEBUGFS_H__

struct iris_core;

void iris_debugfs_init(struct iris_core *core);
void iris_debugfs_deinit(struct iris_core *core);

#endif
