/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/firmware/qcom/memory_dump.h>
#include <linux/soc/qcom/smem.h>

#define DEV_NAME "msm_mem_dump"

static struct platform_device *mem_dump_pdev;

enum dump_ids {
	C0_CONTEXT	= 0x0,
	C100_CONTEXT	= 0x1,
	C200_CONTEXT	= 0x2,
	C300_CONTEXT	= 0x3,
	C400_CONTEXT	= 0x4,
	C500_CONTEXT	= 0x5,
	C600_CONTEXT	= 0x6,
	C700_CONTEXT	= 0x7,
	L1_ITLB10000	= 0x24,
	L1_ITLB10100	= 0x25,
	L1_ITLB10200	= 0x26,
	L1_ITLB10300	= 0x27,
	L1_DTLB10000	= 0x44,
	L1_DTLB10100	= 0x45,
	L1_DTLB10200	= 0x46,
	L1_DTLB10300	= 0x47,
	L1_ICACHE0	= 0x60,
	L1_ICACHE100	= 0x61,
	L1_ICACHE200	= 0x62,
	L1_ICACHE300	= 0x63,
	L1_ICACHE10000	= 0x64,
	L1_ICACHE10100	= 0x65,
	L1_ICACHE10200	= 0x66,
	L1_ICACHE10300	= 0x67,
	L1_DCACHE0	= 0x80,
	L1_DCACHE100	= 0x81,
	L1_DCACHE200	= 0x82,
	L1_DCACHE300	= 0x83,
	L1_DCACHE10000	= 0x84,
	L1_DCACHE10100	= 0x85,
	L1_DCACHE10200	= 0x86,
	L1_DCACHE10300	= 0x87,
	L2_CACHE10000	= 0xc4,
	L2_CACHE10100	= 0xc5,
	L2_CACHE10200	= 0xc6,
	L2_CACHE10300	= 0xc7,
	PMIC		= 0xe4,
	MISC_DATA	= 0xe8,
	RPM_SW		= 0xea,
	RPMH		= 0xec,
	CPUSS_REG	= 0xef,
	TMC_ETF		= 0xf0,
	ETF_SWAO	= 0xf1,
	ETF_LPASS	= 0xf4,
	FCM		= 0xee,
	ETR_REG		= 0x100,
	ETF_REG		= 0x101,
	ETFSWAO_REG	= 0x102,
	ETFLPASS_REG	= 0x104,
	L2_TLB0		= 0x120,
	L2_TLB100	= 0x121,
	L2_TLB200	= 0x122,
	L2_TLB300	= 0x123,
	L2_TLB10000	= 0x124,
	L2_TLB10100	= 0x125,
	L2_TLB10200	= 0x126,
	L2_TLB10300	= 0x127,
	C0_SCANDUMP	= 0x130,
	C100_SCANDUMP	= 0x131,
	C200_SCANDUMP	= 0x132,
	C300_SCANDUMP	= 0x133,
	C10000_SCANDUMP	= 0x134,
	C10100_SCANDUMP	= 0x135,
	C10200_SCANDUMP	= 0x136,
	C10300_SCANDUMP	= 0x137,
	LLCC1_D_CACHE	= 0x140,
	LLCC2_D_CACHE	= 0x141,
	MHM_SCAN	= 0x161,
	GEMNOC		= 0x162,
	OSM_REG		= 0x163,
	PCU_REG		= 0x164,
	FSM_DATA	= 0x165,
};

