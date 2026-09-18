// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/efi.h>
#include <linux/tee.h>
#include <linux/tee_drv.h>
#include <linux/ucs2_string.h>
#include "qcom_tee_uefisecapp.h"

static struct qcomtee_uefisec_app uefisec_app;

static int qcuefi_get_variable(struct tee_param_ubuf in_variable, efi_guid_t *guid,
			       struct tee_param_ubuf in_attributes,
			       struct tee_param_ubuf *data,
			       u32 *out_attributes, u32 *out_errno)
{
	int ret;
	struct tee_ioctl_object_invoke_arg inv_arg;
	u64 obj_id = uefisec_app.uefisec_svc_obj.id;
	u32 nparams = 5;
	struct tee_param param[nparams];

	struct {
		efi_guid_t guid;
		u32 in_data_size;
	} in_cong = { 0 };

	struct {
		u32 out_data_size;
		u32 attributes;
		u32 errno;
	} out_cong = { 0 };

	in_cong.guid = *guid;
	in_cong.in_data_size = data->size;

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	SET_INVOKE_ARG(inv_arg, obj_id, QCOMTEE_UEFI_SEC_OP_GET_VAR, nparams);
	SET_TEE_PARAM_UBUF(param[0], UBUF_INPUT, TEE_PARAM_UBUF(in_cong));
	SET_TEE_PARAM_UBUF(param[1], UBUF_INPUT, in_variable);
	SET_TEE_PARAM_UBUF(param[2], UBUF_INPUT, in_attributes);
	SET_TEE_PARAM_UBUF(param[3], UBUF_OUTPUT, TEE_PARAM_UBUF(out_cong));
	SET_TEE_PARAM_UBUF(param[4], UBUF_OUTPUT, *data);

	ret = tee_client_object_invoke_func(uefisec_app.ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(uefisec_app.dev, "QCOMTEE_UEFI_SEC_OP_GET_VAR invoke ret: %d, err: 0x%x\n",
			ret, inv_arg.ret);
		return ret ?: inv_arg.ret;
	}

	data->size = out_cong.out_data_size;
	*out_attributes = out_cong.attributes;
	*out_errno = out_cong.errno;

	return ret;
}

static efi_status_t qcomtee_uefi_get_variable(efi_char16_t *name, efi_guid_t *guid,
					      u32 *attr, unsigned long *data_size,
					      void *data)
{
	int ret;
	u32 in_attr, out_attributes, out_errno;
	struct tee_param_ubuf in_data, in_var, in_attributes;

	if (!name || !guid)
		return EFI_INVALID_PARAMETER;

	/* 'attr' can be NULL, however an input attribute is always expected
	 * by UefiSecApp TA
	 */
	in_attr = 0;
	if (attr)
		in_attr = *attr;

	in_data = (struct tee_param_ubuf){ .addr = data, *data_size };
	in_var = (struct tee_param_ubuf){ .addr = name,
					  (ucs2_strlen(name) + 1) * sizeof(*name) };
	in_attributes = (struct tee_param_ubuf){ .addr = &in_attr, sizeof(u32) };

	/* On SUCCESS, 'data' member of 'in_data' has already been updated. */
	ret = qcuefi_get_variable(in_var, guid, in_attributes, &in_data,
				  &out_attributes, &out_errno);

	if (ret)
		return EFI_DEVICE_ERROR;

	if (!out_errno || out_errno == QCOMTEE_UEFI_SEC_ERROR_SIZE_OUT) {
		/* If 'attr' is NULL 'out_attributes' is not updated. */
		if (attr)
			*attr = out_attributes;

		*data_size = in_data.size;
	}

	return uefisecapp_err_to_efi_status(out_errno);
}

