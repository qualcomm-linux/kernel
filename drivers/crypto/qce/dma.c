// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/dma/qcom_bam_dma.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <crypto/scatterwalk.h>

#include "core.h"
#include "dma.h"

#define QCE_IGNORE_BUF_SZ		(2 * QCE_BAM_BURST_SIZE)
#define QCE_BAM_CMD_SGL_SIZE		128
#define QCE_BAM_CMD_ELEMENT_SIZE	128
#define QCE_MAX_REG_READ		8

struct qce_desc_info {
	struct dma_async_tx_descriptor *dma_desc;
	enum dma_data_direction dir;
};

struct qce_bam_transaction {
	struct bam_cmd_element bam_ce[QCE_BAM_CMD_ELEMENT_SIZE];
	struct scatterlist wr_sgl[QCE_BAM_CMD_SGL_SIZE];
	struct qce_desc_info *desc;
	u32 bam_ce_idx;
	u32 pre_bam_ce_idx;
	u32 wr_sgl_cnt;
};

void qce_clear_bam_transaction(struct qce_device *qce)
{
	struct qce_bam_transaction *bam_txn = qce->dma.bam_txn;

	bam_txn->bam_ce_idx = 0;
	bam_txn->wr_sgl_cnt = 0;
	bam_txn->bam_ce_idx = 0;
	bam_txn->pre_bam_ce_idx = 0;
}

static int qce_do_submit_cmd_desc(struct qce_device *qce, unsigned long flags)
{
	struct qce_desc_info *qce_desc = qce->dma.bam_txn->desc;
	struct qce_bam_transaction *bam_txn = qce->dma.bam_txn;
	struct dma_async_tx_descriptor *dma_desc;
	struct dma_chan *chan = qce->dma.rxchan;
	unsigned long attrs = DMA_PREP_CMD | flags;
	dma_cookie_t cookie;
	unsigned int mapped;
	int ret;

	mapped = dma_map_sg_attrs(qce->dev, bam_txn->wr_sgl, bam_txn->wr_sgl_cnt,
				  DMA_TO_DEVICE, attrs);
	if (!mapped)
		return -ENOMEM;

	dma_desc = dmaengine_prep_slave_sg(chan, bam_txn->wr_sgl, bam_txn->wr_sgl_cnt,
					   DMA_MEM_TO_DEV, attrs);
	if (!dma_desc) {
		dma_unmap_sg(qce->dev, bam_txn->wr_sgl, bam_txn->wr_sgl_cnt, DMA_TO_DEVICE);
		return -ENOMEM;
	}

	qce_desc->dma_desc = dma_desc;
	cookie = dmaengine_submit(qce_desc->dma_desc);

	ret = dma_submit_error(cookie);
	if (ret)
		return ret;

	qce_dma_issue_pending(&qce->dma);

	return 0;
}

int qce_submit_cmd_desc(struct qce_device *qce)
{
	return qce_do_submit_cmd_desc(qce, 0);
}

int qce_submit_cmd_desc_lock(struct qce_device *qce)
{
	return qce_do_submit_cmd_desc(qce, DMA_PREP_LOCK);
}

int qce_submit_cmd_desc_unlock(struct qce_device *qce)
{
	return qce_do_submit_cmd_desc(qce, DMA_PREP_UNLOCK);
}

static void qce_prep_dma_cmd_desc(struct qce_device *qce, struct qce_dma_data *dma,
				  unsigned int addr, void *buf)
{
	struct qce_bam_transaction *bam_txn = dma->bam_txn;
	struct bam_cmd_element *bam_ce_buf;
	int bam_ce_size, cnt, idx;

	idx = bam_txn->bam_ce_idx;
	bam_ce_buf = &bam_txn->bam_ce[idx];
	bam_prep_ce_le32(bam_ce_buf, addr, BAM_WRITE_COMMAND, *((__le32 *)buf));

	bam_ce_buf = &bam_txn->bam_ce[bam_txn->pre_bam_ce_idx];
	bam_txn->bam_ce_idx++;
	bam_ce_size = (bam_txn->bam_ce_idx - bam_txn->pre_bam_ce_idx) * sizeof(*bam_ce_buf);

	cnt = bam_txn->wr_sgl_cnt;

	sg_set_buf(&bam_txn->wr_sgl[cnt], bam_ce_buf, bam_ce_size);

	++bam_txn->wr_sgl_cnt;
	bam_txn->pre_bam_ce_idx = bam_txn->bam_ce_idx;
}

