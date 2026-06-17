/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017, The Linux Foundation
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef __QCOM_COOLING_H__
#define __QCOM_COOLING_H__

#include <linux/soc/qcom/qmi.h>

#define TMD_SERVICE_ID_V01 0x18
#define TMD_SERVICE_VERS_V01 0x01

#define QMI_TMD_GET_MITIGATION_DEVICE_LIST_RESP_V01 0x0020
#define QMI_TMD_GET_MITIGATION_LEVEL_REQ_V01 0x0022
#define QMI_TMD_GET_SUPPORTED_MSGS_REQ_V01 0x001E
#define QMI_TMD_SET_MITIGATION_LEVEL_REQ_V01 0x0021
#define QMI_TMD_REGISTER_NOTIFICATION_MITIGATION_LEVEL_RESP_V01 0x0023
#define QMI_TMD_GET_SUPPORTED_MSGS_RESP_V01 0x001E
#define QMI_TMD_SET_MITIGATION_LEVEL_RESP_V01 0x0021
#define QMI_TMD_DEREGISTER_NOTIFICATION_MITIGATION_LEVEL_RESP_V01 0x0024
#define QMI_TMD_MITIGATION_LEVEL_REPORT_IND_V01 0x0025
#define QMI_TMD_GET_MITIGATION_LEVEL_RESP_V01 0x0022
#define QMI_TMD_GET_SUPPORTED_FIELDS_REQ_V01 0x001F
#define QMI_TMD_GET_MITIGATION_DEVICE_LIST_REQ_V01 0x0020
#define QMI_TMD_REGISTER_NOTIFICATION_MITIGATION_LEVEL_REQ_V01 0x0023
#define QMI_TMD_DEREGISTER_NOTIFICATION_MITIGATION_LEVEL_REQ_V01 0x0024
#define QMI_TMD_GET_SUPPORTED_FIELDS_RESP_V01 0x001F

#define QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01 32
#define QMI_TMD_MITIGATION_DEV_LIST_MAX_V01 32

struct tmd_mitigation_dev_id_type_v01 {
	char mitigation_dev_id[QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01 + 1];
};

static const struct qmi_elem_info tmd_mitigation_dev_id_type_v01_ei[] = {
	{
		.data_type = QMI_STRING,
		.elem_len = QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01 + 1,
		.elem_size = sizeof(char),
		.array_type = NO_ARRAY,
		.tlv_type = 0,
		.offset = offsetof(struct tmd_mitigation_dev_id_type_v01,
				   mitigation_dev_id),
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_mitigation_dev_list_type_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_dev_id;
	uint8_t max_mitigation_level;
};

static const struct qmi_elem_info tmd_mitigation_dev_list_type_v01_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct tmd_mitigation_dev_id_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = 0,
		.offset = offsetof(struct tmd_mitigation_dev_list_type_v01,
				   mitigation_dev_id),
		.ei_array = tmd_mitigation_dev_id_type_v01_ei,
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0,
		.offset = offsetof(struct tmd_mitigation_dev_list_type_v01,
				   max_mitigation_level),
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_get_mitigation_device_list_req_msg_v01 {
	char placeholder;
};

#define TMD_GET_MITIGATION_DEVICE_LIST_REQ_MSG_V01_MAX_MSG_LEN 0
const struct qmi_elem_info tmd_get_mitigation_device_list_req_msg_v01_ei[] = {
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_get_mitigation_device_list_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
	uint8_t mitigation_device_list_valid;
	uint32_t mitigation_device_list_len;
	struct tmd_mitigation_dev_list_type_v01
		mitigation_device_list[QMI_TMD_MITIGATION_DEV_LIST_MAX_V01];
};

#define TMD_GET_MITIGATION_DEVICE_LIST_RESP_MSG_V01_MAX_MSG_LEN 1099
static const struct qmi_elem_info tmd_get_mitigation_device_list_resp_msg_v01_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct tmd_get_mitigation_device_list_resp_msg_v01,
				   resp),
		.ei_array = qmi_response_type_v01_ei,
	},
	{
		.data_type = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct tmd_get_mitigation_device_list_resp_msg_v01,
				   mitigation_device_list_valid),
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct tmd_get_mitigation_device_list_resp_msg_v01,
				   mitigation_device_list_len),
	},
	{
		.data_type = QMI_STRUCT,
		.elem_len = QMI_TMD_MITIGATION_DEV_LIST_MAX_V01,
		.elem_size = sizeof(struct tmd_mitigation_dev_list_type_v01),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct tmd_get_mitigation_device_list_resp_msg_v01,
				   mitigation_device_list),
		.ei_array = tmd_mitigation_dev_list_type_v01_ei,
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_set_mitigation_level_req_msg_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_dev_id;
	uint8_t mitigation_level;
};

