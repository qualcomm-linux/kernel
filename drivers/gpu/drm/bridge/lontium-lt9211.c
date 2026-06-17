// SPDX-License-Identifier: GPL-2.0
/*
 * Lontium LT9211 bridge driver
 *
 * LT9211 is capable of converting:
 *   2xDSI/2xLVDS/1xDPI -> 2xDSI/2xLVDS/1xDPI
 * Currently supported is:
 *   1xDSI -> 1xLVDS
 *
 * Copyright (C) 2022 Marek Vasut <marex@denx.de>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#define REG_PAGE_CONTROL			0xff
#define REG_CHIPID0				0x8100
#define REG_CHIPID0_VALUE			0x18
#define REG_CHIPID1				0x8101
#define REG_CHIPID1_VALUE			0x01
#define REG_CHIPID2				0x8102
#define REG_CHIPID2_VALUE			0xe3

/* LT9211C chip ID values */
#define REG_CHIPID0_LT9211C_VALUE	0x21
#define REG_CHIPID1_LT9211C_VALUE	0x03
#define REG_CHIPID2_LT9211C_VALUE	0xe1

#define REG_DSI_LANE				0xd000
/* DSI lane count - 0 means 4 lanes ; 1, 2, 3 means 1, 2, 3 lanes. */
#define REG_DSI_LANE_COUNT(n)			((n) & 3)

/* Chip type enum */
enum lt9211_chip_type {
	LT9211,
	LT9211C,
};

struct lt9211 {
	struct drm_bridge		bridge;
	struct device			*dev;
	struct regmap			*regmap;
	struct mipi_dsi_device	*dsi;
	struct drm_bridge		*panel_bridge;
	struct gpio_desc		*reset_gpio;
	struct regulator		*vccio;
	bool					lvds_dual_link;
	bool					lvds_dual_link_even_odd_swap;
	/* LT9211C specific fields */
	enum lt9211_chip_type	chip_type;
	struct workqueue_struct	*wq;
	struct delayed_work		lt9211_dw;
	struct drm_display_mode	mode;
	bool					bpp24;
	bool					jeida;
	bool					de;
};

static const struct regmap_range lt9211_rw_ranges[] = {
	regmap_reg_range(0xff, 0xff),
	regmap_reg_range(0x8100, 0x816b),
	regmap_reg_range(0x8200, 0x82aa),
	regmap_reg_range(0x8500, 0x85ff),
	regmap_reg_range(0x8600, 0x86a0),
	regmap_reg_range(0x8700, 0x8746),
	regmap_reg_range(0xd000, 0xd0a7),
	regmap_reg_range(0xd400, 0xd42c),
	regmap_reg_range(0xd800, 0xd838),
	regmap_reg_range(0xd9c0, 0xd9d5),
};

static const struct regmap_access_table lt9211_rw_table = {
	.yes_ranges = lt9211_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(lt9211_rw_ranges),
};

static const struct regmap_range_cfg lt9211_range = {
	.name = "lt9211",
	.range_min = 0x0000,
	.range_max = 0xda00,
	.selector_reg = REG_PAGE_CONTROL,
	.selector_mask = 0xff,
	.selector_shift = 0,
	.window_start = 0,
	.window_len = 0x100,
};

static const struct regmap_config lt9211_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.rd_table = &lt9211_rw_table,
	.wr_table = &lt9211_rw_table,
	.volatile_table	= &lt9211_rw_table,
	.ranges = &lt9211_range,
	.num_ranges = 1,
	.cache_type = REGCACHE_MAPLE,
	.max_register = 0xda00,
};

static const struct regmap_range lt9211c_rw_ranges[] = {
	regmap_reg_range(0xff, 0xff),
	regmap_reg_range(0x8100, 0x8182),
	regmap_reg_range(0x8200, 0x82aa),
	regmap_reg_range(0x8500, 0x85ff),
	regmap_reg_range(0x8600, 0x86a0),
	regmap_reg_range(0x8700, 0x8746),
	regmap_reg_range(0xd000, 0xd0a7),
	regmap_reg_range(0xd400, 0xd42c),
	regmap_reg_range(0xd800, 0xd838),
	regmap_reg_range(0xd9c0, 0xd9d5),
};

static const struct regmap_access_table lt9211c_rw_table = {
	.yes_ranges = lt9211c_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(lt9211c_rw_ranges),
};

static const struct regmap_range_cfg lt9211c_range = {
	.name = "lt9211c",
	.range_min = 0x0000,
	.range_max = 0xda00,
	.selector_reg = REG_PAGE_CONTROL,
	.selector_mask = 0xff,
	.selector_shift = 0,
	.window_start = 0,
	.window_len = 0x100,
};

static const struct regmap_config lt9211c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.rd_table = &lt9211c_rw_table,
	.wr_table = &lt9211c_rw_table,
	.volatile_table = &lt9211c_rw_table,
	.ranges = &lt9211c_range,
	.num_ranges = 1,
	.cache_type = REGCACHE_RBTREE,
	.max_register = 0xda00,
};

static void lt9211_delayed_work_func(struct work_struct *work);

static struct lt9211 *bridge_to_lt9211(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9211, bridge);
}

static int lt9211_attach(struct drm_bridge *bridge,
			 struct drm_encoder *encoder,
			 enum drm_bridge_attach_flags flags)
{
	struct lt9211 *ctx = bridge_to_lt9211(bridge);

	return drm_bridge_attach(encoder, ctx->panel_bridge,
				 &ctx->bridge, flags);
}

static int lt9211_read_chipid(struct lt9211 *ctx)
{
	u8 chipid[3];
	int ret;

	/* Read Chip ID registers and verify the chip can communicate. */
	ret = regmap_bulk_read(ctx->regmap, REG_CHIPID0, chipid, 3);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to read Chip ID: %d\n", ret);
		return ret;
	}

	/* Test for LT9211 Chip ID. */
	if (chipid[0] == REG_CHIPID0_VALUE && chipid[1] == REG_CHIPID1_VALUE) {
		dev_dbg(ctx->dev, "Detected LT9211 chip\n");
		return 0;
	}

	/* Test for LT9211C Chip ID. */
	if (chipid[0] == REG_CHIPID0_LT9211C_VALUE && chipid[1] == REG_CHIPID1_LT9211C_VALUE) {
		dev_dbg(ctx->dev, "Detected LT9211C chip\n");
		return 0;
	}

	dev_err(ctx->dev, "Unknown Chip ID: 0x%02x 0x%02x\n", chipid[0], chipid[1]);

	return -EINVAL;
}

