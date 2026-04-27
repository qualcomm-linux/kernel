// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/crc8.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include <sound/hdmi-codec.h>
#include <drm/display/drm_hdmi_audio_helper.h>

#define FW_SIZE (64 * 1024)
#define LT_PAGE_SIZE 256
#define FW_FILE  "LT9611UXD.bin"

#define LT9611UXD_CHIP_ID0	0x23
#define LT9611UXD_CHIP_ID1	0x06

DECLARE_CRC8_TABLE(lt9611uxd_crc_table);

struct lt9611uxd {
	struct device *dev;
	struct i2c_client *client;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct regmap *regmap;
	/* Protects all accesses to registers by stopping the on-chip MCU */
	struct mutex ocm_lock;
	struct wait_queue_head wq;
	struct work_struct work;
	struct device_node *dsi0_node;
	struct device_node *dsi1_node;
	struct mipi_dsi_device *dsi0;
	struct mipi_dsi_device *dsi1;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];
	const struct firmware *fw;
	int fw_version;
	u8 fw_crc;

	bool hdmi_connected;
	bool edid_read;
};

#define LT9611D_PAGE_CONTROL	0xff

static const struct regmap_range_cfg lt9611uxd_ranges[] = {
	{
		.name = "register_range",
		.range_min =  0,
		.range_max = 0xffff,
		.selector_reg = LT9611D_PAGE_CONTROL,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 0x100,
	},
};

static const struct regmap_config lt9611uxd_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xffff,
	.ranges = lt9611uxd_ranges,
	.num_ranges = ARRAY_SIZE(lt9611uxd_ranges),
};

static int lt9611uxd_i2c_read_write_flow(struct lt9611uxd *lt9611uxd, u8 *params,
			       unsigned int param_count, u8 *return_buffer,
			       unsigned int return_count)
{
	int count, i;
	unsigned int temp;

	regmap_write(lt9611uxd->regmap, 0xe0de, 0x01);

	count = 0;
	do {
		regmap_read(lt9611uxd->regmap, 0xe0ae, &temp);
		usleep_range(1000, 2000);
		count++;
	} while (count < 100 && temp != 0x01);

	if (temp != 0x01)
		return -1;

	for (i = 0; i < param_count; i++) {
		if (i > 0xdd - 0xb0)
			break;

		regmap_write(lt9611uxd->regmap, 0xe0b0 + i, params[i]);
	}

	regmap_write(lt9611uxd->regmap, 0xe0de, 0x02);

	count = 0;
	do {
		regmap_read(lt9611uxd->regmap, 0xe0ae, &temp);
		usleep_range(1000, 2000);
		count++;
	} while (count < 100 && temp != 0x02);

	if (temp != 0x02)
		return -2;

	regmap_bulk_read(lt9611uxd->regmap, 0xe085, return_buffer, return_count);

	return 0;
}

static int lt9611uxd_prepare_firmware_data(struct lt9611uxd *lt9611uxd)
{
	struct device *dev = lt9611uxd->dev;
	u64 crc_size = FW_SIZE - 1;
	u8 default_val = 0xff;
	int ret;

	/* ensure filesystem ready */
	msleep(3000);
	ret = request_firmware(&lt9611uxd->fw, FW_FILE, dev);
	if (ret) {
		dev_err(dev, "failed load file '%s', error type %d\n", FW_FILE, ret);
		return ret;
	}

	if (lt9611uxd->fw->size > FW_SIZE - 1) {
		dev_err(dev, "firmware too large (%zu > %d)\n", lt9611uxd->fw->size, FW_SIZE - 1);
		lt9611uxd->fw = NULL;
		return -EINVAL;
	}

	dev_info(dev, "firmware size: %zu bytes\n", lt9611uxd->fw->size);

	lt9611uxd->fw_crc = crc8(lt9611uxd_crc_table, lt9611uxd->fw->data,
				 lt9611uxd->fw->size, 0);

	crc_size -= lt9611uxd->fw->size;
	while (crc_size--)
		lt9611uxd->fw_crc = crc8(lt9611uxd_crc_table, &default_val, 1,
					 lt9611uxd->fw_crc);

	dev_info(dev, "LT9611C.bin crc: 0x%02x\n", lt9611uxd->fw_crc);

	return 0;
}

static void lt9611uxd_config_parameters(struct lt9611uxd *lt9611uxd)
{
	struct reg_sequence seq_write_paras[] = {
		REG_SEQ0(0xe0ee, 0x01),
		//fifo rst
		REG_SEQ0(0xe103, 0x3f),
		REG_SEQ0(0xe103, 0xff),

		REG_SEQ0(0xe05e, 0xc1),
		REG_SEQ0(0xe058, 0x00),
		REG_SEQ0(0xe059, 0x50),
		REG_SEQ0(0xe05a, 0x10),
		REG_SEQ0(0xe05a, 0x00),
		REG_SEQ0(0xe058, 0x21),
	};

	regmap_multi_reg_write(lt9611uxd->regmap, seq_write_paras, ARRAY_SIZE(seq_write_paras));
}

