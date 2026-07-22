// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

/*
 * DRM panel driver for DLC0697 1080x1920 60Hz video-mode DSI panel
 *
 * Derived from downstream Qualcomm panel DT data.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct dlc0697 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;

	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;

	struct pinctrl *pinctrl;
	struct pinctrl_state *state_active;
	struct pinctrl_state *state_suspend;
};

static const struct regulator_bulk_data dlc0697_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "bias" },
};

static inline struct dlc0697 *to_dlc0697(struct drm_panel *panel)
{
	return container_of(panel, struct dlc0697, panel);
}

static const struct drm_display_mode dlc0697_mode = {
	.clock = 131911,

	.hdisplay    = 1080,
	.hsync_start = 1080 + 18,
	.hsync_end   = 1080 + 18 + 2,
	.htotal      = 1080 + 18 + 2 + 16,

	.vdisplay    = 1920,
	.vsync_start = 1920 + 26,
	.vsync_end   = 1920 + 26 + 4,
	.vtotal      = 1920 + 26 + 4 + 20,

	.width_mm  = 0,
	.height_mm = 0,
};

static void dlc0697_reset(struct dlc0697 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int dlc0697_on(struct dlc0697 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_soft_reset_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x78, 0x07, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5e, 0x09, 0x99);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x3f, 0xff);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	if (dsi_ctx.accum_err)
		dev_err(&ctx->dsi->dev, "panel on sequence failed: %d\n", dsi_ctx.accum_err);

	return dsi_ctx.accum_err;
}

static int dlc0697_off(struct dlc0697 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	if (dsi_ctx.accum_err)
		dev_err(&ctx->dsi->dev, "panel off sequence failed: %d\n", dsi_ctx.accum_err);

	return dsi_ctx.accum_err;
}

static int dlc0697_enable(struct drm_panel *panel)
{
	struct dlc0697 *ctx = to_dlc0697(panel);

	if (ctx->enable_gpio)
		gpiod_set_value_cansleep(ctx->enable_gpio, 1);

	return 0;
}

static int dlc0697_disable(struct drm_panel *panel)
{
	struct dlc0697 *ctx = to_dlc0697(panel);

	if (ctx->enable_gpio)
		gpiod_set_value_cansleep(ctx->enable_gpio, 0);

	return 0;
}

static int dlc0697_prepare(struct drm_panel *panel)
{
	struct dlc0697 *ctx = to_dlc0697(panel);
	int ret;

	if (ctx->pinctrl && ctx->state_active) {
		ret = pinctrl_select_state(ctx->pinctrl, ctx->state_active);
		if (ret < 0)
			return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(dlc0697_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(ctx->panel.dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	/* qcom,supply-post-on-sleep = <20> */
	msleep(20);

	dlc0697_reset(ctx);

	ret = dlc0697_on(ctx);
	if (ret < 0)
		goto err;

	return 0;

err:
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	if (ctx->pinctrl && ctx->state_suspend)
		pinctrl_select_state(ctx->pinctrl, ctx->state_suspend);

	regulator_bulk_disable(ARRAY_SIZE(dlc0697_supplies), ctx->supplies);

	return ret;
}

static int dlc0697_unprepare(struct drm_panel *panel)
{
	struct dlc0697 *ctx = to_dlc0697(panel);

	dlc0697_off(ctx);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	if (ctx->pinctrl && ctx->state_suspend)
		pinctrl_select_state(ctx->pinctrl, ctx->state_suspend);

	regulator_bulk_disable(ARRAY_SIZE(dlc0697_supplies), ctx->supplies);

	return 0;
}

static int dlc0697_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &dlc0697_mode);
}

static const struct drm_panel_funcs dlc0697_panel_funcs = {
	.prepare   = dlc0697_prepare,
	.unprepare = dlc0697_unprepare,
	.enable    = dlc0697_enable,
	.disable   = dlc0697_disable,
	.get_modes = dlc0697_get_modes,
};

static int dlc0697_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	return 0;
}

static const struct backlight_ops dlc0697_bl_ops = {
	.update_status = dlc0697_bl_update_status,
};

static struct backlight_device *dlc0697_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type           = BACKLIGHT_RAW,
		.brightness     = 4095,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &dlc0697_bl_ops, &props);
}

static int dlc0697_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct dlc0697 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct dlc0697, panel,
				   &dlc0697_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(dlc0697_supplies),
					    dlc0697_supplies, &ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "failed to get reset gpio\n");

	ctx->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->enable_gpio),
				     "failed to get enable gpio\n");

	ctx->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(ctx->pinctrl)) {
		if (PTR_ERR(ctx->pinctrl) == -ENODEV) {
			ctx->pinctrl = NULL;
		} else {
			return dev_err_probe(dev, PTR_ERR(ctx->pinctrl),
					     "failed to get pinctrl\n");
		}
	}

	if (ctx->pinctrl) {
		ctx->state_active = pinctrl_lookup_state(ctx->pinctrl, "default");
		if (IS_ERR(ctx->state_active))
			ctx->state_active = NULL;

		ctx->state_suspend = pinctrl_lookup_state(ctx->pinctrl, "sleep");
		if (IS_ERR(ctx->state_suspend))
			ctx->state_suspend = NULL;
	}

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes      = 4;
	dsi->format     = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = dlc0697_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "failed to attach dsi\n");
	}

	return 0;
}

static void dlc0697_remove(struct mipi_dsi_device *dsi)
{
	drm_panel_remove(mipi_dsi_get_drvdata(dsi));
}

static const struct of_device_id dlc0697_of_match[] = {
	{ .compatible = "dlc,dlc0697" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dlc0697_of_match);

static struct mipi_dsi_driver dlc0697_driver = {
	.probe  = dlc0697_probe,
	.remove = dlc0697_remove,
	.driver = {
		.name           = "panel-dlc0697",
		.of_match_table = dlc0697_of_match,
	},
};
module_mipi_dsi_driver(dlc0697_driver);

MODULE_AUTHOR("Arpit Saini <arpit.saini@oss.qualcomm.com>");
MODULE_DESCRIPTION("DLC0697 1080x1920 video mode DSI panel");
MODULE_LICENSE("GPL");