static int lt9211_system_init(struct lt9211 *ctx)
{
	const struct reg_sequence lt9211_system_init_seq[] = {
		{ 0x8201, 0x18 },
		{ 0x8606, 0x61 },
		{ 0x8607, 0xa8 },
		{ 0x8714, 0x08 },
		{ 0x8715, 0x00 },
		{ 0x8718, 0x0f },
		{ 0x8722, 0x08 },
		{ 0x8723, 0x00 },
		{ 0x8726, 0x0f },
		{ 0x810b, 0xfe },
	};

	return regmap_multi_reg_write(ctx->regmap, lt9211_system_init_seq,
				      ARRAY_SIZE(lt9211_system_init_seq));
}

static int lt9211_configure_rx(struct lt9211 *ctx)
{
	const struct reg_sequence lt9211_rx_phy_seq[] = {
		{ 0x8202, 0x44 },
		{ 0x8204, 0xa0 },
		{ 0x8205, 0x22 },
		{ 0x8207, 0x9f },
		{ 0x8208, 0xfc },
		/* ORR with 0xf8 here to enable DSI DN/DP swap. */
		{ 0x8209, 0x01 },
		{ 0x8217, 0x0c },
		{ 0x8633, 0x1b },
	};

	const struct reg_sequence lt9211_rx_cal_reset_seq[] = {
		{ 0x8120, 0x7f },
		{ 0x8120, 0xff },
	};

	const struct reg_sequence lt9211_rx_dig_seq[] = {
		{ 0x8630, 0x85 },
		/* 0x8588: BIT 6 set = MIPI-RX, BIT 4 unset = LVDS-TX */
		{ 0x8588, 0x40 },
		{ 0x85ff, 0xd0 },
		{ REG_DSI_LANE, REG_DSI_LANE_COUNT(ctx->dsi->lanes) },
		{ 0xd002, 0x05 },
	};

	const struct reg_sequence lt9211_rx_div_reset_seq[] = {
		{ 0x810a, 0xc0 },
		{ 0x8120, 0xbf },
	};

	const struct reg_sequence lt9211_rx_div_clear_seq[] = {
		{ 0x810a, 0xc1 },
		{ 0x8120, 0xff },
	};

	int ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211_rx_phy_seq,
				     ARRAY_SIZE(lt9211_rx_phy_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211_rx_cal_reset_seq,
				     ARRAY_SIZE(lt9211_rx_cal_reset_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211_rx_dig_seq,
				     ARRAY_SIZE(lt9211_rx_dig_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211_rx_div_reset_seq,
				     ARRAY_SIZE(lt9211_rx_div_reset_seq));
	if (ret)
		return ret;

	usleep_range(10000, 15000);

	return regmap_multi_reg_write(ctx->regmap, lt9211_rx_div_clear_seq,
				      ARRAY_SIZE(lt9211_rx_div_clear_seq));
}

static int lt9211_autodetect_rx(struct lt9211 *ctx,
				const struct drm_display_mode *mode)
{
	u16 width, height;
	u32 byteclk;
	u8 buf[5];
	u8 format;
	u8 bc[3];
	int ret;

	/* Measure ByteClock frequency. */
	ret = regmap_write(ctx->regmap, 0x8600, 0x01);
	if (ret)
		return ret;

	/* Give the chip time to lock onto RX stream. */
	msleep(100);

	/* Read the ByteClock frequency from the chip. */
	ret = regmap_bulk_read(ctx->regmap, 0x8608, bc, sizeof(bc));
	if (ret)
		return ret;

	/* RX ByteClock in kHz */
	byteclk = ((bc[0] & 0xf) << 16) | (bc[1] << 8) | bc[2];

	/* Width/Height/Format Auto-detection */
	ret = regmap_bulk_read(ctx->regmap, 0xd082, buf, sizeof(buf));
	if (ret)
		return ret;

	width = (buf[0] << 8) | buf[1];
	height = (buf[3] << 8) | buf[4];
	format = buf[2] & 0xf;

	if (format == 0x3) {		/* YUV422 16bit */
		width /= 2;
	} else if (format == 0xa) {	/* RGB888 24bit */
		width /= 3;
	} else {
		dev_err(ctx->dev, "Unsupported DSI pixel format 0x%01x\n",
			format);
		return -EINVAL;
	}

	if (width != mode->hdisplay) {
		dev_err(ctx->dev,
			"RX: Detected DSI width (%d) does not match mode hdisplay (%d)\n",
			width, mode->hdisplay);
		return -EINVAL;
	}

	if (height != mode->vdisplay) {
		dev_err(ctx->dev,
			"RX: Detected DSI height (%d) does not match mode vdisplay (%d)\n",
			height, mode->vdisplay);
		return -EINVAL;
	}

	dev_dbg(ctx->dev, "RX: %dx%d format=0x%01x byteclock=%d kHz\n",
		width, height, format, byteclk);

	return 0;
}

static int lt9211_configure_timing(struct lt9211 *ctx,
				   const struct drm_display_mode *mode)
{
	const struct reg_sequence lt9211_timing[] = {
		{ 0xd00d, (mode->vtotal >> 8) & 0xff },
		{ 0xd00e, mode->vtotal & 0xff },
		{ 0xd00f, (mode->vdisplay >> 8) & 0xff },
		{ 0xd010, mode->vdisplay & 0xff },
		{ 0xd011, (mode->htotal >> 8) & 0xff },
		{ 0xd012, mode->htotal & 0xff },
		{ 0xd013, (mode->hdisplay >> 8) & 0xff },
		{ 0xd014, mode->hdisplay & 0xff },
		{ 0xd015, (mode->vsync_end - mode->vsync_start) & 0xff },
		{ 0xd016, (mode->hsync_end - mode->hsync_start) & 0xff },
		{ 0xd017, ((mode->vsync_start - mode->vdisplay) >> 8) & 0xff },
		{ 0xd018, (mode->vsync_start - mode->vdisplay) & 0xff },
		{ 0xd019, ((mode->hsync_start - mode->hdisplay) >> 8) & 0xff },
		{ 0xd01a, (mode->hsync_start - mode->hdisplay) & 0xff },
	};

	return regmap_multi_reg_write(ctx->regmap, lt9211_timing,
				      ARRAY_SIZE(lt9211_timing));
}

static int lt9211_configure_plls(struct lt9211 *ctx,
				 const struct drm_display_mode *mode)
{
	const struct reg_sequence lt9211_pcr_seq[] = {
		{ 0xd026, 0x17 },
		{ 0xd027, 0xc3 },
		{ 0xd02d, 0x30 },
		{ 0xd031, 0x10 },
		{ 0xd023, 0x20 },
		{ 0xd038, 0x02 },
		{ 0xd039, 0x10 },
		{ 0xd03a, 0x20 },
		{ 0xd03b, 0x60 },
		{ 0xd03f, 0x04 },
		{ 0xd040, 0x08 },
		{ 0xd041, 0x10 },
		{ 0x810b, 0xee },
		{ 0x810b, 0xfe },
	};

	unsigned int pval;
	int ret;

	/* DeSSC PLL reference clock is 25 MHz XTal. */
	ret = regmap_write(ctx->regmap, 0x822d, 0x48);
	if (ret)
		return ret;

	if (mode->clock < 44000) {
		ret = regmap_write(ctx->regmap, 0x8235, 0x83);
	} else if (mode->clock < 88000) {
		ret = regmap_write(ctx->regmap, 0x8235, 0x82);
	} else if (mode->clock < 176000) {
		ret = regmap_write(ctx->regmap, 0x8235, 0x81);
	} else {
		dev_err(ctx->dev,
			"Unsupported mode clock (%d kHz) above 176 MHz.\n",
			mode->clock);
		return -EINVAL;
	}

	if (ret)
		return ret;

	/* Wait for the DeSSC PLL to stabilize. */
	msleep(100);

	ret = regmap_multi_reg_write(ctx->regmap, lt9211_pcr_seq,
				     ARRAY_SIZE(lt9211_pcr_seq));
	if (ret)
		return ret;

	/* PCR stability test takes seconds. */
	ret = regmap_read_poll_timeout(ctx->regmap, 0xd087, pval, pval & 0x8,
				       20000, 10000000);
	if (ret)
		dev_err(ctx->dev, "PCR unstable, ret=%i\n", ret);

	return ret;
}

static int lt9211_configure_tx(struct lt9211 *ctx, bool jeida,
			       bool bpp24, bool de)
{
	const struct reg_sequence system_lt9211_tx_phy_seq[] = {
		/* DPI output disable */
		{ 0x8262, 0x00 },
		/* BIT(7) is LVDS dual-port */
		{ 0x823b, 0x38 | (ctx->lvds_dual_link ? BIT(7) : 0) },
		{ 0x823e, 0x92 },
		{ 0x823f, 0x48 },
		{ 0x8240, 0x31 },
		{ 0x8243, 0x80 },
		{ 0x8244, 0x00 },
		{ 0x8245, 0x00 },
		{ 0x8249, 0x00 },
		{ 0x824a, 0x01 },
		{ 0x824e, 0x00 },
		{ 0x824f, 0x00 },
		{ 0x8250, 0x00 },
		{ 0x8253, 0x00 },
		{ 0x8254, 0x01 },
		/* LVDS channel order, Odd:Even 0x10..A:B, 0x40..B:A */
		{ 0x8646, ctx->lvds_dual_link_even_odd_swap ? 0x40 : 0x10 },
		{ 0x8120, 0x7b },
		{ 0x816b, 0xff },
	};

	const struct reg_sequence system_lt9211_tx_dig_seq[] = {
		{ 0x8559, 0x40 | (jeida ? BIT(7) : 0) |
			  (de ? BIT(5) : 0) | (bpp24 ? BIT(4) : 0) },
		{ 0x855a, 0xaa },
		{ 0x855b, 0xaa },
		{ 0x855c, ctx->lvds_dual_link ? BIT(0) : 0 },
		{ 0x85a1, 0x77 },
		{ 0x8640, 0x40 },
		{ 0x8641, 0x34 },
		{ 0x8642, 0x10 },
		{ 0x8643, 0x23 },
		{ 0x8644, 0x41 },
		{ 0x8645, 0x02 },
	};

	const struct reg_sequence system_lt9211_tx_pll_seq[] = {
		/* TX PLL power down */
		{ 0x8236, 0x01 },
		{ 0x8237, ctx->lvds_dual_link ? 0x2a : 0x29 },
		{ 0x8238, 0x06 },
		{ 0x8239, 0x30 },
		{ 0x823a, 0x8e },
		{ 0x8737, 0x14 },
		{ 0x8713, 0x00 },
		{ 0x8713, 0x80 },
	};

	unsigned int pval;
	int ret;

	ret = regmap_multi_reg_write(ctx->regmap, system_lt9211_tx_phy_seq,
				     ARRAY_SIZE(system_lt9211_tx_phy_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, system_lt9211_tx_dig_seq,
				     ARRAY_SIZE(system_lt9211_tx_dig_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, system_lt9211_tx_pll_seq,
				     ARRAY_SIZE(system_lt9211_tx_pll_seq));
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(ctx->regmap, 0x871f, pval, pval & 0x80,
				       10000, 1000000);
	if (ret) {
		dev_err(ctx->dev, "TX PLL unstable, ret=%i\n", ret);
		return ret;
	}

	ret = regmap_read_poll_timeout(ctx->regmap, 0x8720, pval, pval & 0x80,
				       10000, 1000000);
	if (ret) {
		dev_err(ctx->dev, "TX PLL unstable, ret=%i\n", ret);
		return ret;
	}

	return 0;
}

static void lt9211_atomic_enable(struct drm_bridge *bridge,
				 struct drm_atomic_state *state)
{
	struct lt9211 *ctx = bridge_to_lt9211(bridge);
	const struct drm_bridge_state *bridge_state;
	const struct drm_crtc_state *crtc_state;
	const struct drm_display_mode *mode;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	bool lvds_format_24bpp;
	bool lvds_format_jeida;
	u32 bus_flags;
	int ret;

	ret = regulator_enable(ctx->vccio);
	if (ret) {
		dev_err(ctx->dev, "Failed to enable vccio: %d\n", ret);
		return;
	}

	/* Deassert reset */
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(20000, 21000);	/* Very long post-reset delay. */

	/* Get the LVDS format from the bridge state. */
	bridge_state = drm_atomic_get_new_bridge_state(state, bridge);
	bus_flags = bridge_state->output_bus_cfg.flags;

	switch (bridge_state->output_bus_cfg.format) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		lvds_format_24bpp = false;
		lvds_format_jeida = true;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
		lvds_format_24bpp = true;
		lvds_format_jeida = true;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		lvds_format_24bpp = true;
		lvds_format_jeida = false;
		break;
	default:
		/*
		 * Some bridges still don't set the correct
		 * LVDS bus pixel format, use SPWG24 default
		 * format until those are fixed.
		 */
		lvds_format_24bpp = true;
		lvds_format_jeida = false;
		dev_warn(ctx->dev,
			"Unsupported LVDS bus format 0x%04x, please check output bridge driver. Falling back to SPWG24.\n",
			bridge_state->output_bus_cfg.format);
		break;
	}

	/*
	 * Retrieve the CRTC adjusted mode. This requires a little dance to go
	 * from the bridge to the encoder, to the connector and to the CRTC.
	 */
	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);
	crtc = drm_atomic_get_new_connector_state(state, connector)->crtc;
	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	mode = &crtc_state->adjusted_mode;
	ret = lt9211_read_chipid(ctx);
	if (ret)
		return;

	if (ctx->chip_type == LT9211C && ctx->wq) {
		drm_mode_copy(&ctx->mode, mode);
		/* LT9211C must enable after mipi clock enable */
		queue_delayed_work(ctx->wq, &ctx->lt9211_dw,
				   msecs_to_jiffies(100));
		dev_dbg(ctx->dev, "LT9211C enabled.\n");
		return;
	}
		ret = lt9211_system_init(ctx);
		if (ret)
			return;

		ret = lt9211_configure_rx(ctx);
		if (ret)
			return;

		ret = lt9211_autodetect_rx(ctx, mode);
		if (ret)
			return;

		ret = lt9211_configure_timing(ctx, mode);
		if (ret)
			return;

		ret = lt9211_configure_plls(ctx, mode);
		if (ret)
			return;

	ret = lt9211_configure_tx(ctx, lvds_format_jeida, lvds_format_24bpp,
					  bus_flags & DRM_BUS_FLAG_DE_HIGH);
		if (ret)
			return;

	dev_dbg(ctx->dev, "LT9211 enabled.\n");
}

static void lt9211_atomic_disable(struct drm_bridge *bridge,
				  struct drm_atomic_state *state)
{
	struct lt9211 *ctx = bridge_to_lt9211(bridge);
	int ret;

	/*
	 * Put the chip in reset, pull nRST line low,
	 * and assure lengthy 10ms reset low timing.
	 */
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);	/* Very long reset duration. */

	ret = regulator_disable(ctx->vccio);
	if (ret)
		dev_err(ctx->dev, "Failed to disable vccio: %d\n", ret);

	regcache_mark_dirty(ctx->regmap);
}

static enum drm_mode_status
lt9211_mode_valid(struct drm_bridge *bridge,
		  const struct drm_display_info *info,
		  const struct drm_display_mode *mode)
{
	/* LVDS output clock range 25..176 MHz */
	if (mode->clock < 25000)
		return MODE_CLOCK_LOW;
	if (mode->clock > 176000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

#define MAX_INPUT_SEL_FORMATS	1

static u32 *
lt9211_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
				 struct drm_bridge_state *bridge_state,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state,
				 u32 output_fmt,
				 unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(MAX_INPUT_SEL_FORMATS, sizeof(*input_fmts),
			     GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	/* This is the DSI-end bus format */
	input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
	*num_input_fmts = 1;

	return input_fmts;
}

static const struct drm_bridge_funcs lt9211_funcs = {
	.attach			= lt9211_attach,
	.mode_valid		= lt9211_mode_valid,
	.atomic_enable		= lt9211_atomic_enable,
	.atomic_disable		= lt9211_atomic_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_get_input_bus_fmts = lt9211_atomic_get_input_bus_fmts,
	.atomic_reset		= drm_atomic_helper_bridge_reset,
};

static int lt9211_parse_dt(struct lt9211 *ctx)
{
	struct device_node *port2, *port3;
	struct drm_bridge *panel_bridge;
	struct device *dev = ctx->dev;
	struct drm_panel *panel;
	int dual_link;
	int ret;

	ctx->vccio = devm_regulator_get(dev, "vccio");
	if (IS_ERR(ctx->vccio))
		return dev_err_probe(dev, PTR_ERR(ctx->vccio),
				     "Failed to get supply 'vccio'\n");

	ctx->lvds_dual_link = false;
	ctx->lvds_dual_link_even_odd_swap = false;

	port2 = of_graph_get_port_by_id(dev->of_node, 2);
	port3 = of_graph_get_port_by_id(dev->of_node, 3);
	dual_link = drm_of_lvds_get_dual_link_pixel_order(port2, port3);
	of_node_put(port2);
	of_node_put(port3);

	if (dual_link == DRM_LVDS_DUAL_LINK_ODD_EVEN_PIXELS) {
		ctx->lvds_dual_link = true;
		/* Odd pixels to LVDS Channel A, even pixels to B */
		ctx->lvds_dual_link_even_odd_swap = false;
	} else if (dual_link == DRM_LVDS_DUAL_LINK_EVEN_ODD_PIXELS) {
		ctx->lvds_dual_link = true;
		/* Even pixels to LVDS Channel A, odd pixels to B */
		ctx->lvds_dual_link_even_odd_swap = true;
	}

	ret = drm_of_find_panel_or_bridge(dev->of_node, 2, 0, &panel, &panel_bridge);
	if (ret < 0)
		return ret;
	if (panel) {
		panel_bridge = devm_drm_panel_bridge_add(dev, panel);
		if (IS_ERR(panel_bridge))
			return PTR_ERR(panel_bridge);
	}

	ctx->panel_bridge = panel_bridge;

	return 0;
}

static int lt9211_host_attach(struct lt9211 *ctx)
{
	struct device *dev = ctx->dev;
	struct device_node *host_node;
	struct device_node *endpoint;
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	int dsi_lanes;
	int ret;

	/* Check if the compatible string matches lt9211c */

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 0, -1);
	dsi_lanes = drm_of_get_data_lanes_count(endpoint, 1, 4);
	host_node = of_graph_get_remote_port_parent(endpoint);
	host = of_find_mipi_dsi_host_by_node(host_node);
	of_node_put(host_node);
	of_node_put(endpoint);

	if (!host)
		return -EPROBE_DEFER;

	if (dsi_lanes < 0)
		return dsi_lanes;

	if (ctx->chip_type == LT9211C) {
		const struct mipi_dsi_device_info info = {
			.type = "lt9211c",
			.channel = 0,
			.node = NULL,
		};
		dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	} else {
		const struct mipi_dsi_device_info info = {
			.type = "lt9211",
			.channel = 0,
			.node = NULL,
		};
		dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	}

	if (IS_ERR(dsi))
		return dev_err_probe(dev, PTR_ERR(dsi),
				     "failed to create dsi device\n");

	ctx->dsi = dsi;

	dsi->lanes = dsi_lanes;
	dsi->format = MIPI_DSI_FMT_RGB888;

	if (ctx->chip_type == LT9211C) {
		dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM;
	} else {
		dsi->mode_flags =
			MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO_NO_HSA |
			MIPI_DSI_MODE_VIDEO_NO_HFP | MIPI_DSI_MODE_VIDEO_NO_HBP |
			MIPI_DSI_MODE_NO_EOT_PACKET;
	}

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host: %d\n", ret);
		return ret;
	}

	return 0;
}

static int lt9211_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct lt9211 *ctx;
	int ret;

	ctx = devm_drm_bridge_alloc(dev, struct lt9211, bridge, &lt9211_funcs);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->dev = dev;
	ctx->chip_type = LT9211;

	/*
	 * Put the chip in reset, pull nRST line low,
	 * and assure lengthy 10ms reset low timing.
	 */
	ctx->reset_gpio = devm_gpiod_get_optional(ctx->dev, "reset",
						  GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return PTR_ERR(ctx->reset_gpio);

	usleep_range(10000, 11000);	/* Very long reset duration. */

	ret = lt9211_parse_dt(ctx);

	if (ret)
		return ret;

	if (of_device_is_compatible(dev->of_node, "lontium,lt9211c")) {
		ctx->chip_type = LT9211C;
		ctx->regmap =
			devm_regmap_init_i2c(client, &lt9211c_regmap_config);
	} else {
		ctx->chip_type = LT9211;
		ctx->regmap =
			devm_regmap_init_i2c(client, &lt9211_regmap_config);
	}
	if (IS_ERR(ctx->regmap))
		return PTR_ERR(ctx->regmap);

	/* Initialize LT9211C-specific fields */
	ctx->wq = create_workqueue("lt9211_work");
	if (!ctx->wq)
		return -ENOMEM;

	INIT_DELAYED_WORK(&ctx->lt9211_dw, lt9211_delayed_work_func);

	dev_set_drvdata(dev, ctx);
	i2c_set_clientdata(client, ctx);

	ctx->bridge.of_node = dev->of_node;
	drm_bridge_add(&ctx->bridge);

	ret = lt9211_host_attach(ctx);
	if (ret)
		drm_bridge_remove(&ctx->bridge);

	return ret;
}

static void lt9211_remove(struct i2c_client *client)
{
	struct lt9211 *ctx = i2c_get_clientdata(client);

	if (ctx->wq)
		destroy_workqueue(ctx->wq);

	drm_bridge_remove(&ctx->bridge);
}

static int lt9211c_configure_rx(struct lt9211 *ctx)
{
	unsigned int pval;

	const struct reg_sequence lt9211c_rx_phy_seq[] = {
		{ REG_DSI_LANE, REG_DSI_LANE_COUNT(ctx->dsi->lanes) },
		{ 0x8201, 0x11 },
		{ 0x8218, 0x48 },
		{ 0x8201, 0x91 },
		{ 0x8202, 0x00 },
		{ 0x8203, 0xee },
		{ 0x8209, 0x21 },
		{ 0x8204, 0x44 },
		{ 0x8205, 0xc4 },
		{ 0x8206, 0x44 },
		{ 0x8213, 0x0c },

		{ 0xd001, 0x00 },
		{ 0xd002, 0x0e },
		{ 0xd005, 0x00 },
		{ 0xd00a, 0x59 },
		{ 0xd00b, 0x20 },
	};

	const struct reg_sequence lt9211c_rx_phy_reset_seq[] = {
		{ 0x8109, 0xde },
		{ 0x8109, 0xdf },
	};

	const struct reg_sequence lt9211c_rx_clk_sel_seq[] = {
		{ 0x85e9, 0x88 },
		{ 0x8180, 0x51 },
		{ 0x8181, 0x10 },
		{ 0x8632, 0x03 },
	};

	const struct reg_sequence lt9211c_rx_input_sel_seq[] = {
		{ 0xd004, 0x00 },
		{ 0xd021, 0x46 },
	};

	const struct reg_sequence lt9211c_rx_dig_seq[] = {
		{ 0x853f, 0x08 },
		{ 0x8540, 0x04 },
		{ 0x8541, 0x03 },
		{ 0x8542, 0x02 },
		{ 0x8543, 0x01 },
		{ 0x8545, 0x04 },
		{ 0x8546, 0x03 },
		{ 0x8547, 0x02 },
		{ 0x8548, 0x01 },
		{ 0x8544, 0x00 },
		{ 0x8549, 0x00 },
	};

	int ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_rx_phy_seq,
				     ARRAY_SIZE(lt9211c_rx_phy_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_rx_phy_reset_seq,
				     ARRAY_SIZE(lt9211c_rx_phy_reset_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_rx_clk_sel_seq,
				     ARRAY_SIZE(lt9211c_rx_clk_sel_seq));
	if (ret)
		return ret;

	ret = regmap_read(ctx->regmap, 0x8180, &pval);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, 0x8180, ((pval & 0xfc) | 0x03));
	if (ret)
		return ret;

	ret = regmap_read(ctx->regmap, 0x8680, &pval);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, 0x863f, (pval & 0xf8));
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, 0x863f, 0x05);
	if (ret)
		return ret;

	ret = regmap_read(ctx->regmap, 0x8530, &pval);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, 0x8530, ((pval & 0xf8) | 0x11));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_rx_input_sel_seq,
				     ARRAY_SIZE(lt9211c_rx_input_sel_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_rx_dig_seq,
				     ARRAY_SIZE(lt9211c_rx_dig_seq));
	if (ret)
		return ret;

	/* Give the chip time to lock onto RX stream. */
	msleep(100);

	return 0;
}

