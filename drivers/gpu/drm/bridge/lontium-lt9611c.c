// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Lontium Semiconductor, Inc.
 */

#include <linux/crc8.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/display/drm_hdmi_audio_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>
#include <sound/hdmi-codec.h>

#define FW_SIZE (64 * 1024)
#define LT_PAGE_SIZE 256
#define FW_FILE  "Lontium/lt9611c_fw.bin"
#define LT9611C_CRC_POLYNOMIAL 0x31
#define LT9611C_PAGE_CONTROL 0xff

enum lt9611_chip_type {
	CHIP_LT9611C = 0,
	CHIP_LT9611EX,
	CHIP_LT9611UXD,
};

enum lt9611c_ports {
	PORT_SWAP_A = 0,
	PORT_SWAP_B,
	PORT_SWAP_AB,
	PORT_MAX,
};

struct lt9611c {
	struct device *dev;
	struct i2c_client *client;
	struct drm_bridge bridge;
	struct regmap *regmap;
	/* Protects all accesses to registers by stopping the on-chip MCU */
	struct mutex ocm_lock;
	struct work_struct work;
	struct device_node *dsi0_node;
	struct device_node *dsi1_node;
	struct mipi_dsi_device *dsi0;
	struct mipi_dsi_device *dsi1;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *hdmi_gpio;
	struct regulator_bulk_data supplies[2];
	int fw_version;
	/* Chip variant: C/EX/UXD */
	enum lt9611_chip_type chip_type;
	 /* HDMI cable connection status */
	bool hdmi_connected;
	/* Selected DSI port configuration */
	int selected_port;
};

DECLARE_CRC8_TABLE(lt9611c_crc8_table);

static const struct regmap_range_cfg lt9611c_ranges[] = {
	{
		.name = "register_range",
		.range_min =  0,
		.range_max = 0xfe9c,
		.selector_reg = LT9611C_PAGE_CONTROL,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 0x100,
	},
};

static const struct regmap_config lt9611c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xfe9c,
	.ranges = lt9611c_ranges,
	.num_ranges = ARRAY_SIZE(lt9611c_ranges),
};

static int lt9611c_read_write_flow(struct lt9611c *lt9611c, u8 *params,
				   unsigned int param_count, u8 *return_buffer,
				   unsigned int return_count)
{
	int ret;
	unsigned int i;
	unsigned int temp;
	unsigned int max_params = 0xe0dd - 0xe0b0 + 1;

	regmap_write(lt9611c->regmap, 0xe0de, 0x01);

	ret = regmap_read_poll_timeout(lt9611c->regmap, 0xe0ae, temp,
				       temp == 0x01, 1000, 400 * 1000);
	if (ret)
		return -ETIMEDOUT;

	for (i = 0; i < param_count && i < max_params; i++)
		regmap_write(lt9611c->regmap, 0xe0b0 + i, params[i]);

	regmap_write(lt9611c->regmap, 0xe0de, 0x02);

	ret = regmap_read_poll_timeout(lt9611c->regmap, 0xe0ae, temp,
				       temp == 0x02, 1000, 400 * 1000);
	if (ret)
		return -ETIMEDOUT;

	return regmap_bulk_read(lt9611c->regmap, 0xe085, return_buffer,
				return_count);
}

