// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/debugfs.h>

#include "iris_core.h"
#include "iris_debugfs.h"

static int iris_fw_level_get(void *data, u64 *val)
{
	struct iris_core *core = data;

	*val = READ_ONCE(core->fw_debug);

	return 0;
}

static int iris_fw_level_set(void *data, u64 val)
{
	struct iris_core *core = data;

	WRITE_ONCE(core->fw_debug, (u32)val & IRIS_FW_DEBUG_LOGMASK);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(iris_fw_level_fops, iris_fw_level_get,
			 iris_fw_level_set, "0x%08llx\n");

void iris_debugfs_init(struct iris_core *core)
{
	core->root = debugfs_create_dir("iris", NULL);
	debugfs_create_file("fw_level", 0600, core->root, core,
			    &iris_fw_level_fops);
}

void iris_debugfs_deinit(struct iris_core *core)
{
	debugfs_remove(core->root);
}