static const struct dump_item lemans_items[] = {
	{ C0_CONTEXT, 0x800, "c0-context" },
	{ C0_SCANDUMP, 0x40000, "c0-scandump" },
	{ C100_CONTEXT, 0x800, "c100-context" },
	{ C100_SCANDUMP, 0x40000, "c100-scandump" },
	{ C200_CONTEXT, 0x800, "c200-context" },
	{ C200_SCANDUMP, 0x40000, "c200-scandump" },
	{ C300_CONTEXT, 0x800, "c300-context" },
	{ C300_SCANDUMP, 0x40000, "c300-scandump" },
	{ C400_CONTEXT, 0x800, "c400-context" },
	{ C500_CONTEXT, 0x800, "c500-context" },
	{ C600_CONTEXT, 0x800, "c600-context" },
	{ C700_CONTEXT, 0x800, "c700-context" },
	{ C10000_SCANDUMP, 0x40000, "c10000-scandump" },
	{ C10100_SCANDUMP, 0x40000, "c10100-scandump" },
	{ C10200_SCANDUMP, 0x40000, "c10200-scandump" },
	{ C10300_SCANDUMP, 0x40000, "c10300-scandump" },
	{ CPUSS_REG, 0x20000, "cpuss-reg" },
	{ ETF_SWAO, 0x10000, "etf-swao" },
	{ ETFSWAO_REG, 0x1000, "etfswao-reg" },
	{ ETR_REG, 0x1000, "etr-reg" },
	{ FCM, 0x8400, "fcm" },
	{ L1_DCACHE0, 0x12100, "l1-dcache0" },
	{ L1_DCACHE100, 0x12100, "l1-dcache100" },
	{ L1_DCACHE200, 0x12100, "l1-dcache200" },
	{ L1_DCACHE300, 0x12100, "l1-dcache300" },
	{ L1_DCACHE10000, 0x12100, "l1-dcache10000" },
	{ L1_DCACHE10100, 0x12100, "l1-dcache10100" },
	{ L1_DCACHE10200, 0x12100, "l1-dcache10200" },
	{ L1_DCACHE10300, 0x12100, "l1-dcache10300" },
	{ L1_DTLB10000, 0x300, "l1-dtlb10000" },
	{ L1_DTLB10100, 0x300, "l1-dtlb10100" },
	{ L1_DTLB10200, 0x300, "l1-dtlb10200" },
	{ L1_DTLB10300, 0x300, "l1-dtlb10300" },
	{ L1_ICACHE0, 0x26100, "l1-icache0" },
	{ L1_ICACHE100, 0x26100, "l1-icache100" },
	{ L1_ICACHE200, 0x26100, "l1-icache200" },
	{ L1_ICACHE300, 0x26100, "l1-icache300" },
	{ L1_ICACHE10000, 0x26100, "l1-icache10000" },
	{ L1_ICACHE10100, 0x26100, "l1-icache10100" },
	{ L1_ICACHE10200, 0x26100, "l1-icache10200" },
	{ L1_ICACHE10300, 0x26100, "l1-icache10300" },
	{ L1_ITLB10000, 0x300, "l1-itlb10000" },
	{ L1_ITLB10100, 0x300, "l1-itlb10100" },
	{ L1_ITLB10200, 0x300, "l1-itlb10200" },
	{ L1_ITLB10300, 0x300, "l1-itlb10300" },
	{ L2_CACHE10000, 0x90100, "l2-cache10000" },
	{ L2_CACHE10100, 0x90100, "l2-cache10100" },
	{ L2_CACHE10200, 0x90100, "l2-cache10200" },
	{ L2_CACHE10300, 0x90100, "l2-cache10300" },
	{ L2_TLB0, 0x6100, "l2-tlb0" },
	{ L2_TLB100, 0x6100, "l2-tlb100" },
	{ L2_TLB200, 0x6100, "l2-tlb200" },
	{ L2_TLB300, 0x6100, "l2-tlb300" },
	{ L2_TLB10000, 0x6100, "l2-tlb10000" },
	{ L2_TLB10100, 0x6100, "l2-tlb10100" },
	{ L2_TLB10200, 0x6100, "l2-tlb10200" },
	{ L2_TLB10300, 0x6100, "l2-tlb10300" },
	{ MISC_DATA, 0x1000, "misc-data" },
	{ PMIC, 0x80000, "pmic" },
	{ RPM_SW, 0x28000, "rpm-sw" },
	{ RPMH, 0x2000000, "rpmh" },
};

