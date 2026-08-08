// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Lontium Semiconductor, Inc.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
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
#include <linux/unaligned.h>
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
#define FW_FILE  "lt9611c_fw.bin"
#define LT9611C_CRC_POLYNOMIAL 0x31
#define LT9611C_PAGE_CONTROL 0xff
#define LT9611C_INFOFRAME_MAX_SIZE 32
#define LT9611C_CMD_HDR_SIZE 4
#define LT9611C_CMD_Y0_SIZE  1  /* Y0 echo byte in ACK response */
#define LT9611C_EDID_BUF_SIZE 32

struct lt9611c_cmd_hdr {
	u8 func;
	u8 type;
	u8 seq;
	u8 sep;
};

/* lt9611c_cmd_hdr.func values */
#define LT9611C_FUNC_WRITE	0x57 /* 'W' */
#define LT9611C_FUNC_READ	0x52 /* 'R' */
#define LT9611C_FUNC_ACK	0x41 /* 'A' */

/* lt9611c_cmd_hdr.type values */
#define LT9611C_TYPE_MIPI	0x4d /* 'M' */
#define LT9611C_TYPE_LVDS	0x4c /* 'L' */
#define LT9611C_TYPE_HDMI	0x48 /* 'H' */
#define LT9611C_TYPE_AUDIO	0x41 /* 'A' */
#define LT9611C_TYPE_CUSTOM	0x43 /* 'C' */

/* lt9611c_cmd_hdr.sep is always ':' */
#define LT9611C_CMD_SEP		0x3a /* ':' */

struct lt9611c_cmd {
	struct lt9611c_cmd_hdr hdr;
	const u8 *data;
	size_t data_len;
};

struct lt9611c_rsp {
	struct lt9611c_cmd_hdr hdr;
	u8 *data;
	unsigned int data_len;
};

enum lt9611_chip_type {
	CHIP_LT9611C = 0,
	CHIP_LT9611EX,
	CHIP_LT9611UXD,
};

struct lt9611c_chip_data {
	enum lt9611_chip_type chip_type;
	unsigned long long max_tmds_rate;
};

static const struct lt9611c_chip_data lt9611c_chip_data[] = {
	[CHIP_LT9611C]   = { CHIP_LT9611C,   340000000 },
	[CHIP_LT9611EX]  = { CHIP_LT9611EX,  340000000 },
	[CHIP_LT9611UXD] = { CHIP_LT9611UXD, 600000000 },
};

struct lt9611c {
	struct device *dev;
	struct i2c_client *client;
	struct drm_bridge bridge;
	struct regmap *regmap;
	struct mutex mcu_lock;
	struct work_struct work;
	struct device_node *dsi0_node;
	struct device_node *dsi1_node;
	struct mipi_dsi_device *dsi0;
	struct mipi_dsi_device *dsi1;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];
	int fw_version;
	/* Chip variant: C/EX/UXD */
	enum lt9611_chip_type chip_type;
	unsigned long long max_tmds_rate;
	bool hdmi_connected;
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

static int lt9611c_read_write_flow(struct lt9611c *lt9611c,
				   const struct lt9611c_cmd *cmd,
				   struct lt9611c_rsp *rsp)
{
	int ret;
	unsigned int i;
	unsigned int temp;
	unsigned int max_params = 0xe0dd - 0xe0b0 + 1;

	regmap_write(lt9611c->regmap, 0xe0de, 0x01);

	ret = regmap_read_poll_timeout(lt9611c->regmap, 0xe0ae, temp,
				       temp == 0x01, 1000, 200 * 1000);
	if (ret)
		return -ETIMEDOUT;

	ret = regmap_bulk_write(lt9611c->regmap, 0xe0b0, &cmd->hdr,
				LT9611C_CMD_HDR_SIZE);
	if (ret)
		return ret;

	for (i = 0; cmd->data && i < cmd->data_len &&
	     (LT9611C_CMD_HDR_SIZE + i) < max_params; i++)
		regmap_write(lt9611c->regmap,
			     0xe0b0 + LT9611C_CMD_HDR_SIZE + i, cmd->data[i]);

	regmap_write(lt9611c->regmap, 0xe0de, 0x02);

