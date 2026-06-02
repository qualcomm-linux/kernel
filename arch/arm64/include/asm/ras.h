/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_RAS_H
#define __ASM_RAS_H

#include <linux/types.h>

struct ras_ext_regs {
	u64 err_fr;
	u64 err_ctlr;
	u64 err_status;
	u64 err_addr;
	u64 err_misc[4];
};

#endif /* __ASM_RAS_H */