static int qcuefi_set_variable(struct tee_param_ubuf in_variable, efi_guid_t *guid,
			       u32 attributes, struct tee_param_ubuf data,
			       u32 *out_errno)
{
	int ret;
	struct tee_ioctl_object_invoke_arg inv_arg;
	u64 obj_id = uefisec_app.uefisec_svc_obj.id;
	u32 nparams = 4;
	struct tee_param param[nparams];

	struct {
		efi_guid_t guid;
		u32 attributes;
		u32 in_data_size;
	} in_cong = { 0 };

	in_cong.guid = *guid;
	in_cong.attributes = attributes;
	in_cong.in_data_size = data.size;

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	SET_INVOKE_ARG(inv_arg, obj_id, QCOMTEE_UEFI_SEC_OP_SET_VAR, nparams);
	SET_TEE_PARAM_UBUF(param[0], UBUF_INPUT, TEE_PARAM_UBUF(in_cong));
	SET_TEE_PARAM_UBUF(param[1], UBUF_INPUT, in_variable);
	SET_TEE_PARAM_UBUF(param[2], UBUF_INPUT, data);
	SET_TEE_PARAM_UBUF(param[3], UBUF_OUTPUT, TEE_PARAM_UBUF(*out_errno));

	ret = tee_client_object_invoke_func(uefisec_app.ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(uefisec_app.dev, "QCOMTEE_UEFI_SEC_OP_SET_VAR invoke ret: %d, err: 0x%x\n",
			ret, inv_arg.ret);
		return ret ?: inv_arg.ret;
	}

	return ret;
}

static efi_status_t qcomtee_uefi_set_variable(efi_char16_t *name, efi_guid_t *guid,
					      u32 attr, unsigned long data_size,
					      void *data)
{
	int ret;
	u32 out_errno;
	struct tee_param_ubuf in_data, in_var;

	if (!name || !guid)
		return EFI_INVALID_PARAMETER;

	in_data = (struct tee_param_ubuf){ .addr = data, data_size };
	in_var = (struct tee_param_ubuf){ .addr = name,
					  (ucs2_strlen(name) + 1) * sizeof(*name) };

	ret = qcuefi_set_variable(in_var, guid, attr, in_data, &out_errno);
	if (ret)
		return EFI_DEVICE_ERROR;

	return uefisecapp_err_to_efi_status(out_errno);
}

static int qcuefi_get_next_variable(struct tee_param_ubuf in_variable, efi_guid_t *guid,
				    struct tee_param_ubuf *out_variable,
				    efi_guid_t *out_vendor_guid, u32 *out_errno)
{
	int ret;
	struct tee_ioctl_object_invoke_arg inv_arg;
	u64 obj_id = uefisec_app.uefisec_svc_obj.id;
	u32 nparams = 4;
	struct tee_param param[nparams];

	struct {
		efi_guid_t guid;
		u32 in_data_size;
	} in_cong = { 0 };

	struct {
		efi_guid_t guid;
		u32 out_data_size;
		u32 errno;
	} out_cong = { 0 };

	/* Pass size of available buffer */
	in_cong.in_data_size = out_variable->size;
	in_cong.guid = *guid;

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	SET_INVOKE_ARG(inv_arg, obj_id, QCOMTEE_UEFI_SEC_OP_GET_NEXT_VAR_NAME, nparams);
	SET_TEE_PARAM_UBUF(param[0], UBUF_INPUT, TEE_PARAM_UBUF(in_cong));
	SET_TEE_PARAM_UBUF(param[1], UBUF_INPUT, in_variable);
	SET_TEE_PARAM_UBUF(param[2], UBUF_OUTPUT, TEE_PARAM_UBUF(out_cong));
	SET_TEE_PARAM_UBUF(param[3], UBUF_OUTPUT, *out_variable);

	ret = tee_client_object_invoke_func(uefisec_app.ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(uefisec_app.dev, "QCOMTEE_UEFI_SEC_OP_GET_NEXT_VAR_NAME invoke ret: %d, err: 0x%x\n",
			ret, inv_arg.ret);
		return ret ?: inv_arg.ret;
	}

	/* UefiSecApp TA does not touch 'out_variable.size'. Update it here.
	 * On SUCCESS (!out_errno), 'out_data_size' is length of name in 'out_variable.addr'.
	 * On failure (out_errno == QCOMTEE_UEFI_SEC_ERROR_SIZE_OUT), 'out_data_size' is
	 * actual name length.
	 * Otherwise, it's undefined.
	 */
	out_variable->size = out_cong.out_data_size;
	*out_vendor_guid = out_cong.guid;
	*out_errno = out_cong.errno;

	return ret;
}

