// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All rights reserved.
 * Novatek NT37802 AMOLED DSI command-mode panel.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <video/mipi_display.h>

struct nt37802_cmd {
	u8 len;
	u8 data[32];
};

#define NT37802_CMD(...) \
	{ .len = sizeof((u8[]){ __VA_ARGS__ }), .data = { __VA_ARGS__ } }

struct novatek_nt37802 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
	bool prepared;
	bool enabled;
};

enum nt37802_supply {
	NT37802_SUPPLY_VDDIO,
	NT37802_SUPPLY_VDD,
	NT37802_SUPPLY_VCI,
	NT37802_SUPPLY_EXVDD,
};

static const struct regulator_bulk_data novatek_nt37802_supplies[] = {
	[NT37802_SUPPLY_VDDIO] = { .supply = "vddio", .init_load_uA = 154000 },
	[NT37802_SUPPLY_VDD] = { .supply = "vdd", .init_load_uA = 220000 },
	[NT37802_SUPPLY_VCI] = { .supply = "vci", .init_load_uA = 10000 },
	[NT37802_SUPPLY_EXVDD] = { .supply = "exvdd" },
};

static int novatek_nt37802_set_supply_voltages(struct novatek_nt37802 *ctx)
{
	int ret;

	ret = regulator_set_voltage(ctx->supplies[NT37802_SUPPLY_VDDIO].consumer,
				    1800000, 2000000);
	if (ret)
		return ret;

	ret = regulator_set_voltage(ctx->supplies[NT37802_SUPPLY_VDD].consumer,
				    1100000, 1950000);
	if (ret)
		return ret;

	return regulator_set_voltage(ctx->supplies[NT37802_SUPPLY_VCI].consumer,
				     3000000, 3544000);
}

static int novatek_nt37802_enable_supplies(struct novatek_nt37802 *ctx)
{
	int ret;

	ret = regulator_enable(ctx->supplies[NT37802_SUPPLY_VDDIO].consumer);
	if (ret)
		return ret;

	msleep(20);

	ret = regulator_enable(ctx->supplies[NT37802_SUPPLY_VDD].consumer);
	if (ret)
		goto disable_vddio;

	ret = regulator_enable(ctx->supplies[NT37802_SUPPLY_VCI].consumer);
	if (ret)
		goto disable_vdd;

	ret = regulator_enable(ctx->supplies[NT37802_SUPPLY_EXVDD].consumer);
	if (ret)
		goto disable_vci;

	msleep(20);

	return 0;

disable_vci:
	regulator_disable(ctx->supplies[NT37802_SUPPLY_VCI].consumer);
disable_vdd:
	regulator_disable(ctx->supplies[NT37802_SUPPLY_VDD].consumer);
disable_vddio:
	regulator_disable(ctx->supplies[NT37802_SUPPLY_VDDIO].consumer);

	return ret;
}

static void novatek_nt37802_disable_supplies(struct novatek_nt37802 *ctx)
{
	regulator_disable(ctx->supplies[NT37802_SUPPLY_EXVDD].consumer);
	regulator_disable(ctx->supplies[NT37802_SUPPLY_VCI].consumer);
	regulator_disable(ctx->supplies[NT37802_SUPPLY_VDD].consumer);
	regulator_disable(ctx->supplies[NT37802_SUPPLY_VDDIO].consumer);
}

static inline struct novatek_nt37802 *
to_novatek_nt37802(struct drm_panel *panel)
{
	return container_of(panel, struct novatek_nt37802, panel);
}

