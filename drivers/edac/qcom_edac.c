// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/soc/qcom/llcc-qcom.h>

#include "edac_mc.h"
#include "edac_device.h"

#define EDAC_LLCC                       "qcom_llcc"

#define LLCC_ERP_PANIC_ON_UE            1

#define TRP_SYN_REG_CNT                 6
#define DRP_SYN_REG_CNT                 8

#define LLCC_LB_CNT_MASK                GENMASK(31, 28)
#define LLCC_LB_CNT_SHIFT               28

/* Mask and shift macros */
#define ECC_DB_ERR_COUNT_MASK           GENMASK(4, 0)
#define ECC_DB_ERR_WAYS_MASK            GENMASK(31, 16)
#define ECC_DB_ERR_WAYS_SHIFT           BIT(4)

#define ECC_SB_ERR_COUNT_MASK           GENMASK(23, 16)
#define ECC_SB_ERR_COUNT_SHIFT          BIT(4)
#define ECC_SB_ERR_WAYS_MASK            GENMASK(15, 0)

#define SB_ECC_ERROR                    BIT(0)
#define DB_ECC_ERROR                    BIT(1)

#define DRP_TRP_INT_CLEAR               GENMASK(1, 0)
#define DRP_TRP_CNT_CLEAR               GENMASK(1, 0)

#define SB_ERROR_THRESHOLD              0x1
#define SB_ERROR_THRESHOLD_SHIFT        24
#define SB_DB_TRP_INTERRUPT_ENABLE      0x3
#define TRP0_INTERRUPT_ENABLE           0x1
#define DRP0_INTERRUPT_ENABLE           BIT(6)
#define SRP_INTERRUPT_ENABLE            BIT(7)
#define SB_DB_DRP_INTERRUPT_ENABLE      0x3

/* LCP/SRP DDR DRAM ECC */
#define LCP_SB_ECC_ERROR                BIT(0)
#define LCP_DB_ECC_ERROR                BIT(1)
#define LCP_SRP_SB_DB_INTERRUPT_ENABLE  0x3
#define LCP_DB_ERR_COUNT_MASK           GENMASK(4, 0)
#define LCP_SB_ERR_COUNT_MASK           GENMASK(23, 16)

#define ECC_POLL_MSEC			5000

enum {
	LLCC_DRAM_CE = 0,
	LLCC_DRAM_UE,
	LLCC_TRAM_CE,
	LLCC_TRAM_UE,
};

static const struct llcc_edac_reg_data edac_reg_data[] = {
	[LLCC_DRAM_CE] = {
		.name = "DRAM Single-bit",
		.reg_cnt = DRP_SYN_REG_CNT,
		.count_mask = ECC_SB_ERR_COUNT_MASK,
		.ways_mask = ECC_SB_ERR_WAYS_MASK,
		.count_shift = ECC_SB_ERR_COUNT_SHIFT,
	},
	[LLCC_DRAM_UE] = {
		.name = "DRAM Double-bit",
		.reg_cnt = DRP_SYN_REG_CNT,
		.count_mask = ECC_DB_ERR_COUNT_MASK,
		.ways_mask = ECC_DB_ERR_WAYS_MASK,
		.ways_shift = ECC_DB_ERR_WAYS_SHIFT,
	},
	[LLCC_TRAM_CE] = {
		.name = "TRAM Single-bit",
		.reg_cnt = TRP_SYN_REG_CNT,
		.count_mask = ECC_SB_ERR_COUNT_MASK,
		.ways_mask = ECC_SB_ERR_WAYS_MASK,
		.count_shift = ECC_SB_ERR_COUNT_SHIFT,
	},
	[LLCC_TRAM_UE] = {
		.name = "TRAM Double-bit",
		.reg_cnt = TRP_SYN_REG_CNT,
		.count_mask = ECC_DB_ERR_COUNT_MASK,
		.ways_mask = ECC_DB_ERR_WAYS_MASK,
		.ways_shift = ECC_DB_ERR_WAYS_SHIFT,
	},
};