static void lt9611uxd_wren(struct lt9611uxd *lt9611uxd)
{
	regmap_write(lt9611uxd->regmap, 0xe05a, 0x04);
	regmap_write(lt9611uxd->regmap, 0xe05a, 0x00);
}

static void lt9611uxd_wrdi(struct lt9611uxd *lt9611uxd)
{
	regmap_write(lt9611uxd->regmap, 0xe05a, 0x08);
	regmap_write(lt9611uxd->regmap, 0xe05a, 0x00);
}

static void lt9611uxd_erase_op(struct lt9611uxd *lt9611uxd, u32 addr)
{
	struct reg_sequence seq_write[] = {
		REG_SEQ0(0xe0ee, 0x01),
		REG_SEQ0(0xe05a, 0x04),
		REG_SEQ0(0xe05a, 0x00),
		REG_SEQ0(0xe05b, (addr >> 16) & 0xff),
		REG_SEQ0(0xe05c, (addr >> 8) & 0xff),
		REG_SEQ0(0xe05d, addr & 0xff),
		REG_SEQ0(0xe05a, 0x01),
		REG_SEQ0(0xe05a, 0x00),
	};

	regmap_multi_reg_write(lt9611uxd->regmap, seq_write, ARRAY_SIZE(seq_write));
}

static void lt9611uxd_read_flash_reg_status(struct lt9611uxd *lt9611uxd, unsigned int *status)
{
	struct reg_sequence seq_write[] = {
		REG_SEQ0(0xe103, 0x3f),
		REG_SEQ0(0xe103, 0xff), //fifo rst

		REG_SEQ0(0xe05e, 0x40),
		REG_SEQ0(0xe056, 0x05),
		REG_SEQ0(0xe055, 0x25),
		REG_SEQ0(0xe055, 0x01),
		REG_SEQ0(0xe058, 0x21),
	};

	regmap_multi_reg_write(lt9611uxd->regmap, seq_write, ARRAY_SIZE(seq_write));

	regmap_read(lt9611uxd->regmap, 0xe05f, status);
}

static void lt9611uxd_crc_to_sram(struct lt9611uxd *lt9611uxd)
{
	struct reg_sequence seq_write[] = {
		REG_SEQ0(0xe051, 0x00),
		REG_SEQ0(0xe055, 0xc0),
		REG_SEQ0(0xe055, 0x80),
		REG_SEQ0(0xe05e, 0xc0),
		REG_SEQ0(0xe058, 0x21),
	};

	regmap_multi_reg_write(lt9611uxd->regmap, seq_write, ARRAY_SIZE(seq_write));
}

static void lt9611uxd_data_to_sram(struct lt9611uxd *lt9611uxd)
{
	struct reg_sequence seq_write[] = {
		REG_SEQ0(0xe051, 0xff),
		REG_SEQ0(0xe055, 0x80),
		REG_SEQ0(0xe05e, 0xc0),
		REG_SEQ0(0xe058, 0x21),
	};

	regmap_multi_reg_write(lt9611uxd->regmap, seq_write, ARRAY_SIZE(seq_write));
}

static void lt9611uxd_sram_to_flash(struct lt9611uxd *lt9611uxd, u64 addr)
{
	struct reg_sequence seq_write[] = {
		REG_SEQ0(0xe05b, (addr >> 16) & 0xff),
		REG_SEQ0(0xe05c, (addr >> 8) & 0xff),
		REG_SEQ0(0xe05d, addr & 0xff),
		REG_SEQ0(0xe05a, 0x30),
		REG_SEQ0(0xe05a, 0x00),
	};

	regmap_multi_reg_write(lt9611uxd->regmap, seq_write, ARRAY_SIZE(seq_write));
}

static void lt9611uxd_block_erase(struct lt9611uxd *lt9611uxd)
{
	struct device *dev = lt9611uxd->dev;
	u32 i = 0;
	unsigned int flash_status = 0;
	unsigned int block_num = 0x00;
	u32 flash_addr = 0x00;

	for (block_num = 0; block_num < 2; block_num++) {
		flash_addr = (block_num * 0x008000);
		lt9611uxd_erase_op(lt9611uxd, flash_addr);
		msleep(100);
		i = 0;
		while (1) {
			lt9611uxd_read_flash_reg_status(lt9611uxd, &flash_status);
			if ((flash_status & 0x01) == 0)
				break;

			if (i > 50)
				break;

			i++;
			msleep(50);
		}
	}

	dev_info(dev, "erase flash done.\n");
}

