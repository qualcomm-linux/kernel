/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Linaro Limited
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * QMI Thermal Mitigation Device (TMD) library header.
 */

#ifndef __QMI_TMD_H__
#define __QMI_TMD_H__

struct device;
struct qmi_tmd_client;

struct qmi_tmd_client *qmi_tmd_init(struct device *dev,
				    const char *remoteproc_name,
				    const char * const *tmd_names,
				    int num_tmds);

void qmi_tmd_exit(struct qmi_tmd_client *tmd_cli);

#endif /* __QMI_TMD_H__ */