static int qcom_llcc_core_setup(struct llcc_drv_data *drv, struct regmap *llcc_bcast_regmap)
{
	u32 sb_err_threshold;
	int ret;

	/*
	 * Configure interrupt enable registers such that Tag, Data RAM related
	 * interrupts are propagated to interrupt controller for servicing
	 */
	ret = regmap_update_bits(llcc_bcast_regmap, drv->edac_reg_offset->cmn_interrupt_0_enable,
				 TRP0_INTERRUPT_ENABLE,
				 TRP0_INTERRUPT_ENABLE);
	if (ret)
		return ret;

	ret = regmap_update_bits(llcc_bcast_regmap, drv->edac_reg_offset->trp_interrupt_0_enable,
				 SB_DB_TRP_INTERRUPT_ENABLE,
				 SB_DB_TRP_INTERRUPT_ENABLE);
	if (ret)
		return ret;

	sb_err_threshold = (SB_ERROR_THRESHOLD << SB_ERROR_THRESHOLD_SHIFT);
	ret = regmap_write(llcc_bcast_regmap, drv->edac_reg_offset->drp_ecc_error_cfg,
			   sb_err_threshold);
	if (ret)
		return ret;

	ret = regmap_update_bits(llcc_bcast_regmap, drv->edac_reg_offset->cmn_interrupt_0_enable,
				 DRP0_INTERRUPT_ENABLE,
				 DRP0_INTERRUPT_ENABLE);
	if (ret)
		return ret;

	ret = regmap_write(llcc_bcast_regmap, drv->edac_reg_offset->drp_interrupt_enable,
			   SB_DB_DRP_INTERRUPT_ENABLE);
	return ret;
}

/*
 * Enable LCP/SRP DDR DRAM ECC interrupt generation and route events to
 * ddrss_apps_interrupt[1] via cmn_interrupt_1_enable.  Only called when
 * !ecc_irq_configured; on platforms like Shikra where firmware pre-configures
 * both TRP/DRP and LCP/SRP routing, the driver skips this and only requests
 * the IRQ handler.
 */
static int qcom_llcc_lcp_setup(struct llcc_drv_data *drv,
			       struct regmap *llcc_bcast_regmap)
{
	int ret;

	ret = regmap_set_bits(llcc_bcast_regmap,
			      drv->edac_reg_offset->cmn_interrupt_1_enable,
			      SRP_INTERRUPT_ENABLE);
	if (ret)
		return ret;

	return regmap_set_bits(llcc_bcast_regmap,
			       drv->edac_reg_offset->lcp_srp_interrupt_enable,
			       LCP_SRP_SB_DB_INTERRUPT_ENABLE);
}

/* Clear the error interrupt and counter registers */
static int
qcom_llcc_clear_error_status(int err_type, struct llcc_drv_data *drv)
{
	int ret;

	switch (err_type) {
	case LLCC_DRAM_CE:
	case LLCC_DRAM_UE:
		ret = regmap_write(drv->bcast_regmap,
				   drv->edac_reg_offset->drp_interrupt_clear,
				   DRP_TRP_INT_CLEAR);
		if (ret)
			return ret;

		ret = regmap_write(drv->bcast_regmap,
				   drv->edac_reg_offset->drp_ecc_error_cntr_clear,
				   DRP_TRP_CNT_CLEAR);
		if (ret)
			return ret;
		break;
	case LLCC_TRAM_CE:
	case LLCC_TRAM_UE:
		ret = regmap_write(drv->bcast_regmap,
				   drv->edac_reg_offset->trp_interrupt_0_clear,
				   DRP_TRP_INT_CLEAR);
		if (ret)
			return ret;

		ret = regmap_write(drv->bcast_regmap,
				   drv->edac_reg_offset->trp_ecc_error_cntr_clear,
				   DRP_TRP_CNT_CLEAR);
		if (ret)
			return ret;
		break;
	default:
		ret = -EINVAL;
		edac_printk(KERN_CRIT, EDAC_LLCC, "Unexpected error type: %d\n",
			    err_type);
	}
	return ret;
}