	ret = regmap_read_poll_timeout(lt9611c->regmap, 0xe0ae, temp,
				       temp == 0x02, 1000, 200 * 1000);
	if (ret)
		return -ETIMEDOUT;

	ret = regmap_bulk_read(lt9611c->regmap, 0xe085,
			       &rsp->hdr, LT9611C_CMD_HDR_SIZE);
	if (ret)
		return ret;


	if (rsp->data && rsp->data_len)
		ret = regmap_bulk_read(lt9611c->regmap,
				       0xe085 + LT9611C_CMD_HDR_SIZE,
				       rsp->data, rsp->data_len);

	return ret;
}

static void lt9611c_lock(struct lt9611c *lt9611c)
{
	mutex_lock(&lt9611c->mcu_lock);
	regmap_write(lt9611c->regmap, 0xe0ee, 0x01);
}

static void lt9611c_unlock(struct lt9611c *lt9611c)
{
	regmap_write(lt9611c->regmap, 0xe0ee, 0x00);
	mutex_unlock(&lt9611c->mcu_lock);
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

static unsigned int read_flash_reg_status(struct lt9611c *lt9611c)
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
	unsigned int status;
	int ret;

	regmap_multi_reg_write(lt9611c->regmap, seq_write, ARRAY_SIZE(seq_write));
	ret = regmap_read(lt9611c->regmap, 0xe05f, &status);
	if (ret)
		return 0xff;

	return status;
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

static int lt9611c_block_erase(struct lt9611c *lt9611c)
{
	struct device *dev = lt9611c->dev;
	unsigned int block_num;
	unsigned int flash_status = 0;
	u32 flash_addr = 0;
	int ret;

	for (block_num = 0; block_num < 2; block_num++) {
		flash_addr = block_num * 0x008000;
		lt9611c_erase_op(lt9611c, flash_addr);
		msleep(100);
		ret = read_poll_timeout(read_flash_reg_status, flash_status,
					!(flash_status & 0x01),
					50 * USEC_PER_MSEC, 2500 * USEC_PER_MSEC,
					false, lt9611c);
		if (ret) {
			dev_err(dev, "flash erase timeout for block %u\n", block_num);
			return ret;
		}
	}

	return 0;
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
	page = DIV_ROUND_UP(size, LT_PAGE_SIZE);
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
	usleep_range(10000, 12000);

	gpiod_set_value_cansleep(lt9611c->reset_gpio, 0);
	msleep(400);
}

static int lt9611c_upgrade_result(struct lt9611c *lt9611c, u8 fw_crc)
{
	struct device *dev = lt9611c->dev;
	unsigned int crc_result;
	int ret;

	regmap_write(lt9611c->regmap, 0xe0ee, 0x01);
	ret = regmap_read(lt9611c->regmap, 0xe021, &crc_result);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read firmware crc\n");

	if (crc_result != fw_crc) {
		dev_err(dev, "lt9611c fw upgrade failed, expected crc=0x%02x, read crc=0x%02x\n",
			fw_crc, crc_result);
		return -EIO;
	}

	dev_info(dev, "lt9611c firmware upgrade success, crc=0x%02x\n", crc_result);
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

	/* load firmware — must happen outside the mcu_lock */
	ret = request_firmware(&fw, FW_FILE, dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to load '%s'\n", FW_FILE);

	if (fw->size > total_size) {
		dev_err(dev, "firmware too large (%zu > %zu)\n", fw->size, total_size);
		ret = -EINVAL;
		goto out_release_fw;
	}
	dev_dbg(dev, "firmware size: %zu bytes\n", fw->size);

	buffer = kzalloc(total_size, GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto out_release_fw;
	}

	memcpy(buffer, fw->data, fw->size);
	memset(buffer + fw->size, 0xff, total_size - fw->size);

	fw_crc = crc8(lt9611c_crc8_table, buffer, total_size, 0);
	kfree(buffer);

	dev_info(dev, "starting firmware upgrade, size: %zu bytes, crc: 0x%02x\n",
		 fw->size, fw_crc);

	/* hardware access requires mcu_lock */
	lt9611c_lock(lt9611c);

	lt9611c_config_parameters(lt9611c);
	ret = lt9611c_block_erase(lt9611c);
	if (ret < 0)
		goto out_unlock;

	ret = lt9611c_write_data(lt9611c, fw, 0);
	if (ret < 0) {
		dev_err(dev, "failed to write firmware data\n");
		goto out_unlock;
	}

	ret = lt9611c_write_crc(lt9611c, fw_crc, FW_SIZE - 1);
	if (ret < 0) {
		dev_err(dev, "failed to write firmware crc\n");
		goto out_unlock;
	}

	lt9611c_reset(lt9611c);
	ret = lt9611c_upgrade_result(lt9611c, fw_crc);

out_unlock:
	lt9611c_unlock(lt9611c);
out_release_fw:
	release_firmware(fw);
	return ret;
}

static struct lt9611c *bridge_to_lt9611c(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9611c, bridge);
}

static const struct lt9611c *bridge_to_lt9611c_const(const struct drm_bridge *bridge)
{
	return container_of_const(bridge, struct lt9611c, bridge);
}

static irqreturn_t lt9611c_irq_thread_handler(int irq, void *dev_id)
{
	struct lt9611c *lt9611c = dev_id;
	struct device *dev = lt9611c->dev;
	int ret;
	unsigned int irq_status;

	guard(mutex)(&lt9611c->mcu_lock);

	ret = regmap_read(lt9611c->regmap, 0xe084, &irq_status);
	if (ret) {
		dev_err(dev, "failed to read irq status: %d\n", ret);
		return IRQ_HANDLED;
	}

	if (!(irq_status & BIT(0)))
		return IRQ_NONE;

	/*Clear interrupt: hardware requires two writes with delay*/
	regmap_write(lt9611c->regmap, 0xe0df, irq_status & BIT(0));
	usleep_range(10000, 12000);
	regmap_write(lt9611c->regmap, 0xe0df, irq_status & (~BIT(0)));

	schedule_work(&lt9611c->work);

	return IRQ_HANDLED;
}

static void lt9611c_hpd_work(struct work_struct *work)
{
	struct lt9611c *lt9611c = container_of(work, struct lt9611c, work);
	struct device *dev = lt9611c->dev;
	static const u8 hpd_data[] = { 0x00 };
	struct lt9611c_cmd cmd = {
		.hdr = { LT9611C_FUNC_READ, LT9611C_TYPE_HDMI, 0x31, LT9611C_CMD_SEP },
		.data = hpd_data,
		.data_len = 1,
	};
	u8 hpd_status;
	struct lt9611c_rsp rsp = { .data = &hpd_status, .data_len = 1 };
	bool connected;
	int ret;

	/* Added delay as need time to reflect hpd after interrupt*/
	msleep(200);

	mutex_lock(&lt9611c->mcu_lock);
	ret = lt9611c_read_write_flow(lt9611c, &cmd, &rsp);
	if (ret)
		dev_err(dev, "failed to read HPD status\n");
	else
		lt9611c->hdmi_connected = (hpd_status == 0x02);

	connected = lt9611c->hdmi_connected;
	mutex_unlock(&lt9611c->mcu_lock);

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

	if (tmds_rate > lt9611c->max_tmds_rate)
		return MODE_CLOCK_HIGH;

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
	u8 timing_data[22];
	struct lt9611c_rsp rsp = {};
	u8 framerate;
	u8 vic = 0x00;
	struct lt9611c_cmd cmd = {
		.hdr = { LT9611C_FUNC_WRITE, LT9611C_TYPE_MIPI, 0x33, LT9611C_CMD_SEP },
		.data = timing_data,
		.data_len = ARRAY_SIZE(timing_data),
	};

	guard(mutex)(&lt9611c->mcu_lock);
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

	put_unaligned_be16(h_total,       &timing_data[0]);
	put_unaligned_be16(hactive,       &timing_data[2]);
	put_unaligned_be16(hfront_porch,  &timing_data[4]);
	put_unaligned_be16(hsync_len,     &timing_data[6]);
	put_unaligned_be16(hback_porch,   &timing_data[8]);
	put_unaligned_be16(v_total,       &timing_data[10]);
	put_unaligned_be16(vactive,       &timing_data[12]);
	put_unaligned_be16(vfront_porch,  &timing_data[14]);
	put_unaligned_be16(vsync_len,     &timing_data[16]);
	put_unaligned_be16(vback_porch,   &timing_data[18]);
	timing_data[20] = framerate;
	timing_data[21] = vic;

	ret = lt9611c_read_write_flow(lt9611c, &cmd, &rsp);
	if (ret)
		dev_err(dev, "video set failed\n");
}

static void lt9611c_bridge_atomic_enable(struct drm_bridge *bridge,
					 struct drm_atomic_commit *state)
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

static enum drm_connector_status
lt9611c_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	struct device *dev = lt9611c->dev;
	int ret;
	bool connected = false;
	static const u8 hpd_data[] = { 0x00 };
	struct lt9611c_cmd cmd = {
		.hdr = { LT9611C_FUNC_READ, LT9611C_TYPE_HDMI, 0x31, LT9611C_CMD_SEP },
		.data = hpd_data,
		.data_len = 1,
	};
	u8 hpd_status;
	struct lt9611c_rsp rsp = { .data = &hpd_status, .data_len = 1 };

	guard(mutex)(&lt9611c->mcu_lock);

	ret = lt9611c_read_write_flow(lt9611c, &cmd, &rsp);
	if (ret)
		dev_err(dev, "failed to read HPD status (err=%d)\n", ret);
	else
		connected = (hpd_status == 0x02);

	lt9611c->hdmi_connected = connected;

	return connected ? connector_status_connected :
				connector_status_disconnected;
}

