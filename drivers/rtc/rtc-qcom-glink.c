// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/auxiliary_bus.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/soc/qcom/pdr.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/timekeeping.h>
#include <linux/workqueue.h>

#define RTC_GLINK_SET_PROPERTY		0x63
#define RTC_GLINK_GET_PROPERTY		0x65
#define RTC_GLINK_GET_RTC_TICKS		0x67
#define RTC_GLINK_GET_REAL_TIME		0x68
#define RTC_GLINK_SET_REAL_TIME		0x69
#define RTC_GLINK_ALARM_EXPIRED		0x6A

enum qcom_rtc_glink_properties {
	QCOM_RTC_GLINK_TIME = 0,
	QCOM_RTC_GLINK_ALARM_TIME,
	QCOM_RTC_GLINK_ALARM_ENABLE,
};

struct qcom_rtc_glink_msg {
	struct pmic_glink_hdr	hdr;
	__le32			property;
	__le32			value;
};

struct qcom_rtc_glink_generic_req {
	struct pmic_glink_hdr	hdr;
};

struct qcom_rtc_glink_status_resp {
	struct pmic_glink_hdr	hdr;
	__le32			return_status;
};

struct qcom_rtc_glink_ticks_resp {
	struct pmic_glink_hdr	hdr;
	__le32			return_status;
	__le32			rtc_ticks;
};

struct qcom_rtc_glink_real_time_resp {
	struct pmic_glink_hdr	hdr;
	__le32			return_status;
	__le32			real_time_data[4];
};

struct qcom_rtc_glink_set_real_time_req {
	struct pmic_glink_hdr	hdr;
	__le32			real_time_data[4];
};

struct qcom_rtc_glink {
	struct device		*dev;
	struct pmic_glink_client *client;
	struct rtc_device	*rtc;

	/* Serializes requests: only one may be in flight at a time */
	struct mutex		lock;
	struct completion	ack;
	struct work_struct	alarm_work;

	/* Protects service_up and request_pending across callback contexts */
	spinlock_t		state_lock;
	bool			service_up;
	bool			request_pending;

	int			error;
	u32			resp_value;
	u32			offset;
	struct rtc_time		resp_tm;
	bool			resp_valid;
	bool			allow_set_time;
};

static int qcom_rtc_glink_request(struct qcom_rtc_glink *rtc_glink,
				  void *data, size_t len)
{
	unsigned long flags;
	unsigned long left;
	int ret;

	/*
	 * Reinit the completion before request_pending becomes visible to
	 * qcom_rtc_glink_pdr_tify(), so a concurrent SSR down-transition
	 * can't complete() a stale completion that gets wiped out by
	 * reinit_completion() right after.
	 */
	reinit_completion(&rtc_glink->ack);
	rtc_glink->error = 0;
	rtc_glink->resp_valid = false;

	spin_lock_irqsave(&rtc_glink->state_lock, flags);
	if (!rtc_glink->service_up) {
		spin_unlock_irqrestore(&rtc_glink->state_lock, flags);
		return -ECONNRESET;
	}
	rtc_glink->request_pending = true;
	spin_unlock_irqrestore(&rtc_glink->state_lock, flags);

	ret = pmic_glink_send(rtc_glink->client, data, len);
	if (ret < 0) {
		spin_lock_irqsave(&rtc_glink->state_lock, flags);
		rtc_glink->request_pending = false;
		spin_unlock_irqrestore(&rtc_glink->state_lock, flags);
		return ret;
	}

	left = wait_for_completion_timeout(&rtc_glink->ack, HZ);
	spin_lock_irqsave(&rtc_glink->state_lock, flags);
	rtc_glink->request_pending = false;
	spin_unlock_irqrestore(&rtc_glink->state_lock, flags);
	if (!left)
		return -ETIMEDOUT;

	return rtc_glink->error;
}

static int qcom_rtc_glink_set_property(struct qcom_rtc_glink *rtc_glink,
				       u32 property, u32 value)
{
	struct qcom_rtc_glink_msg msg = {
		.hdr.owner = cpu_to_le32(PMIC_GLINK_OWNER_RTC),
		.hdr.type = cpu_to_le32(PMIC_GLINK_REQ_RESP),
		.hdr.opcode = cpu_to_le32(RTC_GLINK_SET_PROPERTY),
		.property = cpu_to_le32(property),
		.value = cpu_to_le32(value),
	};
	int ret;

	dev_dbg(rtc_glink->dev,
		"TX opcode=0x%x property=%u value=%u\n",
		RTC_GLINK_SET_PROPERTY, property, value);
	mutex_lock(&rtc_glink->lock);
	ret = qcom_rtc_glink_request(rtc_glink, &msg, sizeof(msg));
	mutex_unlock(&rtc_glink->lock);
	return ret;
}