static efi_status_t qcomtee_uefi_get_next_variable(unsigned long *name_size,
						   efi_char16_t *name,
						   efi_guid_t *guid)
{
	int ret;
	u32 out_errno;
	efi_guid_t out_guid;
	struct tee_param_ubuf in_var, out_var;

	if (!name_size || !name || !guid)
		return EFI_INVALID_PARAMETER;

	if (*name_size == 0)
		return EFI_INVALID_PARAMETER;

	/* For 'in_var', 'name_size' is not necessarily size of 'name';
	 * could be size of buffer where 'name' has been stored. TA expects a
	 * NULL-terminated string in 'name' and ignores the size.
	 * For 'out_var', 'name_size' is size of buffer pointed by 'name'.
	 */
	in_var = (struct tee_param_ubuf){ .addr = name, *name_size };
	out_var = (struct tee_param_ubuf){ .addr = name, *name_size };

	ret = qcuefi_get_next_variable(in_var, guid, &out_var, &out_guid,
				       &out_errno);
	if (ret)
		return EFI_DEVICE_ERROR;

	if (!out_errno)
		*guid = out_guid;

	if (!out_errno || out_errno == QCOMTEE_UEFI_SEC_ERROR_SIZE_OUT)
		*name_size = out_var.size;

	/* On SUCCESS, 'name' stores the next variable name. */
	return uefisecapp_err_to_efi_status(out_errno);
}

static int qcuefi_query_variable_info(u32 attributes,
				      u64 *maximum_variable_storage_size,
				      u64 *remaining_variable_storage_size,
				      u64 *maximum_variable_size, u32 *out_errno)
{
	int ret;
	struct tee_ioctl_object_invoke_arg inv_arg;
	u64 obj_id = uefisec_app.uefisec_svc_obj.id;
	u32 nparams = 2;
	struct tee_param param[nparams];

	struct {
		u64 max_var_storage_size;
		u64 remaining_var_storage_size;
		u64 maximum_var_size;
		u32 errno;
	} out_cong = { 0 };

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	SET_INVOKE_ARG(inv_arg, obj_id, QCOMTEE_UEFI_SEC_OP_QUERY_VAR_INFO, nparams);
	SET_TEE_PARAM_UBUF(param[0], UBUF_INPUT, TEE_PARAM_UBUF(attributes));
	SET_TEE_PARAM_UBUF(param[1], UBUF_OUTPUT, TEE_PARAM_UBUF(out_cong));

	ret = tee_client_object_invoke_func(uefisec_app.ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(uefisec_app.dev, "QCOMTEE_UEFI_SEC_OP_QUERY_VAR_INFO invoke ret: %d, err: 0x%x\n",
			ret, inv_arg.ret);
		return ret ?: inv_arg.ret;
	}

	*maximum_variable_storage_size = out_cong.max_var_storage_size;
	*remaining_variable_storage_size = out_cong.remaining_var_storage_size;
	*maximum_variable_size = out_cong.maximum_var_size;
	*out_errno = out_cong.errno;

	return ret;
}

static efi_status_t qcomtee_uefi_query_variable_info(u32 attr, u64 *storage_space,
						     u64 *remaining_space,
						     u64 *max_variable_size)
{
	int ret;
	u32 out_errno;
	u64 maximum_variable_storage_size;
	u64 remaining_variable_storage_size;
	u64 maximum_variable_size;

	if (!storage_space || !remaining_space || !max_variable_size)
		return EFI_INVALID_PARAMETER;

	ret = qcuefi_query_variable_info(attr,
					 &maximum_variable_storage_size,
					 &remaining_variable_storage_size,
					 &maximum_variable_size,
					 &out_errno);

	if (ret)
		return EFI_DEVICE_ERROR;

	if (!out_errno) {
		*storage_space = maximum_variable_storage_size;
		*remaining_space = remaining_variable_storage_size;
		*max_variable_size = maximum_variable_size;
	}

	return uefisecapp_err_to_efi_status(out_errno);
}