static int lt9611uxd_write_data(struct lt9611uxd *lt9611uxd, u64 addr)
{
	struct device *dev = lt9611uxd->dev;
	int ret;
	int page = 0, num = 0, i = 0;
	u64 size, index;
	const u8 *data;
	unsigned int value;

	data = lt9611uxd->fw->data;
	size = lt9611uxd->fw->size;
	page = (size + LT_PAGE_SIZE - 1) / LT_PAGE_SIZE;
	if (page * LT_PAGE_SIZE > FW_SIZE) {
		dev_err(dev, "firmware size out of range\n");
		return -EINVAL;
	}

	dev_info(dev, "%u pages, total size %llu byte\n", page, size);

	for (num = 0; num < page; num++) {
		lt9611uxd_data_to_sram(lt9611uxd);

		for (i = 0; i < LT_PAGE_SIZE; i++) {
			index = num * LT_PAGE_SIZE + i;
			value = (index < size) ? data[index] : 0xff;

			ret = regmap_write(lt9611uxd->regmap, 0xe059, value);
			if (ret < 0) {
				dev_err(dev, "write error at page %u, index %u\n", num, i);
				return ret;
			}
		}

		lt9611uxd_wren(lt9611uxd);
		lt9611uxd_sram_to_flash(lt9611uxd, addr);

		addr += LT_PAGE_SIZE;
	}

	lt9611uxd_wrdi(lt9611uxd);

	return 0;
}

static int lt9611uxd_write_crc(struct lt9611uxd *lt9611uxd, u64 addr)
{
	struct device *dev = lt9611uxd->dev;
	int ret;
	u8 crc;

	crc = lt9611uxd->fw_crc;
	lt9611uxd_crc_to_sram(lt9611uxd);
	ret = regmap_write(lt9611uxd->regmap, 0xe059, crc);
	if (ret < 0) {
		dev_err(dev, "failed to write CRC\n");
		return -1;
	}

	lt9611uxd_wren(lt9611uxd);
	lt9611uxd_sram_to_flash(lt9611uxd, addr);
	lt9611uxd_wrdi(lt9611uxd);

	dev_info(dev, "CRC 0x%02x written to flash at addr 0x%llx\n", crc, addr);

	return 0;
}

static int lt9611uxd_firmware_upgrade(struct lt9611uxd *lt9611uxd)
{
	struct device *dev = lt9611uxd->dev;
	int ret;

	dev_info(dev, "starting firmware upgrade, size: %zu bytes\n", lt9611uxd->fw->size);

	lt9611uxd_config_parameters(lt9611uxd);
	lt9611uxd_block_erase(lt9611uxd);

	ret = lt9611uxd_write_data(lt9611uxd, 0);
	if (ret < 0) {
		dev_err(dev, "Failed to write firmware data\n");
		return ret;
	}

	ret = lt9611uxd_write_crc(lt9611uxd, FW_SIZE - 1);
	if (ret < 0) {
		dev_err(dev, "Failed to write firmware CRC\n");
		return ret;
	}

	return 0;
}

static int lt9611uxd_upgrade_result(struct lt9611uxd *lt9611uxd)
{
	struct device *dev = lt9611uxd->dev;
	unsigned int crc_result;

	regmap_write(lt9611uxd->regmap, 0xe0ee, 0x01);
	regmap_read(lt9611uxd->regmap, 0xe021, &crc_result);

	if (crc_result != lt9611uxd->fw_crc) {
		dev_err(dev, "LT9611C firmware upgrade failed, expected CRC=0x%02X, read CRC=0x%02X\n",
			lt9611uxd->fw_crc, crc_result);
		return -EIO;
	}

	dev_info(dev, "LT9611C firmware upgrade success, CRC=0x%02x\n", crc_result);
	return 0;
}

static struct lt9611uxd *bridge_to_lt9611uxd(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9611uxd, bridge);
}

static void lt9611uxd_lock(struct lt9611uxd *lt9611uxd)
{
	mutex_lock(&lt9611uxd->ocm_lock);
	regmap_write(lt9611uxd->regmap, 0xe0ee, 0x01);
}

static void lt9611uxd_unlock(struct lt9611uxd *lt9611uxd)
{
	regmap_write(lt9611uxd->regmap, 0xe0ee, 0x00);
	msleep(50);
	mutex_unlock(&lt9611uxd->ocm_lock);
}

static int lt9611uxd_wait_for_edid(struct lt9611uxd *lt9611uxd)
{
	return wait_event_interruptible_timeout(lt9611uxd->wq, lt9611uxd->edid_read,
			msecs_to_jiffies(500));
}

