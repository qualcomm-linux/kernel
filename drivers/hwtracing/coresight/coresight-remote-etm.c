// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/coresight.h>

#include "coresight-qmi.h"

#define CORESIGHT_QMI_SET_ETM_REQ_MAX_LEN	(7)

DEFINE_CORESIGHT_DEVLIST(remote_etm_devs, "remote-etm");

/**
 * struct remote_etm_drvdata - specifics associated to remote etm device
 * @dev:	the device entity associated to this component
 * @csdev:	component vitals needed by the framework
 * @mutex:	lock for seting etm
 * @inst_id:	the instance id of the remote connection
 */
struct remote_etm_drvdata {
	struct device			*dev;
	struct coresight_device		*csdev;
	struct mutex			mutex;
	u32				inst_id;
};

/*
 * Element info to descrbe the coresight_set_etm_req_msg_v01 struct
 * which is used to encode the request.
 */
static struct qmi_elem_info coresight_set_etm_req_msg_v01_ei[] = {
	{
			.data_type = QMI_UNSIGNED_4_BYTE,
			.elem_len  = 1,
			.elem_size = sizeof(enum coresight_etm_state_enum_type_v01),
			.array_type  = NO_ARRAY,
			.tlv_type  = 0x01,
			.offset    = offsetof(struct coresight_set_etm_req_msg_v01,
								state),
			.ei_array  = NULL,
	},
	{
			.data_type = QMI_EOTI,
			.elem_len  = 0,
			.elem_size = 0,
			.array_type  = NO_ARRAY,
			.tlv_type  = 0,
			.offset    = 0,
			.ei_array  = NULL,
	},
};

/*
 * Element info to describe the coresight_set_etm_resp_msg_v01 struct
 * which is used to decode the response.
 */
static struct qmi_elem_info coresight_set_etm_resp_msg_v01_ei[] = {
	{
			.data_type = QMI_STRUCT,
			.elem_len  = 1,
			.elem_size = sizeof(struct qmi_response_type_v01),
			.array_type  = NO_ARRAY,
			.tlv_type  = 0x02,
			.offset    = offsetof(struct coresight_set_etm_resp_msg_v01,
								resp),
			.ei_array  = qmi_response_type_v01_ei,
	},
	{
			.data_type = QMI_EOTI,
			.elem_len  = 0,
			.elem_size = 0,
			.array_type  = NO_ARRAY,
			.tlv_type  = 0,
			.offset    = 0,
			.ei_array  = NULL,
	},
};

static int remote_etm_enable(struct coresight_device *csdev,
			     struct perf_event *event, enum cs_mode mode,
			     __maybe_unused struct coresight_path *path)
{
	struct remote_etm_drvdata *drvdata =
		dev_get_drvdata(csdev->dev.parent);
	struct coresight_set_etm_req_msg_v01 req;
	struct coresight_set_etm_resp_msg_v01 resp = { { 0, 0 } };
	int ret = 0;

	mutex_lock(&drvdata->mutex);

	if (mode != CS_MODE_SYSFS) {
		ret = -EINVAL;
		goto err;
	}

	if (!coresight_take_mode(csdev, mode)) {
		ret = -EBUSY;
		goto err;
	}

	req.state = CORESIGHT_ETM_STATE_ENABLED_V01;

	ret = coresight_send_qmi_request(drvdata->inst_id, CORESIGHT_QMI_SET_ETM_REQ_V01,
			coresight_set_etm_resp_msg_v01_ei,
			coresight_set_etm_req_msg_v01_ei,
			&resp, &req, CORESIGHT_QMI_SET_ETM_REQ_MAX_LEN);

	if (ret)
		goto err;

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		dev_err(drvdata->dev, "QMI request failed 0x%x\n", resp.resp.error);
		ret = -EINVAL;
		goto err;
	}

	mutex_unlock(&drvdata->mutex);
	return 0;
err:
	coresight_set_mode(csdev, CS_MODE_DISABLED);
	mutex_unlock(&drvdata->mutex);
	return ret;

}

static void remote_etm_disable(struct coresight_device *csdev,
			       struct perf_event *event)
{
	struct remote_etm_drvdata *drvdata =
		 dev_get_drvdata(csdev->dev.parent);
	struct coresight_set_etm_req_msg_v01 req;
	struct coresight_set_etm_resp_msg_v01 resp = { { 0, 0 } };
	int ret = 0;

	mutex_lock(&drvdata->mutex);

	req.state = CORESIGHT_ETM_STATE_DISABLED_V01;

	ret = coresight_send_qmi_request(drvdata->inst_id, CORESIGHT_QMI_SET_ETM_REQ_V01,
					coresight_set_etm_resp_msg_v01_ei,
					coresight_set_etm_req_msg_v01_ei,
					&resp, &req, CORESIGHT_QMI_SET_ETM_REQ_MAX_LEN);
	if (ret)
		dev_err(drvdata->dev, "Send qmi request failed %d\n", ret);

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01)
		dev_err(drvdata->dev, "QMI request failed %d\n", resp.resp.error);

	coresight_set_mode(csdev, CS_MODE_DISABLED);
	mutex_unlock(&drvdata->mutex);
}

static const struct coresight_ops_source remote_etm_source_ops = {
	.enable		= remote_etm_enable,
	.disable	= remote_etm_disable,
};

static const struct coresight_ops remote_cs_ops = {
	.source_ops	= &remote_etm_source_ops,
};

static int remote_etm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct coresight_platform_data *pdata;
	struct remote_etm_drvdata *drvdata;
	struct coresight_desc desc = {0 };
	int ret;

	desc.name = coresight_alloc_device_name(&remote_etm_devs, dev);
	if (!desc.name)
		return -ENOMEM;
	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	pdev->dev.platform_data = pdata;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = dev;
	platform_set_drvdata(pdev, drvdata);

	ret = of_property_read_u32(dev->of_node, "qcom,qmi-id",
			&drvdata->inst_id);
	if (ret)
		return ret;

	mutex_init(&drvdata->mutex);

	desc.type = CORESIGHT_DEV_TYPE_SOURCE;
	desc.subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_OTHERS;
	desc.ops = &remote_cs_ops;
	desc.pdata = pdev->dev.platform_data;
	desc.dev = &pdev->dev;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev)) {
		ret = PTR_ERR(drvdata->csdev);
		goto err;
	}

	dev_dbg(dev, "Remote ETM initialized\n");

	return 0;

err:
	return ret;
}

static void remote_etm_remove(struct platform_device *pdev)
{
	struct remote_etm_drvdata *drvdata = platform_get_drvdata(pdev);

	coresight_unregister(drvdata->csdev);
}

static const struct of_device_id remote_etm_match[] = {
	{.compatible = "qcom,coresight-remote-etm"},
	{}
};

static struct platform_driver remote_etm_driver = {
	.probe          = remote_etm_probe,
	.remove         = remote_etm_remove,
	.driver         = {
		.name   = "coresight-remote-etm",
		.of_match_table = remote_etm_match,
	},
};

static int __init remote_etm_init(void)
{
	return platform_driver_register(&remote_etm_driver);
}
module_init(remote_etm_init);

static void __exit remote_etm_exit(void)
{
	platform_driver_unregister(&remote_etm_driver);
}
module_exit(remote_etm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CoreSight Remote ETM driver");