static int lt9611c_get_edid_block(void *data, u8 *buf,
				  unsigned int block, size_t len)
{
	struct lt9611c *lt9611c = data;
	struct device *dev = lt9611c->dev;
	u8 edid_raw[LT9611C_CMD_Y0_SIZE + LT9611C_EDID_BUF_SIZE];
	u8 y0;
	int ret, i, offset = 0;
	struct lt9611c_cmd cmd = {
		.hdr = { LT9611C_FUNC_READ, LT9611C_TYPE_HDMI, 0x33, LT9611C_CMD_SEP },
	};
	struct lt9611c_rsp rsp = {
		.data = edid_raw,
		.data_len = LT9611C_CMD_Y0_SIZE + LT9611C_EDID_BUF_SIZE,
	};

	if (len != 128)
		return -EINVAL;
	guard(mutex)(&lt9611c->mcu_lock);

	for (i = 0; i < 4; i++) {
		y0 = block * 4 + i;
		cmd.data = &y0;
		cmd.data_len = 1;
		ret = lt9611c_read_write_flow(lt9611c, &cmd, &rsp);
		if (ret) {
			dev_err(dev, "Failed to read EDID block %u packet %d\n",
				block, i);
			return ret;
		}
		memcpy(buf + offset, &edid_raw[LT9611C_CMD_Y0_SIZE], LT9611C_EDID_BUF_SIZE);
		offset += LT9611C_EDID_BUF_SIZE;
	}

	return 0;
}

