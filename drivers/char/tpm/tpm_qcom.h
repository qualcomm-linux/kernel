/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __TPM_QCOM_H__
#define __TPM_QCOM_H__

#include <linux/bitfield.h>
#include <linux/tee_drv.h>
#include <linux/tpm.h>
#include <linux/uuid.h>

#define QCOMTEE_ROOT_OP_REG_WITH_CREDENTIALS	5
#define QCOMTEE_OP_CLIENT_ENV_OPEN		0
#define QCOMTEE_MSG_OBJECT_OP_MASK		GENMASK(15, 0)
#define QCOMTEE_MSG_OBJECT_OP_RELEASE		(QCOMTEE_MSG_OBJECT_OP_MASK - 0)

#define QCOMTEE_TPM_OP_SEND_COMMAND	0

/* UID of the "qcom.tz.tpm" service */
#define QCOMTEE_TPM_UID			81

/* Max buffer size supported by TPM TA */
#define MAX_COMMAND_SIZE		SZ_4K
#define MAX_RESPONSE_SIZE		SZ_4K

#define QCOMTEE_TPM_GET_TA_VERSION_ID	0x0001000
#define QCOMTEE_TPM_TA_VERSION_MAJOR	GENMASK(31, 16)
#define QCOMTEE_TPM_TA_VERSION_MINOR	GENMASK(15, 0)

struct tpm_qcom_ta_version_req {
	u32 command_id;
} __packed;

struct tpm_qcom_ta_version_rsp {
	u32 status;
	u32 command_id;
	u32 version_num;
} __packed;

#define QCOMTEE_TPM_TYPE_ID		0x0080000
#define QCOMTEE_TPM_TYPE_DTPM		0x6454504dU
#define QCOMTEE_TPM_TYPE_FTPM		0x6654504dU
#define QCOMTEE_TPM_TYPE_NONE		0x4e6f6e65U

struct tpm_qcom_type_req {
	u32 command_id;
} __packed;

struct tpm_qcom_type_rsp {
	u32 command_id;
	u32 status;
	u32 tpm_type;
} __packed;

/*
 * dTPM SPI transfer optimization:
 * TRANSFER_START before a burst of commands, TRANSFER_END once done.
 */
#define QCOMTEE_TPM_TRANSFER_ID		0x0000002
#define QCOMTEE_TPM_TRANSFER_END	0
#define QCOMTEE_TPM_TRANSFER_START	1

struct tpm_qcom_transfer_req {
	u32 command_id;
	u32 transfer_state;
} __packed;

struct tpm_qcom_transfer_rsp {
	u32 command_id;
	u32 status;
} __packed;

struct tpm_qcom_private {
	struct tpm_chip *chip;
	struct device *dev;
	struct tee_context *ctx;
	struct tee_param_objref tpm_svc_obj;
	bool is_dtpm;
};

#endif /* __TPM_QCOM_H__ */