#define TMD_SET_MITIGATION_LEVEL_REQ_MSG_V01_MAX_MSG_LEN 40
static const struct qmi_elem_info tmd_set_mitigation_level_req_msg_v01_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct tmd_mitigation_dev_id_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = 0x01,
		.offset = offsetof(struct tmd_set_mitigation_level_req_msg_v01,
				   mitigation_dev_id),
		.ei_array = tmd_mitigation_dev_id_type_v01_ei,
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct tmd_set_mitigation_level_req_msg_v01,
				   mitigation_level),
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_set_mitigation_level_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

#define TMD_SET_MITIGATION_LEVEL_RESP_MSG_V01_MAX_MSG_LEN 7
static const struct qmi_elem_info tmd_set_mitigation_level_resp_msg_v01_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct tmd_set_mitigation_level_resp_msg_v01, resp),
		.ei_array = qmi_response_type_v01_ei,
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_get_mitigation_level_req_msg_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_device;
};
#define TMD_GET_MITIGATION_LEVEL_REQ_MSG_V01_MAX_MSG_LEN 36

static const struct qmi_elem_info tmd_get_mitigation_level_req_msg_v01_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct tmd_mitigation_dev_id_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = 0x01,
		.offset = offsetof(struct tmd_get_mitigation_level_req_msg_v01,
				   mitigation_device),
		.ei_array = tmd_mitigation_dev_id_type_v01_ei,
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_get_mitigation_level_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
	uint8_t current_mitigation_level_valid;
	uint8_t current_mitigation_level;
	uint8_t requested_mitigation_level_valid;
	uint8_t requested_mitigation_level;
};

#define TMD_GET_MITIGATION_LEVEL_RESP_MSG_V01_MAX_MSG_LEN 15
static const struct qmi_elem_info tmd_get_mitigation_level_resp_msg_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct tmd_get_mitigation_level_resp_msg_v01, resp),
		.ei_array = qmi_response_type_v01_ei,
	},
	{
		.data_type = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct tmd_get_mitigation_level_resp_msg_v01,
				   current_mitigation_level_valid),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct tmd_get_mitigation_level_resp_msg_v01,
				   current_mitigation_level),
	},
	{
		.data_type = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0x11,
		.offset = offsetof(struct tmd_get_mitigation_level_resp_msg_v01,
				   requested_mitigation_level_valid),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0x11,
		.offset = offsetof(struct tmd_get_mitigation_level_resp_msg_v01,
				   requested_mitigation_level),
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_register_notification_mitigation_level_req_msg_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_device;
};

#define TMD_REGISTER_NOTIFICATION_MITIGATION_LEVEL_REQ_MSG_V01_MAX_MSG_LEN 36
static const struct qmi_elem_info
	tmd_register_notification_mitigation_level_req_msg_v01_ei[] = {
		{
			.data_type = QMI_STRUCT,
			.elem_len = 1,
			.elem_size = sizeof(struct tmd_mitigation_dev_id_type_v01),
			.array_type = NO_ARRAY,
			.tlv_type = 0x01,
			.offset = offsetof(
				struct tmd_register_notification_mitigation_level_req_msg_v01,
				mitigation_device),
			.ei_array = tmd_mitigation_dev_id_type_v01_ei,
		},
		{
			.data_type = QMI_EOTI,
			.array_type = NO_ARRAY,
			.tlv_type = QMI_COMMON_TLV_TYPE,
		},
	};