static const struct dump_item talos_items[] = {
	{ C0_CONTEXT, 0x800, "c0-context" },
	{ C100_CONTEXT, 0x800, "c100-context" },
	{ C200_CONTEXT, 0x800, "c200-context" },
	{ C300_CONTEXT, 0x800, "c300-context" },
	{ C400_CONTEXT, 0x800, "c400-context" },
	{ C500_CONTEXT, 0x800, "c500-context" },
	{ C600_CONTEXT, 0x800, "c600-context" },
	{ C700_CONTEXT, 0x800, "c700-context" },
	{ RPMH, 0x2000000, "rpmh" },
	{ RPM_SW, 0x28000, "rpm-sw" },
	{ PMIC, 0x10000, "pmic" },
	{ FCM, 0x8400, "fcm" },
	{ TMC_ETF, 0x8000, "tmc-etf" },
	{ ETF_SWAO, 0x8000, "etf-swao" },
	{ ETR_REG, 0x1000, "etr-reg" },
	{ ETF_REG, 0x1000, "etf-reg" },
	{ ETFSWAO_REG, 0x1000, "etfswao-reg" },
	{ MISC_DATA, 0x1000, "misc-data" },
	{ L1_ICACHE0, 0x8800, "l1-icache0" },
	{ L1_ICACHE100, 0x8800, "l1-icache100" },
	{ L1_ICACHE200, 0x8800, "l1-icache200" },
	{ L1_ICACHE300, 0x8800, "l1-icache300" },
	{ L1_ICACHE10000, 0x8800, "l1-icache400" },
	{ L1_ICACHE10100, 0x8800, "l1-icache500" },
	{ L1_ICACHE10200, 0x11000, "l1-icache600" },
	{ L1_ICACHE10300, 0x11000, "l1-icache700" },
	{ L1_DCACHE0, 0x9000, "l1-dcache0" },
	{ L1_DCACHE100, 0x9000, "l1-dcache100" },
	{ L1_DCACHE200, 0x9000, "l1-dcache200" },
	{ L1_DCACHE300, 0x9000, "l1-dcache300" },
	{ L1_DCACHE10000, 0x9000, "l1-dcache400" },
	{ L1_DCACHE10100, 0x9000, "l1-dcache500" },
	{ L1_DCACHE10200, 0x12000, "l1-dcache600" },
	{ L1_DCACHE10300, 0x12000, "l1-dcache700" },
	{ L1_ITLB10200, 0x300, "l1-itlb600" },
	{ L1_ITLB10300, 0x300, "l1-itlb700" },
	{ L1_DTLB10200, 0x480, "l1-dtlb600" },
	{ L1_DTLB10300, 0x480, "l1-dtlb700" },
	{ L2_CACHE10200, 0x48000, "l2-cache600" },
	{ L2_CACHE10300, 0x48000, "l2-cache700" },
	{ L2_TLB0, 0x5000, "l2-tlb0" },
	{ L2_TLB100, 0x5000, "l2-tlb100" },
	{ L2_TLB200, 0x5000, "l2-tlb200" },
	{ L2_TLB300, 0x5000, "l2-tlb300" },
	{ L2_TLB10000, 0x5000, "l2-tlb400" },
	{ L2_TLB10100, 0x5000, "l2-tlb500" },
	{ L2_TLB10200, 0x7800, "l2-tlb600" },
	{ L2_TLB10300, 0x7800, "l2-tlb700" },
	{ LLCC1_D_CACHE, 0x6c000, "llcc1-d-cache" },
};

static const struct dump_item kodiak_items[] = {
	{ C0_CONTEXT, 0x800, "c0-context" },
	{ C0_SCANDUMP, 0x10100, "c0-scandump" },
	{ C100_CONTEXT, 0x800, "c100-context" },
	{ C100_SCANDUMP, 0x10100, "c100-scandump" },
	{ C200_CONTEXT, 0x800, "c200-context" },
	{ C200_SCANDUMP, 0x10100, "c200-scandump" },
	{ C300_CONTEXT, 0x800, "c300-context" },
	{ C300_SCANDUMP, 0x10100, "c300-scandump" },
	{ C400_CONTEXT, 0x800, "c400-context" },
	{ C10000_SCANDUMP, 0x40000, "c400-scandump" },
	{ C500_CONTEXT, 0x800, "c500-context" },
	{ C10100_SCANDUMP, 0x40000, "c500-scandump" },
	{ C600_CONTEXT, 0x800, "c600-context" },
	{ C10200_SCANDUMP, 0x40000, "c600-scandump" },
	{ C700_CONTEXT, 0x800, "c700-context" },
	{ C10300_SCANDUMP, 0x40000, "c700-scandump" },
	{ CPUSS_REG, 0x30000, "cpuss-reg" },
	{ ETF_LPASS, 0x4000, "etf-lpass" },
	{ ETFLPASS_REG, 0x1000, "etflpass-reg" },
	{ ETF_SWAO, 0x10000, "etf-swao" },
	{ ETFSWAO_REG, 0x1000, "etfswao-reg" },
	{ ETR_REG, 0x1000, "etr-reg" },
	{ FCM, 0x8400, "fcm" },
	{ FSM_DATA, 0x400, "fsm-data" },
	{ GEMNOC, 0x100000, "gemnoc" },
	{ L1_DCACHE0, 0x9100, "l1-dcache0" },
	{ L1_DCACHE100, 0x9100, "l1-dcache100" },
	{ L1_DCACHE200, 0x9100, "l1-dcache200" },
	{ L1_DCACHE300, 0x9100, "l1-dcache300" },
	{ L1_DCACHE10000, 0x9100, "l1-dcache400" },
	{ L1_DCACHE10100, 0x9100, "l1-dcache500" },
	{ L1_DCACHE10200, 0x9100, "l1-dcache600" },
	{ L1_DCACHE10300, 0x12100, "l1-dcache700" },
	{ L1_DTLB10000, 0x300, "l1-dtlb400" },
	{ L1_DTLB10100, 0x300, "l1-dtlb500" },
	{ L1_DTLB10200, 0x300, "l1-dtlb600" },
	{ L1_DTLB10300, 0x3a0, "l1-dtlb700" },
	{ L1_ICACHE0, 0x10900, "l1-icache0" },
	{ L1_ICACHE100, 0x10900, "l1-icache100" },
	{ L1_ICACHE200, 0x10900, "l1-icache200" },
	{ L1_ICACHE300, 0x10900, "l1-icache300" },
	{ L1_ICACHE10000, 0x15100, "l1-icache400" },
	{ L1_ICACHE10100, 0x15100, "l1-icache500" },
	{ L1_ICACHE10200, 0x15100, "l1-icache600" },
	{ L1_ICACHE10300, 0x32100, "l1-icache700" },
	{ L1_ITLB10000, 0x300, "l1-itlb400" },
	{ L1_ITLB10100, 0x300, "l1-itlb500" },
	{ L1_ITLB10200, 0x300, "l1-itlb600" },
	{ L1_ITLB10300, 0x400, "l1-itlb700" },
	{ L2_CACHE10000, 0x90100, "l2-cache400" },
	{ L2_CACHE10100, 0x90100, "l2-cache500" },
	{ L2_CACHE10200, 0x90100, "l2-cache600" },
	{ L2_CACHE10300, 0x120100, "l2-cache700" },
	{ L2_TLB0, 0x5b00, "l2-tlb0" },
	{ L2_TLB100, 0x5b00, "l2-tlb100" },
	{ L2_TLB200, 0x5b00, "l2-tlb200" },
	{ L2_TLB300, 0x5b00, "l2-tlb300" },
	{ L2_TLB10000, 0x6100, "l2-tlb400" },
	{ L2_TLB10100, 0x6100, "l2-tlb500" },
	{ L2_TLB10200, 0x6100, "l2-tlb600" },
	{ L2_TLB10300, 0xc100, "l2-tlb700" },
	{ LLCC1_D_CACHE, 0x1141c0, "llcc1-d-cache" },
	{ LLCC2_D_CACHE, 0x1141c0, "llcc2-d-cache" },
	{ MHM_SCAN, 0x20000, "mhm-scan" },
	{ MISC_DATA, 0x1000, "misc-data" },
	{ OSM_REG, 0x400, "osm-reg" },
	{ PCU_REG, 0x400, "pcu-reg" },
	{ PMIC, 0x200000, "pmic" },
	{ RPM_SW, 0x28000, "rpm-sw" },
	{ RPMH, 0x2000000, "rpmh" },
};