static irqreturn_t lt9611uxd_irq_thread_handler(int irq, void *dev_id)
{
	struct lt9611uxd *lt9611uxd = dev_id;
	struct device *dev = lt9611uxd->dev;
	int ret;
	unsigned int irq_status;
	u8 cmd[5] = {0x52, 0x48, 0x31, 0x3a, 0x00};
	u8 data[5];

	regmap_read(lt9611uxd->regmap, 0xe084, &irq_status);
	if (!(irq_status & BIT(0)))
		return IRQ_HANDLED;

	lt9611uxd_lock(lt9611uxd);

	/* Check for EDID ready */
	if (irq_status & BIT(0)) {
		lt9611uxd->edid_read = true;
		wake_up_all(&lt9611uxd->wq);
	}

	ret = lt9611uxd_i2c_read_write_flow(lt9611uxd, cmd, 5, data, 5);
	if (ret) {
		dev_err(dev, "failed to read HPD status\n");
	} else {
		lt9611uxd->hdmi_connected = (data[4] == 0x02);
		dev_info(dev, "HDMI %s\n", lt9611uxd->hdmi_connected ? "connected" : "disconnected");
	}

	schedule_work(&lt9611uxd->work);

	/*clear interrupt*/
	regmap_write(lt9611uxd->regmap, 0xe0df, irq_status & BIT(0));
	usleep_range(10000, 12000);
	regmap_write(lt9611uxd->regmap, 0xe0df, irq_status & (~BIT(0)));

	lt9611uxd_unlock(lt9611uxd);

	return IRQ_HANDLED;
}

static void lt9611uxd_hpd_work(struct work_struct *work)
{
	struct lt9611uxd *lt9611uxd = container_of(work, struct lt9611uxd, work);
	bool connected;

	mutex_lock(&lt9611uxd->ocm_lock);
	connected = lt9611uxd->hdmi_connected;
	mutex_unlock(&lt9611uxd->ocm_lock);

	drm_bridge_hpd_notify(&lt9611uxd->bridge, connected ?
			      connector_status_connected :
			      connector_status_disconnected);
}

static void lt9611uxd_reset(struct lt9611uxd *lt9611uxd)
{
	gpiod_set_value_cansleep(lt9611uxd->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(lt9611uxd->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(lt9611uxd->reset_gpio, 1);
	msleep(400);
}

static int lt9611uxd_regulator_init(struct lt9611uxd *lt9611uxd)
{
	struct device *dev = lt9611uxd->dev;
	int ret;

	lt9611uxd->supplies[0].supply = "vcc";
	lt9611uxd->supplies[1].supply = "vdd";

	ret = devm_regulator_bulk_get(dev, 2, lt9611uxd->supplies);
	if (ret < 0)
		return ret;

	return regulator_set_load(lt9611uxd->supplies[0].consumer, 200000);
}

static int lt9611uxd_regulator_enable(struct lt9611uxd *lt9611uxd)
{
	int ret;

	ret = regulator_enable(lt9611uxd->supplies[0].consumer);
	if (ret < 0)
		return ret;

	usleep_range(5000, 10000);

	ret = regulator_enable(lt9611uxd->supplies[1].consumer);
	if (ret < 0) {
		regulator_disable(lt9611uxd->supplies[0].consumer);
		return ret;
	}

	return 0;
}

static struct mipi_dsi_device *lt9611uxd_attach_dsi(struct lt9611uxd *lt9611uxd,
						  struct device_node *dsi_node)
{
	const struct mipi_dsi_device_info info = { "lt9611uxd", 0, NULL };
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	struct device *dev = lt9611uxd->dev;
	int ret;

	host = of_find_mipi_dsi_host_by_node(dsi_node);
	if (!host)
		return ERR_PTR(dev_err_probe(dev, -EPROBE_DEFER, "failed to find dsi host\n"));

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi)) {
		dev_err(dev, "failed to create dsi device\n");
		return dsi;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			 MIPI_DSI_MODE_VIDEO_HSE;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host\n");
		return ERR_PTR(ret);
	}

	return dsi;
}

static int lt9611uxd_bridge_attach(struct drm_bridge *bridge,
				   struct drm_encoder *encoder,
				   enum drm_bridge_attach_flags flags)
{
	struct lt9611uxd *lt9611uxd = bridge_to_lt9611uxd(bridge);

	return drm_bridge_attach(encoder, lt9611uxd->next_bridge,
				 bridge, flags);
}

static enum drm_mode_status lt9611uxd_bridge_mode_valid(struct drm_bridge *bridge,
						      const struct drm_display_info *info,
						      const struct drm_display_mode *mode)
{
	u32 pixclk;

	pixclk = (mode->htotal * mode->vtotal * drm_mode_vrefresh(mode)) / 1000000;

	if (pixclk >= 25 && pixclk <= 340)
		return MODE_OK;
	else
		return MODE_BAD;
}