static const struct drm_edid *lt9611c_bridge_edid_read(struct drm_bridge *bridge,
						       struct drm_connector *connector)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);

	return drm_edid_read_custom(connector, lt9611c_get_edid_block, lt9611c);
}

static int lt9611c_write_infoframe(struct lt9611c *lt9611c, u8 type,
				   const u8 *buffer, size_t len)
{
	u8 extra[1 + LT9611C_INFOFRAME_MAX_SIZE];
	struct lt9611c_rsp rsp = {};
	struct lt9611c_cmd cmd = {
		.hdr = { LT9611C_FUNC_WRITE, LT9611C_TYPE_HDMI, 0x35, LT9611C_CMD_SEP },
	};

	if (WARN_ON(len > LT9611C_INFOFRAME_MAX_SIZE))
		return -EINVAL;

	extra[0] = type;
	memcpy(&extra[1], buffer, len);
	cmd.data = extra;
	cmd.data_len = 1 + len;

	guard(mutex)(&lt9611c->mcu_lock);

	return lt9611c_read_write_flow(lt9611c, &cmd, &rsp);
}

static int lt9611c_clear_infoframe(struct lt9611c *lt9611c, u8 type)
{
	u8 clear_data = type;
	struct lt9611c_cmd cmd = {
		.hdr = { LT9611C_FUNC_WRITE, LT9611C_TYPE_HDMI, 0x42, LT9611C_CMD_SEP },
		.data = &clear_data,
		.data_len = 1,
	};
	struct lt9611c_rsp rsp = {};

	guard(mutex)(&lt9611c->mcu_lock);

	return lt9611c_read_write_flow(lt9611c, &cmd, &rsp);
}