/**
 * qcomtee_release_object() - Release an object returned by QTEE.
 *
 * Each object returned by QTEE repesents a secure service exposed to the
 * client. Whenever an secure service is opened, QTEE may allocate resources
 * on the client's behalf. Therefore, once the client is done accessing the
 * secure service, the object representing it should be explicitly released
 * so that QTEE can release the associated resources as well.
 *
 * @ctx: TEE context.
 * @object: The object to release.
 */
static void qcomtee_release_object(struct tee_context *ctx,
				   struct tee_param_objref object)
{
	struct tee_ioctl_object_invoke_arg inv_arg;

	memset(&inv_arg, 0, sizeof(inv_arg));
	SET_INVOKE_ARG(inv_arg, object.id, QCOMTEE_MSG_OBJECT_OP_RELEASE, 0);
	tee_client_object_invoke_func(ctx, &inv_arg, NULL);
}

/**
 * qcomtee_get_uefisec_svc_obj() - Get a UEFI Secure App service object to
 * begin communication with the service.
 * @ctx: TEE context.
 * @client_env_obj: The client environment object returned earlier by QTEE.
 * @uefisec_svc_obj: The UEFI Secure App service object.
 *
 * Returns 0 on success.
 * Returns < 0 if client environment object invocation failed.
 * Returns > 0 if client environment invocation was success but UEFI Secure App
 * service object could not be returned for some other reason (represented by the
 * returned value)
 */
static int qcomtee_get_uefisec_svc_obj(struct tee_context *ctx,
				       struct tee_param_objref client_env_obj,
				       struct tee_param_objref *uefisec_svc_obj)
{
	int ret;
	struct tee_ioctl_object_invoke_arg inv_arg;
	u64 obj_id = client_env_obj.id;
	u32 nparams = 2;
	struct tee_param param[nparams];
	u32 uefisec_uid = QCOMTEE_UEFI_SEC_UID;

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	SET_INVOKE_ARG(inv_arg, obj_id, QCOMTEE_OP_CLIENT_ENV_OPEN, nparams);
	SET_TEE_PARAM_UBUF(param[0], UBUF_INPUT, TEE_PARAM_UBUF(uefisec_uid));
	SET_TEE_PARAM_OBJREF(param[1], OBJREF_OUTPUT, 0, 0);

	ret = tee_client_object_invoke_func(ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(uefisec_app.dev, "QCOMTEE_CLIENT_ENV_OPEN invoke ret: %d, err: 0x%x\n",
			ret, inv_arg.ret);
		return ret ?: inv_arg.ret;
	}

	*uefisec_svc_obj = param[1].u.objref;
	return ret;
}

/**
 * qcomtee_get_client_env_obj() - Get a client environment object to begin
 * object exchange with QTEE.
 * @ctx: TEE context.
 * @client_env_obj: The client environment object returned by QTEE.
 *
 * Returns 0 on success.
 * Returns < 0 if root object invocation failed.
 * Returns > 0 if root object invocation was success but client environment
 * object could not be returned for some other reason (represented by the
 * returned value)
 */
static int qcomtee_get_client_env_obj(struct tee_context *ctx,
				      struct tee_param_objref *client_env_obj)
{
	int ret;
	struct tee_ioctl_object_invoke_arg inv_arg;
	u32 nparams = 2;
	struct tee_param param[nparams];

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	SET_INVOKE_ARG(inv_arg, TEE_OBJREF_NULL,
		       QCOMTEE_ROOT_OP_REG_WITH_CREDENTIALS, nparams);
	SET_TEE_PARAM_OBJREF(param[0], OBJREF_INPUT, TEE_OBJREF_NULL, 0);
	SET_TEE_PARAM_OBJREF(param[1], OBJREF_OUTPUT, 0, 0);