static int qcom_rtc_glink_get_property(struct qcom_rtc_glink *rtc_glink,
				       u32 property, u32 *value)
{
	struct qcom_rtc_glink_msg msg = {
		.hdr.owner = cpu_to_le32(PMIC_GLINK_OWNER_RTC),
		.hdr.type = cpu_to_le32(PMIC_GLINK_REQ_RESP),
		.hdr.opcode = cpu_to_le32(RTC_GLINK_GET_PROPERTY),
		.property = cpu_to_le32(property),
	};
	int ret;

	dev_dbg(rtc_glink->dev,
		"TX opcode=0x%x property=%u\n",
		RTC_GLINK_GET_PROPERTY, property);
	mutex_lock(&rtc_glink->lock);
	ret = qcom_rtc_glink_request(rtc_glink, &msg, sizeof(msg));
	if (!ret) {
		if (!rtc_glink->resp_valid)
			ret = -EIO;
		else
			*value = rtc_glink->resp_value;
	}
	mutex_unlock(&rtc_glink->lock);
	return ret;
}

static int qcom_rtc_glink_get_ticks(struct qcom_rtc_glink *rtc_glink,
				    u32 *ticks)
{
	struct qcom_rtc_glink_generic_req msg = {
		.hdr.owner = cpu_to_le32(PMIC_GLINK_OWNER_RTC),
		.hdr.type = cpu_to_le32(PMIC_GLINK_REQ_RESP),
		.hdr.opcode = cpu_to_le32(RTC_GLINK_GET_RTC_TICKS),
	};
	int ret;

	dev_dbg(rtc_glink->dev, "TX opcode=0x%x\n", RTC_GLINK_GET_RTC_TICKS);
	mutex_lock(&rtc_glink->lock);
	ret = qcom_rtc_glink_request(rtc_glink, &msg, sizeof(msg));
	if (!ret) {
		if (!rtc_glink->resp_valid)
			ret = -EIO;
		else
			*ticks = rtc_glink->resp_value;
	}
	mutex_unlock(&rtc_glink->lock);
	return ret;
}

static int qcom_rtc_glink_get_real_time(struct qcom_rtc_glink *rtc_glink,
					struct rtc_time *tm)
{
	struct qcom_rtc_glink_generic_req msg = {
		.hdr.owner = cpu_to_le32(PMIC_GLINK_OWNER_RTC),
		.hdr.type = cpu_to_le32(PMIC_GLINK_REQ_RESP),
		.hdr.opcode = cpu_to_le32(RTC_GLINK_GET_REAL_TIME),
	};
	int ret;

	dev_dbg(rtc_glink->dev, "TX opcode=0x%x\n", RTC_GLINK_GET_REAL_TIME);
	mutex_lock(&rtc_glink->lock);
	ret = qcom_rtc_glink_request(rtc_glink, &msg, sizeof(msg));
	if (!ret) {
		if (!rtc_glink->resp_valid)
			ret = -EIO;
		else
			*tm = rtc_glink->resp_tm;
	}
	mutex_unlock(&rtc_glink->lock);
	return ret;
}

static int qcom_rtc_glink_set_real_time(struct qcom_rtc_glink *rtc_glink,
					struct rtc_time *tm)
{
	struct qcom_rtc_glink_set_real_time_req msg = {};
	u32 w0, w1;
	int ret;

	w0 = ((tm->tm_year + 1900) & 0xffff) |
	     (((tm->tm_mon + 1) & 0xff) << 16) |
	     ((tm->tm_mday & 0xff) << 24);
	w1 = (tm->tm_hour & 0xff) |
	     ((tm->tm_min & 0xff) << 8) |
	     ((tm->tm_sec & 0xff) << 16) |
	     (1U << 24);
	msg.hdr.owner = cpu_to_le32(PMIC_GLINK_OWNER_RTC);
	msg.hdr.type = cpu_to_le32(PMIC_GLINK_REQ_RESP);
	msg.hdr.opcode = cpu_to_le32(RTC_GLINK_SET_REAL_TIME);
	msg.real_time_data[0] = cpu_to_le32(w0);
	msg.real_time_data[1] = cpu_to_le32(w1);
	msg.real_time_data[2] = 0;
	msg.real_time_data[3] = 0;
	dev_dbg(rtc_glink->dev,
		"TX opcode=0x%x data=%08x %08x\n",
		RTC_GLINK_SET_REAL_TIME, w0, w1);
	mutex_lock(&rtc_glink->lock);
	ret = qcom_rtc_glink_request(rtc_glink, &msg, sizeof(msg));
	mutex_unlock(&rtc_glink->lock);
	return ret;
}

