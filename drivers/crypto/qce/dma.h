/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
 */

#ifndef _DMA_H_
#define _DMA_H_

#include <linux/dmaengine.h>

struct qce_bam_transaction;
struct qce_device;

/* maximum data transfer block size between BAM and CE */
#define QCE_BAM_BURST_SIZE		64

#define QCE_AUTHIV_REGS_CNT		16
#define QCE_AUTH_BYTECOUNT_REGS_CNT	4
#define QCE_CNTRIV_REGS_CNT		4

struct qce_result_dump {
	u32 auth_iv[QCE_AUTHIV_REGS_CNT];
	u32 auth_byte_count[QCE_AUTH_BYTECOUNT_REGS_CNT];
	u32 encr_cntr_iv[QCE_CNTRIV_REGS_CNT];
	u32 status;
	u32 status2;
};

#define QCE_RESULT_BUF_SZ	\
		ALIGN(sizeof(struct qce_result_dump), QCE_BAM_BURST_SIZE)

struct qce_dma_data {
	struct dma_chan *txchan;
	struct dma_chan *rxchan;
	struct qce_result_dump *result_buf;
	struct qce_bam_transaction *bam_txn;
};

int devm_qce_dma_request(struct qce_device *qce);
int qce_dma_prep_sgs(struct qce_dma_data *dma, struct scatterlist *sg_in,
		     int in_ents, struct scatterlist *sg_out, int out_ents,
		     dma_async_tx_callback cb, void *cb_param);
void qce_dma_issue_pending(struct qce_dma_data *dma);
int qce_dma_terminate_all(struct qce_dma_data *dma);
struct scatterlist *
qce_sgtable_add(struct sg_table *sgt, struct scatterlist *sg_add,
		unsigned int max_len);
void qce_write_dma(struct qce_device *qce, unsigned int offset, u32 val);
int qce_submit_cmd_desc(struct qce_device *qce);
int qce_submit_cmd_desc_lock(struct qce_device *qce);
int qce_submit_cmd_desc_unlock(struct qce_device *qce);
void qce_clear_bam_transaction(struct qce_device *qce);
int qce_bam_lock(struct qce_device *qce);
int qce_bam_unlock(struct qce_device *qce);

#endif /* _DMA_H_ */
