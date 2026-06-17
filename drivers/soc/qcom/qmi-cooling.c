// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017, The Linux Foundation
 * Copyright (c) 2025, Linaro Limited
 *
 * QMI Thermal Mitigation Device (TMD) client driver.
 * This driver provides an in-kernel client to handle hot and cold thermal
 * mitigations for remote subsystems (modem and DSPs) running the TMD service.
 * It doesn't implement any handling of reports from remote subsystems.
 */

#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/remoteproc_cooling.h>
#include <linux/slab.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/thermal.h>

#include "qmi-cooling.h"

#define MODEM0_INSTANCE_ID	0x0
#define ADSP_INSTANCE_ID	0x1
#define CDSP_INSTANCE_ID	0x43
#define SLPI_INSTANCE_ID	0x53

#define QMI_TMD_RESP_TIMEOUT msecs_to_jiffies(100)

/**
 * struct qmi_tmd_client - TMD client state
 * @dev:	Device associated with this client
 * @name:	Friendly name for the remote TMD service
 * @handle:	QMI connection handle
 * @mutex:	Lock to synchronise QMI communication
 * @id:		The QMI TMD service instance ID
 * @cdev_list:	The list of cooling devices (controls) enabled for this instance
 * @svc_arrive_work: Work item for initialising the client when the TMD service
 *		     starts.
 * @connection_active: Whether or not we're connected to the QMI TMD service
 */
struct qmi_tmd_client {
	struct device *dev;
	const char *name;
	struct qmi_handle handle;
	struct mutex mutex;
	u32 id;
	struct list_head cdev_list;
	struct work_struct svc_arrive_work;
	bool connection_active;
};

/**
 * struct qmi_tmd - A TMD cooling device
 * @np:		OF node associated with this control
 * @type:	The control type (exposed via sysfs)
 * @qmi_name:	The common name of this control shared by the remote subsystem
 * @rproc_cdev:	Remote processor cooling device handle
 * @cur_state:	The current cooling/warming/mitigation state
 * @max_state:	The maximum state
 * @client:	The TMD client instance this control is associated with
 */
struct qmi_tmd {
	struct device_node *np;
	const char *type;
	char qmi_name[QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01 + 1];
	struct list_head node;
	struct remoteproc_cdev *rproc_cdev;
	unsigned int cur_state;
	unsigned int max_state;
	struct qmi_tmd_client *client;
};

/**
 * struct qmi_instance_id - QMI instance match data
 * @id:		The QMI instance ID
 * @name:	Friendly name for this instance
 */
struct qmi_instance_data {
	u32 id;
	const char *name;
};

/* Notify the remote subsystem of the requested cooling state */
static int qmi_tmd_send_state_request(struct qmi_tmd *tmd)
{
	struct tmd_set_mitigation_level_resp_msg_v01 tmd_resp = { 0 };
	struct tmd_set_mitigation_level_req_msg_v01 req = { 0 };
	struct qmi_tmd_client *client;
	struct qmi_txn txn;
	int ret = 0;

	client = tmd->client;

	guard(mutex)(&client->mutex);

	/*
	 * This function is called by qmi_set_cur_state() which does not know if
	 * the QMI service is actually online. If it isn't then we noop here.
	 * The state is cached in tmd->cur_state and will be broadcast via
	 * qmi_tmd_init_control() when the service comes up.
	 */
	if (!client->connection_active)
		return 0;

	strscpy(req.mitigation_dev_id.mitigation_dev_id, tmd->qmi_name,
		QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01 + 1);
	req.mitigation_level = tmd->cur_state;

	ret = qmi_txn_init(&client->handle, &txn,
			   tmd_set_mitigation_level_resp_msg_v01_ei, &tmd_resp);
	if (ret < 0) {
		dev_err(client->dev, "qmi set state %d txn init failed for %s ret %d\n",
			tmd->cur_state, tmd->type, ret);
		return ret;
	}

	ret = qmi_send_request(&client->handle, NULL, &txn,
			       QMI_TMD_SET_MITIGATION_LEVEL_REQ_V01,
			       TMD_SET_MITIGATION_LEVEL_REQ_MSG_V01_MAX_MSG_LEN,
			       tmd_set_mitigation_level_req_msg_v01_ei, &req);
	if (ret < 0) {
		dev_err(client->dev, "qmi set state %d txn send failed for %s ret %d\n",
			tmd->cur_state, tmd->type, ret);
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, QMI_TMD_RESP_TIMEOUT);
	if (ret < 0) {
		dev_err(client->dev, "qmi set state %d txn wait failed for %s ret %d\n",
			tmd->cur_state, tmd->type, ret);
		return ret;
	}

	if (tmd_resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ret = -tmd_resp.resp.result;
		dev_err(client->dev, "qmi set state %d NOT success for %s ret %d\n",
			tmd->cur_state, tmd->type, ret);
		return ret;
	}

	dev_dbg(client->dev, "Requested state %d/%d for %s\n", tmd->cur_state,
		tmd->max_state, tmd->type);

	return 0;
}