static void lt9611uxd_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adj_mode)
{
	struct lt9611uxd *lt9611uxd = bridge_to_lt9611uxd(bridge);
	struct device *dev = lt9611uxd->dev;
	int ret;
	u32 h_total, hactive, hsync_len, hfront_porch, hback_porch;
	u32 v_total, vactive, vsync_len, vfront_porch, vback_porch;
	u8 video_timing_set_cmd[26] = {0x57, 0x4d, 0x33, 0x3a};
	u8 return_timing_set_param[3];
	u8 framerate;
	u8 vic = 0x00;

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

	dev_info(dev, "hactive=%d, vactive=%d\n", hactive, vactive);
	dev_info(dev, "framerate=%d\n", framerate);
	dev_info(dev, "vic = 0x%02x\n", vic);

	video_timing_set_cmd[4] = (h_total >> 8) & 0xff;
	video_timing_set_cmd[5] = h_total & 0xff;
	video_timing_set_cmd[6] = (hactive >> 8) & 0xff;
	video_timing_set_cmd[7] = hactive & 0xff;
	video_timing_set_cmd[8] = (hfront_porch >> 8) & 0xff;
	video_timing_set_cmd[9] = hfront_porch & 0xff;
	video_timing_set_cmd[10] = (hsync_len >> 8) & 0xff;
	video_timing_set_cmd[11] = hsync_len & 0xff;
	video_timing_set_cmd[12] = (hback_porch >> 8) & 0xff;
	video_timing_set_cmd[13] = hback_porch & 0xff;
	video_timing_set_cmd[14] = (v_total >> 8) & 0xff;
	video_timing_set_cmd[15] = v_total & 0xff;
	video_timing_set_cmd[16] = (vactive >> 8) & 0xff;
	video_timing_set_cmd[17] = vactive & 0xFF;
	video_timing_set_cmd[18] = (vfront_porch >> 8) & 0xff;
	video_timing_set_cmd[19] = vfront_porch & 0xff;
	video_timing_set_cmd[20] = (vsync_len >> 8) & 0xff;
	video_timing_set_cmd[21] = vsync_len & 0xff;
	video_timing_set_cmd[22] = (vback_porch >> 8) & 0xff;
	video_timing_set_cmd[23] = vback_porch & 0xff;
	video_timing_set_cmd[24] = framerate;
	video_timing_set_cmd[25] = vic;

	mutex_lock(&lt9611uxd->ocm_lock);
	ret = lt9611uxd_i2c_read_write_flow(lt9611uxd, video_timing_set_cmd, 26, return_timing_set_param, 3);
	if (ret)
		dev_err(dev, "video set failed\n");
	mutex_unlock(&lt9611uxd->ocm_lock);
}

static enum drm_connector_status lt9611uxd_bridge_detect(struct drm_bridge *bridge,
							 struct drm_connector *connector)
{
	struct lt9611uxd *lt9611uxd = bridge_to_lt9611uxd(bridge);
	struct device *dev = lt9611uxd->dev;
	int ret;
	bool connected = false;
	u8 cmd[5] = {0x52, 0x48, 0x31, 0x3a, 0x00};
	u8 data[5];

	mutex_lock(&lt9611uxd->ocm_lock);
	ret = lt9611uxd_i2c_read_write_flow(lt9611uxd, cmd, 5, data, 5);
	if (ret)
		dev_err(dev, "Failed to read HPD status (err=%d)\n", ret);
	else
		connected = (data[4] == 0x02);

	lt9611uxd->hdmi_connected = connected;

	mutex_unlock(&lt9611uxd->ocm_lock);

	return connected ? connector_status_connected :
				connector_status_disconnected;
}

