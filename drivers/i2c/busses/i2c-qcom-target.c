// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/interconnect.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

/* Register offsets */
#define I2C_S_DEVICE_ADDR			0x00
#define I2C_S_IRQ_STATUS			0x08
#define I2C_S_IRQ_CLR				0x0C
#define I2C_S_IRQ_EN				0x10
#define I2C_S_CONFIG				0x18
#define I2C_S_CONTROL				0x1C
#define I2C_S_FIFOS_STATUS			0x20
#define I2C_S_TX_FIFO				0x24
#define I2C_S_RX_FIFO				0x28
#define I2C_S_DEBUG_REG1			0x3C
#define I2C_S_DEBUG_REG2			0x40
#define I2C_S_SW_RESET_REG			0x4C
#define I2C_S_CLK_LOW_TIMEOUT			0x50
#define I2C_S_CLK_RELEASE_DELAY_CNT_VAL	0x54
#define I2C_S_SDA_HOLD_CNT_VAL			0x58

/* I2C_S_CONFIG register fields */
#define I2C_S_CORE_EN				BIT(0)

/* I2C_S_CONTROL register fields */
#define CLEAR_RX_FIFO				BIT(0)
#define CLEAR_TX_FIFO				BIT(1)
#define NACK					BIT(2)
#define ACK_RESUME				BIT(3)

/* I2C_S_SW_RESET_REG register fields */
#define SW_RESET				BIT(0)

/* I2C_S_FIFOS_STATUS register fields */
#define RX_FIFO_COUNT_MASK			GENMASK(31, 16)

/* Interconnect bandwidth vote in bytes per second */
#define APPS_PROC_TO_I2C_TARGET_VOTE		1190000

/**
 * enum qcom_i2c_target_irq - IRQ bit positions in I2C_S_IRQ_STATUS
 * @STOP_DETECTED:	I2C stop condition detected on the bus
 * @RX_FIFO_FULL:	receive FIFO has reached capacity
 * @TX_FIFO_EMPTY:	transmit FIFO is empty
 * @RX_DATA_AVAIL:	receive data is available in the RX FIFO
 * @CLOCK_LOW_TIMEOUT:	SCL held low longer than the configured timeout
 * @STRCH_WR:		clock stretching during a write (Rx) phase
 * @STRCH_RD:		clock stretching during a read (Tx) phase
 * @GCA_DETECTED:	general call address detected (not used)
 * @ERR_CONDITION:	unexpected start or stop bit detected (error)
 * @RESTART_DETECTED:	repeated start condition detected
 */
enum qcom_i2c_target_irq {
	STOP_DETECTED,
	RX_FIFO_FULL,
	TX_FIFO_EMPTY,
	RX_DATA_AVAIL,
	CLOCK_LOW_TIMEOUT,
	STRCH_WR,
	STRCH_RD,
	GCA_DETECTED,
	ERR_CONDITION,
	RESTART_DETECTED,
};

/*
 * TX_FIFO_EMPTY is excluded: this driver fills the TX FIFO one byte at a
 * time in response to STRCH_RD (clock-stretch during read phase), so
 * TX_FIFO_EMPTY adds no value and would double the interrupt rate.
 * GCA (general call address) is unsupported.
 */
#define QCOM_I2C_TARGET_ALL_IRQ	(GENMASK(RESTART_DETECTED, STOP_DETECTED) \
				 & ~(BIT(GCA_DETECTED) | BIT(TX_FIFO_EMPTY)))

/* Bit indices for the status bitmask, used with set_bit/test_bit/clear_bit */
enum qcom_i2c_target_status {
	READ_IN_PROGRESS,
	WRITE_IN_PROGRESS,
};

/**
 * struct qcom_i2c_target - Qualcomm I2C target controller private data
 * @dev:		driver model device node
 * @base:		base address of HW registers
 * @adap:		I2C adapter (slave mode)
 * @ahb_clk:		AHB bus clock
 * @xo_clk:		XO reference clock
 * @icc_path:		interconnect bandwidth path
 * @slave:		currently registered slave backend client; written only
 *			under disable_irq() in reg_slave()/unreg_slave() so the
 *			ISR sees a stable pointer for its entire execution
 * @status:		bitmask of enum qcom_i2c_target_status flags; must be
 *			unsigned long for set_bit/test_bit/clear_bit; accessed
 *			only from the ISR and from reg_slave()/unreg_slave()
 *			under disable_irq(), so no additional lock is needed
 * @irq:		interrupt line number
 */