static int lt9611c_select_port(struct lt9611c *lt9611c, int port_select)
{
	int ret;
	u8 set_port_swap_cmd_A[6] = {0x57, 0x4d, 0x31, 0x3a, 0x01, 0xc0};
	u8 set_port_swap_cmd_B[6] = {0x57, 0x4d, 0x31, 0x3a, 0x01, 0x40};
	u8 set_port_swap_cmd_AB[6] = {0x57, 0x4d, 0x31, 0x3a, 0x02, 0xd0};
	u8 set_port_swap_ret[5];

	if (!lt9611c)
		return -EINVAL;

	/* MCU must be running (0xe0ee=0x00) for lt9611c_read_write_flow */
	guard(mutex)(&lt9611c->ocm_lock);
	regmap_write(lt9611c->regmap, 0xe0ee, 0x00);

	switch (port_select) {
	case PORT_SWAP_A:
		ret = lt9611c_read_write_flow(lt9611c, set_port_swap_cmd_A,
				6, set_port_swap_ret, 5);
		if (ret < 0 || set_port_swap_ret[4] == 0)
			return ret < 0 ? ret : -EIO;
		break;

	case PORT_SWAP_B:
		ret = lt9611c_read_write_flow(lt9611c, set_port_swap_cmd_B,
				6, set_port_swap_ret, 5);
		if (ret < 0 || set_port_swap_ret[4] == 0)
			return ret < 0 ? ret : -EIO;
		break;

	case PORT_SWAP_AB:
		ret = lt9611c_read_write_flow(lt9611c, set_port_swap_cmd_AB,
				6, set_port_swap_ret, 5);
		if (ret < 0 || set_port_swap_ret[4] == 0)
			return ret < 0 ? ret : -EIO;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void lt9611c_config_parameters(struct lt9611c *lt9611c)
{
	const struct reg_sequence seq_write_paras[] = {
		REG_SEQ0(0xe0ee, 0x01),
		REG_SEQ0(0xe103, 0x3f), /*fifo rst*/
		REG_SEQ0(0xe103, 0xff),
		REG_SEQ0(0xe05e, 0xc1),
		REG_SEQ0(0xe058, 0x00),
		REG_SEQ0(0xe059, 0x50),
		REG_SEQ0(0xe05a, 0x10),
		REG_SEQ0(0xe05a, 0x00),
		REG_SEQ0(0xe058, 0x21),
	};

	regmap_multi_reg_write(lt9611c->regmap, seq_write_paras, ARRAY_SIZE(seq_write_paras));
}

static void lt9611c_wren(struct lt9611c *lt9611c)
{
	regmap_write(lt9611c->regmap, 0xe05a, 0x04);
	regmap_write(lt9611c->regmap, 0xe05a, 0x00);
}

static void lt9611c_wrdi(struct lt9611c *lt9611c)
{
	regmap_write(lt9611c->regmap, 0xe05a, 0x08);
	regmap_write(lt9611c->regmap, 0xe05a, 0x00);
}

static void lt9611c_erase_op(struct lt9611c *lt9611c, u32 addr)
{
	const struct reg_sequence seq_write[] = {
		REG_SEQ0(0xe0ee, 0x01),
		REG_SEQ0(0xe05a, 0x04),
		REG_SEQ0(0xe05a, 0x00),
		REG_SEQ0(0xe05b, (addr >> 16) & 0xff),
		REG_SEQ0(0xe05c, (addr >> 8) & 0xff),
		REG_SEQ0(0xe05d, addr & 0xff),
		REG_SEQ0(0xe05a, 0x01),
		REG_SEQ0(0xe05a, 0x00),
	};

	regmap_multi_reg_write(lt9611c->regmap, seq_write, ARRAY_SIZE(seq_write));
}

static void read_flash_reg_status(struct lt9611c *lt9611c, unsigned int *status)
{
	const struct reg_sequence seq_write[] = {
		REG_SEQ0(0xe103, 0x3f),
		REG_SEQ0(0xe103, 0xff),
		REG_SEQ0(0xe05e, 0x40),
		REG_SEQ0(0xe056, 0x05),
		REG_SEQ0(0xe055, 0x25),
		REG_SEQ0(0xe055, 0x01),
		REG_SEQ0(0xe058, 0x21),
	};

	regmap_multi_reg_write(lt9611c->regmap, seq_write, ARRAY_SIZE(seq_write));

	regmap_read(lt9611c->regmap, 0xe05f, status);
}

static void lt9611c_crc_to_sram(struct lt9611c *lt9611c)
{
	const struct reg_sequence seq_write[] = {
		REG_SEQ0(0xe051, 0x00),
		REG_SEQ0(0xe055, 0xc0),
		REG_SEQ0(0xe055, 0x80),
		REG_SEQ0(0xe05e, 0xc0),
		REG_SEQ0(0xe058, 0x21),
	};

	regmap_multi_reg_write(lt9611c->regmap, seq_write, ARRAY_SIZE(seq_write));
}

static void lt9611c_data_to_sram(struct lt9611c *lt9611c)
{
	const struct reg_sequence seq_write[] = {
		REG_SEQ0(0xe051, 0xff),
		REG_SEQ0(0xe055, 0x80),
		REG_SEQ0(0xe05e, 0xc0),
		REG_SEQ0(0xe058, 0x21),
	};

	regmap_multi_reg_write(lt9611c->regmap, seq_write, ARRAY_SIZE(seq_write));
}

static void lt9611c_sram_to_flash(struct lt9611c *lt9611c, size_t addr)
{
	const struct reg_sequence seq_write[] = {
		REG_SEQ0(0xe05b, (addr >> 16) & 0xff),
		REG_SEQ0(0xe05c, (addr >> 8) & 0xff),
		REG_SEQ0(0xe05d, addr & 0xff),
		REG_SEQ0(0xe05a, 0x30),
		REG_SEQ0(0xe05a, 0x00),
	};

	regmap_multi_reg_write(lt9611c->regmap, seq_write, ARRAY_SIZE(seq_write));
}

static void lt9611c_block_erase(struct lt9611c *lt9611c)
{
	struct device *dev = lt9611c->dev;
	int i;
	unsigned int block_num;
	unsigned int flash_status = 0;
	u32 flash_addr = 0;

	for (block_num = 0; block_num < 2; block_num++) {
		flash_addr = (block_num * 0x008000);
		lt9611c_erase_op(lt9611c, flash_addr);
		msleep(100);
		i = 0;
		while (1) {
			read_flash_reg_status(lt9611c, &flash_status);
			if ((flash_status & 0x01) == 0)
				break;

			if (i > 50)
				break;

			i++;
			msleep(50);
		}
	}

	dev_dbg(dev, "erase flash done.\n");
}

static int lt9611c_write_data(struct lt9611c *lt9611c, const struct firmware *fw, size_t addr)
{
	struct device *dev = lt9611c->dev;
	int ret;
	unsigned int page = 0, num = 0, i = 0;
	size_t size, index;
	const u8 *data;
	u8 value;

	data = fw->data;
	size = fw->size;
	page = (size + LT_PAGE_SIZE - 1) / LT_PAGE_SIZE;
	if (page * LT_PAGE_SIZE > FW_SIZE) {
		dev_err(dev, "firmware size out of range\n");
		return -EINVAL;
	}

	dev_dbg(dev, "%u pages, total size %zu byte\n", page, size);

	for (num = 0; num < page; num++) {
		lt9611c_data_to_sram(lt9611c);

		for (i = 0; i < LT_PAGE_SIZE; i++) {
			index = num * LT_PAGE_SIZE + i;
			value = (index < size) ? data[index] : 0xff;

			ret = regmap_write(lt9611c->regmap, 0xe059, value);
			if (ret < 0) {
				dev_err(dev, "write error at page %u, index %u\n", num, i);
				return ret;
			}
		}

		lt9611c_wren(lt9611c);
		lt9611c_sram_to_flash(lt9611c, addr);

		addr += LT_PAGE_SIZE;
	}

	lt9611c_wrdi(lt9611c);

	return 0;
}

static int lt9611c_write_crc(struct lt9611c *lt9611c, u8 fw_crc, size_t addr)
{
	struct device *dev = lt9611c->dev;
	int ret;

	lt9611c_crc_to_sram(lt9611c);
	ret = regmap_write(lt9611c->regmap, 0xe059, fw_crc);
	if (ret < 0) {
		dev_err(dev, "failed to write crc\n");
		return ret;
	}

	lt9611c_wren(lt9611c);
	lt9611c_sram_to_flash(lt9611c, addr);
	lt9611c_wrdi(lt9611c);

	dev_dbg(dev, "crc 0x%02x written to flash at addr 0x%zx\n", fw_crc, addr);

	return 0;
}

static void lt9611c_reset(struct lt9611c *lt9611c)
{
	gpiod_set_value_cansleep(lt9611c->reset_gpio, 1);
	msleep(20);

	gpiod_set_value_cansleep(lt9611c->reset_gpio, 0);
	msleep(20);

	gpiod_set_value_cansleep(lt9611c->reset_gpio, 1);
	msleep(400);

	dev_dbg(lt9611c->dev, "lt9611c reset");
}

static int lt9611c_upgrade_result(struct lt9611c *lt9611c, u8 fw_crc)
{
	struct device *dev = lt9611c->dev;
	unsigned int crc_result;

	regmap_write(lt9611c->regmap, 0xe0ee, 0x01);
	regmap_read(lt9611c->regmap, 0xe021, &crc_result);

	if (crc_result != fw_crc) {
		dev_err(dev, "lt9611c fw upgrade failed, expected crc=0x%02x, read crc=0x%02x\n",
			fw_crc, crc_result);
		return -1;
	}

	dev_dbg(dev, "lt9611c firmware upgrade success, crc=0x%02x\n", crc_result);
	return 0;
}

static int lt9611c_firmware_upgrade(struct lt9611c *lt9611c)
{
	struct device *dev = lt9611c->dev;
	const struct firmware *fw;
	u8 *buffer;
	size_t total_size = FW_SIZE - 1;
	u8 fw_crc;
	int ret;

	/* 1. load firmware */
	ret = request_firmware(&fw, FW_FILE, dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to load '%s'\n", FW_FILE);

	/* 2. check size */
	if (fw->size > total_size) {
		dev_err(dev, "firmware too large (%zu > %zu)\n", fw->size, total_size);
		ret = -EINVAL;
		goto out_release_fw;
	}
	dev_dbg(dev, "firmware size: %zu bytes\n", fw->size);

	/* 3. calculate crc8 */
	buffer = kzalloc(total_size, GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto out_release_fw;
	}

	memset(buffer, 0xff, total_size);
	memcpy(buffer, fw->data, fw->size);

	fw_crc = crc8(lt9611c_crc8_table, buffer, total_size, 0);
	kfree(buffer);

	dev_dbg(dev, "firmware crc: 0x%02x\n", fw_crc);
	dev_dbg(dev, "starting firmware upgrade, size: %zu bytes\n", fw->size);

	/* 4. firmware upgrade */
	lt9611c_config_parameters(lt9611c);
	lt9611c_block_erase(lt9611c);

	ret = lt9611c_write_data(lt9611c, fw, 0);
	if (ret < 0) {
		dev_err(dev, "failed to write firmware data\n");
		goto out_release_fw;
	}

	ret = lt9611c_write_crc(lt9611c, fw_crc, FW_SIZE - 1);
	if (ret < 0) {
		dev_err(dev, "failed to write firmware crc\n");
		goto out_release_fw;
	}

	/* 5. check upgrade of result */
	lt9611c_reset(lt9611c);
	ret = lt9611c_upgrade_result(lt9611c, fw_crc);

out_release_fw:
	release_firmware(fw);
	return ret;
}

static struct lt9611c *bridge_to_lt9611c(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9611c, bridge);
}

/*read only*/
static const struct lt9611c *bridge_to_lt9611c_const(const struct drm_bridge *bridge)
{
	return container_of(bridge, const struct lt9611c, bridge);
}

static void lt9611c_lock(struct lt9611c *lt9611c)
{
	mutex_lock(&lt9611c->ocm_lock);
	regmap_write(lt9611c->regmap, 0xe0ee, 0x01);
}

static void lt9611c_unlock(struct lt9611c *lt9611c)
{
	regmap_write(lt9611c->regmap, 0xe0ee, 0x00);
	mutex_unlock(&lt9611c->ocm_lock);
}

static irqreturn_t lt9611c_irq_thread_handler(int irq, void *dev_id)
{
	struct lt9611c *lt9611c = dev_id;
	struct device *dev = lt9611c->dev;
	int ret;
	unsigned int irq_status;
	u8 cmd[5] = {0x52, 0x48, 0x31, 0x3a, 0x00};
	u8 data[5];

	mutex_lock(&lt9611c->ocm_lock);

	/* Ensure MCU is running for HPD status query */
	regmap_write(lt9611c->regmap, 0xe0ee, 0x00);

	ret = regmap_read(lt9611c->regmap, 0xe084, &irq_status);
	if (ret) {
		dev_err(dev, "failed to read irq status: %d\n", ret);
		mutex_unlock(&lt9611c->ocm_lock);
		return IRQ_HANDLED;
	}

	if (!(irq_status & BIT(0))) {
		mutex_unlock(&lt9611c->ocm_lock);
		return IRQ_HANDLED;
	}

	ret = lt9611c_read_write_flow(lt9611c, cmd, ARRAY_SIZE(cmd), data, ARRAY_SIZE(data));
	if (ret) {
		dev_err(dev, "failed to read HPD status\n");
	} else {
		lt9611c->hdmi_connected = (data[4] == 0x02);
		dev_dbg(dev, "HDMI %s\n", lt9611c->hdmi_connected ? "connected" : "disconnected");
	}

	regmap_write(lt9611c->regmap, 0xe0df, BIT(0));
	usleep_range(10000, 12000);
	regmap_write(lt9611c->regmap, 0xe0df, 0x00);

	mutex_unlock(&lt9611c->ocm_lock);

	schedule_work(&lt9611c->work);

	return IRQ_HANDLED;
}

static void lt9611c_hpd_work(struct work_struct *work)
{
	struct lt9611c *lt9611c = container_of(work, struct lt9611c, work);
	bool connected;

	mutex_lock(&lt9611c->ocm_lock);
	connected = lt9611c->hdmi_connected;
	mutex_unlock(&lt9611c->ocm_lock);

	drm_bridge_hpd_notify(&lt9611c->bridge,
			      connected ? connector_status_connected :
			      connector_status_disconnected);
}

static int lt9611c_regulator_init(struct lt9611c *lt9611c)
{
	struct device *dev = lt9611c->dev;
	int ret;

	lt9611c->supplies[0].supply = "vcc";
	lt9611c->supplies[1].supply = "vdd";

	ret = devm_regulator_bulk_get(dev, 2, lt9611c->supplies);

	return ret;
}

static struct mipi_dsi_device *lt9611c_attach_dsi(struct lt9611c *lt9611c,
						  struct device_node *dsi_node)
{
	const struct mipi_dsi_device_info info = { "lt9611c", 0, NULL };
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	struct device *dev = lt9611c->dev;
	int ret;

	host = of_find_mipi_dsi_host_by_node(dsi_node);
	if (!host)
		return ERR_PTR(dev_err_probe(dev, -EPROBE_DEFER, "failed to find dsi host\n"));

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi))
		return ERR_PTR(dev_err_probe(dev, PTR_ERR(dsi), "failed to create dsi device\n"));

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			 MIPI_DSI_MODE_VIDEO_HSE;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0)
		return ERR_PTR(dev_err_probe(dev, ret, "failed to attach dsi to host\n"));

	return dsi;
}

