/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared submode contract between dwc3-qcom glue and the Qualcomm QMP
 * USB3/combo PHY driver for communicating live SuperSpeed link presence
 * at suspend-entry, via phy_set_mode_ext()'s submode argument.
 */

#ifndef _PHY_QCOM_QMP_USB_SS_H_
#define _PHY_QCOM_QMP_USB_SS_H_

enum qcom_usb_ss_state {
	QCOM_USB_SS_UNKNOWN = 0,
	QCOM_USB_SS_PRESENT,
	QCOM_USB_SS_ABSENT,
};

#endif /* _PHY_QCOM_QMP_USB_SS_H_ */
