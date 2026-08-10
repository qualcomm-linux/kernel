/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Qualcomm QMP PHY constants
 *
 * Copyright (C) 2022 Linaro Limited
 */

#ifndef _DT_BINDINGS_PHY_QMP
#define _DT_BINDINGS_PHY_QMP

/* QMP USB4-USB3-DP clocks */
#define QMP_USB43DP_USB3_PIPE_CLK	0
#define QMP_USB43DP_DP_LINK_CLK		1
#define QMP_USB43DP_DP_VCO_DIV_CLK	2

/* QMP USB4-USB3-DP PHYs */
#define QMP_USB43DP_USB3_PHY		0
#define QMP_USB43DP_DP_PHY		1

/* QMP PCIE PHYs */
#define QMP_PCIE_PIPE_CLK		0
#define QMP_PCIE_PHY_AUX_CLK		1

/* Generic QMP logical PHY selectors */
#define QMP_PHY_SELECTOR_0		0
#define QMP_PHY_SELECTOR_1		1
#define QMP_PHY_SELECTOR_2		2
#define QMP_PHY_SELECTOR_3		3

/*
 * Nord QMP PCIe link-mode values (TCSR_PCIE_LINK_CONFIG_MODE)
 * These match the hardware register encoding programmed by the bootloader.
 */
#define QMP_PCIE_NORD_MODE_X16		0  /* all 16 lanes -> single x16 port        */
#define QMP_PCIE_NORD_MODE_X8_X8	1  /* x8 (port A) + x8 (ports B+C+D ganged)  */
#define QMP_PCIE_NORD_MODE_X8_X4_X4	2  /* x8 (A) + x4 (B) + x4 (C+D ganged)   */
#define QMP_PCIE_NORD_MODE_X8_X4_X2_X2	3  /* x8 (A) + x4 (B) + x2 (C) + x2 (D)  */

#endif /* _DT_BINDINGS_PHY_QMP */