static int lt9611c_bridge_attach(struct drm_bridge *bridge,
				 struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);

	return drm_bridge_attach(encoder, lt9611c->bridge.next_bridge, bridge, flags);
}

static enum drm_mode_status
lt9611c_hdmi_tmds_char_rate_valid(const struct drm_bridge *bridge,
				  const struct drm_display_mode *mode,
				  unsigned long long tmds_rate)
{
	const struct lt9611c *lt9611c = bridge_to_lt9611c_const(bridge);

	if (lt9611c->chip_type == CHIP_LT9611UXD) {
		if (tmds_rate > 600000000)
			return MODE_CLOCK_HIGH;

	} else {
		if (tmds_rate > 340000000)
			return MODE_CLOCK_HIGH;
	}

	if (tmds_rate < 25000000)
		return MODE_CLOCK_LOW;

	return MODE_OK;
}

static void lt9611c_video_setup(struct lt9611c *lt9611c,
				const struct drm_display_mode *mode)
{
	struct device *dev = lt9611c->dev;
	int ret;
	u32 h_total, hactive, hsync_len, hfront_porch, hback_porch;
	u32 v_total, vactive, vsync_len, vfront_porch, vback_porch;
	u8 timing_set_cmd[26] = {0x57, 0x4d, 0x33, 0x3a};
	u8 return_param[3];
	u8 framerate;
	u8 vic = 0x00;

	guard(mutex)(&lt9611c->ocm_lock);
	h_total = mode->htotal;
	hactive = mode->hdisplay;
	hsync_len = mode->hsync_end - mode->hsync_start;
	hfront_porch = mode->hsync_start - mode->hdisplay;
	hback_porch = mode->htotal - mode->hsync_end;

	v_total = mode->vtotal;
	vactive = mode->vdisplay;
	vsync_len = mode->vsync_end - mode->vsync_start;
	vfront_porch = mode->vsync_start - mode->vdisplay;
	vback_porch = mode->vtotal - mode->vsync_end;
	framerate = drm_mode_vrefresh(mode);
	vic = drm_match_cea_mode(mode);

	dev_dbg(dev, "hactive=%d, vactive=%d\n", hactive, vactive);
	dev_dbg(dev, "framerate=%d\n", framerate);
	dev_dbg(dev, "vic = 0x%02x\n", vic);

	timing_set_cmd[4] = (h_total >> 8) & 0xff;
	timing_set_cmd[5] = h_total & 0xff;
	timing_set_cmd[6] = (hactive >> 8) & 0xff;
	timing_set_cmd[7] = hactive & 0xff;
	timing_set_cmd[8] = (hfront_porch >> 8) & 0xff;
	timing_set_cmd[9] = hfront_porch & 0xff;
	timing_set_cmd[10] = (hsync_len >> 8) & 0xff;
	timing_set_cmd[11] = hsync_len & 0xff;
	timing_set_cmd[12] = (hback_porch >> 8) & 0xff;
	timing_set_cmd[13] = hback_porch & 0xff;
	timing_set_cmd[14] = (v_total >> 8) & 0xff;
	timing_set_cmd[15] = v_total & 0xff;
	timing_set_cmd[16] = (vactive >> 8) & 0xff;
	timing_set_cmd[17] = vactive & 0xFF;
	timing_set_cmd[18] = (vfront_porch >> 8) & 0xff;
	timing_set_cmd[19] = vfront_porch & 0xff;
	timing_set_cmd[20] = (vsync_len >> 8) & 0xff;
	timing_set_cmd[21] = vsync_len & 0xff;
	timing_set_cmd[22] = (vback_porch >> 8) & 0xff;
	timing_set_cmd[23] = vback_porch & 0xff;
	timing_set_cmd[24] = framerate;
	timing_set_cmd[25] = vic;

	ret = lt9611c_read_write_flow(lt9611c,
				      timing_set_cmd, ARRAY_SIZE(timing_set_cmd),
				      return_param, ARRAY_SIZE(return_param));
	if (ret)
		dev_err(dev, "video set failed\n");
}