static int lt9611uxd_get_edid_block(void *data, u8 *buf,
				  unsigned int block, size_t len)
{
	struct lt9611uxd *lt9611uxd = data;
	struct device *dev = lt9611uxd->dev;
	u8 cmd[5] = {0x52, 0x48, 0x33, 0x3a, 0x00};
	u8 packet[37];
	int ret, i, offset = 0;

	if (len != 128)
		return -EINVAL;

	for (i = 0; i < 4; i++) {
		cmd[4] = block * 4 + i;
		ret = lt9611uxd_i2c_read_write_flow(lt9611uxd, cmd, sizeof(cmd),
						    packet, sizeof(packet));
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

static const struct drm_edid *lt9611uxd_bridge_edid_read(struct drm_bridge *bridge,
						       struct drm_connector *connector)
{
	struct lt9611uxd *lt9611uxd = bridge_to_lt9611uxd(bridge);
	int ret;

	ret = lt9611uxd_wait_for_edid(lt9611uxd);
	if (ret < 0) {
		dev_err(lt9611uxd->dev, "wait for EDID failed: %d\n", ret);
		return NULL;
	} else if (ret == 0) {
		dev_err(lt9611uxd->dev, "wait for EDID timeout\n");
		return NULL;
	}

	return drm_edid_read_custom(connector, lt9611uxd_get_edid_block, lt9611uxd);
}

static void lt9611uxd_bridge_hpd_notify(struct drm_bridge *bridge,
					struct drm_connector *connector,
					enum drm_connector_status status)
{
	const struct drm_edid *drm_edid;

	if (status == connector_status_disconnected) {
		drm_connector_hdmi_audio_plugged_notify(connector, false);
		drm_edid_connector_update(connector, NULL);
		return;
	}

	drm_edid = lt9611uxd_bridge_edid_read(bridge, connector);
	drm_edid_connector_update(connector, drm_edid);
	drm_edid_free(drm_edid);

	if (status == connector_status_connected)
		drm_connector_hdmi_audio_plugged_notify(connector, true);
}

static int lt9611uxd_hdmi_audio_prepare(struct drm_bridge *bridge,
					struct drm_connector *connector,
					struct hdmi_codec_daifmt *fmt,
					struct hdmi_codec_params *hparms)
{
	struct lt9611uxd *lt9611uxd = bridge_to_lt9611uxd(bridge);

	dev_info(lt9611uxd->dev, "SOC sample_rate: %d, sample_width: %d, fmt: %d\n",
		 hparms->sample_rate, hparms->sample_width, fmt->fmt);

	switch (hparms->sample_rate) {
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
	case 176400:
	case 192000:
		break;
	default:
		return -EINVAL;
	}

	switch (hparms->sample_width) {
	case 16:
	case 18:
	case 20:
	case 24:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt->fmt) {
	case HDMI_I2S:
	case HDMI_SPDIF:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void lt9611uxd_hdmi_audio_shutdown(struct drm_bridge *bridge,
					  struct drm_connector *connector)
{
}

static const struct drm_bridge_funcs lt9611uxd_bridge_funcs = {
	.attach = lt9611uxd_bridge_attach,
	.mode_valid = lt9611uxd_bridge_mode_valid,
	.mode_set = lt9611uxd_bridge_mode_set,
	.detect = lt9611uxd_bridge_detect,
	.edid_read = lt9611uxd_bridge_edid_read,
	.hpd_notify = lt9611uxd_bridge_hpd_notify,
	.hdmi_audio_prepare = lt9611uxd_hdmi_audio_prepare,
	.hdmi_audio_shutdown = lt9611uxd_hdmi_audio_shutdown,
};

static int lt9611uxd_parse_dt(struct device *dev,
			    struct lt9611uxd *lt9611uxd)
{
	lt9611uxd->dsi0_node = of_graph_get_remote_node(dev->of_node, 0, -1);
	if (!lt9611uxd->dsi0_node) {
		dev_err(dev, "failed to get remote node for primary dsi\n");
		return -ENODEV;
	}

	lt9611uxd->dsi1_node = of_graph_get_remote_node(dev->of_node, 1, -1);

	return drm_of_find_panel_or_bridge(dev->of_node, 2, -1, NULL, &lt9611uxd->next_bridge);
}

static int lt9611uxd_gpio_init(struct lt9611uxd *lt9611uxd)
{
	struct device *dev = lt9611uxd->dev;

	/*
	 * Initialise reset GPIO low (active-low reset: LOW = chip held in reset).
	 * lt9611uxd_reset() will immediately toggle HIGH->LOW->HIGH to release.
	 */
	lt9611uxd->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(lt9611uxd->reset_gpio)) {
		dev_err(dev, "failed to acquire reset gpio\n");
		return PTR_ERR(lt9611uxd->reset_gpio);
	}

	return 0;
}

static int lt9611uxd_read_version(struct lt9611uxd *lt9611uxd)
{
	u8 buf[2];
	int ret;

	ret = regmap_write(lt9611uxd->regmap, 0xe0ee, 0x01);
	if (ret)
		return ret;

	ret = regmap_bulk_read(lt9611uxd->regmap, 0xe080, buf, 2);
	if (ret)
		return ret;

	return (buf[0] << 8) | buf[1];
}

static int lt9611uxd_read_chipid(struct lt9611uxd *lt9611uxd)
{
	struct device *dev = lt9611uxd->dev;
	u8 chipid[2];
	int ret;

	ret = regmap_write(lt9611uxd->regmap, 0xe0ee, 0x01);
	if (ret) {
		dev_err(dev, "Failed to write unlock register: %d\n", ret);
		return ret;
	}

	ret = regmap_bulk_read(lt9611uxd->regmap, 0xe100, chipid, 2);
	if (ret) {
		dev_err(dev, "Failed to read chip ID: %d\n", ret);
		return ret;
	}

	if (chipid[0] != LT9611UXD_CHIP_ID0 || chipid[1] != LT9611UXD_CHIP_ID1) {
		dev_err(dev, "unexpected ChipID: 0x%02x 0x%02x, expected: 0x%02x 0x%02x\n",
			chipid[0], chipid[1], LT9611UXD_CHIP_ID0, LT9611UXD_CHIP_ID1);
		return -ENODEV;
	}
	return 0;
}

static ssize_t lt9611uxd_firmware_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t len)
{
	struct lt9611uxd *lt9611uxd = dev_get_drvdata(dev);
	int ret;

	lt9611uxd_lock(lt9611uxd);
	ret = lt9611uxd_prepare_firmware_data(lt9611uxd);
	if (ret < 0) {
		dev_err(dev, "Failed prepare firmware data: %d\n", ret);
		goto out;
	}

	ret = lt9611uxd_firmware_upgrade(lt9611uxd);
	if (ret < 0) {
		dev_err(dev, "upgrade failure\n");
		goto out;
	}
	lt9611uxd_reset(lt9611uxd);
	ret = lt9611uxd_upgrade_result(lt9611uxd);
	if (ret < 0)
		goto out;

out:
	lt9611uxd_unlock(lt9611uxd);
	lt9611uxd_reset(lt9611uxd);
	if (lt9611uxd->fw) {
		release_firmware(lt9611uxd->fw);
		lt9611uxd->fw = NULL;
	}

	return ret < 0 ? ret : len;
}

static ssize_t lt9611uxd_firmware_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lt9611uxd *lt9611uxd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%04x\n", lt9611uxd->fw_version);
}

static DEVICE_ATTR_RW(lt9611uxd_firmware);

static struct attribute *lt9611uxd_attrs[] = {
	&dev_attr_lt9611uxd_firmware.attr,
	NULL,
};

static const struct attribute_group lt9611uxd_attr_group = {
	.attrs = lt9611uxd_attrs,
};

static const struct attribute_group *lt9611uxd_attr_groups[] = {
	&lt9611uxd_attr_group,
	NULL,
};

static int lt9611uxd_probe(struct i2c_client *client)
{
	struct lt9611uxd *lt9611uxd;
	struct device *dev = &client->dev;
	int ret;
	bool fw_updated = false;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "device doesn't support I2C\n");
		return -ENODEV;
	}

	lt9611uxd = devm_drm_bridge_alloc(dev, struct lt9611uxd, bridge, &lt9611uxd_bridge_funcs);
	if (IS_ERR(lt9611uxd))
		return PTR_ERR(lt9611uxd);

	lt9611uxd->dev = dev;
	lt9611uxd->client = client;
	mutex_init(&lt9611uxd->ocm_lock);

	lt9611uxd->regmap = devm_regmap_init_i2c(client, &lt9611uxd_regmap_config);
	if (IS_ERR(lt9611uxd->regmap)) {
		dev_err(dev, "regmap i2c init failed\n");
		return PTR_ERR(lt9611uxd->regmap);
	}

	ret = lt9611uxd_parse_dt(dev, lt9611uxd);
	if (ret) {
		dev_err(dev, "failed to parse device tree\n");
		return ret;
	}

	ret = lt9611uxd_gpio_init(lt9611uxd);
	if (ret < 0)
		goto err_of_put;

	ret = lt9611uxd_regulator_init(lt9611uxd);
	if (ret < 0)
		goto err_of_put;

	ret = lt9611uxd_regulator_enable(lt9611uxd);
	if (ret)
		goto err_of_put;

	lt9611uxd_reset(lt9611uxd);

	ret = lt9611uxd_read_chipid(lt9611uxd);
	if (ret < 0) {
		dev_err(dev, "failed to read chip id.\n");
		goto err_disable_regulators;
	}

	lt9611uxd_lock(lt9611uxd);
