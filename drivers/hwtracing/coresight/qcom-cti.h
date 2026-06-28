/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _CORESIGHT_QCOM_CTI_H
#define _CORESIGHT_QCOM_CTI_H

#include "coresight-cti.h"

#define ARCHITECT_QCOM 0x477

/* CTI programming registers */
#define QCOM_CTIINTACK		0x020
#define QCOM_CTIAPPSET		0x004
#define QCOM_CTIAPPCLEAR	0x008
#define QCOM_CTIAPPPULSE	0x00C
#define QCOM_CTIINEN		0x400
#define QCOM_CTIOUTEN		0x800
#define QCOM_CTITRIGINSTATUS	0x040
#define QCOM_CTITRIGOUTSTATUS	0x060
#define QCOM_CTICHINSTATUS	0x080
#define QCOM_CTICHOUTSTATUS	0x084
#define QCOM_CTIGATE		0x088
#define QCOM_ASICCTL		0x08C
/* Integration test registers */
#define QCOM_ITCHINACK		0xE70
#define QCOM_ITTRIGINACK	0xE80
#define QCOM_ITCHOUT		0xE74
#define QCOM_ITTRIGOUT		0xEA0
#define QCOM_ITCHOUTACK		0xE78
#define QCOM_ITTRIGOUTACK	0xEC0
#define QCOM_ITCHIN		0xE7C
#define QCOM_ITTRIGIN		0xEE0

static noinline u32 cti_qcom_reg_off(u32 offset)
{
	switch (offset) {
	case CTIINTACK:		return QCOM_CTIINTACK;
	case CTIAPPSET:		return QCOM_CTIAPPSET;
	case CTIAPPCLEAR:	return QCOM_CTIAPPCLEAR;
	case CTIAPPPULSE:	return QCOM_CTIAPPPULSE;
	case CTIINEN:		return QCOM_CTIINEN;
	case CTIOUTEN:		return QCOM_CTIOUTEN;
	case CTITRIGINSTATUS:	return QCOM_CTITRIGINSTATUS;
	case CTITRIGOUTSTATUS:	return QCOM_CTITRIGOUTSTATUS;
	case CTICHINSTATUS:	return QCOM_CTICHINSTATUS;
	case CTICHOUTSTATUS:	return QCOM_CTICHOUTSTATUS;
	case CTIGATE:		return QCOM_CTIGATE;
	case ASICCTL:		return QCOM_ASICCTL;
	case ITCHINACK:		return QCOM_ITCHINACK;
	case ITTRIGINACK:	return QCOM_ITTRIGINACK;
	case ITCHOUT:		return QCOM_ITCHOUT;
	case ITTRIGOUT:		return QCOM_ITTRIGOUT;
	case ITCHOUTACK:	return QCOM_ITCHOUTACK;
	case ITTRIGOUTACK:	return QCOM_ITTRIGOUTACK;
	case ITCHIN:		return QCOM_ITCHIN;
	case ITTRIGIN:		return QCOM_ITTRIGIN;

	default:
		return offset;
	}
}

#endif  /* _CORESIGHT_QCOM_CTI_H */