static void lt9611c_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					     struct drm_atomic_state *state)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	int ret;

	if (lt9611c->hdmi_gpio) {
		gpiod_set_value_cansleep(lt9611c->hdmi_gpio, 1);
		msleep(20);
	}
//	lt9611c_reset(lt9611c);

	/* Reapply port selection after reset */
	if (lt9611c->selected_port >= 0) {
		msleep(200);
		ret = lt9611c_select_port(lt9611c, lt9611c->selected_port);
		if (ret < 0)
			dev_err(lt9611c->dev, "failed to reapply port selection: %d\n", ret);
		msleep(200);
	}
}

static void lt9611c_bridge_atomic_enable(struct drm_bridge *bridge,
					 struct drm_atomic_state *state)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	struct drm_display_mode *mode;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	if (WARN_ON(!connector))
		return;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!conn_state))
		return;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (WARN_ON(!crtc_state))
		return;

	mode = &crtc_state->adjusted_mode;

	lt9611c_video_setup(lt9611c, mode);
}

static void lt9611c_bridge_atomic_post_disable(struct drm_bridge *bridge,
					       struct drm_atomic_state *state)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);

	/* Keep chip active for HPD detection */
	mutex_lock(&lt9611c->ocm_lock);
	regmap_write(lt9611c->regmap, 0xe0ee, 0x00);
	regmap_write(lt9611c->regmap, 0xe0d0, 0x01);
	mutex_unlock(&lt9611c->ocm_lock);
}

