/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_RAS_H
#define __ASM_RAS_H

#include <linux/bits.h>
#include <linux/types.h>

/* ERR<n>CTLR */
#define ERR_CTLR_CFI			BIT(8)
#define ERR_CTLR_FI			BIT(3)
#define ERR_CTLR_UI			BIT(2)

/* ERRIRQCR<n> */
#define ERRFHICR0_OFFSET		0x0
#define ERRERICR0_OFFSET		0x10

struct ras_ext_regs {
	u64 err_fr;
	u64 err_ctlr;
	u64 err_status;
	u64 err_addr;
	u64 err_misc[4];
};

#endif /* __ASM_RAS_H */