struct qcom_i2c_target {
	struct device		*dev;
	void __iomem		*base;
	struct i2c_adapter	adap;
	struct clk		*ahb_clk;
	struct clk		*xo_clk;
	struct icc_path		*icc_path;
	struct i2c_client	*slave;
	unsigned long		status;
	int			irq;
};

static void qcom_i2c_target_dump_regs(struct qcom_i2c_target *target)
{
	dev_dbg(target->dev, "I2C_S_DEVICE_ADDR:               0x%x\n",
		readl_relaxed(target->base + I2C_S_DEVICE_ADDR));
	dev_dbg(target->dev, "I2C_S_IRQ_STATUS:                0x%x\n",
		readl_relaxed(target->base + I2C_S_IRQ_STATUS));
	dev_dbg(target->dev, "I2C_S_CONFIG:                    0x%x\n",
		readl_relaxed(target->base + I2C_S_CONFIG));
	dev_dbg(target->dev, "I2C_S_IRQ_EN:                    0x%x\n",
		readl_relaxed(target->base + I2C_S_IRQ_EN));
	dev_dbg(target->dev, "I2C_S_FIFOS_STATUS:              0x%x\n",
		readl_relaxed(target->base + I2C_S_FIFOS_STATUS));
	dev_dbg(target->dev, "I2C_S_DEBUG_REG1:                0x%x\n",
		readl_relaxed(target->base + I2C_S_DEBUG_REG1));
	dev_dbg(target->dev, "I2C_S_DEBUG_REG2:                0x%x\n",
		readl_relaxed(target->base + I2C_S_DEBUG_REG2));
	dev_dbg(target->dev, "I2C_S_CLK_LOW_TIMEOUT:           0x%x\n",
		readl_relaxed(target->base + I2C_S_CLK_LOW_TIMEOUT));
	dev_dbg(target->dev, "I2C_S_CLK_RELEASE_DELAY_CNT_VAL: 0x%x\n",
		readl_relaxed(target->base + I2C_S_CLK_RELEASE_DELAY_CNT_VAL));
	dev_dbg(target->dev, "I2C_S_SDA_HOLD_CNT_VAL:          0x%x\n",
		readl_relaxed(target->base + I2C_S_SDA_HOLD_CNT_VAL));
}

static void qcom_i2c_target_hw_init(struct qcom_i2c_target *target)
{
	dev_dbg(target->dev, "HW init: resetting FIFOs, enabling IRQs and core\n");
	writel(CLEAR_TX_FIFO | CLEAR_RX_FIFO, target->base + I2C_S_CONTROL);
	writel(QCOM_I2C_TARGET_ALL_IRQ, target->base + I2C_S_IRQ_EN);
	writel(I2C_S_CORE_EN, target->base + I2C_S_CONFIG);
}

static void qcom_i2c_target_write_requested(struct qcom_i2c_target *target)
{
	u8 val = 0;

	if (!test_bit(WRITE_IN_PROGRESS, &target->status)) {
		dev_dbg(target->dev, "Write phase started\n");
		set_bit(WRITE_IN_PROGRESS, &target->status);
		i2c_slave_event(target->slave, I2C_SLAVE_WRITE_REQUESTED, &val);
	}
}

static int qcom_i2c_target_drain_rx_fifo(struct qcom_i2c_target *target)
{
	unsigned int rx_count;
	int i, ret = 0;
	u8 val;

	rx_count = FIELD_GET(RX_FIFO_COUNT_MASK,
			     readl_relaxed(target->base + I2C_S_FIFOS_STATUS));

	for (i = 0; i < rx_count; i++) {
		val = (u8)readl_relaxed(target->base + I2C_S_RX_FIFO);
		dev_dbg(target->dev, "Data from RX FIFO: 0x%x\n", val);
		ret = i2c_slave_event(target->slave, I2C_SLAVE_WRITE_RECEIVED, &val);
		if (ret)
			break;
	}

	return ret;
}

static void qcom_i2c_target_hw_reset(struct qcom_i2c_target *target)
{
	/* Clear error bits before SW_RESET; the reset may not be instantaneous */
	writel(BIT(ERR_CONDITION) | BIT(CLOCK_LOW_TIMEOUT),
	       target->base + I2C_S_IRQ_CLR);
	writel(SW_RESET, target->base + I2C_S_SW_RESET_REG);
	qcom_i2c_target_hw_init(target);
	writel(target->slave->addr, target->base + I2C_S_DEVICE_ADDR);
}