static const struct nt37802_cmd nt37802_init_cmds[] = {
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x08),
	NT37802_CMD(0xc1, 0x8e, 0xff, 0xee, 0xff, 0xff, 0xee, 0xff,
		    0xff, 0xee, 0xff, 0xff, 0xee, 0xff, 0xff, 0xee,
		    0xff, 0xff, 0xff, 0x7f, 0xff, 0x0e, 0xff),
	NT37802_CMD(0x6f, 0x20),
	NT37802_CMD(0xc1, 0x3f, 0xfd, 0xff),
	NT37802_CMD(0x6f, 0x23),
	NT37802_CMD(0xc1, 0x0e, 0xff, 0xee, 0xff, 0xff, 0xee, 0xff,
		    0xff, 0xee, 0xff, 0xff, 0xee, 0xff, 0xff, 0xee,
		    0xff, 0xff, 0xdf, 0x7f, 0x11),
	NT37802_CMD(0x2a, 0x00, 0x00, 0x05, 0x9f),
	NT37802_CMD(0x2b, 0x00, 0x00, 0x0c, 0x7f),
	NT37802_CMD(0x90, 0x03, 0x43),
	NT37802_CMD(0x91, 0xab, 0xa8, 0x00, 0x14, 0xc2, 0x00, 0x02,
		    0x68, 0x02, 0x2e, 0x00, 0x0a, 0x05, 0x0e, 0x03, 0xd1, 0x10, 0xf0),
	NT37802_CMD(0x35, 0x00),
	NT37802_CMD(0x51, 0x0f, 0xff),
	NT37802_CMD(0x6f, 0x04),
	NT37802_CMD(0x51, 0x0f, 0xff),
	NT37802_CMD(0x53, 0x20),
	NT37802_CMD(0x6f, 0x01),
	NT37802_CMD(0x8b, 0x00),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x04),
	NT37802_CMD(0xc0, 0x20),
	NT37802_CMD(0x8c, 0x00, 0x00, 0x11),
	NT37802_CMD(0xff, 0xaa, 0x55, 0xa5, 0x81),
	NT37802_CMD(0x6f, 0x21),
	NT37802_CMD(0xf5, 0x03),
	NT37802_CMD(0x6f, 0x43),
	NT37802_CMD(0xf5, 0x01, 0x02, 0x01, 0x04, 0x00, 0x01, 0x02, 0x00,
		    0x00, 0x00, 0x43, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x10,
		    0x05, 0x00, 0x11, 0x31, 0x12, 0x11),
	NT37802_CMD(0x6f, 0x1d),
	NT37802_CMD(0xfb, 0x64, 0x07, 0x80),
	NT37802_CMD(0x6f, 0x07),
	NT37802_CMD(0xfc, 0x04),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01),
	NT37802_CMD(0x6f, 0x02),
	NT37802_CMD(0xd6, 0x03),
	NT37802_CMD(0xff, 0xaa, 0x55, 0xa5, 0x84),
	NT37802_CMD(0x6f, 0x9a),
	NT37802_CMD(0xf3, 0x0f),
	NT37802_CMD(0x6f, 0x02),
	NT37802_CMD(0x2f, 0x32),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x03),
	NT37802_CMD(0xc8, 0x01, 0xff, 0xfb),
	NT37802_CMD(0xff, 0xaa, 0x55, 0xa5, 0x80),
	NT37802_CMD(0x6f, 0x22),
	NT37802_CMD(0xfe, 0x06),
	NT37802_CMD(0x6d, 0x00),
	NT37802_CMD(0x68, 0x20),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00),
	NT37802_CMD(0x6f, 0x31),
	NT37802_CMD(0xc0, 0x01, 0x00),
	NT37802_CMD(0x6f, 0x35),
	NT37802_CMD(0xc0, 0x23, 0x20),
	NT37802_CMD(0x6f, 0x01),
	NT37802_CMD(0x5f, 0x40),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00),
	NT37802_CMD(0x6f, 0x78),
	NT37802_CMD(0xb0, 0x11, 0x00, 0x2a, 0x00, 0xc2, 0x01, 0xda, 0x03, 0x7c,
		    0x05, 0xb1, 0x08, 0x80, 0x0b, 0xee, 0x0f, 0xff,
		    0x05, 0x55, 0x05, 0x56, 0x05, 0x55),
	NT37802_CMD(0x6f, 0x6f),
	NT37802_CMD(0xb0, 0x02, 0xd0, 0x05, 0xa0, 0x02, 0xd0),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x03),
	NT37802_CMD(0x6f, 0x04),
	NT37802_CMD(0xc7, 0xfd),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00),
	NT37802_CMD(0xc7, 0x33),
	NT37802_CMD(0xa3, 0x83, 0x01, 0x9b, 0x01, 0x9b, 0x02, 0x00, 0xc0, 0x09, 0x09, 0x00),
	NT37802_CMD(0x6f, 0x53),
	NT37802_CMD(0xa3, 0x02, 0x00, 0xc0, 0x09, 0x09, 0x55),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00),
	NT37802_CMD(0xb5, 0x80, 0x02, 0x38, 0xc8, 0x00, 0x00, 0xff, 0x0c,
		    0x60, 0x00, 0x00, 0x00, 0x0c, 0x60, 0x00, 0x00),
	NT37802_CMD(0x6f, 0x10),
	NT37802_CMD(0xb5, 0x00, 0x60, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00,
		    0x11, 0x2b, 0x00, 0x00, 0x00, 0x11, 0x2b, 0x00),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00),
	NT37802_CMD(0x6f, 0x08),
	NT37802_CMD(0xd5, 0x08),
	NT37802_CMD(0xd5, 0x4e, 0x10),
	NT37802_CMD(0x6f, 0x02),
	NT37802_CMD(0xd5, 0x01),
	NT37802_CMD(0x6f, 0xd9),
	NT37802_CMD(0xd5, 0x7f, 0x7f),
	NT37802_CMD(0x6f, 0x4e),
	NT37802_CMD(0xd5, 0x02),
	NT37802_CMD(0x6f, 0x4f),
	NT37802_CMD(0xd5, 0x05, 0x07),
	NT37802_CMD(0x6f, 0x52),
	NT37802_CMD(0xd5, 0x11, 0x02),
	NT37802_CMD(0x6f, 0x55),
	NT37802_CMD(0xd5, 0x01),
	NT37802_CMD(0x6f, 0x56),
	NT37802_CMD(0xd5, 0x07),
	NT37802_CMD(0x6f, 0x59),
	NT37802_CMD(0xd5, 0x70),
	NT37802_CMD(0x6f, 0x5c),
	NT37802_CMD(0xd5, 0x03),
	NT37802_CMD(0x6f, 0x5d),
	NT37802_CMD(0xd5, 0x02, 0x03, 0x07),
	NT37802_CMD(0x6f, 0x63),
	NT37802_CMD(0xd5, 0x08, 0x60, 0x07),
	NT37802_CMD(0x6f, 0x69),
	NT37802_CMD(0xd5, 0x01),
	NT37802_CMD(0x6f, 0x6a),
	NT37802_CMD(0xd5, 0x07),
	NT37802_CMD(0x6f, 0x70),
	NT37802_CMD(0xd5, 0x16),
	NT37802_CMD(0x6f, 0xb8),
	NT37802_CMD(0xd5, 0x02),
	NT37802_CMD(0x6f, 0xb9),
	NT37802_CMD(0xd5, 0x1c, 0x1f),
	NT37802_CMD(0x6f, 0xbc),
	NT37802_CMD(0xd5, 0x5d, 0x02),
	NT37802_CMD(0x6f, 0xbf),
	NT37802_CMD(0xd5, 0x01),
	NT37802_CMD(0x6f, 0xc0),
	NT37802_CMD(0xd5, 0x1f),
	NT37802_CMD(0x6f, 0xc3),
	NT37802_CMD(0xd5, 0x00),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01),
	NT37802_CMD(0x6f, 0x04),
	NT37802_CMD(0xb2, 0x30),
	NT37802_CMD(0x6f, 0x25),
	NT37802_CMD(0xd8, 0x30),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00),
	NT37802_CMD(0x6f, 0xd1),
	NT37802_CMD(0xd5, 0x13),
	NT37802_CMD(0x6f, 0xd2),
	NT37802_CMD(0xd5, 0x56),
	NT37802_CMD(0x6f, 0x03),
	NT37802_CMD(0xb5, 0x35),
	NT37802_CMD(0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01),
	NT37802_CMD(0x6f, 0x04),
	NT37802_CMD(0xb2, 0x20),
};

