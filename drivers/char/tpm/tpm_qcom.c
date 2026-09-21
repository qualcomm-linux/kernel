// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/tee.h>
#include <linux/tee_drv.h>
#include <linux/tpm.h>
#include <linux/uuid.h>

#include "tpm.h"
#include "tpm_qcom.h"

/* UUID of the QTEE-bus device representing the TPM TA. */
static const uuid_t tpm_qcom_uuid =
	UUID_INIT(0xaabcb593, 0x7083, 0x5536,
		  0xac, 0x27, 0x3d, 0x2d, 0x89, 0x41, 0x9d, 0xdb);

static void tpm_qcom_release_object(struct tee_context *ctx,
				    struct tee_param_objref object)
{
	struct tee_ioctl_object_invoke_arg inv_arg = {};

	inv_arg.id = object.id;
	inv_arg.op = QCOMTEE_MSG_OBJECT_OP_RELEASE;
	inv_arg.num_params = 0;

	tee_client_object_invoke_func(ctx, &inv_arg, NULL);
}

static int tpm_qcom_get_client_env_obj(struct tee_context *ctx,
				       struct tee_param_objref *client_env_obj)
{
	struct tee_ioctl_object_invoke_arg inv_arg = {};
	struct tee_param param[2] = {};
	int ret;

	inv_arg.id = TEE_OBJREF_NULL;
	inv_arg.op = QCOMTEE_ROOT_OP_REG_WITH_CREDENTIALS;
	inv_arg.num_params = 2;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_INPUT;
	param[0].u.objref.id = TEE_OBJREF_NULL;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_OUTPUT;

	ret = tee_client_object_invoke_func(ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ?: inv_arg.ret;

	*client_env_obj = param[1].u.objref;
	return ret;
}

static int tpm_qcom_get_svc_obj(struct tee_context *ctx,
				struct tee_param_objref client_env_obj,
				struct tee_param_objref *tpm_svc_obj)
{
	struct tee_ioctl_object_invoke_arg inv_arg = {};
	struct tee_param param[2] = {};
	u32 tpm_uid = QCOMTEE_TPM_UID;
	int ret;

	inv_arg.id = client_env_obj.id;
	inv_arg.op = QCOMTEE_OP_CLIENT_ENV_OPEN;
	inv_arg.num_params = 2;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_INPUT;
	param[0].u.ubuf = (struct tee_param_ubuf){ .addr = &tpm_uid,
						    .size = sizeof(tpm_uid) };
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_OUTPUT;

