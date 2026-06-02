/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_RAS_H
#define __ASM_RAS_H

#include <linux/bits.h>
#include <linux/types.h>

/* ERR<n>STATUS */
#define ERR_STATUS_AV			BIT(31)
#define ERR_STATUS_V			BIT(30)
#define ERR_STATUS_UE			BIT(29)
#define ERR_STATUS_ER			BIT(28)
#define ERR_STATUS_OF			BIT(27)
#define ERR_STATUS_MV			BIT(26)
#define ERR_STATUS_CE			GENMASK(25, 24)
#define ERR_STATUS_DE			BIT(23)
#define ERR_STATUS_PN			BIT(22)
#define ERR_STATUS_UET			GENMASK(21, 20)
#define ERR_STATUS_CI			BIT(19)
#define ERR_STATUS_IERR			GENMASK_ULL(15, 8)
#define ERR_STATUS_SERR			GENMASK_ULL(7, 0)

/* These bits are write-one-to-clear */
#define ERR_STATUS_W1TC                                                  \
	(ERR_STATUS_AV | ERR_STATUS_V | ERR_STATUS_UE | ERR_STATUS_ER |  \
	 ERR_STATUS_OF | ERR_STATUS_MV | ERR_STATUS_CE | ERR_STATUS_DE | \
	 ERR_STATUS_PN | ERR_STATUS_UET | ERR_STATUS_CI)

#define ERR_STATUS_UET_UC		0
#define ERR_STATUS_UET_UEU		1
#define ERR_STATUS_UET_UEO		2
#define ERR_STATUS_UET_UER		3

/* ERR<n>ADDR */
#define ERR_ADDR_AI			BIT(61)
#define ERR_ADDR_PADDR			GENMASK_ULL(55, 0)

/* ERR<n>CTLR */
#define ERR_CTLR_CFI			BIT(8)
#define ERR_CTLR_FI			BIT(3)
#define ERR_CTLR_UI			BIT(2)

/* ERRIRQCR<n> */
#define ERRFHICR0_OFFSET		0x0
#define ERRERICR0_OFFSET		0x10

/* ERRDEVARCH */
#define ERRDEVARCH_REV			GENMASK(19, 16)

struct ras_ext_regs {
	u64 err_fr;
	u64 err_ctlr;
	u64 err_status;
	u64 err_addr;
	u64 err_misc[4];
};

#endif /* __ASM_RAS_H */