static int lt9211c_autodetect_rx(struct lt9211 *ctx,
				 const struct drm_display_mode *mode)
{
	u16 width, height;
	u8 buf[5];
	u8 format;
	u8 sot[8];
	int ret;

	/* Read the SOT from the chip. */
	ret = regmap_bulk_read(ctx->regmap, 0xd088, sot, sizeof(sot));
	if (ret)
		return ret;

	dev_dbg(ctx->dev, "Sot Num = 0x%02x, 0x%02x, 0x%02x, 0x%02x\n", sot[0],
		sot[2], sot[4], sot[6]);

	dev_dbg(ctx->dev, "Sot Data = 0x%02x, 0x%02x, 0x%02x, 0x%02x\n", sot[1],
		sot[3], sot[5], sot[7]);
	/* HS Settle Set */
	if ((sot[0] > 0x10) && (sot[0] < 0x50))
		regmap_write(ctx->regmap, 0xd002, sot[0] - 5);
	else
		regmap_write(ctx->regmap, 0xd002, 0x08);

	/* Width/Height/Format Auto-detection */
	ret = regmap_bulk_read(ctx->regmap, 0xd082, buf, sizeof(buf));
	if (ret)
		return ret;

	width = (buf[0] << 8) | buf[1];
	height = (buf[3] << 8) | buf[4];
	format = buf[2] & 0xf;

	if (format == 0x3) { /* YUV422 16bit */
		width /= 2;
	} else if (format == 0xa) { /* RGB888 24bit */
		width /= 3;
	} else {
		dev_err(ctx->dev, "Unsupported DSI format 0x%01x\n", format);
		return -EINVAL;
	}

	if (width != mode->hdisplay) {
		dev_err(ctx->dev,
			"RX: Detected DSI width (%d) does not match mode hdisplay (%d)\n",
			width, mode->hdisplay);
		return -EINVAL;
	}

	if (height != mode->vdisplay) {
		dev_err(ctx->dev,
			"RX: Detected DSI height (%d) does not match mode vdisplay (%d)\n",
			height, mode->vdisplay);
		return -EINVAL;
	}

	dev_dbg(ctx->dev, "RX: %dx%d format=0x%01x\n", width, height, format);
	return 0;
}