static irqreturn_t qcom_i2c_target_handle_error(struct qcom_i2c_target *target, u32 irq_stat)
{
	u8 val = 0;

	if (irq_stat & BIT(ERR_CONDITION))
		dev_err(target->dev, "Error condition: unexpected Start/Stop bits\n");
	else
		dev_err(target->dev, "Clock low timeout\n");
	qcom_i2c_target_dump_regs(target);
	qcom_i2c_target_hw_reset(target);
	i2c_slave_event(target->slave, I2C_SLAVE_STOP, &val);
	target->status = 0;
	return IRQ_HANDLED;
}

static irqreturn_t qcom_i2c_target_handle_stop(struct qcom_i2c_target *target, u32 irq_stat)
{
	u8 val = 0;

	dev_dbg(target->dev, "Stop bit detected\n");
	/*
	 * Short-write corner case: WRITE_IN_PROGRESS was never set because
	 * no RX_DATA_AVAIL or STRCH_WR fired before STOP. Do not call
	 * qcom_i2c_target_write_requested() here — STOP ends the transaction
	 * so setting WRITE_IN_PROGRESS now would leave a stale bit after
	 * target->status is cleared below.
	 */
	if (!test_bit(WRITE_IN_PROGRESS, &target->status)) {
		unsigned int rx_count;

		rx_count = FIELD_GET(RX_FIFO_COUNT_MASK,
				     readl_relaxed(target->base + I2C_S_FIFOS_STATUS));
		if (rx_count)
			i2c_slave_event(target->slave, I2C_SLAVE_WRITE_REQUESTED,
					&val);
	}
	qcom_i2c_target_drain_rx_fifo(target);
	i2c_slave_event(target->slave, I2C_SLAVE_STOP, &val);
	target->status = 0;
	writel(CLEAR_RX_FIFO, target->base + I2C_S_CONTROL);
	/*
	 * STOP terminates the transaction, so any other Rx or clock
	 * stretch bits latched in this same status read have already
	 * been serviced by the drain above or are now stale. Ack the
	 * whole word rather than just STOP_DETECTED; clearing only STOP
	 * would leave those bits set and immediately re-enter the
	 * handler, which for a co-asserted STRCH_RD would push a stale
	 * byte into the TX FIFO.
	 */
	writel(irq_stat, target->base + I2C_S_IRQ_CLR);
	return IRQ_HANDLED;
}

static void qcom_i2c_target_handle_rx_data(struct qcom_i2c_target *target, u32 rx_irq_bits)
{
	int ret;

	dev_dbg(target->dev, "Rx data event (rx_irq_bits=0x%x)\n", rx_irq_bits);
	qcom_i2c_target_write_requested(target);
	ret = qcom_i2c_target_drain_rx_fifo(target);
	if (ret)
		dev_dbg(target->dev, "Backend requested NACK\n");
	writel(ret ? NACK | CLEAR_RX_FIFO : ACK_RESUME,
	       target->base + I2C_S_CONTROL);
	writel(rx_irq_bits, target->base + I2C_S_IRQ_CLR);
}

static void qcom_i2c_target_handle_strch_rd(struct qcom_i2c_target *target)
{
	enum i2c_slave_event event = I2C_SLAVE_READ_PROCESSED;
	u8 val = 0;

	dev_dbg(target->dev, "Clock stretching during read (Tx) phase\n");
	if (!test_bit(READ_IN_PROGRESS, &target->status)) {
		set_bit(READ_IN_PROGRESS, &target->status);
		/* Repeated-start: master switched direction from write to read */
		clear_bit(WRITE_IN_PROGRESS, &target->status);
		event = I2C_SLAVE_READ_REQUESTED;
	}
	i2c_slave_event(target->slave, event, &val);
	dev_dbg(target->dev, "Data to TX FIFO: 0x%x\n", val);
	writel(val, target->base + I2C_S_TX_FIFO);
	writel(ACK_RESUME, target->base + I2C_S_CONTROL);
	writel(BIT(STRCH_RD), target->base + I2C_S_IRQ_CLR);
}

