// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/coresight.h>

#include "coresight-qmi.h"
static LIST_HEAD(qmi_data);

static int service_coresight_qmi_new_server(struct qmi_handle *qmi,
		struct qmi_service *svc)
{
	struct qmi_data *data = container_of(qmi,
					struct qmi_data, handle);

	data->s_addr.sq_family = AF_QIPCRTR;
	data->s_addr.sq_node = svc->node;
	data->s_addr.sq_port = svc->port;
	data->service_connected = true;
	pr_debug("Connection established between QMI handle and %d service\n",
		data->qmi_id);

	return 0;
}

static void service_coresight_qmi_del_server(struct qmi_handle *qmi,
		struct qmi_service *svc)
{
	struct qmi_data *data = container_of(qmi,
					struct qmi_data, handle);
	data->service_connected = false;
	pr_debug("Connection disconnected between QMI handle and %d service\n",
		data->qmi_id);
}

static struct qmi_ops server_ops = {
	.new_server = service_coresight_qmi_new_server,
	.del_server = service_coresight_qmi_del_server,
};

static int coresight_qmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *child_node;
	int ret;

	/**
	 * Get the instance id and service id of the QMI service connection
	 * from DT node. Creates QMI handle and register new lookup for each
	 * QMI connection.
	 */
	for_each_available_child_of_node(node, child_node) {
		struct qmi_data *data;

		data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		ret = of_property_read_u32(child_node, "qmi-id", &data->qmi_id);
		if (ret)
			return ret;

		ret = of_property_read_u32(child_node, "service-id", &data->service_id);
		if (ret)
			return ret;

		ret = qmi_handle_init(&data->handle,
				CORESIGHT_QMI_MAX_MSG_LEN,
				&server_ops, NULL);
		if (ret < 0) {
			dev_err(dev, "qmi client init failed ret:%d\n", ret);
			return ret;
		}

		qmi_add_lookup(&data->handle,
				data->service_id,
				CORESIGHT_QMI_VERSION,
				data->qmi_id);

		list_add(&data->node, &qmi_data);
	}

	return 0;
}

/**
 * coresight_get_qmi_data() - Get the qmi data struct from qmi_data
 * @id:	instance id to get the qmi data
 *
 * Return: qmi data struct on success, NULL on failure.
 */
static struct qmi_data *coresight_get_qmi_data(int id)
{
	struct qmi_data *data;

	list_for_each_entry(data, &qmi_data, node) {
		if (data->qmi_id == id)
			return data;
	}

	return NULL;
}

/**
 * coresight_send_qmi_request() - Send a QMI message to remote subsystem
 * @instance_id:	QMI Instance id of the remote subsystem
 * @msg_id:	message id of the request
 * @resp_ei:	description of how to decode a matching response
 * @req_ei:	description of how to encode a matching request
 * @resp:	pointer to the object to decode the response info
 * @req:	pointer to the object to encode the request info
 * @len:	max length of the QMI message
 *
 * Return: 0 on success, negative errno on failure.
 */
int coresight_send_qmi_request(int instance_id, int msg_id, struct qmi_elem_info *resp_ei,
			struct qmi_elem_info *req_ei, void *resp, void *req, int len)
{
	struct qmi_txn txn;
	int ret;
	struct qmi_data *data;

	data = coresight_get_qmi_data(instance_id);
	if (!data) {
		pr_err("No QMI data for QMI service!\n");
		ret = -EINVAL;
		return ret;
	}

	if (!data->service_connected) {
		pr_err("QMI service not connected!\n");
		ret = -EINVAL;
		return ret;
	}

	ret = qmi_txn_init(&data->handle, &txn,
			resp_ei,
			resp);

	if (ret < 0) {
		pr_err("QMI tx init failed , ret:%d\n", ret);
		return ret;
	}

	ret = qmi_send_request(&data->handle, &data->s_addr,
			&txn, msg_id,
			len,
			req_ei,
			req);

	if (ret < 0) {
		pr_err("QMI send ACK failed, ret:%d\n", ret);
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, msecs_to_jiffies(TIMEOUT_MS));
	if (ret < 0) {
		pr_err("QMI qmi txn wait failed, ret:%d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(coresight_send_qmi_request);

static void coresight_qmi_remove(struct platform_device *pdev)
{
	struct qmi_data *data;

	list_for_each_entry(data, &qmi_data, node) {
		qmi_handle_release(&data->handle);
	}
}

static const struct of_device_id coresight_qmi_match[] = {
	{.compatible = "qcom,coresight-qmi"},
	{}
};

static struct platform_driver coresight_qmi_driver = {
	.probe          = coresight_qmi_probe,
	.remove         = coresight_qmi_remove,
	.driver         = {
		.name   = "coresight-qmi",
		.of_match_table = coresight_qmi_match,
	},
};

static int __init coresight_qmi_init(void)
{
	return platform_driver_register(&coresight_qmi_driver);
}
module_init(coresight_qmi_init);

static void __exit coresight_qmi_exit(void)
{
	platform_driver_unregister(&coresight_qmi_driver);
}
module_exit(coresight_qmi_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CoreSight QMI driver");