	ret = tee_client_object_invoke_func(ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ?: inv_arg.ret;

	*tpm_svc_obj = param[1].u.objref;
	return ret;
}

static int tpm_qcom_send_command(struct tpm_qcom_private *pvt_data,
				 u32 locality, void *req, size_t req_len,
				 void *rsp, size_t *rsp_len)
{
	struct tee_ioctl_object_invoke_arg inv_arg = {};
	struct tee_param param[3] = {};
	u8 locality_arg = locality;
	int ret;

	inv_arg.id = pvt_data->tpm_svc_obj.id;
	inv_arg.op = QCOMTEE_TPM_OP_SEND_COMMAND;
	inv_arg.num_params = 3;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_INPUT;
	param[0].u.ubuf = (struct tee_param_ubuf){ .addr = &locality_arg,
						   .size = sizeof(locality_arg) };
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_INPUT;
	param[1].u.ubuf = (struct tee_param_ubuf){ .addr = req, .size = req_len };
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_OUTPUT;
	param[2].u.ubuf = (struct tee_param_ubuf){ .addr = rsp, .size = *rsp_len };

	ret = tee_client_object_invoke_func(pvt_data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(pvt_data->dev,
			"send_command invoke ret: %d, err: 0x%x\n",
			ret, inv_arg.ret);
		return ret ?: inv_arg.ret;
	}

	*rsp_len = param[2].u.ubuf.size;

	return ret;
}

static int tpm_qcom_get_ta_details(struct tpm_qcom_private *pvt_data)
{
	struct tpm_qcom_ta_version_req ver_req = {
		.command_id = QCOMTEE_TPM_GET_TA_VERSION_ID,
	};
	struct tpm_qcom_ta_version_rsp ver_rsp;
	size_t ver_rsp_len = sizeof(ver_rsp);
	struct tpm_qcom_type_req type_req = {
		.command_id = QCOMTEE_TPM_TYPE_ID,
	};
	struct tpm_qcom_type_rsp type_rsp;
	size_t type_rsp_len = sizeof(type_rsp);
	int ret;

	ret = tpm_qcom_send_command(pvt_data, 0, &ver_req, sizeof(ver_req),
				    &ver_rsp, &ver_rsp_len);
	if (ret || ver_rsp_len < sizeof(ver_rsp) || ver_rsp.status != 0) {
		dev_err(pvt_data->dev,
			"failed to query TA version: ret=%d, status=%u\n",
			ret, ret ? 0 : ver_rsp.status);
		return ret ?: -EIO;
	}

	dev_info(pvt_data->dev, "TPM TA version %lu.%lu\n",
		 FIELD_GET(QCOMTEE_TPM_TA_VERSION_MAJOR, ver_rsp.version_num),
		 FIELD_GET(QCOMTEE_TPM_TA_VERSION_MINOR, ver_rsp.version_num));

	ret = tpm_qcom_send_command(pvt_data, 0, &type_req, sizeof(type_req),
				    &type_rsp, &type_rsp_len);
	if (ret || type_rsp_len < sizeof(type_rsp) || type_rsp.status != 0) {
		dev_err(pvt_data->dev,
			"failed to query TPM type: ret=%d, status=%u\n",
			ret, ret ? 0 : type_rsp.status);
		return ret ?: -EIO;
	}

	switch (type_rsp.tpm_type) {
	case QCOMTEE_TPM_TYPE_FTPM:
		dev_info(pvt_data->dev, "TPM type: fTPM\n");
		pvt_data->is_dtpm = false;
		break;
	case QCOMTEE_TPM_TYPE_DTPM:
		dev_info(pvt_data->dev, "TPM type: dTPM\n");
		pvt_data->is_dtpm = true;
		break;
	default:
		dev_err(pvt_data->dev, "unsupported TPM type: 0x%08x\n",
			type_rsp.tpm_type);
		return -EIO;
	}

	return 0;
}

/* fTPM does not implement this command and to be invoked via dtpm only. */
static void tpm_qcom_transfer(struct tpm_qcom_private *pvt_data,
			      u32 transfer_state)
{
	struct tpm_qcom_transfer_req req = {
		.command_id = QCOMTEE_TPM_TRANSFER_ID,
		.transfer_state = transfer_state,
	};
	struct tpm_qcom_transfer_rsp rsp;
	size_t rsp_len = sizeof(rsp);
	int ret;

	ret = tpm_qcom_send_command(pvt_data, 0, &req, sizeof(req), &rsp,
				    &rsp_len);
	if (ret || rsp_len < sizeof(rsp) || rsp.status != 0)
		dev_warn(pvt_data->dev,
			 "transfer state=%u hint failed: ret=%d, status=%u\n",
			 transfer_state, ret, ret ? 0 : rsp.status);
}

static int tpm_qcom_cmd_ready(struct tpm_chip *chip)
{
	struct tpm_qcom_private *pvt_data = dev_get_drvdata(chip->dev.parent);

	if (pvt_data->is_dtpm)
		tpm_qcom_transfer(pvt_data, QCOMTEE_TPM_TRANSFER_START);

	return 0;
}

static int tpm_qcom_go_idle(struct tpm_chip *chip)
{
	struct tpm_qcom_private *pvt_data = dev_get_drvdata(chip->dev.parent);

	if (pvt_data->is_dtpm)
		tpm_qcom_transfer(pvt_data, QCOMTEE_TPM_TRANSFER_END);

	return 0;
}

/*
 * The raw TPM2 command in @buf is sent directly as send_command's UBUF-in
 * param and the raw TPM2 response is read back from its UBUF-out param.
 */
static int tpm_qcom_send(struct tpm_chip *chip, u8 *buf, size_t bufsiz,
			 size_t cmd_len)
{
	struct tpm_qcom_private *pvt_data = dev_get_drvdata(chip->dev.parent);
	size_t rsp_len = PAGE_ALIGN(MAX_RESPONSE_SIZE);
	size_t copy_len;
	int ret;

	if (cmd_len > MAX_COMMAND_SIZE) {
		dev_err(&chip->dev, "len=%zd exceeds MAX_COMMAND_SIZE\n", cmd_len);
		return -EIO;
	}

	u8 *response __free(kfree) = kzalloc(rsp_len, GFP_KERNEL);
	if (!response)
		return -ENOMEM;

	ret = tpm_qcom_send_command(pvt_data, 0, buf, cmd_len, response,
				    &rsp_len);
	if (ret < 0) {
		dev_err(&chip->dev, "send_command failed: ret=%d\n", ret);
		return ret;
	}

	copy_len = min_t(size_t, bufsiz, rsp_len);
	memcpy(buf, response, copy_len);

	return copy_len;
}

static const struct tpm_class_ops tpm_qcom_ops = {
	.flags = TPM_OPS_AUTO_STARTUP,
	.send = tpm_qcom_send,
	.cmd_ready = tpm_qcom_cmd_ready,
	.go_idle = tpm_qcom_go_idle,
};

static int tpm_qcom_ctx_match(struct tee_ioctl_version_data *ver,
			      const void *data)
{
	return (ver->impl_id == TEE_IMPL_ID_QTEE);
}

static int tpm_qcom_probe(struct tee_client_device *tee_dev)
{
	struct device *dev = &tee_dev->dev;
	struct tpm_qcom_private *pvt_data;
	struct tee_param_objref client_env_obj;
	struct tee_param_objref tpm_svc_obj;
	struct tpm_chip *chip;
	int rc, err;

	pvt_data = devm_kzalloc(dev, sizeof(*pvt_data), GFP_KERNEL);
	if (!pvt_data)
		return -ENOMEM;

	dev_set_drvdata(dev, pvt_data);

	pvt_data->ctx = tee_client_open_context(NULL, tpm_qcom_ctx_match, NULL, NULL);
	if (IS_ERR(pvt_data->ctx))
		return -ENODEV;

	rc = tpm_qcom_get_client_env_obj(pvt_data->ctx, &client_env_obj);
	if (rc) {
		err = -EINVAL;
		goto out_ctx;
	}

	rc = tpm_qcom_get_svc_obj(pvt_data->ctx, client_env_obj, &tpm_svc_obj);
	if (rc) {
		err = -EINVAL;
		goto out_client_env;
	}
	pvt_data->tpm_svc_obj = tpm_svc_obj;
	pvt_data->dev = dev;

	err = tpm_qcom_get_ta_details(pvt_data);
	if (err)
		goto out_svc_obj;

	chip = tpm_chip_alloc(dev, &tpm_qcom_ops);
	if (IS_ERR(chip)) {
		dev_err(dev, "tpm_chip_alloc failed\n");
		err = PTR_ERR(chip);
		goto out_svc_obj;
	}

	pvt_data->chip = chip;
	pvt_data->chip->flags |= TPM_CHIP_FLAG_TPM2 | TPM_CHIP_FLAG_SYNC;

	err = tpm_chip_register(pvt_data->chip);
	if (err) {
		dev_err(dev, "tpm_chip_register failed with rc=%d\n", err);
		goto out_chip;
	}

	tpm_qcom_release_object(pvt_data->ctx, client_env_obj);
	return 0;

out_chip:
	put_device(&pvt_data->chip->dev);
out_svc_obj:
	tpm_qcom_release_object(pvt_data->ctx, tpm_svc_obj);
out_client_env:
	tpm_qcom_release_object(pvt_data->ctx, client_env_obj);
out_ctx:
	tee_client_close_context(pvt_data->ctx);
	return err;
}

static void tpm_qcom_remove(struct tee_client_device *tee_dev)
{
	struct tpm_qcom_private *pvt_data = dev_get_drvdata(&tee_dev->dev);

	tpm_chip_unregister(pvt_data->chip);
	put_device(&pvt_data->chip->dev);
	tpm_qcom_release_object(pvt_data->ctx, pvt_data->tpm_svc_obj);
	tee_client_close_context(pvt_data->ctx);
}

static const struct tee_client_device_id tpm_qcom_id_table[] = {
	{ tpm_qcom_uuid },
	{}
};
MODULE_DEVICE_TABLE(tee, tpm_qcom_id_table);

static struct tee_client_driver tpm_qcom_driver = {
	.id_table	= tpm_qcom_id_table,
	.probe		= tpm_qcom_probe,
	.remove		= tpm_qcom_remove,
	.driver		= {
		.name	= "tpm_qcom",
	},
};

module_tee_client_driver(tpm_qcom_driver);

MODULE_DESCRIPTION("TPM driver for Qualcomm TPM TA");
MODULE_AUTHOR("Kuldeep Singh <kuldeep.singh@oss.qualcomm.com>");
MODULE_LICENSE("GPL");