static enum drm_connector_status
lt9611c_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	struct device *dev = lt9611c->dev;
	int ret;
	bool connected = false;
	u8 cmd[5] = {0x52, 0x48, 0x31, 0x3a, 0x00};
	u8 data[5];

	mutex_lock(&lt9611c->ocm_lock);

	/* Ensure MCU is running for HPD status query */
	regmap_write(lt9611c->regmap, 0xe0ee, 0x00);

	ret = lt9611c_read_write_flow(lt9611c, cmd, ARRAY_SIZE(cmd), data, ARRAY_SIZE(data));
	if (ret)
		dev_err(dev, "failed to read HPD status (err=%d)\n", ret);
	else
		connected = (data[4] == 0x02);

	lt9611c->hdmi_connected = connected;

	mutex_unlock(&lt9611c->ocm_lock);

	return connected ? connector_status_connected :
				connector_status_disconnected;
}

static int lt9611c_get_edid_block(void *data, u8 *buf,
				  unsigned int block, size_t len)
{
	struct lt9611c *lt9611c = data;
	struct device *dev = lt9611c->dev;
	u8 cmd[5] = {0x52, 0x48, 0x33, 0x3a, 0x00};
	u8 packet[37];
	int ret, i, offset = 0;

	if (len != 128)
		return -EINVAL;
	guard(mutex)(&lt9611c->ocm_lock);

	for (i = 0; i < 4; i++) {
		cmd[4] = block * 4 + i;
		ret = lt9611c_read_write_flow(lt9611c, cmd, ARRAY_SIZE(cmd),
					      packet, ARRAY_SIZE(packet));
		if (ret) {
			dev_err(dev, "Failed to read EDID block %u packet %d\n",
				block, i);
			return ret;
		}
		memcpy(buf + offset, &packet[5], 32);
		offset += 32;
	}

	return 0;
}

static const struct drm_edid *lt9611c_bridge_edid_read(struct drm_bridge *bridge,
						       struct drm_connector *connector)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);

	return drm_edid_read_custom(connector, lt9611c_get_edid_block, lt9611c);
}

static int lt9611c_hdmi_write_avi_infoframe(struct drm_bridge *bridge,
					    const u8 *buffer, size_t len)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	u8 *cmd;
	u8 data[5];
	int ret;

	guard(mutex)(&lt9611c->ocm_lock);

	cmd = kmalloc(5 + len, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd[0] = 0x57;
	cmd[1] = 0x48;
	cmd[2] = 0x35;
	cmd[3] = 0x3a;
	cmd[4] = 0x01;/*write avi*/
	memcpy(cmd + 5, buffer, len);

	ret = lt9611c_read_write_flow(lt9611c, cmd, 5 + len,
				      data, ARRAY_SIZE(data));
	kfree(cmd);

	if (ret < 0) {
		dev_err(lt9611c->dev, "write avi infoframe failed!\n");
		return ret;
	}

	return 0;
}

static int lt9611c_hdmi_clear_avi_infoframe(struct drm_bridge *bridge)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	u8 cmd[5] = {0x57, 0x48, 0x42, 0x3a, 0x01};
	u8 data[5];
	int ret;

	guard(mutex)(&lt9611c->ocm_lock);

	ret = lt9611c_read_write_flow(lt9611c, cmd, ARRAY_SIZE(cmd),
				      data, ARRAY_SIZE(data));

	if (ret < 0) {
		dev_err(lt9611c->dev, "clear avi infoframe failed!\n");
		return ret;
	}

	return 0;
}

static int lt9611c_hdmi_write_hdmi_infoframe(struct drm_bridge *bridge,
					     const u8 *buffer, size_t len)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	u8 *cmd;
	u8 data[5];
	int ret;

	guard(mutex)(&lt9611c->ocm_lock);

	cmd = kmalloc(5 + len, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd[0] = 0x57;
	cmd[1] = 0x48;
	cmd[2] = 0x35;
	cmd[3] = 0x3a;
	cmd[4] = 0x03;/*write hdmi infoframe*/
	memcpy(cmd + 5, buffer, len);

	ret = lt9611c_read_write_flow(lt9611c, cmd, 5 + len,
				      data, ARRAY_SIZE(data));
	kfree(cmd);

	if (ret < 0) {
		dev_err(lt9611c->dev, "write hdmi infoframe failed!\n");
		return ret;
	}

	return 0;
}

static int lt9611c_hdmi_clear_hdmi_infoframe(struct drm_bridge *bridge)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	u8 cmd[5] = {0x57, 0x48, 0x42, 0x3a, 0x03};
	u8 data[5];
	int ret;

	guard(mutex)(&lt9611c->ocm_lock);

	ret = lt9611c_read_write_flow(lt9611c, cmd, ARRAY_SIZE(cmd),
				      data, ARRAY_SIZE(data));

	if (ret < 0) {
		dev_err(lt9611c->dev, "clear hdmi infoframe failed!\n");
		return ret;
	}

	return 0;
}

