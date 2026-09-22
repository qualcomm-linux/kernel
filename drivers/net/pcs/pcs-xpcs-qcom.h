/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __PCS_XPCS_QCOM_H
#define __PCS_XPCS_QCOM_H

#include <linux/io.h>
#include <linux/types.h>

struct platform_device;

int xpcs_qcom_reg_read(struct platform_device *pdev, void __iomem *reg_base,
		       int dev, int reg);
int xpcs_qcom_reg_write(struct platform_device *pdev, void __iomem *reg_base,
			int dev, int reg, u16 val);

#endif /* __PCS_XPCS_QCOM_H */