static int lt9211c_configure_timing(struct lt9211 *ctx,
				    const struct drm_display_mode *mode)
{
	const struct reg_sequence lt9211c_timing[] = {
		{ 0xd00d, (mode->vtotal >> 8) & 0xff },
		{ 0xd00e, mode->vtotal & 0xff },
		{ 0xd00f, (mode->vdisplay >> 8) & 0xff },
		{ 0xd010, mode->vdisplay & 0xff },
		{ 0xd011, (mode->htotal >> 8) & 0xff },
		{ 0xd012, mode->htotal & 0xff },
		{ 0xd013, (mode->hdisplay >> 8) & 0xff },
		{ 0xd014, mode->hdisplay & 0xff },
		{ 0xd015, (mode->vsync_end - mode->vsync_start) & 0xff },
		{ 0xd04c, ((mode->hsync_end - mode->hsync_start) >> 8) & 0xff },
		{ 0xd016, (mode->hsync_end - mode->hsync_start) & 0xff },
		{ 0xd017, ((mode->vsync_start - mode->vdisplay) >> 8) & 0xff },
		{ 0xd018, (mode->vsync_start - mode->vdisplay) & 0xff },
		{ 0xd019, ((mode->hsync_start - mode->hdisplay) >> 8) & 0xff },
		{ 0xd01a, (mode->hsync_start - mode->hdisplay) & 0xff },
	};

	return regmap_multi_reg_write(ctx->regmap, lt9211c_timing,
				      ARRAY_SIZE(lt9211c_timing));
}

