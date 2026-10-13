// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm DesignWare XPCS platform helpers
 *
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/io.h>
#include <linux/mdio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "pcs-xpcs.h"
#include "pcs-xpcs-qcom.h"

/* Qualcomm Nord direct MMIO XPCS windows. */
#define QCOM_XPCS_SR_XS_BASE		0x0000
#define QCOM_XPCS_VR_XS_BASE		0x2000
#define QCOM_XPCS_SR_PMA_BASE		0x3000
#define QCOM_XPCS_SR_MII_BASE		0x4000
#define QCOM_XPCS_VR_MII_BASE		0x5000

static int xpcs_qcom_reg_offset(int dev, int reg)
{
	switch (dev) {
	case MDIO_MMD_PCS:
		if (reg & DW_VENDOR)
			return QCOM_XPCS_VR_XS_BASE +
			       ((reg & ~DW_VENDOR) << 2);

		return QCOM_XPCS_SR_XS_BASE + (reg << 2);

	case MDIO_MMD_PMAPMD:
		return QCOM_XPCS_SR_PMA_BASE + (reg << 2);

	case MDIO_MMD_VEND2:
		if (reg >= DW_VENDOR)
			return QCOM_XPCS_VR_MII_BASE +
			       ((reg - DW_VENDOR) << 2);

		return QCOM_XPCS_SR_MII_BASE + (reg << 2);

	default:
		return -EOPNOTSUPP;
	}
}

int xpcs_qcom_reg_read(struct platform_device *pdev, void __iomem *reg_base,
		       int dev, int reg)
{
	int offset, ret;

	offset = xpcs_qcom_reg_offset(dev, reg);
	if (offset < 0)
		return offset;

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		return ret;

	ret = readl(reg_base + offset) & 0xffff;

	pm_runtime_put(&pdev->dev);

	return ret;
}

int xpcs_qcom_reg_write(struct platform_device *pdev, void __iomem *reg_base,
			int dev, int reg, u16 val)
{
	int offset, ret;

	offset = xpcs_qcom_reg_offset(dev, reg);
	if (offset < 0)
		return offset;

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		return ret;

	writel(val, reg_base + offset);

	pm_runtime_put(&pdev->dev);

	return 0;
}