static int qmi_get_max_level(void *devdata, unsigned long *level)
{
	struct qmi_tmd *tmd = devdata;

	if (!tmd)
		return -EINVAL;

	*level = tmd->max_state;

	return 0;
}

static int qmi_get_cur_level(void *devdata, unsigned long *level)
{
	struct qmi_tmd *tmd = devdata;

	if (!tmd)
		return -EINVAL;

	*level = tmd->cur_state;

	return 0;
}

static int qmi_set_cur_level(void *devdata, unsigned long level)
{
	struct qmi_tmd *tmd = devdata;

	if (!tmd)
		return -EINVAL;

	if (level > tmd->max_state)
		return -EINVAL;

	if (tmd->cur_state == level)
		return 0;

	tmd->cur_state = level;

	return qmi_tmd_send_state_request(tmd);
}

static const struct remoteproc_cooling_ops qmi_rproc_ops = {
	.get_max_level = qmi_get_max_level,
	.get_cur_level = qmi_get_cur_level,
	.set_cur_level = qmi_set_cur_level,
};

static int qmi_register_cooling_device(struct qmi_tmd *tmd)
{
	struct remoteproc_cdev *rproc_cdev;

	rproc_cdev = remoteproc_cooling_register(tmd->np,
						 tmd->type,
						 &qmi_rproc_ops,
						 tmd);

	if (IS_ERR(rproc_cdev))
		return dev_err_probe(tmd->client->dev, PTR_ERR(rproc_cdev),
				     "Failed to register cooling device %s\n",
				     tmd->qmi_name);

	tmd->rproc_cdev = rproc_cdev;
	return 0;
}

/*
 * Init a single TMD control by registering a cooling device for it, or
 * synchronising state with the remote subsystem if recovering from a service
 * restart. This is called when the TMD service starts up.
 */
static int qmi_tmd_init_control(struct qmi_tmd_client *client, const char *label,
				u8 max_state)
{
	struct qmi_tmd *tmd = NULL;

	list_for_each_entry(tmd, &client->cdev_list, node)
		if (!strncasecmp(tmd->qmi_name, label,
				 QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01 + 1))
			goto found;

	dev_dbg(client->dev,
		"TMD '%s' available in firmware but not specified in DT\n",
		label);
	return 0;

found:
	tmd->max_state = max_state;
	/*
	 * If the cooling device already exists then the QMI service went away and
	 * came back. So just make sure the current cooling device state is
	 * reflected on the remote side and then return.
	 */
	if (tmd->rproc_cdev)
		return qmi_tmd_send_state_request(tmd);

	return qmi_register_cooling_device(tmd);
}

/*
 * When the QMI service starts up on a remote subsystem this function will fetch
 * the list of TMDs on the subsystem, match it to the TMDs specified in devicetree
 * and call qmi_tmd_init_control() for each
 */