static int lt9211c_configure_plls(struct lt9211 *ctx,
				  const struct drm_display_mode *mode)
{
	const struct reg_sequence lt9211c_dessc_pll_reset[] = {
		{ 0x8103, 0xfe, 2000 },
		{ 0x8103, 0xff, 0 },
	};

	const struct reg_sequence lt9211c_pcr_cali_seq[] = {
		{ 0xd00a, 0x5f },
		{ 0xd01e, 0x51 },
		{ 0xd023, 0x80 },
		{ 0xd024, 0x70 },
		{ 0xd025, 0x80 },
		{ 0xd02a, 0x10 },
		{ 0xd021, 0x4f },
		{ 0xd022, 0xf0 },
		{ 0xd038, 0x04 },
		{ 0xd039, 0x08 },
		{ 0xd03a, 0x10 },
		{ 0xd03b, 0x20 },
		{ 0xd03f, 0x04 },
		{ 0xd040, 0x08 },
		{ 0xd041, 0x10 },
		{ 0xd042, 0x20 },
		{ 0xd02b, 0xA0 },
	};

	const struct reg_sequence lt9211c_pcr_reset_seq[] = {
		{ 0xd009, 0xdb },
		{ 0xd009, 0xdf },
		{ 0xd008, 0x80 },
		{ 0xd008, 0x00 },
	};

	unsigned int pval;
	int ret;
	u8 div;
	u32 pcr_m;
	u32 pcr_k;
	u32 pcr_up;
	u32 pcr_down;

	/* DeSSC PLL reference clock is 25 MHz XTal. */
	ret = regmap_write(ctx->regmap, 0x8226, 0x20);
	if (ret)
		return ret;

	/* Prediv = 0 */
	ret = regmap_write(ctx->regmap, 0x8227, 0x40);
	if (ret)
		return ret;

	if (mode->clock < 22000) {
		ret = regmap_write(ctx->regmap, 0x822f, 0x07);
		ret |= regmap_write(ctx->regmap, 0x822c, 0x01);
		div = 16;
	} else if (mode->clock < 44000) {
		ret = regmap_write(ctx->regmap, 0x822f, 0x07);
		div = 16;
	} else if (mode->clock < 88000) {
		ret = regmap_write(ctx->regmap, 0x822f, 0x06);
		div = 8;
	} else if (mode->clock < 176000) {
		ret = regmap_write(ctx->regmap, 0x822f, 0x05);
		div = 4;
	} else {
		ret = regmap_write(ctx->regmap, 0x822f, 0x04);
		div = 2;
	}

	if (ret)
		return ret;

	pcr_m = (mode->clock * div) / 25;
	pcr_k = pcr_m % 1000;
	pcr_m /= 1000;

	pcr_up = pcr_m + 1;
	pcr_down = pcr_m - 1;

	pcr_k <<= 14;

	ret = regmap_write(ctx->regmap, 0xd008, 0x00);
	if (ret < 0)
		return ret;

	/* 0xd026: pcr_m */
	ret = regmap_write(ctx->regmap, 0xd026, (0x80 | (u8)pcr_m) & 0x7f);
	if (ret < 0)
		return ret;

	/* 0xd027 0xd028 0xd029: pcr_k */
	ret = regmap_write(ctx->regmap, 0xd027, (pcr_k >> 16) & 0xff);
	if (ret < 0)
		return ret;

	ret = regmap_write(ctx->regmap, 0xd028, (pcr_k >> 8) & 0xff);
	if (ret < 0)
		return ret;

	ret = regmap_write(ctx->regmap, 0xd029, pcr_k & 0xff);
	if (ret < 0)
		return ret;

	/* 0xd02d: pcr_m overflow limit setting */
	ret = regmap_write(ctx->regmap, 0xd02d, pcr_up);
	if (ret < 0)
		return ret;

	/* 0xd031: pcr_m underflow limit setting */
	ret = regmap_write(ctx->regmap, 0xd031, pcr_down);
	if (ret < 0)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_dessc_pll_reset,
				     ARRAY_SIZE(lt9211c_dessc_pll_reset));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_pcr_cali_seq,
				     ARRAY_SIZE(lt9211c_pcr_cali_seq));
	if (ret)
		return ret;

	if (mode->clock < 44000) {
		ret = regmap_write(ctx->regmap, 0xd00c, 0x60);
		ret |= regmap_write(ctx->regmap, 0xd01b, 0x00);
		ret |= regmap_write(ctx->regmap, 0xd01c, 0x60);
	} else {
		ret = regmap_write(ctx->regmap, 0xd00c, 0x40);
		ret |= regmap_write(ctx->regmap, 0xd01b, 0x00);
		ret |= regmap_write(ctx->regmap, 0xd01c, 0x40);
	}
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_pcr_reset_seq,
				     ARRAY_SIZE(lt9211c_pcr_reset_seq));
	if (ret)
		return ret;

	/* PCR stability test takes seconds. */
	ret = regmap_read_poll_timeout(ctx->regmap, 0xd087, pval,
				       ((pval & 0x18) == 0x18), 20000, 3000000);
	if (ret)
		dev_err(ctx->dev, "PCR unstable, ret=%i\n", ret);

	ret = regmap_write(ctx->regmap, 0x8180, 0x51);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, 0x863f, 0x00);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, 0x863f, 0x01);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(ctx->regmap, 0x8640, pval,
				       ((pval & 0x01) == 0x01), 50000, 250000);
	if (ret)
		dev_err(ctx->dev, "Video check not stable, ret=%i\n", ret);

	return ret;
}