static int lt9611c_hdmi_write_avi_infoframe(struct drm_bridge *bridge,
					    const u8 *buffer, size_t len)
{
	return lt9611c_write_infoframe(bridge_to_lt9611c(bridge), 0x01, buffer, len);
}

static int lt9611c_hdmi_clear_avi_infoframe(struct drm_bridge *bridge)
{
	return lt9611c_clear_infoframe(bridge_to_lt9611c(bridge), 0x01);
}

static int lt9611c_hdmi_write_hdmi_infoframe(struct drm_bridge *bridge,
					     const u8 *buffer, size_t len)
{
	return lt9611c_write_infoframe(bridge_to_lt9611c(bridge), 0x04, buffer, len);
}

static int lt9611c_hdmi_clear_hdmi_infoframe(struct drm_bridge *bridge)
{
	return lt9611c_clear_infoframe(bridge_to_lt9611c(bridge), 0x04);
}

static int lt9611c_hdmi_write_audio_infoframe(struct drm_bridge *bridge,
					      const u8 *buffer, size_t len)
{
	return lt9611c_write_infoframe(bridge_to_lt9611c(bridge), 0x02, buffer, len);
}

static int lt9611c_hdmi_clear_audio_infoframe(struct drm_bridge *bridge)
{
	return lt9611c_clear_infoframe(bridge_to_lt9611c(bridge), 0x02);
}

static int lt9611c_hdmi_audio_prepare(struct drm_bridge *bridge,
				      struct drm_connector *connector,
				      struct hdmi_codec_daifmt *fmt,
				      struct hdmi_codec_params *hparms)
{
	struct lt9611c *lt9611c = bridge_to_lt9611c(bridge);
	u8 audio_extra[2];
	struct lt9611c_rsp rsp = {};
	int ret;
	struct lt9611c_cmd cmd = {
		.hdr = { LT9611C_FUNC_WRITE, LT9611C_TYPE_HDMI, 0x36, LT9611C_CMD_SEP },
				   .data = audio_extra,
				   .data_len = ARRAY_SIZE(audio_extra) };

	if (hparms->sample_width == 32)
		return -EINVAL;

	switch (fmt->fmt) {
	case HDMI_I2S:
		audio_extra[0] = 0x01;
		break;
	case HDMI_SPDIF:
		audio_extra[0] = 0x02;
		break;
	default:
		return -EINVAL;
	}

	audio_extra[1] = hparms->channels;

	scoped_guard(mutex, &lt9611c->mcu_lock) {
		ret = lt9611c_read_write_flow(lt9611c, &cmd, &rsp);
		if (ret < 0) {
			dev_err(lt9611c->dev, "set audio info failed!\n");
			return ret;
		}
	}

	return drm_atomic_helper_connector_hdmi_update_audio_infoframe(connector,
									&hparms->cea);
}

static void lt9611c_hdmi_audio_shutdown(struct drm_bridge *bridge,
					struct drm_connector *connector)
{
	drm_atomic_helper_connector_hdmi_clear_audio_infoframe(connector);
}

static const struct drm_bridge_funcs lt9611c_bridge_funcs = {
	.attach = lt9611c_bridge_attach,
	.detect = lt9611c_bridge_detect,
	.edid_read = lt9611c_bridge_edid_read,
	.atomic_enable = lt9611c_bridge_atomic_enable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_create_state = drm_atomic_helper_bridge_create_state,

	.hdmi_tmds_char_rate_valid = lt9611c_hdmi_tmds_char_rate_valid,
	.hdmi_write_avi_infoframe = lt9611c_hdmi_write_avi_infoframe,
	.hdmi_clear_avi_infoframe = lt9611c_hdmi_clear_avi_infoframe,
	.hdmi_write_hdmi_infoframe = lt9611c_hdmi_write_hdmi_infoframe,
	.hdmi_clear_hdmi_infoframe = lt9611c_hdmi_clear_hdmi_infoframe,
	.hdmi_write_audio_infoframe = lt9611c_hdmi_write_audio_infoframe,
	.hdmi_clear_audio_infoframe = lt9611c_hdmi_clear_audio_infoframe,

	.hdmi_audio_prepare = lt9611c_hdmi_audio_prepare,
	.hdmi_audio_shutdown = lt9611c_hdmi_audio_shutdown,
};