struct qcom_llcc_syn_regs {
	u32 synd_reg;
	u32 count_status_reg;
	u32 ways_status_reg;
};

static void get_reg_offsets(struct llcc_drv_data *drv, int err_type,
			    struct qcom_llcc_syn_regs *syn_regs)
{
	const struct llcc_edac_reg_offset *edac_reg_offset = drv->edac_reg_offset;

	switch (err_type) {
	case LLCC_DRAM_CE:
		syn_regs->synd_reg = edac_reg_offset->drp_ecc_sb_err_syn0;
		syn_regs->count_status_reg = edac_reg_offset->drp_ecc_error_status1;
		syn_regs->ways_status_reg = edac_reg_offset->drp_ecc_error_status0;
		break;
	case LLCC_DRAM_UE:
		syn_regs->synd_reg = edac_reg_offset->drp_ecc_db_err_syn0;
		syn_regs->count_status_reg = edac_reg_offset->drp_ecc_error_status1;
		syn_regs->ways_status_reg = edac_reg_offset->drp_ecc_error_status0;
		break;
	case LLCC_TRAM_CE:
		syn_regs->synd_reg = edac_reg_offset->trp_ecc_sb_err_syn0;
		syn_regs->count_status_reg = edac_reg_offset->trp_ecc_error_status1;
		syn_regs->ways_status_reg = edac_reg_offset->trp_ecc_error_status0;
		break;
	case LLCC_TRAM_UE:
		syn_regs->synd_reg = edac_reg_offset->trp_ecc_db_err_syn0;
		syn_regs->count_status_reg = edac_reg_offset->trp_ecc_error_status1;
		syn_regs->ways_status_reg = edac_reg_offset->trp_ecc_error_status0;
		break;
	}
}

/* Dump Syndrome registers data for Tag RAM, Data RAM bit errors*/
static int
dump_syn_reg_values(struct llcc_drv_data *drv, u32 bank, int err_type)
{
	struct llcc_edac_reg_data reg_data = edac_reg_data[err_type];
	struct qcom_llcc_syn_regs regs = { };
	int err_cnt, err_ways, ret, i;
	u32 synd_reg, synd_val;

	get_reg_offsets(drv, err_type, &regs);

	for (i = 0; i < reg_data.reg_cnt; i++) {
		synd_reg = regs.synd_reg + (i * 4);
		ret = regmap_read(drv->regmaps[bank], synd_reg,
				  &synd_val);
		if (ret)
			goto clear;

		edac_printk(KERN_CRIT, EDAC_LLCC, "%s: ECC_SYN%d: 0x%8x\n",
			    reg_data.name, i, synd_val);
	}

	ret = regmap_read(drv->regmaps[bank], regs.count_status_reg,
			  &err_cnt);
	if (ret)
		goto clear;

	err_cnt &= reg_data.count_mask;
	err_cnt >>= reg_data.count_shift;
	edac_printk(KERN_CRIT, EDAC_LLCC, "%s: Error count: 0x%4x\n",
		    reg_data.name, err_cnt);

	ret = regmap_read(drv->regmaps[bank], regs.ways_status_reg,
			  &err_ways);
	if (ret)
		goto clear;

	err_ways &= reg_data.ways_mask;
	err_ways >>= reg_data.ways_shift;

	edac_printk(KERN_CRIT, EDAC_LLCC, "%s: Error ways: 0x%4x\n",
		    reg_data.name, err_ways);

clear:
	return qcom_llcc_clear_error_status(err_type, drv);
}