static int lt9211c_configure_tx(struct lt9211 *ctx,
				const struct drm_display_mode *mode)
{
	const struct reg_sequence lt9211c_tx_phy_off_seq[] = {
		{ 0x8236, 0x00 },
		{ 0x8237, 0x00 },
		{ 0x8108, 0x6f },
		{ 0x8103, 0xbf },
	};

	const struct reg_sequence lt9211c_tx_phy_seq[] = {
		{ 0x8236, 0x03 },
		{ 0x8237, 0x44 },
		{ 0x8238, 0x14 },
		{ 0x8239, 0x31 },
		{ 0x823a, 0xc8 },
		{ 0x823b, 0x00 },
		{ 0x823c, 0x0f },
		{ 0x8246, 0x40 },
		{ 0x8247, 0x40 },
		{ 0x8248, 0x40 },
		{ 0x8249, 0x40 },
		{ 0x824a, 0x40 },
		{ 0x824b, 0x40 },
		{ 0x824c, 0x40 },
		{ 0x824d, 0x40 },
		{ 0x824e, 0x40 },
		{ 0x824f, 0x40 },
		{ 0x8250, 0x40 },
		{ 0x8251, 0x40 },
	};

	const struct reg_sequence lt9211c_tx_mltx_reset[] = {
		{ 0x8103, 0xbf },
		{ 0x8103, 0xff },
	};

	const struct reg_sequence lt9211c_tx_dig_seq[] = {
		{ 0x854a, 0x01 },
		{ 0x854b, 0x00 },
		{ 0x854c, 0x10 },
		{ 0x854d, 0x20 },
		{ 0x854e, 0x50 },
		{ 0x854f, 0x30 },
		{ 0x8550, 0x46 },
		{ 0x8551, 0x10 },
		{ 0x8552, 0x20 },
		{ 0x8553, 0x50 },
		{ 0x8554, 0x30 },
		{ 0x8555, 0x00 },
		{ 0x8556, 0x20 },

		{ 0x8568, 0x00 },
		{ 0x856e, 0x10 | (ctx->de ? BIT(6) : 0) },
		{ 0x856f, 0x81 | (ctx->jeida ? BIT(6) : 0) |
				  (ctx->lvds_dual_link ? BIT(4) : 0) |
				  (ctx->bpp24 ? BIT(2) : 0) },
	};

	const struct reg_sequence lt9211c_tx_ssc_seq[] = {
		{ 0x8234, 0x00 },
		{ 0x856e, 0x10 },
		{ 0x8181, 0x15 },
		{ 0x871e, 0x00 },
		{ 0x8717, 0x02 },
		{ 0x8718, 0x04 },
		{ 0x8719, 0xd4 },
		{ 0x871A, 0x00 },
		{ 0x871B, 0x12 },
		{ 0x871C, 0x00 },
		{ 0x871D, 0x24 },
		{ 0x871F, 0x1c },
		{ 0x8720, 0x00 },
		{ 0x8721, 0x00 },
		{ 0x871e, 0x02 },
	};

	const struct reg_sequence lt9211c_tx_pll_reset_seq[] = {
		{ 0x810c, 0xfe, 2000 },
		{ 0x810c, 0xff, 0 },
	};

	const struct reg_sequence lt9211c_tx_sw_reset_seq[] = {
		{ 0x8108, 0x6f, 2000 },
		{ 0x8108, 0x7f, 0 },
	};

	unsigned int pval;
	int ret;
	u32 phy_clk;
	u8 pixclk_div;
	u8 pre_div;
	u8 div_set;
	u8 sericlk_div;
	u8 val;

	dev_info(ctx->dev,
		 "dual_link=%d,even_odd_swap=%d,bpp24=%d,jeida=%d,de=%d\n",
		 ctx->lvds_dual_link, ctx->lvds_dual_link_even_odd_swap,
		 ctx->bpp24, ctx->jeida, ctx->de);

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_tx_phy_off_seq,
				     ARRAY_SIZE(lt9211c_tx_phy_off_seq));
	if (ret)
		return ret;

	ret = regmap_read(ctx->regmap, 0x8530, &pval);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, 0x8530, ((pval & 0x3f) | 0x40));
	if (ret)
		return ret;

	/* [7]0:txpll normal work; txpll ref clk sel pix clk */
	ret = regmap_write(ctx->regmap, 0x8230, 0x00);
	if (ret)
		return ret;

	if (ctx->lvds_dual_link)
		phy_clk = (u32)(mode->clock * 7 / 2);
	else
		phy_clk = (u32)(mode->clock * 7);

	/* 0x8231: prediv sel */
	if (mode->clock < 20000) {
		val = 0x28;
		pre_div = 1;
	} else if (mode->clock < 40000) {
		val = 0x28;
		pre_div = 1;
	} else if (mode->clock < 80000) {
		val = 0x29;
		pre_div = 2;
	} else if (mode->clock < 160000) {
		val = 0x2a;
		pre_div = 4;
	} else if (mode->clock < 320000) {
		val = 0x2b;
		pre_div = 8;
	} else {
		val = 0x2f;
		pre_div = 16;
	}
	ret = regmap_write(ctx->regmap, 0x8231, val);
	if (ret < 0)
		return ret;

	/* 0x8232: serickdiv sel */
	if (phy_clk < 80000) {
		val = 0x32;
		sericlk_div = 16;
	} else if (phy_clk < 160000) {
		val = 0x22;
		sericlk_div = 8;
	} else if (phy_clk < 320000) {
		val = 0x12;
		sericlk_div = 4;
	} else if (phy_clk < 640000) {
		val = 0x02;
		sericlk_div = 2;
	} else {
		val = 0x42;
		sericlk_div = 1;
	}
	ret = regmap_write(ctx->regmap, 0x8232, val);
	if (ret < 0)
		return ret;

	/* 0x8233: pix_mux sel & pix_div sel
	 * To avoid floating point operations, The pixclk_div is enlarged by 10 times
	 */
	if (mode->clock > 150000) {
		val = 0x04;
		pixclk_div = 35;
	} else {
		pixclk_div =
			(u8)((phy_clk * sericlk_div * 10) / (mode->clock * 7));
		if (pixclk_div <= 10)
			val = 0x00;
		else if (pixclk_div <= 20)
			val = 0x01;
		else if (pixclk_div <= 40)
			val = 0x02;
		else
			val = 0x03;
	}
	ret = regmap_write(ctx->regmap, 0x8233, val);
	if (ret < 0)
		return ret;

	ret = regmap_write(ctx->regmap, 0x8234, 0x01);
	if (ret < 0)
		return ret;

	/* 0x8235: div set */
	div_set = (u8)(phy_clk * sericlk_div / mode->clock / pre_div);
	ret = regmap_write(ctx->regmap, 0x8235, div_set);
	if (ret < 0)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_tx_ssc_seq,
				     ARRAY_SIZE(lt9211c_tx_ssc_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_tx_pll_reset_seq,
				     ARRAY_SIZE(lt9211c_tx_pll_reset_seq));
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(ctx->regmap, 0x8739, pval, pval & 0x04,
				       10000, 1000000);
	if (ret) {
		dev_err(ctx->dev, "TX PLL unstable, ret=%i\n", ret);
		return ret;
	}

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_tx_phy_seq,
				     ARRAY_SIZE(lt9211c_tx_phy_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_tx_mltx_reset,
				     ARRAY_SIZE(lt9211c_tx_mltx_reset));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_tx_dig_seq,
				     ARRAY_SIZE(lt9211c_tx_dig_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211c_tx_sw_reset_seq,
				     ARRAY_SIZE(lt9211c_tx_sw_reset_seq));
	if (ret)
		return ret;

	return 0;
}