static int qcom_rtc_glink_get_time(struct qcom_rtc_glink *rtc_glink,
				   struct rtc_time *time)
{
	u32 ticks;
	int ret;

	ret = qcom_rtc_glink_get_real_time(rtc_glink, time);
	if (!ret)
		return 0;
	dev_warn(rtc_glink->dev, "0x68 failed (%d), falling back\n", ret);

	ret = qcom_rtc_glink_get_ticks(rtc_glink, &ticks);
	if (!ret) {
		rtc_time64_to_tm((time64_t)ticks + rtc_glink->offset, time);
		return 0;
	}

	ret = qcom_rtc_glink_get_property(rtc_glink, QCOM_RTC_GLINK_TIME,
					  &ticks);
	if (!ret && ticks != U32_MAX) {
		rtc_time64_to_tm((time64_t)ticks + rtc_glink->offset, time);
		return 0;
	}

	dev_err(rtc_glink->dev, "all time sources failed\n");
	return -EIO;
}

static int qcom_rtc_glink_set_time(struct qcom_rtc_glink *rtc_glink,
				   struct rtc_time *time)
{
	time64_t t = rtc_tm_to_time64(time);
	u32 ticks;
	int ret;

	if (!rtc_glink->allow_set_time)
		return -EOPNOTSUPP;

	ret = qcom_rtc_glink_set_real_time(rtc_glink, time);
	if (ret)
		dev_warn(rtc_glink->dev, "0x69 failed (%d)\n", ret);

	ret = qcom_rtc_glink_set_property(rtc_glink, QCOM_RTC_GLINK_TIME, (u32)t);
	if (ret)
		return ret;

	ret = qcom_rtc_glink_get_ticks(rtc_glink, &ticks);
	if (!ret && ticks != U32_MAX) {
		rtc_glink->offset = (u32)t - ticks;
	} else {
		dev_warn(rtc_glink->dev, "no tick source for offset\n");
		return -EIO;
	}

	return 0;
}

static int qcom_rtc_glink_set_alarm_en(struct qcom_rtc_glink *rtc_glink,
				       int enabled)
{
	return qcom_rtc_glink_set_property(rtc_glink,
					   QCOM_RTC_GLINK_ALARM_ENABLE,
					   enabled);
}

static int qcom_rtc_glink_set_alarm(struct qcom_rtc_glink *rtc_glink,
				    struct rtc_wkalrm *alarm)
{
	time64_t alarm_t = rtc_tm_to_time64(&alarm->time);
	struct rtc_time now_tm;
	time64_t now_real, secs_until_alarm;
	u32 fw_current, fw_alarm;
	int ret;

	ret = qcom_rtc_glink_get_time(rtc_glink, &now_tm);
	if (ret)
		return ret;
	now_real = rtc_tm_to_time64(&now_tm);
	secs_until_alarm = alarm_t - now_real;
	dev_dbg(rtc_glink->dev,
		"set_alarm: now_real=%lld alarm_t=%lld delta=%lld\n",
		now_real, alarm_t, secs_until_alarm);
	if (secs_until_alarm < 0)
		return -EINVAL;
	if (secs_until_alarm > U32_MAX)
		return -ERANGE;

	ret = qcom_rtc_glink_get_property(rtc_glink, QCOM_RTC_GLINK_TIME,
					  &fw_current);
	if (ret)
		return ret;
	if (fw_current == U32_MAX)
		fw_current = 0;
	fw_alarm = fw_current + (u32)secs_until_alarm;
	dev_dbg(rtc_glink->dev,
		"set_alarm: fw_current=%u fw_alarm=%u\n",
		fw_current, fw_alarm);

	ret = qcom_rtc_glink_set_alarm_en(rtc_glink, 0);
	if (ret)
		return ret;

	ret = qcom_rtc_glink_set_property(rtc_glink,
					  QCOM_RTC_GLINK_ALARM_TIME, fw_alarm);
	if (ret)
		return ret;

