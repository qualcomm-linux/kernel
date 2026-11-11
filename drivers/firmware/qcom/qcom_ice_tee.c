// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>
#include <linux/firmware/qcom/qcom_ice_tee.h>

#include <soc/qcom/ice.h>

#define PTA_QCOM_ICE_INVALIDATE_KEY		0
#define PTA_QCOM_ICE_SET_CONFIG_KEY		1
#define PTA_QCOM_ICE_GENERATE_KEY		2
#define PTA_QCOM_ICE_IMPORT_KEY			3
#define PTA_QCOM_ICE_EXPORT_KEY			4
#define PTA_QCOM_ICE_GET_RAW_SECRET		5
#define PTA_QCOM_ICE_HAS_WRAPPED_KEY_SUPPORT	6

#define TEE_NUM_PARAMS				4

struct qcom_ice_tee_private {
	struct device *dev;
	struct tee_context *ctx;
	u32 session_id;
};

static int qcom_ice_tee_invalidate_key(struct device *dev, u32 index)
{
	struct qcom_ice_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = PTA_QCOM_ICE_INVALIDATE_KEY,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS,
	};
	struct tee_param param[TEE_NUM_PARAMS] = {
		[0] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = index,
			.u.value.b = 0,
		}
	};
	int ret;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		ret = ret ?: -EINVAL;

	return ret;
}

static int qcom_ice_tee_set_key(struct device *dev, u32 index,
				const u8 *key, u32 key_size,
				enum qcom_ice_cipher cipher,
				u32 data_unit_size)
{
	struct qcom_ice_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = PTA_QCOM_ICE_SET_CONFIG_KEY,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS,
	};
	struct tee_param param[TEE_NUM_PARAMS] = {
		[0] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = index,
			.u.value.b = cipher,
		},
		[1] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
			.u.value.a = data_unit_size,
		},
		[2] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT,
		}
	};
	struct tee_shm *key_shm;
	u8 *key_buf;
	int ret;

	key_shm = tee_shm_alloc_kernel_buf(data->ctx, key_size);
	if (IS_ERR(key_shm))
		return PTR_ERR(key_shm);

	key_buf = tee_shm_get_va(key_shm, 0);
	if (IS_ERR(key_buf)) {
		ret = PTR_ERR(key_buf);
		goto exit;
	}

	memcpy(key_buf, key, key_size);
	param[2].u.memref.shm = key_shm;
	param[2].u.memref.size = key_size;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	memzero_explicit(key_buf, key_size);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(dev,
			"ICE set_key failed, idx=%u, ret=%d, err=0x%x\n",
			index, ret, inv_arg.ret);
		ret = ret ?: -EINVAL;
	}
exit:
	tee_shm_free(key_shm);
	return ret;
}

static int qcom_ice_tee_wrapped_key_call(struct device *dev, u32 cmd,
					 const u8 *in, size_t in_size,
					 u8 *out, size_t out_size)
{
	struct qcom_ice_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = cmd,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS,
	};
	struct tee_param param[TEE_NUM_PARAMS] = { };
	struct tee_shm *in_shm = NULL;
	struct tee_shm *out_shm = NULL;
	size_t out_buf_size;
	u8 *buf;
	int ret;

	if (in && in_size) {
		in_shm = tee_shm_alloc_kernel_buf(data->ctx, in_size);
		if (IS_ERR(in_shm))
			return PTR_ERR(in_shm);

		buf = tee_shm_get_va(in_shm, 0);
		if (IS_ERR(buf)) {
			ret = PTR_ERR(buf);
			goto exit;
		}
		memcpy(buf, in, in_size);

		param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
		param[0].u.memref.shm = in_shm;
		param[0].u.memref.size = in_size;
	}

	if (out && out_size) {
		out_shm = tee_shm_alloc_kernel_buf(data->ctx, out_size);
		if (IS_ERR(out_shm)) {
			ret = PTR_ERR(out_shm);
			goto exit;
		}

		param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
		param[1].u.memref.shm = out_shm;
		param[1].u.memref.size = out_size;
	}

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(dev,
			"ICE cmd %u failed, ret=%d, err=0x%x\n",
			cmd, ret, inv_arg.ret);
		ret = ret ?: -EINVAL;
		goto exit;
	}

	if (out && out_size) {
		out_buf_size = param[1].u.memref.size;
		if (out_size != out_buf_size) {
			ret = -EINVAL;
			goto exit;
		}

		buf = tee_shm_get_va(out_shm, 0);
		if (IS_ERR(buf)) {
			ret = PTR_ERR(buf);
			goto exit;
		}
		memcpy(out, buf, out_buf_size);
		memzero_explicit(buf, out_buf_size);
	}
exit:
	if (in_shm && !IS_ERR(in_shm)) {
		buf = tee_shm_get_va(in_shm, 0);
		if (!IS_ERR(buf))
			memzero_explicit(buf, in_size);
		tee_shm_free(in_shm);
	}
	if (out_shm && !IS_ERR(out_shm)) {
		buf = tee_shm_get_va(out_shm, 0);
		if (!IS_ERR(buf))
			memzero_explicit(buf, out_size);
		tee_shm_free(out_shm);
	}

	return ret;
}

static int qcom_ice_tee_derive_sw_secret(struct device *dev,
					 const u8 *eph_key,
					 size_t eph_key_size,
					 u8 *sw_secret,
					 size_t sw_secret_size)
{
	return qcom_ice_tee_wrapped_key_call(dev, PTA_QCOM_ICE_GET_RAW_SECRET,
					     eph_key, eph_key_size,
					     sw_secret, sw_secret_size);
}

