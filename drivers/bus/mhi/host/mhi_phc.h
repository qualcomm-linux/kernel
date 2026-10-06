/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifdef CONFIG_MHI_BUS_PHC
int mhi_phc_init(struct mhi_controller *mhi_cntrl);
int mhi_phc_start(struct mhi_controller *mhi_cntrl);
int mhi_phc_stop(struct mhi_controller *mhi_cntrl);
void mhi_phc_exit(struct mhi_controller *mhi_cntrl);
#else
static inline int mhi_phc_init(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static inline int mhi_phc_start(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static inline int mhi_phc_stop(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static inline void mhi_phc_exit(struct mhi_controller *mhi_cntrl) {}
#endif