static int
dump_syn_reg(struct edac_device_ctl_info *edev_ctl, int err_type, u32 bank)
{
	struct llcc_drv_data *drv = edev_ctl->dev->platform_data;
	int ret;

	ret = dump_syn_reg_values(drv, bank, err_type);
	if (ret)
		return ret;

	switch (err_type) {
	case LLCC_DRAM_CE:
		edac_device_handle_ce(edev_ctl, 0, bank,
				      "LLCC Data RAM correctable Error");
		break;
	case LLCC_DRAM_UE:
		edac_device_handle_ue(edev_ctl, 0, bank,
				      "LLCC Data RAM uncorrectable Error");
		break;
	case LLCC_TRAM_CE:
		edac_device_handle_ce(edev_ctl, 0, bank,
				      "LLCC Tag RAM correctable Error");
		break;
	case LLCC_TRAM_UE:
		edac_device_handle_ue(edev_ctl, 0, bank,
				      "LLCC Tag RAM uncorrectable Error");
		break;
	default:
		ret = -EINVAL;
		edac_printk(KERN_CRIT, EDAC_LLCC, "Unexpected error type: %d\n",
			    err_type);
	}

	return ret;
}

static irqreturn_t llcc_ecc_irq_handler(int irq, void *edev_ctl)
{
	struct edac_device_ctl_info *edac_dev_ctl = edev_ctl;
	struct llcc_drv_data *drv = edac_dev_ctl->dev->platform_data;
	irqreturn_t irq_rc = IRQ_NONE;
	u32 drp_error, trp_error, i;
	int ret;

	/* Iterate over the banks and look for Tag RAM or Data RAM errors */
	for (i = 0; i < drv->num_banks; i++) {
		ret = regmap_read(drv->regmaps[i], drv->edac_reg_offset->drp_interrupt_status,
				  &drp_error);

		if (!ret && (drp_error & SB_ECC_ERROR)) {
			edac_printk(KERN_CRIT, EDAC_LLCC,
				    "Single Bit Error detected in Data RAM\n");
			ret = dump_syn_reg(edev_ctl, LLCC_DRAM_CE, i);
		} else if (!ret && (drp_error & DB_ECC_ERROR)) {
			edac_printk(KERN_CRIT, EDAC_LLCC,
				    "Double Bit Error detected in Data RAM\n");
			ret = dump_syn_reg(edev_ctl, LLCC_DRAM_UE, i);
		}
		if (!ret)
			irq_rc = IRQ_HANDLED;

		ret = regmap_read(drv->regmaps[i], drv->edac_reg_offset->trp_interrupt_0_status,
				  &trp_error);

		if (!ret && (trp_error & SB_ECC_ERROR)) {
			edac_printk(KERN_CRIT, EDAC_LLCC,
				    "Single Bit Error detected in Tag RAM\n");
			ret = dump_syn_reg(edev_ctl, LLCC_TRAM_CE, i);
		} else if (!ret && (trp_error & DB_ECC_ERROR)) {
			edac_printk(KERN_CRIT, EDAC_LLCC,
				    "Double Bit Error detected in Tag RAM\n");
			ret = dump_syn_reg(edev_ctl, LLCC_TRAM_UE, i);
		}
		if (!ret)
			irq_rc = IRQ_HANDLED;
	}

	return irq_rc;
}

static void llcc_ecc_check(struct edac_device_ctl_info *edev_ctl)
{
	llcc_ecc_irq_handler(0, edev_ctl);
}

/*
 * Handler for the optional LCP/SRP DDR DRAM ECC interrupt
 * (ddrss_apps_interrupt[1]).  The LCP/SRP registers live in each LLCC
 * instance's address space, so iterate over all banks and read status
 * per-instance.  Broadcast-write the clear after the full loop, mirroring
 * the TRP/DRP handler pattern.
 */
