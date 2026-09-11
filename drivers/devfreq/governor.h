/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Compatibility wrapper for out-of-tree drivers.
 *
 * drivers/devfreq/governor.h was moved to include/linux/devfreq-governor.h
 * by commit 6df46fd1d1b5 ("FROMGIT: PM / devfreq: Move governor.h to a
 * public header location"), which was backported into qcom-6.18.y.
 *
 * Out-of-tree modules (e.g. kgsl-dlkm) that add the devfreq drivers/
 * directory to their include path via -I$(KERNEL_SRC)/drivers/devfreq and
 * use #include "governor.h" will land here and be transparently redirected
 * to the new public header location.
 *
 * New drivers must use #include <linux/devfreq-governor.h> directly.
 * This shim will be removed once all known consumers are updated.
 */
#ifndef __DEVFREQ_GOVERNOR_COMPAT_H
#define __DEVFREQ_GOVERNOR_COMPAT_H

#include <linux/devfreq-governor.h>

#endif /* __DEVFREQ_GOVERNOR_COMPAT_H */