static irqreturn_t qcom_i2c_target_irq(int irq, void *dev)
{
	struct qcom_i2c_target *target = dev;
	u32 irq_stat, rx_bits;

	/*
	 * Dispatch priority (highest first):
	 *   ERR_CONDITION / CLOCK_LOW_TIMEOUT  — hardware error, triggers SW reset
	 *   STOP_DETECTED                      — end of transaction, clears all state
	 *   STRCH_RD                           — read-phase data supply
	 *   RX_FIFO_FULL / RX_DATA_AVAIL /
	 *   STRCH_WR                           — write-phase Rx, coalesced into one drain
	 *   RESTART_DETECTED                   — repeated start, resets state
	 */
	irq_stat = readl_relaxed(target->base + I2C_S_IRQ_STATUS);
	if (!irq_stat)
		return IRQ_NONE;

	dev_dbg(target->dev, "IRQ status: 0x%x\n", irq_stat);

	/*
	 * Load target->slave once. Both reg_slave() and unreg_slave() disable
	 * the IRQ before writing the pointer, so it cannot change while this
	 * handler runs. Sub-handlers may dereference target->slave directly.
	 */
	if (!READ_ONCE(target->slave)) {
		writel(irq_stat, target->base + I2C_S_IRQ_CLR);
		return IRQ_HANDLED;
	}

	if (irq_stat & (BIT(ERR_CONDITION) | BIT(CLOCK_LOW_TIMEOUT)))
		return qcom_i2c_target_handle_error(target, irq_stat);

	if (irq_stat & BIT(STOP_DETECTED))
		return qcom_i2c_target_handle_stop(target, irq_stat);

	if (irq_stat & BIT(STRCH_RD))
		qcom_i2c_target_handle_strch_rd(target);

	/*
	 * Coalesce all write-phase Rx bits into a single drain+ACK. When the
	 * RX FIFO fills at threshold, RX_FIFO_FULL, RX_DATA_AVAIL and STRCH_WR
	 * can all assert in the same irq_stat snapshot. Pass the combined mask
	 * so ACK_RESUME is written exactly once and all bits are cleared together.
	 */
	rx_bits = irq_stat & (BIT(RX_FIFO_FULL) | BIT(RX_DATA_AVAIL) |
			      BIT(STRCH_WR));
	if (rx_bits)
		qcom_i2c_target_handle_rx_data(target, rx_bits);

	if (irq_stat & BIT(RESTART_DETECTED)) {
		dev_dbg(target->dev, "Repeated start bit detected\n");
		target->status = 0;
		writel(ACK_RESUME, target->base + I2C_S_CONTROL);
		writel(BIT(RESTART_DETECTED), target->base + I2C_S_IRQ_CLR);
	}

	return IRQ_HANDLED;
}

static int qcom_i2c_target_reg_slave(struct i2c_client *slave)
{
	struct qcom_i2c_target *target = i2c_get_adapdata(slave->adapter);

	if (target->slave)
		return -EBUSY;

	if (slave->flags & I2C_CLIENT_TEN)
		return -EAFNOSUPPORT;

	disable_irq(target->irq);
	WRITE_ONCE(target->slave, slave);
	writel(slave->addr, target->base + I2C_S_DEVICE_ADDR);
	enable_irq(target->irq);

	return 0;
}

static int qcom_i2c_target_unreg_slave(struct i2c_client *slave)
{
	struct qcom_i2c_target *target = i2c_get_adapdata(slave->adapter);

	if (!target->slave)
		return -EINVAL;
	disable_irq(target->irq);
	WRITE_ONCE(target->slave, NULL);
	target->status = 0;
	writel(0, target->base + I2C_S_DEVICE_ADDR);
	enable_irq(target->irq);

	return 0;
}

static int qcom_i2c_target_icc_init(struct qcom_i2c_target *target)
{
	int ret;

	target->icc_path = devm_of_icc_get(target->dev, "i2c");
	if (IS_ERR(target->icc_path))
		return dev_err_probe(target->dev, PTR_ERR(target->icc_path),
				     "failed to get ICC path\n");

	/*
	 * The controller only needs interconnect bandwidth while it is
	 * powered. The vote is placed here and across resume with
	 * icc_set_bw(), and dropped with icc_set_bw(path, 0, 0) on suspend
	 * and remove, so the vote and unvote are always symmetric.
	 */
	ret = icc_set_bw(target->icc_path, APPS_PROC_TO_I2C_TARGET_VOTE,
			 APPS_PROC_TO_I2C_TARGET_VOTE);
	if (ret)
		return dev_err_probe(target->dev, ret, "icc_set_bw failed\n");

	return 0;
}

static u32 qcom_i2c_target_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SLAVE;
}

static const struct i2c_algorithm qcom_i2c_target_algo = {
	.reg_target	= qcom_i2c_target_reg_slave,
	.unreg_target	= qcom_i2c_target_unreg_slave,
	.functionality	= qcom_i2c_target_func,
};

static int qcom_i2c_target_adap_init(struct qcom_i2c_target *target)
{
	target->adap.algo = &qcom_i2c_target_algo;
	target->adap.dev.parent = target->dev;
	target->adap.dev.of_node = target->dev->of_node;
	strscpy(target->adap.name, "qcom-i2c-target",
		sizeof(target->adap.name));
	i2c_set_adapdata(&target->adap, target);

	return i2c_add_adapter(&target->adap);
}

