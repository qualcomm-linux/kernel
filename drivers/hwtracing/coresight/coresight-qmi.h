/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _CORESIGHT_QMI_H
#define _CORESIGHT_QMI_H

#include <linux/soc/qcom/qmi.h>

#define CORESIGHT_QMI_VERSION			(1)

#define CORESIGHT_QMI_SET_ETM_REQ_V01		(0x002C)
#define CORESIGHT_QMI_SET_ETM_RESP_V01		(0x002C)

#define CORESIGHT_QMI_MAX_MSG_LEN (50)

#define TIMEOUT_MS				(10000)

/* Qmi data for the QMI connection */
struct qmi_data {
	u32			qmi_id;
	u32			service_id;
	struct list_head	node;
	struct qmi_handle	handle;
	bool			service_connected;
	struct sockaddr_qrtr	s_addr;
};

/**
 * QMI service IDs
 *
 * CORESIGHT_QMI_QDSSC_SVC_ID for remote etm
 * CORESIGHT_QMI_QDCP_SVC_ID for STM/TPDM/CTI
 */
enum coresight_qmi_service_id {
	CORESIGHT_QMI_QDSSC_SVC_ID = 0x33,
	CORESIGHT_QMI_QDCP_SVC_ID = 0xff,
};

enum coresight_qmi_instance_id {
	CORESIGHT_QMI_INSTANCE_MODEM_V01 = 2,
	CORESIGHT_QMI_INSTANCE_WLAN_V01 = 3,
	CORESIGHT_QMI_INSTANCE_AOP_V01 = 4,
	CORESIGHT_QMI_INSTANCE_ADSP_V01 = 5,
	CORESIGHT_QMI_INSTANCE_VENUS_V01 = 6,
	CORESIGHT_QMI_INSTANCE_GNSS_V01 = 7,
	CORESIGHT_QMI_INSTANCE_SENSOR_V01 = 8,
	CORESIGHT_QMI_INSTANCE_AUDIO_V01 = 9,
	CORESIGHT_QMI_INSTANCE_VPU_V01 = 10,
	CORESIGHT_QMI_INSTANCE_MODEM2_V01 = 11,
	CORESIGHT_QMI_INSTANCE_SENSOR2_V01 = 12,
	CORESIGHT_QMI_INSTANCE_CDSP_V01 = 13,
	CORESIGHT_QMI_INSTANCE_NPU_V01 = 14,
	CORESIGHT_QMI_INSTANCE_CDSP_USER_V01 = 15,
	CORESIGHT_QMI_INSTANCE_CDSP1_V01 = 16,
	CORESIGHT_QMI_INSTANCE_GPDSP0_V01 = 17,
	CORESIGHT_QMI_INSTANCE_GPDSP1_V01 = 18,
	CORESIGHT_QMI_INSTANCE_TBD_V01 = 19,
	CORESIGHT_QMI_INSTANCE_GPDSP0_AUDI0_V01 = 20,
	CORESIGHT_QMI_INSTANCE_GPDSP1_AUDI0_V01 = 21,
	CORESIGHT_QMI_INSTANCE_MODEM_OEM_V01 = 22,
	CORESIGHT_QMI_INSTANCE_ADSP1_V01 = 23,
	CORESIGHT_QMI_INSTANCE_ADSP1_AUDIO_V01 = 24,
	CORESIGHT_QMI_INSTANCE_ADSP2_V01 = 25,
	CORESIGHT_QMI_INSTANCE_ADSP2_AUDIO_V01 = 26,
	CORESIGHT_QMI_INSTANCE_CDSP2_V01 = 27,
	CORESIGHT_QMI_INSTANCE_CDSP3_V01 = 28,
	CORESIGHT_QMI_INSTANCE_SOCCP_V01 = 29,
	CORESIGHT_QMI_INSTANCE_QECP_V01 = 30,
};

enum coresight_etm_state_enum_type_v01 {
	/* To force a 32 bit signed enum. Do not change or use */
	CORESIGHT_ETM_STATE_ENUM_TYPE_MIN_ENUM_VAL_V01 = INT_MIN,
	CORESIGHT_ETM_STATE_DISABLED_V01 = 0,
	CORESIGHT_ETM_STATE_ENABLED_V01 = 1,
	CORESIGHT_ETM_STATE_ENUM_TYPE_MAX_ENUM_VAL_01 = INT_MAX,
};

/**
 * Set remote etm request message
 *
 * @state enable/disable state
 */
struct coresight_set_etm_req_msg_v01 {
	enum coresight_etm_state_enum_type_v01 state;
};

/**
 * Set remote etm response message
 */
struct coresight_set_etm_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

#if IS_ENABLED(CONFIG_CORESIGHT_QMI)
extern int coresight_send_qmi_request(int instance_id, int msg_id,
		struct qmi_elem_info *resp_ei,
		struct qmi_elem_info *req_ei, void *resp, void *req, int len);
#else

static inline int coresight_send_qmi_request(int instance_id, int msg_id,
		struct qmi_elem_info *resp_ei,
		struct qmi_elem_info *req_ei, void *resp, void *req, int len) {return NULL; }
#endif

#endif