	ret = tee_client_object_invoke_func(ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(uefisec_app.dev, "QCOMTEE_ROOT_OP_REG_WITH_CREDENTIALS invoke ret: %d, err: 0x%x\n",
			ret, inv_arg.ret);
		return ret ?: inv_arg.ret;
	}

	*client_env_obj = param[1].u.objref;
	return ret;
}

static const struct efivar_operations qcom_efivar_ops = {
	.get_variable = qcomtee_uefi_get_variable,
	.set_variable = qcomtee_uefi_set_variable,
	.get_next_variable = qcomtee_uefi_get_next_variable,
	.query_variable_info = qcomtee_uefi_query_variable_info,
};

static int qcomtee_ctx_match(struct tee_ioctl_version_data *ver,
			     const void *data)
{
	return (ver->impl_id == TEE_IMPL_ID_QTEE);
}

static int qcomtee_uefisecapp_probe(struct tee_client_device *tee_dev)
{
	int ret, err;
	struct tee_param_objref client_env_obj;
	struct tee_param_objref uefisec_svc_obj;

	uefisec_app.dev = &tee_dev->dev;
	/* Open context with QCOMTEE driver */
	uefisec_app.ctx = tee_client_open_context(NULL, qcomtee_ctx_match, NULL,
						  NULL);
	if (IS_ERR(uefisec_app.ctx))
		return -ENODEV;

	/* Obtain a reference to client_env object to begin object exchange
	 * with QTEE
	 */
	ret = qcomtee_get_client_env_obj(uefisec_app.ctx, &client_env_obj);
	if (ret) {
		err = -EINVAL;
		goto err_get_client_env;
	}

	/* Obtain a reference to the uefisec_svc object which provides access to
	 * the EFI var storage.
	 */
	ret = qcomtee_get_uefisec_svc_obj(uefisec_app.ctx, client_env_obj,
					  &uefisec_svc_obj);
	if (ret) {
		err = -EINVAL;
		goto err_get_uefisec_svc;
	}
	uefisec_app.uefisec_svc_obj = uefisec_svc_obj;

	ret = efivars_register(&uefisec_app.efivars, &qcom_efivar_ops);
	if (ret) {
		err = ret;
		goto err_efi_vars_reg;
	}

	/* We don't need to keep a reference to this object anymore, we only
	 * needed it to obtain the uefisec_svc object.
	 */
	qcomtee_release_object(uefisec_app.ctx, client_env_obj);
	return 0;

err_efi_vars_reg:
	qcomtee_release_object(uefisec_app.ctx, uefisec_svc_obj);
err_get_uefisec_svc:
	qcomtee_release_object(uefisec_app.ctx, client_env_obj);
err_get_client_env:
	tee_client_close_context(uefisec_app.ctx);

	return err;
}

static void qcomtee_uefisecapp_remove(struct tee_client_device *tee_dev)
{
	efivars_unregister(&uefisec_app.efivars);
	qcomtee_release_object(uefisec_app.ctx, uefisec_app.uefisec_svc_obj);
	tee_client_close_context(uefisec_app.ctx);
}

static const struct tee_client_device_id qcomtee_uefisecapp_id_table[] = {
	{UEFISECAPP_UUID},
	{}
};
MODULE_DEVICE_TABLE(tee, qcomtee_uefisecapp_id_table);

static struct tee_client_driver qcomtee_uefisecapp_driver = {
	.id_table	= qcomtee_uefisecapp_id_table,
	.probe		= qcomtee_uefisecapp_probe,
	.remove		= qcomtee_uefisecapp_remove,
	.driver		= {
		.name		= "qcom-tee-uefisecapp",
	},
};

module_tee_client_driver(qcomtee_uefisecapp_driver);

MODULE_AUTHOR("Qualcomm");
MODULE_DESCRIPTION("TEE client driver for Qualcomm TEE UEFI Secure App");
MODULE_LICENSE("GPL");