retry:
	ret = lt9611uxd_read_version(lt9611uxd);
	if (ret < 0) {
		dev_err(dev, "failed to read FW version\n");
		lt9611uxd_unlock(lt9611uxd);
		goto err_disable_regulators;

	} else if (ret == 0) { /*Upgrade conditions*/
		if (!fw_updated) {
			fw_updated = true;
			ret = lt9611uxd_prepare_firmware_data(lt9611uxd);
			if (ret < 0) {
				lt9611uxd_unlock(lt9611uxd);
				goto err_disable_regulators;
			}

			ret = lt9611uxd_firmware_upgrade(lt9611uxd);
			if (ret < 0) {
				lt9611uxd_unlock(lt9611uxd);
				goto err_disable_regulators;
			}

			lt9611uxd_reset(lt9611uxd);

			ret = lt9611uxd_upgrade_result(lt9611uxd);
			if (ret < 0) {
				lt9611uxd_unlock(lt9611uxd);
				goto err_disable_regulators;
			} else {
				goto retry;
			}
		} else {
			dev_err(dev, "FW version 0x%04x, update failed\n", ret);
			ret = -EOPNOTSUPP;
			lt9611uxd_unlock(lt9611uxd);
			goto err_disable_regulators;
		}
	}

	if (lt9611uxd->fw) {
		release_firmware(lt9611uxd->fw);
		lt9611uxd->fw = NULL;
	}
	lt9611uxd_unlock(lt9611uxd);
	lt9611uxd->fw_version = ret;

	dev_info(dev, "current version:0x%04x", lt9611uxd->fw_version);

	init_waitqueue_head(&lt9611uxd->wq);
	INIT_WORK(&lt9611uxd->work, lt9611uxd_hpd_work);

	ret = request_threaded_irq(client->irq, NULL,
				   lt9611uxd_irq_thread_handler,
				   IRQF_ONESHOT, "lt9611uxd", lt9611uxd);

	if (ret) {
		dev_err(dev, "failed to request irq\n");
		goto err_disable_regulators;
	}

	i2c_set_clientdata(client, lt9611uxd);

	lt9611uxd->bridge.of_node = client->dev.of_node;
	lt9611uxd->bridge.ops = DRM_BRIDGE_OP_DETECT |
		DRM_BRIDGE_OP_EDID |
		DRM_BRIDGE_OP_HPD |
		DRM_BRIDGE_OP_HDMI_AUDIO;
	lt9611uxd->bridge.type = DRM_MODE_CONNECTOR_HDMIA;

	lt9611uxd->bridge.hdmi_audio_dev = dev;
	lt9611uxd->bridge.hdmi_audio_max_i2s_playback_channels = 8;
	lt9611uxd->bridge.hdmi_audio_dai_port = 2;

	drm_bridge_add(&lt9611uxd->bridge);

	crc8_populate_msb(lt9611uxd_crc_table, 0x31);

	/* Attach primary DSI */
	lt9611uxd->dsi0 = lt9611uxd_attach_dsi(lt9611uxd, lt9611uxd->dsi0_node);
	if (IS_ERR(lt9611uxd->dsi0)) {
		ret = PTR_ERR(lt9611uxd->dsi0);
		goto err_remove_bridge;
	}

	/* Attach secondary DSI, if specified */
	if (lt9611uxd->dsi1_node) {
		lt9611uxd->dsi1 = lt9611uxd_attach_dsi(lt9611uxd, lt9611uxd->dsi1_node);
		if (IS_ERR(lt9611uxd->dsi1)) {
			ret = PTR_ERR(lt9611uxd->dsi1);
			goto err_remove_bridge;
		}
	}

	return 0;