	return qcom_rtc_glink_set_alarm_en(rtc_glink, alarm->enabled);
}

static int qcom_rtc_glink_get_alarm(struct qcom_rtc_glink *rtc_glink,
				    struct rtc_wkalrm *alarm)
{
	u32 fw_alarm_time, fw_current, alarm_en;
	struct rtc_time now_tm;
	time64_t now_real, secs_until_alarm, alarm_real;
	int ret;

	ret = qcom_rtc_glink_get_property(rtc_glink,
					  QCOM_RTC_GLINK_ALARM_TIME,
					  &fw_alarm_time);
	if (ret)
		return ret;

	ret = qcom_rtc_glink_get_property(rtc_glink, QCOM_RTC_GLINK_TIME,
					  &fw_current);
	if (ret)
		return ret;

	ret = qcom_rtc_glink_get_property(rtc_glink,
					  QCOM_RTC_GLINK_ALARM_ENABLE,
					  &alarm_en);
	if (ret)
		return ret;
	if (fw_current == U32_MAX)
		fw_current = 0;

	ret = qcom_rtc_glink_get_time(rtc_glink, &now_tm);
	if (ret)
		return ret;
	now_real = rtc_tm_to_time64(&now_tm);
	secs_until_alarm = (time64_t)fw_alarm_time - (time64_t)fw_current;
	alarm_real = now_real + secs_until_alarm;
	dev_dbg(rtc_glink->dev,
		"get_alarm: now_real=%lld fw_current=%u fw_alarm=%u delta=%lld\n",
		now_real, fw_current, fw_alarm_time, secs_until_alarm);
	rtc_time64_to_tm(alarm_real, &alarm->time);
	alarm->enabled = !!alarm_en;
	return 0;
}

static void qcom_rtc_glink_alarm_work(struct work_struct *work)
{
	struct qcom_rtc_glink *rtc_glink =
		container_of(work, struct qcom_rtc_glink, alarm_work);
	int ret;

	ret = qcom_rtc_glink_set_alarm_en(rtc_glink, 0);
	if (ret)
		dev_err(rtc_glink->dev,
			"failed to disable alarm after expiry (%d)\n", ret);
	rtc_update_irq(rtc_glink->rtc, 1, RTC_IRQF | RTC_AF);
}