static int lt9611c_hdmi_write_audio_infoframe(struct drm_bridge *bridge,
					      const u8 *buffer, size_t len)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	u8 *cmd;
	u8 data[5];
	int ret;

	guard(mutex)(&lt9611c->ocm_lock);

	cmd = kmalloc(5 + len, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd[0] = 0x57;
	cmd[1] = 0x48;
	cmd[2] = 0x35;
	cmd[3] = 0x3a;
	cmd[4] = 0x02;/*write audio*/
	memcpy(cmd + 5, buffer, len);

	ret = lt9611c_read_write_flow(lt9611c, cmd, 5 + len,
				      data, ARRAY_SIZE(data));

	kfree(cmd);

	if (ret < 0) {
		dev_err(lt9611c->dev, "write audio infoframe failed!\n");
		return ret;
	}

	return 0;
}

static int lt9611c_hdmi_clear_audio_infoframe(struct drm_bridge *bridge)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	u8 cmd[5] = {0x57, 0x48, 0x42, 0x3a, 0x02};
	u8 data[5];
	int ret;

	guard(mutex)(&lt9611c->ocm_lock);

	ret = lt9611c_read_write_flow(lt9611c, cmd, ARRAY_SIZE(cmd),
				      data, ARRAY_SIZE(data));

	if (ret < 0) {
		dev_err(lt9611c->dev, "clear audio infoframe failed!\n");
		return ret;
	}

	return 0;
}

static int lt9611c_hdmi_audio_prepare(struct drm_bridge *bridge,
				      struct drm_connector *connector,
				      struct hdmi_codec_daifmt *fmt,
				      struct hdmi_codec_params *hparms)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	u8 audio_cmd[6] = {0x57, 0x48, 0x36, 0x3a};
	u8 data[5];
	int ret;

	if (hparms->sample_width == 32)
		return -EINVAL;

	switch (fmt->fmt) {
	case HDMI_I2S:
		audio_cmd[4] = 0x01;
		break;
	case HDMI_SPDIF:
		audio_cmd[4] = 0x02;
		break;
	default:
		return -EINVAL;
	}

	audio_cmd[5] = hparms->channels;
	guard(mutex)(&lt9611c->ocm_lock);

	ret = lt9611c_read_write_flow(lt9611c, audio_cmd, sizeof(audio_cmd),
				      data, sizeof(data));
	if (ret < 0) {
		dev_err(lt9611c->dev, "set audio info failed!\n");
		return ret;
	}

	return drm_atomic_helper_connector_hdmi_update_audio_infoframe(connector,
									&hparms->cea);
}

static void lt9611c_hdmi_audio_shutdown(struct drm_bridge *bridge,
					struct drm_connector *connector)
{
	drm_atomic_helper_connector_hdmi_clear_audio_infoframe(connector);
}

static int lt9611c_hdmi_audio_startup(struct drm_bridge *bridge,
				      struct drm_connector *connector)
{
	return 0;
}

static const struct drm_bridge_funcs lt9611c_bridge_funcs = {
	.attach = lt9611c_bridge_attach,
	.detect = lt9611c_bridge_detect,
	.edid_read = lt9611c_bridge_edid_read,
	.atomic_pre_enable = lt9611c_bridge_atomic_pre_enable,
	.atomic_enable = lt9611c_bridge_atomic_enable,
	.atomic_post_disable = lt9611c_bridge_atomic_post_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,

	.hdmi_tmds_char_rate_valid = lt9611c_hdmi_tmds_char_rate_valid,
	.hdmi_write_avi_infoframe = lt9611c_hdmi_write_avi_infoframe,
	.hdmi_clear_avi_infoframe = lt9611c_hdmi_clear_avi_infoframe,
	.hdmi_write_hdmi_infoframe = lt9611c_hdmi_write_hdmi_infoframe,
	.hdmi_clear_hdmi_infoframe = lt9611c_hdmi_clear_hdmi_infoframe,
	.hdmi_write_audio_infoframe = lt9611c_hdmi_write_audio_infoframe,
	.hdmi_clear_audio_infoframe = lt9611c_hdmi_clear_audio_infoframe,

	.hdmi_audio_startup = lt9611c_hdmi_audio_startup,
	.hdmi_audio_prepare = lt9611c_hdmi_audio_prepare,
	.hdmi_audio_shutdown = lt9611c_hdmi_audio_shutdown,
};

static int lt9611c_parse_dt(struct device *dev,
			    struct lt9611c *lt9611c)
{
	lt9611c->dsi0_node = of_graph_get_remote_node(dev->of_node, 0, -1);
	if (!lt9611c->dsi0_node)
		return dev_err_probe(dev, -ENODEV, "failed to get remote node for primary dsi\n");

	lt9611c->dsi1_node = of_graph_get_remote_node(dev->of_node, 1, -1);

	return drm_of_find_panel_or_bridge(dev->of_node, 2, -1, NULL, &lt9611c->bridge.next_bridge);
}

static int lt9611c_gpio_init(struct lt9611c *lt9611c)
{
	struct device *dev = lt9611c->dev;

	lt9611c->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(lt9611c->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(lt9611c->reset_gpio),
				"failed to acquire reset gpio\n");

	lt9611c->hdmi_gpio = devm_gpiod_get_optional(dev, "hdmi", GPIOD_OUT_LOW);
	if (IS_ERR(lt9611c->hdmi_gpio))
		return dev_err_probe(dev, PTR_ERR(lt9611c->hdmi_gpio),
				"failed to acquire hdmi gpio\n");

	return 0;
}

