/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_RAS_H
#define __ASM_RAS_H

#include <linux/bits.h>
#include <linux/types.h>

/* ERR<n>FR */
#define ERR_FR_CE GENMASK_ULL(54, 53)
#define ERR_FR_RP BIT(15)
#define ERR_FR_CEC GENMASK_ULL(14, 12)

#define ERR_FR_RP_SINGLE_COUNTER 0
#define ERR_FR_RP_DOUBLE_COUNTER 1

#define ERR_FR_CEC_0B_COUNTER 0
#define ERR_FR_CEC_8B_COUNTER BIT(1)
#define ERR_FR_CEC_16B_COUNTER BIT(2)

/* ERR<n>MISC0 */

/* ERR<n>FR.CEC == 0b010, ERR<n>FR.RP == 0  */
#define ERR_MISC0_8B_OF BIT(39)
#define ERR_MISC0_8B_CEC GENMASK_ULL(38, 32)

/* ERR<n>FR.CEC == 0b100, ERR<n>FR.RP == 0  */
#define ERR_MISC0_16B_OF BIT(47)
#define ERR_MISC0_16B_CEC GENMASK_ULL(46, 32)

#define ERR_MISC0_CEC_SHIFT 31

#define ERR_8B_CEC_MAX (ERR_MISC0_8B_CEC >> ERR_MISC0_CEC_SHIFT)
#define ERR_16B_CEC_MAX (ERR_MISC0_16B_CEC >> ERR_MISC0_CEC_SHIFT)

/* ERR<n>FR.CEC == 0b100, ERR<n>FR.RP == 1  */
#define ERR_MISC0_16B_OFO BIT(63)
#define ERR_MISC0_16B_CECO GENMASK_ULL(62, 48)
#define ERR_MISC0_16B_OFR BIT(47)
#define ERR_MISC0_16B_CECR GENMASK_ULL(46, 32)

/* ERR<n>STATUS */
#define ERR_STATUS_AV BIT(31)
#define ERR_STATUS_V BIT(30)
#define ERR_STATUS_UE BIT(29)
#define ERR_STATUS_ER BIT(28)
#define ERR_STATUS_OF BIT(27)
#define ERR_STATUS_MV BIT(26)
#define ERR_STATUS_CE (BIT(25) | BIT(24))
#define ERR_STATUS_DE BIT(23)
#define ERR_STATUS_PN BIT(22)
#define ERR_STATUS_UET (BIT(21) | BIT(20))
#define ERR_STATUS_CI BIT(19)
#define ERR_STATUS_IERR GENMASK_ULL(15, 8)
#define ERR_STATUS_SERR GENMASK_ULL(7, 0)

/* Theses bits are	 write-one-to-clear */
#define ERR_STATUS_W1TC                                                  \
	(ERR_STATUS_AV | ERR_STATUS_V | ERR_STATUS_UE | ERR_STATUS_ER |  \
	 ERR_STATUS_OF | ERR_STATUS_MV | ERR_STATUS_CE | ERR_STATUS_DE | \
	 ERR_STATUS_PN | ERR_STATUS_UET | ERR_STATUS_CI)

#define ERR_STATUS_UET_UC 0
#define ERR_STATUS_UET_UEU 1
#define ERR_STATUS_UET_UEO 2
#define ERR_STATUS_UET_UER 3

/* ERR<n>ADDR */
#define ERR_ADDR_AI BIT(61)
#define ERR_ADDR_PADDR GENMASK_ULL(55, 0)

/* ERR<n>CTLR */
#define ERR_CTLR_CFI BIT(8)
#define ERR_CTLR_FI BIT(3)
#define ERR_CTLR_UI BIT(2)

/* ERRDEVARCH */
#define ERRDEVARCH_REV GENMASK(19, 16)

enum ras_ce_threshold {
	RAS_CE_THRESHOLD_0B,
	RAS_CE_THRESHOLD_8B,
	RAS_CE_THRESHOLD_16B,
	RAS_CE_THRESHOLD_32B,
	UNKNOWN,
};

struct ras_ext_regs {
	u64 err_fr;
	u64 err_ctlr;
	u64 err_status;
	u64 err_addr;
	u64 err_misc[4];
};

#endif /* __ASM_RAS_H */
