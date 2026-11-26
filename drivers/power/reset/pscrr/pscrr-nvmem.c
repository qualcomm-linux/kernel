// SPDX-License-Identifier: GPL-2.0
/*
 * pscrr-nvmem.c - NVMEM layout driver for PSCRR
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (C) 2025 Pengutronix, Oleksij Rempel <o.rempel@pengutronix.de>
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/pscrr.h>
#include <linux/slab.h>

struct pscrr_nvmem {
	struct nvmem_device *nvmem;
	u32 offset;		/* byte offset of the cell, from reg[0] */
	u32 bit_offset;		/* 0 when the cell has no "bits" property */
	u32 nbits;		/* BITS_PER_BYTE when the cell has no "bits" property */
	enum psc_reason reason;
};

static inline u8 pscrr_nvmem_mask(struct pscrr_nvmem *priv)
{
	return (u8)GENMASK(priv->bit_offset + priv->nbits - 1, priv->bit_offset);
}

static int pscrr_nvmem_raw_read(struct pscrr_nvmem *priv, u8 *raw)
{
	int ret;

	ret = nvmem_device_read(priv->nvmem, priv->offset, sizeof(*raw), raw);

	return ret < 0 ? ret : 0;
}

static int pscrr_nvmem_raw_write(struct pscrr_nvmem *priv, u8 raw)
{
	int ret;

	ret = nvmem_device_write(priv->nvmem, priv->offset, sizeof(raw), &raw);

	return ret < 0 ? ret : 0;
}

static int pscrr_nvmem_cell_read(struct pscrr_nvmem *priv, u8 *out)
{
	u8 raw, mask = pscrr_nvmem_mask(priv);
	int ret;

	ret = pscrr_nvmem_raw_read(priv, &raw);
	if (ret)
		return ret;

	*out = (raw & mask) >> priv->bit_offset;

	return 0;
}

static int pscrr_nvmem_cell_write(struct pscrr_nvmem *priv, u8 val)
{
	u8 raw, mask = pscrr_nvmem_mask(priv);
	int ret;

	/* Sub-byte cell: preserve the neighbouring bits, read-modify-write. */
	if (priv->nbits < BITS_PER_BYTE) {
		ret = pscrr_nvmem_raw_read(priv, &raw);
		if (ret)
			return ret;
	} else {
		raw = 0;
	}

	raw = (raw & ~mask) | ((val << priv->bit_offset) & mask);

	return pscrr_nvmem_raw_write(priv, raw);
}

static int pscrr_nvmem_read_reasons(struct pscrr_provider *p,
				    unsigned long *reasons)
{
	struct pscrr_nvmem *priv = p->priv;

	/*
	 * Report the reason latched at probe (or overwritten by a later
	 * record); the cell itself was cleared at probe, so it is not read
	 * live here.
	 */
	set_bit(READ_ONCE(priv->reason), reasons);

	return 0;
}

static int pscrr_nvmem_write_reason(struct pscrr_provider *p,
				    enum psc_reason reason)
{
	struct pscrr_nvmem *priv = p->priv;
	int ret;

	if (reason >= PSCR_REASON_COUNT)
		return -EINVAL;

	ret = pscrr_nvmem_cell_write(priv, reason);
	if (ret)
		return ret;

	WRITE_ONCE(priv->reason, reason);

	return 0;
}

static const struct pscrr_provider_ops pscrr_nvmem_ops = {
	.read_reasons = pscrr_nvmem_read_reasons,
	.write_reason = pscrr_nvmem_write_reason,
};

static struct device_node *pscrr_nvmem_get_cell(struct device *dev,
						struct nvmem_device *nvmem,
						u32 reg[2])
{
	struct device_node *layout_np, *cell_np = NULL;

	layout_np = of_nvmem_layout_get_container(nvmem);
	if (!layout_np)
		return ERR_PTR(-ENOENT);

	for_each_child_of_node_scoped(layout_np, child) {
		if (of_property_read_u32_array(child, "reg", reg, 2))
			continue;

		cell_np = of_node_get(child);
		break;
	}
	of_node_put(layout_np);

	return cell_np ?: ERR_PTR(dev_err_probe(dev, -ENOENT,
						"missing pscr nvmem cell\n"));
}