static int lt9611c_parse_dt(struct device *dev,
			    struct lt9611c *lt9611c)
{
	int ret;

	lt9611c->dsi0_node = of_graph_get_remote_node(dev->of_node, 0, -1);
	if (!lt9611c->dsi0_node)
		return dev_err_probe(dev, -ENODEV, "failed to get remote node for primary dsi\n");

	lt9611c->dsi1_node = of_graph_get_remote_node(dev->of_node, 1, -1);

	if (lt9611c->dsi1_node && lt9611c->chip_type == CHIP_LT9611C) {
		ret = dev_err_probe(dev, -EINVAL,
				    "LT9611C does not support dual DSI\n");
		goto err_put_dsi1;
	}

	lt9611c->bridge.next_bridge = of_drm_get_bridge_by_endpoint(dev->of_node, 2, -1);
	if (IS_ERR(lt9611c->bridge.next_bridge)) {
		ret = PTR_ERR(lt9611c->bridge.next_bridge);
		goto err_put_dsi1;
	}

	return 0;

err_put_dsi1:
	of_node_put(lt9611c->dsi1_node);
	of_node_put(lt9611c->dsi0_node);
	return ret;
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

static ssize_t firmware_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t len)
{
	struct lt9611c *lt9611c = dev_get_drvdata(dev);
	int ret;

	dev_warn(dev, "starting firmware upgrade — display will be disrupted\n");

	ret = lt9611c_firmware_upgrade(lt9611c);
	if (ret < 0) {
		dev_err(dev, "upgrade failure\n");
		return ret;
	}

	lt9611c_lock(lt9611c);
	lt9611c->fw_version = lt9611c_read_version(lt9611c);
	lt9611c_unlock(lt9611c);

	if (lt9611c->fw_version < 0)
		dev_warn(dev, "upgrade succeeded but failed to read new fw version\n");
	else
		dev_info(dev, "firmware upgrade succeeded, version: 0x%04x\n",
			 lt9611c->fw_version);

	return len;
}

static ssize_t firmware_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lt9611c *lt9611c = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%04x\n", lt9611c->fw_version);
}

static DEVICE_ATTR_RW(firmware);

static struct attribute *lt9611c_attrs[] = {
	&dev_attr_firmware.attr,
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

	const struct lt9611c_chip_data *cdata = i2c_get_match_data(client);

	if (!cdata)
		return dev_err_probe(dev, -EINVAL, "no match data for device\n");

	lt9611c->chip_type = cdata->chip_type;
	lt9611c->max_tmds_rate = cdata->max_tmds_rate;

	ret = devm_mutex_init(dev, &lt9611c->mcu_lock);
	if (ret)
		return dev_err_probe(dev, ret, "failed to init mutex\n");

	lt9611c->regmap = devm_regmap_init_i2c(client, &lt9611c_regmap_config);
	if (IS_ERR(lt9611c->regmap))
		return dev_err_probe(dev, PTR_ERR(lt9611c->regmap), "regmap i2c init failed\n");

	ret = lt9611c_parse_dt(dev, lt9611c);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse device tree\n");