static void qcom_rtc_glink_callback(const void *data, size_t len, void *priv)
{
	struct qcom_rtc_glink *rtc_glink = priv;
	const struct pmic_glink_hdr *hdr = data;
	unsigned long flags;
	bool pending;

	if (len < sizeof(*hdr))
		return;

	dev_dbg(rtc_glink->dev,
		"RX opcode=0x%x type=0x%x owner=0x%x len=%zu\n",
		le32_to_cpu(hdr->opcode), le32_to_cpu(hdr->type),
		le32_to_cpu(hdr->owner), len);

	if (le32_to_cpu(hdr->opcode) == RTC_GLINK_ALARM_EXPIRED) {
		dev_info(rtc_glink->dev, "alarm expired\n");
		schedule_work(&rtc_glink->alarm_work);
		return;
	}

	/*
	 * Hold state_lock across the pending check and the response fields
	 * below: qcom_rtc_glink_pdr_notify() writes the same error/resp_*
	 * fields under this lock when it force-completes a request on an
	 * SSR down-transition, and the two must not race. Dropping the
	 * response here if !pending also covers requests that already
	 * timed out or were aborted; qcom_rtc_glink_request() will call
	 * reinit_completion() again before this response could be mistaken
	 * for a later request's.
	 */
	spin_lock_irqsave(&rtc_glink->state_lock, flags);
	pending = rtc_glink->request_pending;

	if (!pending) {
		spin_unlock_irqrestore(&rtc_glink->state_lock, flags);
		dev_dbg(rtc_glink->dev,
			"dropping stale response opcode=0x%x\n",
			le32_to_cpu(hdr->opcode));
		return;
	}

	switch (le32_to_cpu(hdr->opcode)) {
	case RTC_GLINK_GET_PROPERTY:
		fallthrough;
	case RTC_GLINK_SET_PROPERTY: {
		const struct qcom_rtc_glink_msg *msg = data;

		if (len < sizeof(struct pmic_glink_hdr) + sizeof(__le32)) {
			rtc_glink->error = -EINVAL;
			break;
		}
		rtc_glink->resp_value = le32_to_cpu(msg->property);
		rtc_glink->resp_valid = true;
		rtc_glink->error = 0;
		dev_dbg(rtc_glink->dev, "ACK opcode=0x%x resp_value=%u\n",
			le32_to_cpu(hdr->opcode), rtc_glink->resp_value);
		break;
	}

	case RTC_GLINK_GET_RTC_TICKS: {
		const struct qcom_rtc_glink_ticks_resp *resp = data;

		if (len < sizeof(*resp)) {
			rtc_glink->error = -EINVAL;
			break;
		}
		if (le32_to_cpu(resp->return_status)) {
			rtc_glink->error = -EIO;
			break;
		}
		rtc_glink->resp_value = le32_to_cpu(resp->rtc_ticks);
		rtc_glink->resp_valid = true;
		rtc_glink->error = 0;
		dev_dbg(rtc_glink->dev, "ACK opcode=0x%x ticks=%u\n",
			RTC_GLINK_GET_RTC_TICKS, rtc_glink->resp_value);
		break;
	}

	case RTC_GLINK_GET_REAL_TIME: {
		const struct qcom_rtc_glink_real_time_resp *resp = data;
		u32 w0, w1;

		if (len < sizeof(*resp)) {
			rtc_glink->error = -EINVAL;
			break;
		}
		if (le32_to_cpu(resp->return_status)) {
			rtc_glink->error = -EIO;
			break;
		}
		w0 = le32_to_cpu(resp->real_time_data[0]);
		w1 = le32_to_cpu(resp->real_time_data[1]);
		rtc_glink->resp_tm.tm_year = (w0 & 0xffff) - 1900;
		rtc_glink->resp_tm.tm_mon = ((w0 >> 16) & 0xff) - 1;
		rtc_glink->resp_tm.tm_mday = (w0 >> 24) & 0xff;
		rtc_glink->resp_tm.tm_hour = w1 & 0xff;
		rtc_glink->resp_tm.tm_min = (w1 >> 8) & 0xff;
		rtc_glink->resp_tm.tm_sec = (w1 >> 16) & 0xff;
		rtc_glink->resp_valid = true;
		rtc_glink->error = 0;
		dev_dbg(rtc_glink->dev,
			"ACK opcode=0x%x %04d-%02d-%02d %02d:%02d:%02d\n",
			RTC_GLINK_GET_REAL_TIME,
			rtc_glink->resp_tm.tm_year + 1900,
			rtc_glink->resp_tm.tm_mon + 1,
			rtc_glink->resp_tm.tm_mday,
			rtc_glink->resp_tm.tm_hour,
			rtc_glink->resp_tm.tm_min,
			rtc_glink->resp_tm.tm_sec);
		break;
	}

	case RTC_GLINK_SET_REAL_TIME: {
		const struct qcom_rtc_glink_status_resp *resp = data;

		if (len < sizeof(*resp)) {
			rtc_glink->error = -EINVAL;
			break;
		}
		rtc_glink->error = le32_to_cpu(resp->return_status) ? -EIO : 0;
		dev_dbg(rtc_glink->dev, "ACK opcode=0x%x status=%u\n",
			RTC_GLINK_SET_REAL_TIME,
			le32_to_cpu(resp->return_status));
		break;
	}

	default:
		spin_unlock_irqrestore(&rtc_glink->state_lock, flags);
		dev_warn(rtc_glink->dev,
			 "unhandled RX opcode=0x%x len=%zu\n",
			 le32_to_cpu(hdr->opcode), len);
		return;
	}

	spin_unlock_irqrestore(&rtc_glink->state_lock, flags);
	complete(&rtc_glink->ack);
}

static void qcom_rtc_glink_pdr_notify(void *priv, int state)
{
	struct qcom_rtc_glink *rtc_glink = priv;
	unsigned long flags;
	bool up = (state == SERVREG_SERVICE_STATE_UP);

	spin_lock_irqsave(&rtc_glink->state_lock, flags);
	rtc_glink->service_up = up;
	if (!up && rtc_glink->request_pending) {
		rtc_glink->error = -ECONNRESET;
		rtc_glink->resp_valid = false;
		complete(&rtc_glink->ack);
	}
	spin_unlock_irqrestore(&rtc_glink->state_lock, flags);
}

static int qcom_rtc_glink_read_time(struct device *dev, struct rtc_time *tm)
{
	return qcom_rtc_glink_get_time(dev_get_drvdata(dev), tm);
}

static int qcom_rtc_glink_set_time_dev(struct device *dev, struct rtc_time *tm)
{
	return qcom_rtc_glink_set_time(dev_get_drvdata(dev), tm);
}

