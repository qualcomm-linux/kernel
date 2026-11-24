/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */

#ifndef _DT_BINDINGS_ARM_RAS_H
#define _DT_BINDINGS_ARM_RAS_H

/* arm,group-format - error record group register window layout */
#define ARM_RAS_GROUP_4K		0	/* 4 KiB,  1 ERRGSR  */
#define ARM_RAS_GROUP_16K		1	/* 16 KiB, 4 ERRGSRs */
#define ARM_RAS_GROUP_64K		2	/* 64 KiB, 14 ERRGSRs */

#endif /* _DT_BINDINGS_ARM_RAS_H */