err_remove_bridge:
	free_irq(client->irq, lt9611uxd);
	cancel_work_sync(&lt9611uxd->work);
	drm_bridge_remove(&lt9611uxd->bridge);

err_disable_regulators:
	regulator_bulk_disable(ARRAY_SIZE(lt9611uxd->supplies), lt9611uxd->supplies);

err_of_put:
	of_node_put(lt9611uxd->dsi1_node);
	of_node_put(lt9611uxd->dsi0_node);

	if (lt9611uxd->fw) {
		release_firmware(lt9611uxd->fw);
		lt9611uxd->fw = NULL;
	}

	return ret;
}

static void lt9611uxd_remove(struct i2c_client *client)
{
	struct lt9611uxd *lt9611uxd = i2c_get_clientdata(client);

	free_irq(client->irq, lt9611uxd);
	cancel_work_sync(&lt9611uxd->work);
	drm_bridge_remove(&lt9611uxd->bridge);
	mutex_destroy(&lt9611uxd->ocm_lock);
	regulator_bulk_disable(ARRAY_SIZE(lt9611uxd->supplies), lt9611uxd->supplies);
	of_node_put(lt9611uxd->dsi1_node);
	of_node_put(lt9611uxd->dsi0_node);
}

static const struct i2c_device_id lt9611uxd_id[] = {
	{ "lontium,lt9611uxd" },
	{ /* sentinel */ }
};

static const struct of_device_id lt9611uxd_match_table[] = {
	{ .compatible = "lontium,lt9611uxd" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lt9611uxd_match_table);

static struct i2c_driver lt9611uxd_driver = {
	.driver = {
		.name = "lt9611uxd",
		.of_match_table = lt9611uxd_match_table,
		.dev_groups = lt9611uxd_attr_groups,
	},
	.probe = lt9611uxd_probe,
	.remove = lt9611uxd_remove,
	.id_table = lt9611uxd_id,
};
module_i2c_driver(lt9611uxd_driver);

MODULE_AUTHOR("mohit dsor <mohit.dsor@oss.qualcomm.com>");
MODULE_DESCRIPTION("Lontium LT9611UXD MIPI DSI to HDMI bridge driver");
MODULE_LICENSE("GPL v2");

MODULE_FIRMWARE(FW_FILE);
