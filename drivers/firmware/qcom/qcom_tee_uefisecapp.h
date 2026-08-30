/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef QCOM_TEE_UEFISECAPP_H
#define QCOM_TEE_UEFISECAPP_H

#define QCOMTEE_OP_CLIENT_ENV_OPEN 0
#define QCOMTEE_ROOT_OP_REG_WITH_CREDENTIALS 5

/* Each service exposed by QTEE is identified by a 32-bit UID */
#define QCOMTEE_UEFI_SEC_UID                 413

/* Operations supported by the UEFI Sec App service */
#define QCOMTEE_UEFI_SEC_OP_GET_VAR 0
#define QCOMTEE_UEFI_SEC_OP_SET_VAR 1
#define QCOMTEE_UEFI_SEC_OP_QUERY_VAR_INFO 2
#define QCOMTEE_UEFI_SEC_OP_GET_NEXT_VAR_NAME 3

/* Error codes returned by the UEFI Sec App service */
#define QCOMTEE_UEFI_SEC_SUCCESS 0
#define QCOMTEE_UEFI_SEC_ERROR_INVALID_PARAMETER 10
#define QCOMTEE_UEFI_SEC_ERROR_UNSUPPORTED 11
#define QCOMTEE_UEFI_SEC_ERROR_WRITE_PROTECTED 12
#define QCOMTEE_UEFI_SEC_ERROR_SECURITY_VIOLATION 13
#define QCOMTEE_UEFI_SEC_ERROR_DEVICE_ERROR 14
#define QCOMTEE_UEFI_SEC_ERROR_OUT_OF_RESOURCES 15
#define QCOMTEE_UEFI_SEC_ERROR_VOLUME_CORRUPTED 16
#define QCOMTEE_UEFI_SEC_ERROR_SIZE_OUT 17
#define QCOMTEE_UEFI_SEC_ERROR_NOT_FOUND 18
#define QCOMTEE_UEFI_SEC_ERROR_ALREADY_STARTED 19

/* Operations for objects are 32-bit. QCOMTEE transport uses the upper 16 bits. */
#define QCOMTEE_MSG_OBJECT_OP_MASK GENMASK(15, 0)
#define QCOMTEE_MSG_OBJECT_OP_RELEASE (QCOMTEE_MSG_OBJECT_OP_MASK - 0)

/**
 * struct qcomtee_uefisec_app - An instance of UEFI Secure Application.
 * @dev: TEE client device on the TEE bus which represents uefisecapp.
 * @ctx: The context opened with the TEE subsystem by the uefisecapp client.
 * @uefisec_svc_obj: A TEE object representing the uefisecapp service.
 * @efivars: EFI variables registered with the EFI subsystem.
 */
struct qcomtee_uefisec_app {
	struct device *dev;
	struct tee_context *ctx;
	struct tee_param_objref uefisec_svc_obj;
	struct efivars efivars;
};

#define UEFISECAPP_UUID \
	UUID_INIT(0x01f95dcd, 0x2d7e, 0x58be, \
		  0xa1, 0x43, 0x81, 0x32, 0xa1, 0x72, 0xdb, 0x7d)

/* Short-hands for these long attribute names */
#define UBUF_INPUT     TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_INPUT
#define UBUF_OUTPUT    TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_OUTPUT
#define OBJREF_INPUT   TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_INPUT
#define OBJREF_OUTPUT  TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_OUTPUT

/* Init instance of 'struct tee_param_objref'. */
#define SET_TEE_PARAM_OBJREF(param, attri, obj_id, obj_flag) do { \
		(param).attr = (attri); \
		(param).u.objref.id = (obj_id); \
		(param).u.objref.flags = (obj_flag); \
	} while (0)

/* Init instance of 'struct tee_param_ubuf'. */
#define SET_TEE_PARAM_UBUF(param, attri, ubuff) do { \
		(param).attr = (attri); \
		(param).u.ubuf = (ubuff);  \
	} while (0)

#define TEE_PARAM_UBUF(x) ((struct tee_param_ubuf){ .addr = &(x), sizeof(x) })

#define SET_INVOKE_ARG(arg, object_id, opp, nparam) do { \
		(arg).id = (object_id); \
		(arg).op = (opp); \
		(arg).num_params = (nparam); \
	} while (0)

static inline efi_status_t uefisecapp_err_to_efi_status(u32 err)
{
	switch (err) {
	case QCOMTEE_UEFI_SEC_SUCCESS:
		return EFI_SUCCESS;

	case QCOMTEE_UEFI_SEC_ERROR_INVALID_PARAMETER:
		return EFI_INVALID_PARAMETER;

	case QCOMTEE_UEFI_SEC_ERROR_UNSUPPORTED:
		return EFI_UNSUPPORTED;

	case QCOMTEE_UEFI_SEC_ERROR_WRITE_PROTECTED:
		return EFI_WRITE_PROTECTED;

	case QCOMTEE_UEFI_SEC_ERROR_SECURITY_VIOLATION:
		return EFI_SECURITY_VIOLATION;

	case QCOMTEE_UEFI_SEC_ERROR_DEVICE_ERROR:
		return EFI_DEVICE_ERROR;

	case QCOMTEE_UEFI_SEC_ERROR_OUT_OF_RESOURCES:
		return EFI_OUT_OF_RESOURCES;

	case QCOMTEE_UEFI_SEC_ERROR_SIZE_OUT:
		return EFI_BUFFER_TOO_SMALL;

	case QCOMTEE_UEFI_SEC_ERROR_NOT_FOUND:
		return EFI_NOT_FOUND;

	/* No matching on EFI_* list. */
	case QCOMTEE_UEFI_SEC_ERROR_ALREADY_STARTED: /* EFI_ALREADY_STARTED.  */
	case QCOMTEE_UEFI_SEC_ERROR_VOLUME_CORRUPTED: /* EFI_VOLUME_CORRUPTED. */
	default:
		return EFI_DEVICE_ERROR;
	}
}
#endif /* QCOM_TEE_UEFISECAPP_H */