static int qcom_i2c_target_probe(struct platform_device *pdev)
{
	struct qcom_i2c_target *target;
	struct device *dev = &pdev->dev;
	int ret;

	target = devm_kzalloc(dev, sizeof(*target), GFP_KERNEL);
	if (!target)
		return -ENOMEM;

	target->dev = dev;

	target->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(target->base))
		return dev_err_probe(dev, PTR_ERR(target->base),
				     "failed to map registers\n");

	target->xo_clk = devm_clk_get_enabled(dev, "xo");
	if (IS_ERR(target->xo_clk))
		return dev_err_probe(dev, PTR_ERR(target->xo_clk),
				     "failed to get and enable XO clock\n");

	target->ahb_clk = devm_clk_get_enabled(dev, "ahb");
	if (IS_ERR(target->ahb_clk))
		return dev_err_probe(dev, PTR_ERR(target->ahb_clk),
				     "failed to get and enable AHB clock\n");

	target->irq = platform_get_irq(pdev, 0);
	if (target->irq < 0)
		return target->irq;

	ret = qcom_i2c_target_icc_init(target);
	if (ret)
		return ret;

	ret = devm_request_irq(dev, target->irq, qcom_i2c_target_irq, 0,
			       dev_name(dev), target);
	if (ret)
		return dev_err_probe(dev, ret, "request_irq failed for IRQ %d\n",
				     target->irq);

	qcom_i2c_target_hw_init(target);

	platform_set_drvdata(pdev, target);

	ret = qcom_i2c_target_adap_init(target);
	if (ret)
		return dev_err_probe(dev, ret, "i2c_add_adapter failed\n");

	return 0;
}

static void qcom_i2c_target_remove(struct platform_device *pdev)
{
	struct qcom_i2c_target *target = platform_get_drvdata(pdev);

	disable_irq(target->irq);
	i2c_del_adapter(&target->adap);
	writel(0, target->base + I2C_S_CONFIG);
	icc_set_bw(target->icc_path, 0, 0);
}

static int qcom_i2c_target_suspend(struct device *dev)
{
	struct qcom_i2c_target *target = dev_get_drvdata(dev);
	int ret;

	ret = icc_set_bw(target->icc_path, 0, 0);
	if (ret)
		dev_err(dev, "icc_set_bw failed on suspend: %d\n", ret);

	clk_disable_unprepare(target->xo_clk);
	clk_disable_unprepare(target->ahb_clk);

	return 0;
}

static int qcom_i2c_target_resume(struct device *dev)
{
	struct qcom_i2c_target *target = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(target->ahb_clk);
	if (ret) {
		dev_err(dev, "failed to enable AHB clock\n");
		return ret;
	}

	ret = clk_prepare_enable(target->xo_clk);
	if (ret) {
		dev_err(dev, "failed to enable XO clock\n");
		goto err_disable_ahb;
	}

	ret = icc_set_bw(target->icc_path, APPS_PROC_TO_I2C_TARGET_VOTE,
			 APPS_PROC_TO_I2C_TARGET_VOTE);
	if (ret) {
		dev_err(dev, "icc_set_bw failed\n");
		goto err_disable_xo;
	}

	qcom_i2c_target_hw_init(target);
	if (target->slave)
		writel(target->slave->addr, target->base + I2C_S_DEVICE_ADDR);

	return 0;

err_disable_xo:
	clk_disable_unprepare(target->xo_clk);
err_disable_ahb:
	clk_disable_unprepare(target->ahb_clk);
	return ret;
}

static const struct dev_pm_ops qcom_i2c_target_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(qcom_i2c_target_suspend,
				      qcom_i2c_target_resume)
};

static const struct of_device_id qcom_i2c_target_dt_match[] = {
	{ .compatible = "qcom,qdu1000-i2c-target" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_i2c_target_dt_match);

static struct platform_driver qcom_i2c_target_driver = {
	.driver = {
		.name		= "qcom-i2c-target",
		.pm		= &qcom_i2c_target_pm_ops,
		.of_match_table	= qcom_i2c_target_dt_match,
	},
	.probe	= qcom_i2c_target_probe,
	.remove	= qcom_i2c_target_remove,
};
module_platform_driver(qcom_i2c_target_driver);

MODULE_DESCRIPTION("Qualcomm I2C target controller driver");
MODULE_AUTHOR("Viken Dadhaniya <viken.dadhaniya@oss.qualcomm.com>");
MODULE_LICENSE("GPL");