static void lt9211_delayed_work_func(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct lt9211 *ctx = container_of(dw, struct lt9211, lt9211_dw);
	int ret;
	const struct drm_display_mode *mode = &ctx->mode;

	/* For LT9211C */
	if (ctx->chip_type != LT9211C) {
		dev_err(ctx->dev, "LT9211: Delayed work called for non-LT9211C chip\n");
		return;
	}
		ret = lt9211c_configure_rx(ctx);
		if (ret)
			return;

		ret = lt9211c_autodetect_rx(ctx, mode);
		if (ret)
			return;

		ret = lt9211c_configure_timing(ctx, mode);
		if (ret)
			return;

		ret = lt9211c_configure_plls(ctx, mode);
		if (ret)
			return;

		ret = lt9211c_configure_tx(ctx, mode);
		if (ret)
			return;

}

static const struct i2c_device_id lt9211_id[] = {
	{ "lontium,lt9211" },
	{ "lontium,lt9211c" },
	{},
};
MODULE_DEVICE_TABLE(i2c, lt9211_id);

static const struct of_device_id lt9211_match_table[] = {
	{ .compatible = "lontium,lt9211" },
	{ .compatible = "lontium,lt9211c" },
	{},
};
MODULE_DEVICE_TABLE(of, lt9211_match_table);

static struct i2c_driver lt9211_driver = {
	.probe = lt9211_probe,
	.remove = lt9211_remove,
	.id_table = lt9211_id,
	.driver = {
		.name = "lt9211",
		.of_match_table = lt9211_match_table,
	},
};
module_i2c_driver(lt9211_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Lontium LT9211 DSI/LVDS/DPI bridge driver");
MODULE_LICENSE("GPL");