static int qcom_rtc_glink_read_alarm(struct device *dev,
				     struct rtc_wkalrm *alrm)
{
	return qcom_rtc_glink_get_alarm(dev_get_drvdata(dev), alrm);
}

static int qcom_rtc_glink_set_alarm_dev(struct device *dev,
					struct rtc_wkalrm *alrm)
{
	return qcom_rtc_glink_set_alarm(dev_get_drvdata(dev), alrm);
}

static int qcom_rtc_glink_alarm_irq_enable(struct device *dev,
					   unsigned int enabled)
{
	return qcom_rtc_glink_set_alarm_en(dev_get_drvdata(dev), enabled);
}

static const struct rtc_class_ops qcom_rtc_glink_rtc_ops = {
	.read_time = qcom_rtc_glink_read_time,
	.set_time = qcom_rtc_glink_set_time_dev,
	.read_alarm = qcom_rtc_glink_read_alarm,
	.set_alarm = qcom_rtc_glink_set_alarm_dev,
	.alarm_irq_enable = qcom_rtc_glink_alarm_irq_enable,
};

static const struct of_device_id qcom_rtc_glink_of_variants[] = {
	{ .compatible = "qcom,glymur-pmic-glink" },
	{}
};

static int qcom_rtc_glink_probe(struct auxiliary_device *adev,
				const struct auxiliary_device_id *id)
{
	struct device *dev = &adev->dev;
	struct qcom_rtc_glink *rtc_glink;

	if (!of_match_device(qcom_rtc_glink_of_variants, dev->parent))
		return -ENXIO;

	rtc_glink = devm_kzalloc(dev, sizeof(*rtc_glink), GFP_KERNEL);
	if (!rtc_glink)
		return -ENOMEM;

	rtc_glink->dev            = dev;
	rtc_glink->allow_set_time = device_property_read_bool(dev->parent,
							      "allow-set-time");

	mutex_init(&rtc_glink->lock);
	spin_lock_init(&rtc_glink->state_lock);
	init_completion(&rtc_glink->ack);
	INIT_WORK(&rtc_glink->alarm_work, qcom_rtc_glink_alarm_work);
	dev_set_drvdata(dev, rtc_glink);

	rtc_glink->client = devm_pmic_glink_client_alloc(dev,
							 PMIC_GLINK_OWNER_RTC,
							 qcom_rtc_glink_callback,
							 qcom_rtc_glink_pdr_notify,
							 rtc_glink);
	if (IS_ERR(rtc_glink->client))
		return dev_err_probe(dev, PTR_ERR(rtc_glink->client),
				     "failed to allocate glink client\n");

	pmic_glink_client_register(rtc_glink->client);

	device_init_wakeup(dev, true);

	rtc_glink->rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc_glink->rtc))
		return dev_err_probe(dev, PTR_ERR(rtc_glink->rtc),
				     "failed to allocate RTC device\n");

	rtc_glink->rtc->ops      = &qcom_rtc_glink_rtc_ops;
	rtc_glink->rtc->range_min = 0;
	rtc_glink->rtc->range_max = U32_MAX;

	return devm_rtc_register_device(rtc_glink->rtc);
}

static void qcom_rtc_glink_remove(struct auxiliary_device *adev)
{
	struct qcom_rtc_glink *rtc_glink = dev_get_drvdata(&adev->dev);
	/*
	 * Nothing left to arm alarm_work after this returns: the glink
	 * client (and its callback/pdr_notify) is torn down by devm after
	 * .remove() returns, and rtc_update_irq() below still targets a
	 * live devm-managed rtc device.
	 */
	cancel_work_sync(&rtc_glink->alarm_work);
}

static const struct auxiliary_device_id qcom_rtc_glink_id_table[] = {
	{ .name = "pmic_glink.rtc-glink" },
	{}
};

MODULE_DEVICE_TABLE(auxiliary, qcom_rtc_glink_id_table);

static struct auxiliary_driver qcom_rtc_glink_driver = {
	.name = "qcom_pmic_rtc_glink",
	.id_table = qcom_rtc_glink_id_table,
	.probe = qcom_rtc_glink_probe,
	.remove = qcom_rtc_glink_remove,
};

module_auxiliary_driver(qcom_rtc_glink_driver);
MODULE_DESCRIPTION("Qualcomm PMIC GLINK RTC driver");
MODULE_LICENSE("GPL");