	lt9611c->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lt9611c->reset_gpio)) {
		ret = PTR_ERR(lt9611c->reset_gpio);
		goto err_of_put;
	}

	ret = lt9611c_regulator_init(lt9611c);
	if (ret < 0)
		goto err_of_put;

	ret = regulator_bulk_enable(ARRAY_SIZE(lt9611c->supplies), lt9611c->supplies);
	if (ret)
		goto err_of_put;

	lt9611c_reset(lt9611c);

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
			lt9611c_unlock(lt9611c);
			ret = lt9611c_firmware_upgrade(lt9611c);
			if (ret < 0)
				goto err_disable_regulators;
			lt9611c_lock(lt9611c);
			goto retry;

		} else {
			dev_err(dev, "fw version 0x%04x, update failed\n", lt9611c->fw_version);
			ret = -EOPNOTSUPP;
			lt9611c_unlock(lt9611c);
			goto err_disable_regulators;
		}
	}

	lt9611c_unlock(lt9611c);
	dev_dbg(dev, "current version:0x%04x", lt9611c->fw_version);

	INIT_WORK(&lt9611c->work, lt9611c_hpd_work);

	ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					lt9611c_irq_thread_handler,
					IRQF_TRIGGER_FALLING |
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
	lt9611c->bridge.product = "LT9611C";

	lt9611c->bridge.hdmi_audio_dev = dev;
	lt9611c->bridge.hdmi_audio_max_i2s_playback_channels = 8;
	lt9611c->bridge.hdmi_audio_dai_port = 2;

	drm_bridge_add(&lt9611c->bridge);

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
	enable_irq(client->irq);

	return 0;

err_remove_bridge:
	drm_bridge_remove(&lt9611c->bridge);
	cancel_work_sync(&lt9611c->work);

err_disable_regulators:
	regulator_bulk_disable(ARRAY_SIZE(lt9611c->supplies), lt9611c->supplies);

err_of_put:
	of_node_put(lt9611c->dsi1_node);
	of_node_put(lt9611c->dsi0_node);

	return ret;
}

static void lt9611c_remove(struct i2c_client *client)
{
	struct lt9611c *lt9611c = i2c_get_clientdata(client);

	disable_irq(client->irq);
	cancel_work_sync(&lt9611c->work);
	drm_bridge_remove(&lt9611c->bridge);
	regulator_bulk_disable(ARRAY_SIZE(lt9611c->supplies), lt9611c->supplies);
	of_node_put(lt9611c->dsi1_node);
	of_node_put(lt9611c->dsi0_node);
}

static int lt9611c_bridge_suspend(struct device *dev)
{
	struct lt9611c *lt9611c = dev_get_drvdata(dev);
	int ret;

	disable_irq(lt9611c->client->irq);
	cancel_work_sync(&lt9611c->work);

	gpiod_set_value_cansleep(lt9611c->reset_gpio, 1);

	ret = regulator_bulk_disable(ARRAY_SIZE(lt9611c->supplies), lt9611c->supplies);
	if (ret) {
		dev_err(lt9611c->dev, "regulator bulk disable failed.\n");
		gpiod_set_value_cansleep(lt9611c->reset_gpio, 0);
		enable_irq(lt9611c->client->irq);
		return ret;
	}

	return 0;
}

static int lt9611c_bridge_resume(struct device *dev)
{
	struct lt9611c *lt9611c = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(lt9611c->supplies), lt9611c->supplies);
	if (ret) {
		dev_err(lt9611c->dev, "regulator bulk enable failed.\n");
		return ret;
	}
	lt9611c_reset(lt9611c);
	enable_irq(lt9611c->client->irq);

	return ret;
}

static const struct dev_pm_ops lt9611c_bridge_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(lt9611c_bridge_suspend,
				lt9611c_bridge_resume)
};

static struct i2c_device_id lt9611c_id[] = {
	{ "lontium,lt9611c",   (kernel_ulong_t)&lt9611c_chip_data[CHIP_LT9611C]   },
	{ "lontium,lt9611ex",  (kernel_ulong_t)&lt9611c_chip_data[CHIP_LT9611EX]  },
	{ "lontium,lt9611uxd", (kernel_ulong_t)&lt9611c_chip_data[CHIP_LT9611UXD] },
	{ /* sentinel */ }
};

static const struct of_device_id lt9611c_match_table[] = {
	{ .compatible = "lontium,lt9611c",   .data = &lt9611c_chip_data[CHIP_LT9611C]   },
	{ .compatible = "lontium,lt9611ex",  .data = &lt9611c_chip_data[CHIP_LT9611EX]  },
	{ .compatible = "lontium,lt9611uxd", .data = &lt9611c_chip_data[CHIP_LT9611UXD] },
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