static int lt9611c_read_version(struct lt9611c *lt9611c)
{
	u8 buf[2];
	int ret;

	ret = regmap_write(lt9611c->regmap, 0xe0ee, 0x01);
	if (ret)
		return ret;

	ret = regmap_bulk_read(lt9611c->regmap, 0xe080, buf, ARRAY_SIZE(buf));
	if (ret)
		return ret;

	return (buf[0] << 8) | buf[1];
}

static int lt9611c_read_chipid(struct lt9611c *lt9611c)
{
	struct device *dev = lt9611c->dev;
	u8 chipid[2];
	int ret;

	ret = regmap_write(lt9611c->regmap, 0xe0ee, 0x01);
	if (ret)
		return ret;

	ret = regmap_bulk_read(lt9611c->regmap, 0xe100, chipid, 2);
	if (ret)
		return ret;

	if (chipid[0] != 0x23 || chipid[1] != 0x06) {
		dev_err(dev, "ChipID: 0x%02x 0x%02x\n", chipid[0], chipid[1]);
		return -ENODEV;
	}

	return 0;
}

static ssize_t lt9611c_firmware_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t len)
{
	struct lt9611c *lt9611c = dev_get_drvdata(dev);
	int ret;

	lt9611c_lock(lt9611c);

	ret = lt9611c_firmware_upgrade(lt9611c);
	if (ret < 0)
		dev_err(dev, "upgrade failure\n");

	lt9611c_unlock(lt9611c);

	return ret < 0 ? ret : len;
}

static ssize_t lt9611c_firmware_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lt9611c *lt9611c = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%04x\n", lt9611c->fw_version);
}

static DEVICE_ATTR_RW(lt9611c_firmware);

static struct attribute *lt9611c_attrs[] = {
	&dev_attr_lt9611c_firmware.attr,
	NULL,
};

static const struct attribute_group lt9611c_attr_group = {
	.attrs = lt9611c_attrs,
};

static const struct attribute_group *lt9611c_attr_groups[] = {
	&lt9611c_attr_group,
	NULL,
};

static int lt9611c_probe(struct i2c_client *client)
{
	struct lt9611c *lt9611c;
	struct device *dev = &client->dev;
	bool fw_updated = false;
	int ret;

	crc8_populate_msb(lt9611c_crc8_table, LT9611C_CRC_POLYNOMIAL);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return dev_err_probe(dev, -ENODEV, "device doesn't support I2C\n");

	lt9611c = devm_drm_bridge_alloc(dev, struct lt9611c, bridge, &lt9611c_bridge_funcs);
	if (IS_ERR(lt9611c))
		return dev_err_probe(dev, PTR_ERR(lt9611c), "drm bridge alloc failed.\n");

	lt9611c->dev = dev;
	lt9611c->client = client;
	lt9611c->chip_type = (enum lt9611_chip_type)(uintptr_t)of_device_get_match_data(dev);
	ret = devm_mutex_init(dev, &lt9611c->ocm_lock);
	if (ret)
		return dev_err_probe(dev, ret, "failed to init mutex\n");

	lt9611c->regmap = devm_regmap_init_i2c(client, &lt9611c_regmap_config);
	if (IS_ERR(lt9611c->regmap))
		return dev_err_probe(dev, PTR_ERR(lt9611c->regmap), "regmap i2c init failed\n");

	ret = lt9611c_parse_dt(dev, lt9611c);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse device tree\n");

	ret = lt9611c_gpio_init(lt9611c);
	if (ret < 0)
		goto err_of_put;

	if (lt9611c->hdmi_gpio) {
		gpiod_set_value_cansleep(lt9611c->hdmi_gpio, 1);
		msleep(20);
	}

	lt9611c_reset(lt9611c);
	msleep(300);

	lt9611c_lock(lt9611c);

	ret = lt9611c_read_chipid(lt9611c);
	if (ret < 0) {
		dev_err(dev, "failed to read chip id.\n");
		lt9611c_unlock(lt9611c);
		goto err_disable_regulators;
	}

retry:
	lt9611c->fw_version = lt9611c_read_version(lt9611c);
	if (lt9611c->fw_version < 0) {
		dev_err(dev, "failed to read fw version\n");
		ret = -EOPNOTSUPP;
		lt9611c_unlock(lt9611c);
		goto err_disable_regulators;

	} else if (lt9611c->fw_version == 0) {
		if (!fw_updated) {
			fw_updated = true;
			ret = lt9611c_firmware_upgrade(lt9611c);
			if (ret < 0) {
				lt9611c_unlock(lt9611c);
				goto err_disable_regulators;
			}
			goto retry;

		} else {
			dev_err(dev, "fw version 0x%04x, update failed\n", lt9611c->fw_version);
			ret = -EOPNOTSUPP;
			lt9611c_unlock(lt9611c);
			goto err_disable_regulators;
		}
	}

	lt9611c_unlock(lt9611c);

	/* Select port B so the chip is configured for the correct DSI input */
	msleep(200);
	ret = lt9611c_select_port(lt9611c, PORT_SWAP_B);
	if (ret < 0) {
		dev_warn(dev, "port B selection failed (%d), HPD may not work\n", ret);
		lt9611c->selected_port = -1;
	} else {
		lt9611c->selected_port = PORT_SWAP_B;
	}
	msleep(200);

	dev_dbg(dev, "current version:0x%04x", lt9611c->fw_version);

	INIT_WORK(&lt9611c->work, lt9611c_hpd_work);

	ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					lt9611c_irq_thread_handler,
					IRQF_TRIGGER_RISING |
					IRQF_ONESHOT |
					IRQF_NO_AUTOEN,
					"lt9611c", lt9611c);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		goto err_disable_regulators;
	}

	lt9611c->bridge.of_node = client->dev.of_node;
	lt9611c->bridge.ops = DRM_BRIDGE_OP_DETECT |
			DRM_BRIDGE_OP_EDID |
			DRM_BRIDGE_OP_HPD |
			DRM_BRIDGE_OP_HDMI |
			DRM_BRIDGE_OP_HDMI_AUDIO;
	lt9611c->bridge.type = DRM_MODE_CONNECTOR_HDMIA;

	lt9611c->bridge.vendor = "Lontium";
	switch (lt9611c->chip_type) {
	case CHIP_LT9611C:
		lt9611c->bridge.product = "LT9611C";
		break;
	case CHIP_LT9611EX:
		lt9611c->bridge.product = "LT9611EX";
		break;
	case CHIP_LT9611UXD:
	default:
		lt9611c->bridge.product = "LT9611UXD";
		break;
	}

	lt9611c->bridge.hdmi_audio_dev = dev;
	lt9611c->bridge.hdmi_audio_max_i2s_playback_channels = 8;
	lt9611c->bridge.hdmi_audio_dai_port = 2;

	devm_drm_bridge_add(dev, &lt9611c->bridge);

	/* Attach primary DSI */
	lt9611c->dsi0 = lt9611c_attach_dsi(lt9611c, lt9611c->dsi0_node);
	if (IS_ERR(lt9611c->dsi0)) {
		ret = PTR_ERR(lt9611c->dsi0);
		goto err_remove_bridge;
	}

	/* Attach secondary DSI, if specified */
	if (lt9611c->dsi1_node) {
		lt9611c->dsi1 = lt9611c_attach_dsi(lt9611c, lt9611c->dsi1_node);
		if (IS_ERR(lt9611c->dsi1)) {
			ret = PTR_ERR(lt9611c->dsi1);
			goto err_remove_bridge;
		}
	}

	lt9611c->hdmi_connected = false;
	i2c_set_clientdata(client, lt9611c);

	/* Enable HPD interrupt in the chip */
	{
		unsigned int irq_status;

		mutex_lock(&lt9611c->ocm_lock);
		regmap_write(lt9611c->regmap, 0xe0ee, 0x01);
		regmap_read(lt9611c->regmap, 0xe084, &irq_status);
		if (irq_status) {
			regmap_write(lt9611c->regmap, 0xe0df, irq_status);
			msleep(20);
			regmap_write(lt9611c->regmap, 0xe0df, 0x00);
		}
		regmap_write(lt9611c->regmap, 0xe0d0, 0x01);
		regmap_write(lt9611c->regmap, 0xe0ee, 0x00);
		mutex_unlock(&lt9611c->ocm_lock);
	}

	enable_irq(client->irq);
	msleep(100);

	return 0;