static void qmi_tmd_svc_arrive(struct work_struct *work)
{
	struct qmi_tmd_client *client =
		container_of(work, struct qmi_tmd_client, svc_arrive_work);

	struct tmd_get_mitigation_device_list_req_msg_v01 req = { 0 };
	struct tmd_get_mitigation_device_list_resp_msg_v01 *resp __free(kfree);
	int ret = 0, i;
	struct qmi_txn txn;

	/* resp struct is 1.1kB, allocate it on the heap. */
	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp)
		return;

	/* Get a list of TMDs supported by the remoteproc */
	scoped_guard(mutex, &client->mutex) {
		ret = qmi_txn_init(&client->handle, &txn,
				tmd_get_mitigation_device_list_resp_msg_v01_ei, resp);
		if (ret < 0) {
			dev_err(client->dev,
				"Transaction init error for instance_id: %#x ret %d\n",
				client->id, ret);
			return;
		}

		ret = qmi_send_request(&client->handle, NULL, &txn,
				QMI_TMD_GET_MITIGATION_DEVICE_LIST_REQ_V01,
				TMD_GET_MITIGATION_DEVICE_LIST_REQ_MSG_V01_MAX_MSG_LEN,
				tmd_get_mitigation_device_list_req_msg_v01_ei, &req);
		if (ret < 0) {
			qmi_txn_cancel(&txn);
			return;
		}

		ret = qmi_txn_wait(&txn, QMI_TMD_RESP_TIMEOUT);
		if (ret < 0) {
			dev_err(client->dev, "Transaction wait error for client %#x ret:%d\n",
				client->id, ret);
			return;
		}
		if (resp->resp.result != QMI_RESULT_SUCCESS_V01) {
			ret = resp->resp.result;
			dev_err(client->dev, "Failed to get device list for client %#x ret:%d\n",
				client->id, ret);
			return;
		}

		client->connection_active = true;
	}

	for (i = 0; i < resp->mitigation_device_list_len; i++) {
		struct tmd_mitigation_dev_list_type_v01 *device =
			&resp->mitigation_device_list[i];

		ret = qmi_tmd_init_control(client,
					   device->mitigation_dev_id.mitigation_dev_id,
					   device->max_mitigation_level);
		if (ret)
			break;
	}
}

static void thermal_qmi_net_reset(struct qmi_handle *qmi)
{
	struct qmi_tmd_client *client = container_of(qmi, struct qmi_tmd_client, handle);
	struct qmi_tmd *tmd = NULL;

	list_for_each_entry(tmd, &client->cdev_list, node) {
		qmi_tmd_send_state_request(tmd);
	}
}

static void thermal_qmi_del_server(struct qmi_handle *qmi, struct qmi_service *service)
{
	struct qmi_tmd_client *client = container_of(qmi, struct qmi_tmd_client, handle);

	scoped_guard(mutex, &client->mutex)
		client->connection_active = false;
}

static int thermal_qmi_new_server(struct qmi_handle *qmi, struct qmi_service *service)
{
	struct qmi_tmd_client *client = container_of(qmi, struct qmi_tmd_client, handle);
	struct sockaddr_qrtr sq = { AF_QIPCRTR, service->node, service->port };

	scoped_guard(mutex, &client->mutex)
		kernel_connect(qmi->sock, (struct sockaddr_unsized *)&sq, sizeof(sq), 0);

	queue_work(system_highpri_wq, &client->svc_arrive_work);

	return 0;
}

static struct qmi_ops thermal_qmi_event_ops = {
	.new_server = thermal_qmi_new_server,
	.del_server = thermal_qmi_del_server,
	.net_reset = thermal_qmi_net_reset,
};

static void qmi_tmd_cleanup(struct qmi_tmd_client *client)
{
	struct qmi_tmd *tmd, *c_next;

	guard(mutex)(&client->mutex);

	client->connection_active = false;

	qmi_handle_release(&client->handle);
	cancel_work(&client->svc_arrive_work);
	list_for_each_entry_safe(tmd, c_next, &client->cdev_list, node) {
		if (tmd->rproc_cdev)
			remoteproc_cooling_unregister(tmd->rproc_cdev);

		list_del(&tmd->node);
	}
}

