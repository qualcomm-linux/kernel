/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 */

#ifndef __DT_BINDINGS_MAILBOX_IPCC_H
#define __DT_BINDINGS_MAILBOX_IPCC_H

/* Signal IDs for MPROC protocol */
#define IPCC_MPROC_SIGNAL_GLINK_QMP	0
#define IPCC_MPROC_SIGNAL_TZ		1
#define IPCC_MPROC_SIGNAL_SMP2P		2
#define IPCC_MPROC_SIGNAL_PING		3

/* Client IDs */
#define IPCC_CLIENT_AOP			0
#define IPCC_CLIENT_TZ			1
#define IPCC_CLIENT_MPSS		2
#define IPCC_CLIENT_LPASS		3
#define IPCC_CLIENT_SLPI		4
#define IPCC_CLIENT_SDC			5
#define IPCC_CLIENT_CDSP		6
#define IPCC_CLIENT_NPU			7
#define IPCC_CLIENT_APSS		8
#define IPCC_CLIENT_GPU			9
#define IPCC_CLIENT_CVP			10
#define IPCC_CLIENT_CAM			11
#define IPCC_CLIENT_VPU			12
#define IPCC_CLIENT_PCIE0		13
#define IPCC_CLIENT_PCIE1		14
#define IPCC_CLIENT_PCIE2		15
#define IPCC_CLIENT_SPSS		16
#define IPCC_CLIENT_NSP1		18
#define IPCC_CLIENT_TME			23
#define IPCC_CLIENT_WPSS		24
#define IPCC_CLIENT_GPDSP0		31
#define IPCC_CLIENT_GPDSP1		32

/* Physical client IDs */
#define IPCC_MPROC_AOP                  0
#define IPCC_MPROC_TZ                   1
#define IPCC_MPROC_MPSS                 2
#define IPCC_MPROC_LPASS                3
#define IPCC_MPROC_SDC                  4
#define IPCC_MPROC_CDSP                 5
#define IPCC_MPROC_APSS                 6
#define IPCC_MPROC_SOCCP                13
#define IPCC_MPROC_DCP                  14
#define IPCC_MPROC_SPSS                 15
#define IPCC_MPROC_TME                  16

#define IPCC_COMPUTE_L0_CDSP            2
#define IPCC_COMPUTE_L0_APSS            3
#define IPCC_COMPUTE_L0_GPU             4
#define IPCC_COMPUTE_L0_CVP             8
#define IPCC_COMPUTE_L0_CAM             9
#define IPCC_COMPUTE_L0_CAM1            10
#define IPCC_COMPUTE_L0_DCP             11
#define IPCC_COMPUTE_L0_VPU             12
#define IPCC_COMPUTE_L0_SOCCP           16

#define IPCC_COMPUTE_L1_CDSP            2
#define IPCC_COMPUTE_L1_APSS            3
#define IPCC_COMPUTE_L1_GPU             4
#define IPCC_COMPUTE_L1_CVP             8
#define IPCC_COMPUTE_L1_CAM             9
#define IPCC_COMPUTE_L1_CAM1            10
#define IPCC_COMPUTE_L1_DCP             11
#define IPCC_COMPUTE_L1_VPU             12
#define IPCC_COMPUTE_L1_SOCCP           16

#define IPCC_PERIPH_CDSP                2
#define IPCC_PERIPH_APSS                3
#define IPCC_PERIPH_PCIE0               4
#define IPCC_PERIPH_PCIE1               5

#define IPCC_FENCE_CDSP                 2
#define IPCC_FENCE_APSS                 3
#define IPCC_FENCE_GPU                  4
#define IPCC_FENCE_CVP                  8
#define IPCC_FENCE_CAM                  8
#define IPCC_FENCE_VPU                  20
#define IPCC_FENCE_SOCCP                24
#define IPCC_FENCE_CAM1                 10
#define IPCC_FENCE_DCP                  11

#endif