static void novatek_nt37802_set_reset(struct novatek_nt37802 *ctx, int value)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, value);
}

static void novatek_nt37802_reset(struct novatek_nt37802 *ctx)
{
	novatek_nt37802_set_reset(ctx, 0);
	usleep_range(10000, 11000);
	novatek_nt37802_set_reset(ctx, 1);
	usleep_range(10000, 11000);
	novatek_nt37802_set_reset(ctx, 0);
	usleep_range(10000, 11000);
	/* qcom,platform-reset-gpio-release-delay */
	msleep(120);
}

static int novatek_nt37802_write_init(struct novatek_nt37802 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(nt37802_init_cmds); i++)
		mipi_dsi_dcs_write_buffer_multi(&dsi_ctx,
						nt37802_init_cmds[i].data,
						nt37802_init_cmds[i].len);

	return dsi_ctx.accum_err;
}

static int novatek_nt37802_prepare(struct drm_panel *panel)
{
	struct novatek_nt37802 *ctx = to_novatek_nt37802(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = novatek_nt37802_enable_supplies(ctx);
	if (ret)
		return ret;

	novatek_nt37802_reset(ctx);
	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = novatek_nt37802_write_init(ctx);
	if (ret) {
		novatek_nt37802_set_reset(ctx, 1);
		novatek_nt37802_disable_supplies(ctx);
		return ret;
	}

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);
	mipi_dsi_picture_parameter_set_multi(&dsi_ctx, &pps);
	mipi_dsi_compression_mode_multi(&dsi_ctx, true);
	if (dsi_ctx.accum_err) {
		novatek_nt37802_set_reset(ctx, 1);
		novatek_nt37802_disable_supplies(ctx);
		return dsi_ctx.accum_err;
	}

	ctx->prepared = true;
	return 0;
}