err_remove_bridge:
	cancel_work_sync(&lt9611c->work);

err_disable_regulators:
err_of_put:
	of_node_put(lt9611c->dsi1_node);
	of_node_put(lt9611c->dsi0_node);

	return ret;
}

static void lt9611c_remove(struct i2c_client *client)
{
	struct lt9611c *lt9611c = i2c_get_clientdata(client);

	/*
	 * IRQ was requested with devm_request_threaded_irq and is freed
	 * automatically by devres — do NOT call free_irq() here.
	 */
	cancel_work_sync(&lt9611c->work);
	of_node_put(lt9611c->dsi1_node);
	of_node_put(lt9611c->dsi0_node);
}

static int lt9611c_bridge_suspend(struct device *dev)
{
	struct lt9611c *lt9611c = dev_get_drvdata(dev);

	dev_dbg(lt9611c->dev, "suspend\n");
	disable_irq(lt9611c->client->irq);
	gpiod_set_value_cansleep(lt9611c->reset_gpio, 0);
	if (lt9611c->hdmi_gpio)
		gpiod_set_value_cansleep(lt9611c->hdmi_gpio, 0);

	return 0;
}

static int lt9611c_bridge_resume(struct device *dev)
{
	struct lt9611c *lt9611c = dev_get_drvdata(dev);
	int ret;

	if (lt9611c->hdmi_gpio) {
		gpiod_set_value_cansleep(lt9611c->hdmi_gpio, 1);
		msleep(20);
	}
	lt9611c_reset(lt9611c);

	/* Reapply port selection after reset, same as atomic_pre_enable */
	if (lt9611c->selected_port >= 0) {
		msleep(200);
		ret = lt9611c_select_port(lt9611c, lt9611c->selected_port);
		if (ret < 0)
			dev_warn(lt9611c->dev, "resume: failed to reapply port selection, ret=%d\n", ret);
		msleep(200);
	}

	enable_irq(lt9611c->client->irq);
	dev_dbg(lt9611c->dev, "resume\n");

	return 0;
}

static const struct dev_pm_ops lt9611c_bridge_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(lt9611c_bridge_suspend,
				lt9611c_bridge_resume)
};

static struct i2c_device_id lt9611c_id[] = {
	/* chip_type */
	{ "lontium,lt9611c", 0 },
	{ "lontium,lt9611ex", 1 },
	{ "lontium,lt9611uxd", 2 },
	{ /* sentinel */ }
};

static const struct of_device_id lt9611c_match_table[] = {
	{ .compatible = "lontium,lt9611c",   .data = (void *)CHIP_LT9611C },
	{ .compatible = "lontium,lt9611ex",  .data = (void *)CHIP_LT9611EX },
	{ .compatible = "lontium,lt9611uxd", .data = (void *)CHIP_LT9611UXD },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lt9611c_match_table);

static struct i2c_driver lt9611c_driver = {
	.driver = {
		.name = "lt9611c",
		.of_match_table = lt9611c_match_table,
		.pm = &lt9611c_bridge_pm_ops,
		.dev_groups = lt9611c_attr_groups,
	},
	.probe = lt9611c_probe,
	.remove = lt9611c_remove,
	.id_table = lt9611c_id,
};
module_i2c_driver(lt9611c_driver);

MODULE_AUTHOR("SunYun Yang <syyang@lontium.com>");
MODULE_DESCRIPTION("Lontium LT9611C(EX/UXD) MIPI DSI to HDMI driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FW_FILE);
