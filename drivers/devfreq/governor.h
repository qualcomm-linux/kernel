/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Compatibility shim: governor.h was moved to include/linux/devfreq-governor.h
 * in kernel 6.19 (commit 447c4e8338db). This file exists to support out-of-tree
 * modules (e.g. kgsl) that still reference the old location on 6.18.y kernels.
 */
#include <linux/devfreq-governor.h>