void qce_write_dma(struct qce_device *qce, unsigned int offset, u32 val)
{
	unsigned int reg_addr = ((unsigned int)(qce->base_phys) + offset);

	qce_prep_dma_cmd_desc(qce, &qce->dma, reg_addr, &val);
}

int devm_qce_dma_request(struct qce_device *qce)
{
	struct qce_dma_data *dma = &qce->dma;
	struct device *dev = qce->dev;

	dma->txchan = devm_dma_request_chan(dev, "tx");
	if (IS_ERR(dma->txchan))
		return PTR_ERR(dma->txchan);

	dma->rxchan = devm_dma_request_chan(dev, "rx");
	if (IS_ERR(dma->rxchan))
		return PTR_ERR(dma->rxchan);

	dma->result_buf = devm_kmalloc(dev, QCE_RESULT_BUF_SZ + QCE_IGNORE_BUF_SZ, GFP_KERNEL);
	if (!dma->result_buf)
		return -ENOMEM;

	dma->bam_txn = devm_kzalloc(dev, sizeof(*dma->bam_txn), GFP_KERNEL);
	if (!dma->bam_txn)
		return -ENOMEM;

	dma->bam_txn->desc = devm_kzalloc(dev, sizeof(*dma->bam_txn->desc), GFP_KERNEL);
	if (!dma->bam_txn->desc)
		return -ENOMEM;

	sg_init_table(dma->bam_txn->wr_sgl, QCE_BAM_CMD_SGL_SIZE);

	qce->read_buf = dmam_alloc_coherent(qce->dev, QCE_MAX_REG_READ * sizeof(*qce->read_buf),
					    &qce->read_buf_dma, GFP_KERNEL);
	if (!qce->read_buf)
		return -ENOMEM;

	return 0;
}

struct scatterlist *
qce_sgtable_add(struct sg_table *sgt, struct scatterlist *new_sgl,
		unsigned int max_len)
{
	struct scatterlist *sg = sgt->sgl, *sg_last = NULL;
	unsigned int new_len;

	while (sg) {
		if (!sg_page(sg))
			break;
		sg = sg_next(sg);
	}

	if (!sg)
		return ERR_PTR(-EINVAL);

	while (new_sgl && sg && max_len) {
		new_len = new_sgl->length > max_len ? max_len : new_sgl->length;
		sg_set_page(sg, sg_page(new_sgl), new_len, new_sgl->offset);
		sg_last = sg;
		sg = sg_next(sg);
		new_sgl = sg_next(new_sgl);
		max_len -= new_len;
	}

	return sg_last;
}

static int qce_dma_prep_sg(struct dma_chan *chan, struct scatterlist *sg,
			   int nents, unsigned long flags,
			   enum dma_transfer_direction dir,
			   dma_async_tx_callback cb, void *cb_param)
{
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;

	if (!sg || !nents)
		return -EINVAL;

	desc = dmaengine_prep_slave_sg(chan, sg, nents, dir, flags);
	if (!desc)
		return -EINVAL;

	desc->callback = cb;
	desc->callback_param = cb_param;
	cookie = dmaengine_submit(desc);

	return dma_submit_error(cookie);
}

int qce_dma_prep_sgs(struct qce_dma_data *dma, struct scatterlist *rx_sg,
		     int rx_nents, struct scatterlist *tx_sg, int tx_nents,
		     dma_async_tx_callback cb, void *cb_param)
{
	struct dma_chan *rxchan = dma->rxchan;
	struct dma_chan *txchan = dma->txchan;
	unsigned long flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
	int ret;

	ret = qce_dma_prep_sg(rxchan, rx_sg, rx_nents, flags, DMA_MEM_TO_DEV,
			     NULL, NULL);
	if (ret)
		return ret;

	return qce_dma_prep_sg(txchan, tx_sg, tx_nents, flags, DMA_DEV_TO_MEM,
			       cb, cb_param);
}

void qce_dma_issue_pending(struct qce_dma_data *dma)
{
	dma_async_issue_pending(dma->rxchan);
	dma_async_issue_pending(dma->txchan);
}

int qce_dma_terminate_all(struct qce_dma_data *dma)
{
	int ret;

	ret = dmaengine_terminate_all(dma->rxchan);
	return ret ?: dmaengine_terminate_all(dma->txchan);
}