static int qcom_ice_tee_generate_key(struct device *dev,
				     u8 *lt_key, size_t lt_key_size)
{
	struct qcom_ice_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = PTA_QCOM_ICE_GENERATE_KEY,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS,
	};
	struct tee_param param[TEE_NUM_PARAMS] = { };
	struct tee_shm *key_shm;
	u8 *buf;
	size_t out_size;
	int ret;

	key_shm = tee_shm_alloc_kernel_buf(data->ctx, lt_key_size);
	if (IS_ERR(key_shm))
		return PTR_ERR(key_shm);

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[0].u.memref.shm = key_shm;
	param[0].u.memref.shm_offs = 0;
	param[0].u.memref.size = lt_key_size;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(dev,
			"ICE generate_key failed, ret=%d, err=0x%x\n",
			ret, inv_arg.ret);
		ret = ret ?: -EINVAL;
		goto exit;
	}

	buf = tee_shm_get_va(key_shm, 0);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		goto exit;
	}

	out_size = param[0].u.memref.size;
	if (out_size != lt_key_size) {
		ret = -EINVAL;
		goto clear;
	}

	memcpy(lt_key, buf, out_size);
clear:
	memzero_explicit(buf, lt_key_size);
exit:
	tee_shm_free(key_shm);
	return ret;
}

static int qcom_ice_tee_prepare_key(struct device *dev,
				    const u8 *lt_key,
				    size_t lt_key_size,
				    u8 *eph_key,
				    size_t eph_key_size)
{
	return qcom_ice_tee_wrapped_key_call(dev, PTA_QCOM_ICE_EXPORT_KEY,
					     lt_key, lt_key_size,
					     eph_key, eph_key_size);
}

static int qcom_ice_tee_import_key(struct device *dev,
				   const u8 *raw_key,
				   size_t raw_key_size,
				   u8 *lt_key,
				   size_t lt_key_size)
{
	return qcom_ice_tee_wrapped_key_call(dev, PTA_QCOM_ICE_IMPORT_KEY,
					     raw_key, raw_key_size,
					     lt_key, lt_key_size);
}

static bool qcom_ice_tee_has_wrapped_key_support(struct device *dev)
{
	struct qcom_ice_tee_private *data = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {
		.func = PTA_QCOM_ICE_HAS_WRAPPED_KEY_SUPPORT,
		.session = data->session_id,
		.num_params = TEE_NUM_PARAMS,
	};
	struct tee_param param[TEE_NUM_PARAMS] = {
		[0] = {
			.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT,
			.u.value.a = 0,
		}
	};
	int ret;
	bool has_support = false;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0) {
		dev_err(dev,
			"ICE wrapped_key_support failed, ret=%d, err=0x%x\n",
			ret, inv_arg.ret);
		goto exit;
	}

	has_support = param[0].u.value.a;
exit:
	return has_support;
}

static struct qcom_ice_tz_ops qcom_ice_tee_ops = {
	.drv_name = "qcom-ice-tee",
	.ice_invalidate_key = qcom_ice_tee_invalidate_key,
	.ice_set_key = qcom_ice_tee_set_key,
	.ice_has_wrapped_key_support = qcom_ice_tee_has_wrapped_key_support,
	.ice_derive_sw_secret = qcom_ice_tee_derive_sw_secret,
	.ice_generate_key = qcom_ice_tee_generate_key,
	.ice_prepare_key = qcom_ice_tee_prepare_key,
	.ice_import_key = qcom_ice_tee_import_key,
};

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	return ver->impl_id == TEE_IMPL_ID_OPTEE;
}

static int qcom_ice_tee_probe(struct tee_client_device *ice_dev)
{
	struct device *dev = &ice_dev->dev;
	struct qcom_ice_tee_private *data;
	struct tee_ioctl_open_session_arg sess_arg = {
		.clnt_login = TEE_IOCTL_LOGIN_REE_KERNEL,
	};
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(data->ctx))
		return -ENODEV;

	export_uuid(sess_arg.uuid, &ice_dev->id.uuid);
	ret = tee_client_open_session(data->ctx, &sess_arg, NULL);
	if (ret < 0 || sess_arg.ret != 0) {
		dev_err(dev,
			"tee_client_open_session failed, ret=%d, err=0x%x\n",
			ret, sess_arg.ret);
		ret = ret ?: -EINVAL;
		goto err_sess;
	}

	data->session_id = sess_arg.session;
	dev_set_drvdata(dev, data);

	qcom_ice_tee_ops.dev = dev;
	ret = qcom_ice_tz_ops_register(&qcom_ice_tee_ops);
	if (ret)
		goto err_ops;

	return 0;
err_ops:
	tee_client_close_session(data->ctx, data->session_id);
err_sess:
	tee_client_close_context(data->ctx);
	return ret;
}

static void qcom_ice_tee_remove(struct tee_client_device *ice_dev)
{
	struct device *dev = &ice_dev->dev;
	struct qcom_ice_tee_private *data = dev_get_drvdata(dev);

	qcom_ice_tz_ops_unregister(&qcom_ice_tee_ops);
	tee_client_close_session(data->ctx, data->session_id);
	tee_client_close_context(data->ctx);
}

static const struct tee_client_device_id qcom_ice_tee_id_table[] = {
	{UUID_INIT(0x29e87b9e, 0x012a, 0x4878,
		   0xa1, 0xe1, 0xa1, 0xb9, 0x0a, 0x21, 0x5b, 0x16)},
	{}
};
MODULE_DEVICE_TABLE(tee, qcom_ice_tee_id_table);

static struct tee_client_driver qcom_ice_tee_driver = {
	.probe		= qcom_ice_tee_probe,
	.remove		= qcom_ice_tee_remove,
	.id_table	= qcom_ice_tee_id_table,
	.driver		= {
		.name = "qcom-ice-tee",
	},
};

module_tee_client_driver(qcom_ice_tee_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm ICE TEE driver");