static int novatek_nt37802_enable(struct drm_panel *panel)
{
	struct novatek_nt37802 *ctx = to_novatek_nt37802(panel);

	if (!ctx->prepared)
		return -EPERM;

	if (ctx->enabled)
		return 0;

	ctx->enabled = true;
	return 0;
}

static int novatek_nt37802_disable(struct drm_panel *panel)
{
	struct novatek_nt37802 *ctx = to_novatek_nt37802(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	if (!ctx->enabled)
		return 0;

	/* The off sequence sends display-off in HS mode, then waits 10 ms. */
	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 10);

	ctx->enabled = false;
	return dsi_ctx.accum_err;
}

static int novatek_nt37802_unprepare(struct drm_panel *panel)
{
	struct novatek_nt37802 *ctx = to_novatek_nt37802(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	if (!ctx->prepared)
		return 0;

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	/* Assert the active-low reset after the downstream sleep delay. */
	novatek_nt37802_set_reset(ctx, 1);
	ctx->prepared = false;
	novatek_nt37802_disable_supplies(ctx);

	return dsi_ctx.accum_err;
}

static const struct drm_display_mode novatek_nt37802_mode = {
	/* (1440 + 32 + 16 + 32) * (3200 + 16 + 2 + 48) * 120 Hz */
	.clock = (1440 + 32 + 16 + 32) * (3200 + 16 + 2 + 48) * 120 / 1000,
	.hdisplay = 1440,
	.hsync_start = 1440 + 32,
	.hsync_end = 1440 + 32 + 16,
	.htotal = 1440 + 32 + 16 + 32,
	.vdisplay = 3200,
	.vsync_start = 3200 + 16,
	.vsync_end = 3200 + 16 + 2,
	.vtotal = 3200 + 16 + 2 + 48,
	.width_mm = 74,
	.height_mm = 156,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int novatek_nt37802_get_modes(struct drm_panel *panel,
				     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector,
						    &novatek_nt37802_mode);
}

static const struct drm_panel_funcs novatek_nt37802_panel_funcs = {
	.prepare = novatek_nt37802_prepare,
	.enable = novatek_nt37802_enable,
	.disable = novatek_nt37802_disable,
	.unprepare = novatek_nt37802_unprepare,
	.get_modes = novatek_nt37802_get_modes,
};

static int novatek_nt37802_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);

	return mipi_dsi_dcs_set_display_brightness_large(dsi,
							backlight_get_brightness(bl));
}

static const struct backlight_ops novatek_nt37802_bl_ops = {
	.update_status = novatek_nt37802_bl_update_status,
};

static struct backlight_device *
novatek_nt37802_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 4095,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &novatek_nt37802_bl_ops, &props);
}

static void novatek_nt37802_init_dsc(struct novatek_nt37802 *ctx)
{
	struct drm_dsc_config *dsc = &ctx->dsc;

	dsc->dsc_version_major = 1;
	dsc->dsc_version_minor = 2;
	dsc->bits_per_component = 8;
	dsc->bits_per_pixel = 8 << 4;
	dsc->block_pred_enable = true;
	dsc->slice_width = 720;
	dsc->slice_height = 20;
	dsc->slice_count = 2;
}

static int novatek_nt37802_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct novatek_nt37802 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct novatek_nt37802, panel,
				   &novatek_nt37802_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(novatek_nt37802_supplies),
					    novatek_nt37802_supplies,
					    &ctx->supplies);
	if (ret)
		return ret;

	ret = novatek_nt37802_set_supply_voltages(ctx);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set regulator voltages\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset GPIO\n");

	novatek_nt37802_init_dsc(ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = novatek_nt37802_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	dsi->dsc = &ctx->dsc;
	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void novatek_nt37802_remove(struct mipi_dsi_device *dsi)
{
	struct novatek_nt37802 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id novatek_nt37802_of_match[] = {
	{ .compatible = "novatek,nt37802" },
	{}
};
MODULE_DEVICE_TABLE(of, novatek_nt37802_of_match);

static struct mipi_dsi_driver novatek_nt37802_driver = {
	.probe = novatek_nt37802_probe,
	.remove = novatek_nt37802_remove,
	.driver = {
		.name = "panel-novatek-nt37802",
		.of_match_table = novatek_nt37802_of_match,
	},
};
module_mipi_dsi_driver(novatek_nt37802_driver);

MODULE_AUTHOR("Jayant Shekhar <jshekhar@oss.qualcomm.com>");
MODULE_DESCRIPTION("Panel driver for the Novatek NT37802 AMOLED DSI panel");
MODULE_LICENSE("GPL");