struct tmd_register_notification_mitigation_level_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

#define TMD_REGISTER_NOTIFICATION_MITIGATION_LEVEL_RESP_MSG_V01_MAX_MSG_LEN 7
static const struct qmi_elem_info
	tmd_register_notification_mitigation_level_resp_msg_v01_ei[] = {
		{
			.data_type = QMI_STRUCT,
			.elem_len = 1,
			.elem_size = sizeof(struct qmi_response_type_v01),
			.array_type = NO_ARRAY,
			.tlv_type = 0x02,
			.offset = offsetof(
				struct tmd_register_notification_mitigation_level_resp_msg_v01,
				resp),
			.ei_array = qmi_response_type_v01_ei,
		},
		{
			.data_type = QMI_EOTI,
			.array_type = NO_ARRAY,
			.tlv_type = QMI_COMMON_TLV_TYPE,
		},
	};

struct tmd_deregister_notification_mitigation_level_req_msg_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_device;
};

#define TMD_DEREGISTER_NOTIFICATION_MITIGATION_LEVEL_REQ_MSG_V01_MAX_MSG_LEN 36
static const struct qmi_elem_info
	tmd_deregister_notification_mitigation_level_req_msg_v01_ei[] = {
		{
			.data_type = QMI_STRUCT,
			.elem_len = 1,
			.elem_size = sizeof(struct tmd_mitigation_dev_id_type_v01),
			.array_type = NO_ARRAY,
			.tlv_type = 0x01,
			.offset = offsetof(
				struct tmd_deregister_notification_mitigation_level_req_msg_v01,
				mitigation_device),
			.ei_array = tmd_mitigation_dev_id_type_v01_ei,
		},
		{
			.data_type = QMI_EOTI,
			.array_type = NO_ARRAY,
			.tlv_type = QMI_COMMON_TLV_TYPE,
		},
	};

struct tmd_deregister_notification_mitigation_level_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

#define TMD_DEREGISTER_NOTIFICATION_MITIGATION_LEVEL_RESP_MSG_V01_MAX_MSG_LEN 7
static const struct qmi_elem_info
	tmd_deregister_notification_mitigation_level_resp_msg_v01_ei[] = {
		{
			.data_type = QMI_STRUCT,
			.elem_len = 1,
			.elem_size = sizeof(struct qmi_response_type_v01),
			.array_type = NO_ARRAY,
			.tlv_type = 0x02,
			.offset = offsetof(
				struct tmd_deregister_notification_mitigation_level_resp_msg_v01,
				resp),
			.ei_array = qmi_response_type_v01_ei,
		},
		{
			.data_type = QMI_EOTI,
			.array_type = NO_ARRAY,
			.tlv_type = QMI_COMMON_TLV_TYPE,
		},
	};

struct tmd_mitigation_level_report_ind_msg_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_device;
	uint8_t current_mitigation_level;
};

#define TMD_MITIGATION_LEVEL_REPORT_IND_MSG_V01_MAX_MSG_LEN 40
static const struct qmi_elem_info tmd_mitigation_level_report_ind_msg_v01_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct tmd_mitigation_dev_id_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = 0x01,
		.offset = offsetof(struct tmd_mitigation_level_report_ind_msg_v01,
				   mitigation_device),
		.ei_array = tmd_mitigation_dev_id_type_v01_ei,
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct tmd_mitigation_level_report_ind_msg_v01,
				   current_mitigation_level),
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

#endif /* __QMI_COOLING_INTERNAL_H__ */