static irqreturn_t llcc_lcp_ecc_irq_handler(int irq, void *edev_ctl)
{
	struct edac_device_ctl_info *edac_dev_ctl = edev_ctl;
	struct llcc_drv_data *drv = edac_dev_ctl->dev->platform_data;
	const struct llcc_edac_reg_offset *reg = drv->edac_reg_offset;
	irqreturn_t irq_rc = IRQ_NONE;
	u32 intr_status, err_status, clr_reg;
	int i, ret;

	/* Iterate over banks: LCP/SRP status registers are per-LLCC instance */
	for (i = 0; i < drv->num_banks; i++) {
		ret = regmap_read(drv->regmaps[i], reg->lcp_srp_interrupt_status,
				  &intr_status);
		if (ret)
			continue;

		intr_status &= LCP_SB_ECC_ERROR | LCP_DB_ECC_ERROR;
		if (!intr_status)
			continue;

		irq_rc = IRQ_HANDLED;

		ret = regmap_read(drv->regmaps[i], reg->lcp_srp_ecc_error_status1,
				  &err_status);
		if (ret) {
			edac_printk(KERN_ERR, EDAC_LLCC,
				    "LCP/SRP error status read failed for bank%u: %d\n",
				    i, ret);
			continue;
		}

		if (intr_status & LCP_DB_ECC_ERROR) {
			u32 db_cnt = FIELD_GET(LCP_DB_ERR_COUNT_MASK, err_status);

			edac_printk(KERN_CRIT, EDAC_LLCC,
				    "LCP/SRP DDR DRAM Double-bit ECC error in bank%u: count=%u status=0x%08x\n",
				    i, db_cnt, err_status);
			edac_device_handle_ue(edac_dev_ctl, 0, i,
					      "LCP/SRP DDR DRAM uncorrectable error");
		}

		if (intr_status & LCP_SB_ECC_ERROR) {
			u32 sb_cnt = FIELD_GET(LCP_SB_ERR_COUNT_MASK, err_status);

			edac_printk(KERN_WARNING, EDAC_LLCC,
				    "LCP/SRP DDR DRAM Single-bit ECC error in bank%u: count=%u status=0x%08x\n",
				    i, sb_cnt, err_status);
			edac_device_handle_ce(edac_dev_ctl, 0, i,
					      "LCP/SRP DDR DRAM correctable error");
		}
	}

	if (irq_rc != IRQ_HANDLED)
		return irq_rc;

	clr_reg = reg->lcp_srp_interrupt_clear ?
			reg->lcp_srp_interrupt_clear :
			reg->lcp_srp_interrupt_status;

	if (clr_reg) {
		ret = regmap_write(drv->bcast_regmap, clr_reg,
				   LCP_SB_ECC_ERROR | LCP_DB_ECC_ERROR);
		if (ret)
			edac_printk(KERN_ERR, EDAC_LLCC,
				    "LCP/SRP interrupt clear failed: %d\n", ret);
	}

	if (reg->lcp_srp_ecc_error_cntr_clear) {
		ret = regmap_write(drv->bcast_regmap,
				   reg->lcp_srp_ecc_error_cntr_clear,
				   DRP_TRP_CNT_CLEAR);
		if (ret)
			edac_printk(KERN_ERR, EDAC_LLCC,
				    "LCP/SRP counter clear failed: %d\n", ret);
	}

	return irq_rc;
}