static const struct dump_table lemans_dump_table = {
	.items		= lemans_items,
	.num_of_items	= ARRAY_SIZE(lemans_items),
	.imem_base	= 0x146d8010,
	.imem_size	= 0x8,
};

static const struct dump_table talos_dump_table = {
	.items		= talos_items,
	.num_of_items	= ARRAY_SIZE(lemans_items),
	.imem_base	= 0x146aa010,
	.imem_size	= 0x8,
};

static const struct dump_table kodiak_dump_table = {
	.items		= kodiak_items,
	.num_of_items	= ARRAY_SIZE(kodiak_items),
	.imem_base	= 0x146aa010,
	.imem_size	= 0x8,
};

static int __init mem_dump_dev_init(void)
{
	int ret;
	u32 soc_id;

	mem_dump_pdev = platform_device_alloc(DEV_NAME, -1);
	if (!mem_dump_pdev)
		return -ENOMEM;

	ret = qcom_smem_get_soc_id(&soc_id);
	if (ret)
		goto fail;

	switch (soc_id) {
	case 377:
	case 380:
	case 384:
	case 401:
	case 406:
	case 680:
		ret = platform_device_add_data(mem_dump_pdev,
				&talos_dump_table, sizeof(talos_dump_table));
		if (ret)
			goto fail;

		break;
	case 534:
	case 606:
	case 667:
	case 674:
	case 675:
	case 676:
		ret = platform_device_add_data(mem_dump_pdev,
				&lemans_dump_table, sizeof(lemans_dump_table));
		if (ret)
			goto fail;

		break;
	case 475:
	case 497:
	case 498:
	case 515:
		ret = platform_device_add_data(mem_dump_pdev,
				&kodiak_dump_table, sizeof(kodiak_dump_table));
		if (ret)
			goto fail;

		break;
	default:
		dev_err(&mem_dump_pdev->dev, "Invalid SoC ID\n");
		ret = -EINVAL;
		goto fail;
	}

	ret = platform_device_add(mem_dump_pdev);
	if (ret)
		goto fail;

	dev_info(&mem_dump_pdev->dev, "Register platform device successfully\n");

	return 0;

fail:
	dev_err(&mem_dump_pdev->dev,
		"Failed to register memory dump platform device\n");
	platform_device_put(mem_dump_pdev);

	return ret;
}

static void __exit mem_dump_dev_exit(void)
{
	platform_device_unregister(mem_dump_pdev);
}

module_init(mem_dump_dev_init);
module_exit(mem_dump_dev_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies Inc. Memory Dump driver V2, device stub");
