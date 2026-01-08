// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/smem.h>
#include "qcom-dcc.h"

#define DEV_NAME "qcom-dcc"

static struct platform_device *dcc_pdev;

static const struct dcc_pdata talos_pdata = {
	.base		= 0x010a2000,
	.size		= 0x00001000,
	.ram_base	= 0x010ae000,
	.ram_size	= 0x00002000,
	.dcc_offset	= 0x6000,
	.map_ver	= 0x1,
};

static const struct dcc_pdata lemans_pdata = {
	.base		= 0x040ff000,
	.size		= 0x00001000,
	.ram_base	= 0x040b8800,
	.ram_size	= 0x00006000,
	.dcc_offset	= 0x38800,
	.map_ver	= 0x3,
};

static const struct dcc_pdata kodiak_pdata = {
	.base		= 0x0117f000,
	.size		= 0x00001000,
	.ram_base	= 0x01112000,
	.ram_size	= 0x00006000,
	.dcc_offset	= 0x12000,
	.map_ver	= 0x2,
};

static int __init dcc_dev_init(void)
{
	int ret;
	u32 soc_id;

	dcc_pdev = platform_device_alloc(DEV_NAME, -1);
	if (!dcc_pdev)
		return -ENOMEM;

	ret = qcom_smem_get_soc_id(&soc_id);
	if (ret)
		goto fail;

	switch (soc_id) {
	case 475:
	case 497:
	case 498:
	case 515:
		ret = platform_device_add_data(dcc_pdev, &kodiak_pdata, sizeof(kodiak_pdata));
		if (ret)
			goto fail;

		break;
	case 534:
	case 606:
	case 667:
	case 674:
	case 675:
	case 676:
		ret = platform_device_add_data(dcc_pdev, &lemans_pdata, sizeof(lemans_pdata));
		if (ret)
			goto fail;

		break;
	case 377:
	case 380:
	case 384:
	case 401:
	case 406:
	case 680:
		ret = platform_device_add_data(dcc_pdev, &talos_pdata, sizeof(talos_pdata));
		if (ret)
			goto fail;

		break;
	default:
		pr_err("DCC: Invalid SoC ID\n");
		ret = -EINVAL;
		goto fail;
	}

	ret = platform_device_add(dcc_pdev);
	if (ret)
		goto fail;

	pr_info("DCC platform device has registered\n");

	return 0;

fail:
	pr_err("Failed to register DCC platform device\n");
	platform_device_put(dcc_pdev);

	return ret;
}

static void __exit dcc_dev_exit(void)
{
	platform_device_unregister(dcc_pdev);
}

module_init(dcc_dev_init);
module_exit(dcc_dev_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies Inc. DCC driver, device stub");