static int qcom_llcc_edac_probe(struct platform_device *pdev)
{
	struct llcc_drv_data *llcc_driv_data = pdev->dev.platform_data;
	struct edac_device_ctl_info *edev_ctl;
	struct device *dev = &pdev->dev;
	int ecc_irq;
	int rc;

	if (!llcc_driv_data->ecc_irq_configured) {
		rc = qcom_llcc_core_setup(llcc_driv_data, llcc_driv_data->bcast_regmap);
		if (rc)
			return rc;
	}

	/* Allocate edac control info */
	edev_ctl = edac_device_alloc_ctl_info(0, "qcom-llcc", 1, "bank",
					      llcc_driv_data->num_banks, 1,
					      edac_device_alloc_index());

	if (!edev_ctl)
		return -ENOMEM;

	edev_ctl->dev = dev;
	edev_ctl->mod_name = dev_name(dev);
	edev_ctl->dev_name = dev_name(dev);
	edev_ctl->ctl_name = "llcc";
	edev_ctl->panic_on_ue = LLCC_ERP_PANIC_ON_UE;

	/* Check if LLCC driver has passed ECC IRQ */
	ecc_irq = llcc_driv_data->ecc_irq;
	if (ecc_irq > 0) {
		/* Use interrupt mode if IRQ is available */
		rc = devm_request_irq(dev, ecc_irq, llcc_ecc_irq_handler,
			      IRQF_TRIGGER_HIGH, "llcc_ecc", edev_ctl);
		if (!rc) {
			edac_op_state = EDAC_OPSTATE_INT;
			goto irq_done;
		}
		dev_warn(dev, "failed to request LLCC ECC irq %d: %d, falling back to polling\n",
			 ecc_irq, rc);
	}

	/* Fall back to polling mode otherwise */
	edev_ctl->poll_msec = ECC_POLL_MSEC;
	edev_ctl->edac_check = llcc_ecc_check;
	edac_op_state = EDAC_OPSTATE_POLL;

irq_done:
	rc = edac_device_add_device(edev_ctl);
	if (rc) {
		edac_device_free_ctl_info(edev_ctl);
		return rc;
	}

	/*
	 * Set up LCP/SRP DDR DRAM ECC after the EDAC device is published.
	 * Skip hardware routing setup if firmware already configured IRQ
	 * propagation (irq_configured); only request the IRQ handler.
	 * Guard on a non-zero lcp_srp_interrupt_enable offset to confirm
	 * this LLCC variant actually has an LCP/SRP block.
	 */
	if (llcc_driv_data->lcp_irq > 0 &&
	    llcc_driv_data->edac_reg_offset->lcp_srp_interrupt_enable) {
		if (!llcc_driv_data->ecc_irq_configured) {
			rc = qcom_llcc_lcp_setup(llcc_driv_data,
						 llcc_driv_data->bcast_regmap);
			if (rc) {
				dev_warn(dev, "LCP/SRP interrupt setup failed: %d\n", rc);
				goto lcp_done;
			}
		}
		rc = devm_request_irq(dev, llcc_driv_data->lcp_irq,
				      llcc_lcp_ecc_irq_handler,
				      0, "llcc_lcp_ecc", edev_ctl);
		if (rc)
			dev_warn(dev,
				 "failed to request LCP ECC irq %d: %d, DDR DRAM ECC reporting disabled\n",
				 llcc_driv_data->lcp_irq, rc);
	}
lcp_done:
	rc = 0;
	platform_set_drvdata(pdev, edev_ctl);

	return rc;
}

static void qcom_llcc_edac_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edev_ctl = dev_get_drvdata(&pdev->dev);

	edac_device_del_device(edev_ctl->dev);
	edac_device_free_ctl_info(edev_ctl);
}

static const struct platform_device_id qcom_llcc_edac_id_table[] = {
	{ .name = "qcom_llcc_edac" },
	{}
};
MODULE_DEVICE_TABLE(platform, qcom_llcc_edac_id_table);

static struct platform_driver qcom_llcc_edac_driver = {
	.probe = qcom_llcc_edac_probe,
	.remove = qcom_llcc_edac_remove,
	.driver = {
		.name = "qcom_llcc_edac",
	},
	.id_table = qcom_llcc_edac_id_table,
};
module_platform_driver(qcom_llcc_edac_driver);

MODULE_DESCRIPTION("QCOM EDAC driver");
MODULE_LICENSE("GPL v2");
