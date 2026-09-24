/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __LINUX_USB_QCOM_EUD_H
#define __LINUX_USB_QCOM_EUD_H

#include <linux/usb/role.h>

#if IS_ENABLED(CONFIG_USB_QCOM_EUD)
void qcom_eud_usb_role_notify(struct device_node *eud_node, struct phy *phy,
			      enum usb_role role);
#else
static inline void qcom_eud_usb_role_notify(struct device_node *eud_node, struct phy *phy,
					    enum usb_role role)
{
}
#endif

#endif /* __LINUX_USB_QCOM_EUD_H */