static int pscrr_nvmem_add_cells(struct nvmem_layout *layout)
{
	struct device *dev = &layout->dev;
	struct pscrr_nvmem *priv;
	struct device_node *cell_np;
	struct nvmem_cell_info info = { };
	u32 reg[2], bits[2];
	u8 val;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->nvmem = layout->nvmem;

	cell_np = pscrr_nvmem_get_cell(dev, priv->nvmem, reg);
	if (IS_ERR(cell_np))
		return PTR_ERR(cell_np);

	/* The reason is a single byte; a bit cell still occupies one. */
	if (reg[1] != sizeof(val)) {
		of_node_put(cell_np);
		return dev_err_probe(dev, -EINVAL,
				     "unsupported pscr nvmem cell size\n");
	}
	priv->offset = reg[0];

	if (!of_property_read_u32_array(cell_np, "bits", bits, ARRAY_SIZE(bits))) {
		priv->bit_offset = bits[0];
		priv->nbits = bits[1];
	} else {
		priv->bit_offset = 0;
		priv->nbits = BITS_PER_BYTE;
	}

	if (priv->bit_offset + priv->nbits > BITS_PER_BYTE * reg[1]) {
		of_node_put(cell_np);
		return dev_err_probe(dev, -EINVAL, "bits exceed cell size\n");
	}

	/* PSCR_REASON_COUNT must fit in the allotted bits. */
	if (PSCR_REASON_COUNT > (1U << priv->nbits)) {
		of_node_put(cell_np);
		return dev_err_probe(dev, -EINVAL,
				     "too few bits for the reason range\n");
	}

	/*
	 * The cell survives resets, so at boot it still holds whatever the
	 * previous session recorded before it went down. Latch that as this
	 * boot's reason, then clear the cell back to PSCR_UNKNOWN: if this
	 * session is later cut short by an abrupt reset that never runs the
	 * recorder (watchdog power-cycle, sudden power loss), the next boot
	 * reads "unknown" instead of a stale reason left over from an earlier
	 * cycle.
	 */
	ret = pscrr_nvmem_cell_read(priv, &val);
	if (ret) {
		of_node_put(cell_np);
		return dev_err_probe(dev, ret,
				     "failed to read the pscr nvmem cell\n");
	}

	priv->reason = val < PSCR_REASON_COUNT ? val : PSCR_UNKNOWN;

	ret = pscrr_nvmem_cell_write(priv, PSCR_UNKNOWN);
	if (ret) {
		of_node_put(cell_np);
		return dev_err_probe(dev, ret,
				     "failed to clear the pscr nvmem cell\n");
	}

	info.name = kasprintf(GFP_KERNEL, "%pOFn", cell_np);
	if (!info.name) {
		of_node_put(cell_np);
		return -ENOMEM;
	}
	info.offset = reg[0];
	info.bytes = reg[1];
	info.bit_offset = priv->bit_offset;
	info.nbits = priv->nbits;
	info.np = cell_np;

	ret = nvmem_add_one_cell(priv->nvmem, &info);
	kfree(info.name);
	if (ret) {
		of_node_put(cell_np);
		return ret;
	}

	return PTR_ERR_OR_ZERO(devm_pscrr_provider_register(dev, "nvmem",
							    &pscrr_nvmem_ops, NULL,
							    priv));
}

static int pscrr_nvmem_probe(struct nvmem_layout *layout)
{
	layout->add_cells = pscrr_nvmem_add_cells;

	return nvmem_layout_register(layout);
}

static void pscrr_nvmem_remove(struct nvmem_layout *layout)
{
	nvmem_layout_unregister(layout);
}

static const struct of_device_id pscrr_nvmem_of_match[] = {
	{ .compatible = "pscrr-nvmem" },
	{ }
};
MODULE_DEVICE_TABLE(of, pscrr_nvmem_of_match);

static struct nvmem_layout_driver pscrr_nvmem_layout = {
	.driver	= {
		.name		= "pscrr-nvmem",
		.of_match_table	= pscrr_nvmem_of_match,
	},
	.probe	= pscrr_nvmem_probe,
	.remove	= pscrr_nvmem_remove,
};
module_nvmem_layout_driver(pscrr_nvmem_layout);

MODULE_AUTHOR("Oleksij Rempel <o.rempel@pengutronix.de>");
MODULE_AUTHOR("Faruque Ansari <faruque.ansari@oss.qualcomm.com>");
MODULE_DESCRIPTION("NVMEM recorder provider for PSCRR");
MODULE_LICENSE("GPL");