/* Parse the controls and allocate a qmi_tmd for each of them */
static int qmi_tmd_alloc_cdevs(struct qmi_tmd_client *client)
{
	struct device *dev = client->dev;
	struct qmi_tmd *tmd;
	struct device_node *subnode, *node = dev->of_node;
	int ret;

	for_each_available_child_of_node(node, subnode) {
		const char *name;

		tmd = devm_kzalloc(dev, sizeof(*tmd), GFP_KERNEL);
		if (!tmd)
			return dev_err_probe(client->dev, -ENOMEM,
					     "Couldn't allocate tmd\n");

		tmd->type = devm_kasprintf(client->dev, GFP_KERNEL, "%s:%s",
						client->name, subnode->name);
		if (!tmd->type)
			return dev_err_probe(dev, -ENOMEM,
					     "Couldn't allocate cooling device name\n");

		if (of_property_read_string(subnode, "label", &name)) {
			return dev_err_probe(client->dev, -EINVAL,
					     "Failed to parse dev name for %s\n",
					     subnode->name);
		}

		ret = strscpy(tmd->qmi_name, name,
			      QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01 + 1);
		if (ret == -E2BIG) {
			return dev_err_probe(dev, -EINVAL, "TMD label %s is too long\n",
					     name);
		}

		tmd->client = client;
		tmd->np = subnode;
		tmd->cur_state = 0;
		list_add(&tmd->node, &client->cdev_list);
	}

	if (list_empty(&client->cdev_list))
		return dev_err_probe(client->dev, -EINVAL,
				     "No cooling devices specified for client %s (%#x)\n",
				     client->name, client->id);

	return 0;
}

static int qmi_tmd_client_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qmi_tmd_client *client;
	const struct qmi_instance_data *match;
	int ret;
	client = devm_kzalloc(dev, sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	client->dev = dev;

	match = of_device_get_match_data(dev);
	if (!match)
		return dev_err_probe(dev, -EINVAL, "No match data\n");

	client->id = match->id;
	client->name = match->name;

	mutex_init(&client->mutex);
	INIT_LIST_HEAD(&client->cdev_list);
	INIT_WORK(&client->svc_arrive_work, qmi_tmd_svc_arrive);

	ret = qmi_tmd_alloc_cdevs(client);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, client);

	ret = qmi_handle_init(&client->handle,
			      TMD_GET_MITIGATION_DEVICE_LIST_RESP_MSG_V01_MAX_MSG_LEN,
			      &thermal_qmi_event_ops, NULL);
	if (ret < 0)
		return dev_err_probe(client->dev, ret, "QMI handle init failed for client %#x\n",
			      client->id);

	ret = qmi_add_lookup(&client->handle, TMD_SERVICE_ID_V01, TMD_SERVICE_VERS_V01,
			     client->id);
	if (ret < 0) {
		qmi_handle_release(&client->handle);
		return dev_err_probe(client->dev, ret, "QMI register failed for client 0x%x\n",
			      client->id);
	}

	return 0;
}

static void qmi_tmd_client_remove(struct platform_device *pdev)
{
	struct qmi_tmd_client *client = platform_get_drvdata(pdev);

	qmi_tmd_cleanup(client);
}

static const struct of_device_id qmi_tmd_device_table[] = {
	{
		.compatible = "qcom,qmi-cooling-cdsp",
		.data = &((struct qmi_instance_data) { CDSP_INSTANCE_ID, "cdsp" }),
	},
	{}
};
MODULE_DEVICE_TABLE(of, qmi_tmd_device_table);

static struct platform_driver qmi_tmd_device_driver = {
	.probe = qmi_tmd_client_probe,
	.remove = qmi_tmd_client_remove,
	.driver = {
		.name = "qcom-qmi-cooling",
		.of_match_table = qmi_tmd_device_table,
	},
};

module_platform_driver(qmi_tmd_device_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm QMI Thermal Mitigation Device driver");
