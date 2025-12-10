// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

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

#define FW_FILE "lt8713sx_fw.bin"

#define LT8713SX_PAGE_SIZE 256
#define FW_12K_SIZE        (12  * 1024)
#define FW_64K_SIZE        (64  * 1024)
#define FW_256K_SIZE       (256 * 1024)

struct crc_config {
	u8 width;
	u32 poly;
	u32 crc_init;
	u32 xor_out;
	bool ref_in;
	bool ref_out;
};

struct lt8713sx {
	struct device *dev;

	struct regmap *regmap;
	/* Protects all accesses to registers by stopping the on-chip MCU */
	struct mutex ocm_lock;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;

	struct regulator_bulk_data supplies[2];

	struct i2c_client *client;
	const struct firmware *fw;

	u8 *fw_buffer;

	u32 main_crc_value;
	u32 bank_crc_value[17];

	int bank_num;
};

static void lt8713sx_reset(struct lt8713sx *lt8713sx);

static const struct regmap_range lt8713sx_ranges[] = {
	{
		.range_min = 0,
		.range_max = 0xffff
	},
};

static const struct regmap_access_table lt8713sx_table = {
	.yes_ranges = lt8713sx_ranges,
	.n_yes_ranges = ARRAY_SIZE(lt8713sx_ranges),
};

static const struct regmap_config lt8713sx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &lt8713sx_table,
	.cache_type = REGCACHE_NONE,
};

static void lt8713sx_i2c_enable(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xff, 0xe0);
	regmap_write(lt8713sx->regmap, 0xee, 0x01);
}

static void lt8713sx_i2c_disable(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xff, 0xe0);
	regmap_write(lt8713sx->regmap, 0xee, 0x00);
}

static unsigned int bits_reverse(u32 in_val, u8 bits)
{
	u32 out_val = 0;
	u8 i;

	for (i = 0; i < bits; i++) {
		if (in_val & (1 << i))
			out_val |= 1 << (bits - 1 - i);
	}

	return out_val;
}

static unsigned int get_crc(struct crc_config crc_cfg, const  u8 *buf, u64 buf_len)
{
	u8 width    = crc_cfg.width;
	u32  poly   = crc_cfg.poly;
	u32  crc    = crc_cfg.crc_init;
	u32  xorout = crc_cfg.xor_out;
	bool refin  = crc_cfg.ref_in;
	bool refout = crc_cfg.ref_out;
	u8 n;
	u32  bits;
	u32  data;
	u8 i;

	n    =  (width < 8) ? 0 : (width - 8);
	crc  =  (width < 8) ? (crc << (8 - width)) : crc;
	bits =  (width < 8) ? 0x80 : (1 << (width - 1));
	poly =  (width < 8) ? (poly << (8 - width)) : poly;

	while (buf_len--) {
		data = *(buf++);
		if (refin)
			data = bits_reverse(data, 8);
		crc ^= (data << n);
		for (i = 0; i < 8; i++) {
			if (crc & bits)
				crc = (crc << 1) ^ poly;
			else
				crc = crc << 1;
		}
	}
	crc = (width < 8) ? (crc >> (8 - width)) : crc;
	if (refout)
		crc = bits_reverse(crc, width);
	crc ^= xorout;

	return (crc & ((2 << (width - 1)) - 1));
}

static u32 calculate_64K_crc(const u8 *upgrade_data, u64 len)
{
	struct crc_config crc_cfg = {
		.width = 8,
		.poly  = 0x31,
		.crc_init = 0,
		.xor_out = 0,
		.ref_out = false,
		.ref_in = false,
	};
	u64 crc_size = FW_64K_SIZE - 1;
	u8 default_val = 0xFF;

	crc_cfg.crc_init = get_crc(crc_cfg, upgrade_data, len);

	crc_size -= len;
	while (crc_size--)
		crc_cfg.crc_init = get_crc(crc_cfg, &default_val, 1);

	return crc_cfg.crc_init;
}

static u32 calculate_12K_crc(const u8 *upgrade_data, u64 len)
{
	struct crc_config crc_cfg = {
		.width = 8,
		.poly  = 0x31,
		.crc_init = 0,
		.xor_out = 0,
		.ref_out = false,
		.ref_in = false,
	};
	u64 crc_size = FW_12K_SIZE;
	u8 default_val = 0xFF;

	crc_cfg.crc_init = get_crc(crc_cfg, upgrade_data, len);

	crc_size -= len;
	while (crc_size--)
		crc_cfg.crc_init = get_crc(crc_cfg, &default_val, 1);

	return crc_cfg.crc_init;
}

static int lt8713sx_prepare_firmware_data(struct lt8713sx *lt8713sx)
{
	int ret = 0;

	ret = request_firmware(&lt8713sx->fw, FW_FILE, lt8713sx->dev);
	if (ret < 0) {
		pr_err("request firmware failed\n");
		return ret;
	}

	pr_debug("Firmware size: %zu bytes\n", lt8713sx->fw->size);

	if (lt8713sx->fw->size > FW_256K_SIZE - 1) {
		pr_err("Firmware size exceeds 256KB limit\n");
		release_firmware(lt8713sx->fw);
		return -EINVAL;
	}

	lt8713sx->fw_buffer = kzalloc(FW_256K_SIZE, GFP_KERNEL);
	if (!lt8713sx->fw_buffer) {
		release_firmware(lt8713sx->fw);
		return -ENOMEM;
	}

	memset(lt8713sx->fw_buffer, 0xFF, FW_256K_SIZE);

	if (lt8713sx->fw->size < FW_64K_SIZE) {
		/*TODO: CRC should be calculated with 0xff also */
		memcpy(lt8713sx->fw_buffer, lt8713sx->fw->data, lt8713sx->fw->size);
		lt8713sx->fw_buffer[FW_64K_SIZE - 1] =
				calculate_64K_crc(lt8713sx->fw->data, lt8713sx->fw->size);
		lt8713sx->main_crc_value = lt8713sx->fw_buffer[FW_64K_SIZE - 1];
		pr_debug("Main Firmware Data  Crc=0x%02X\n", lt8713sx->main_crc_value);

	} else {
		//main firmware
		memcpy(lt8713sx->fw_buffer, lt8713sx->fw->data, FW_64K_SIZE - 1);
		lt8713sx->fw_buffer[FW_64K_SIZE - 1] =
				calculate_64K_crc(lt8713sx->fw_buffer, FW_64K_SIZE - 1);
		lt8713sx->main_crc_value = lt8713sx->fw_buffer[FW_64K_SIZE - 1];
		pr_debug("Main Firmware Data  Crc=0x%02X\n", lt8713sx->main_crc_value);

		//bank firmware
		memcpy(lt8713sx->fw_buffer + FW_64K_SIZE,
		       lt8713sx->fw->data + FW_64K_SIZE,
		       lt8713sx->fw->size - FW_64K_SIZE);

		lt8713sx->bank_num = (lt8713sx->fw->size - FW_64K_SIZE + FW_12K_SIZE - 1) /
					FW_12K_SIZE;
		pr_debug("Bank Number Total is %d.\n", lt8713sx->bank_num);

		for (int i = 0; i < lt8713sx->bank_num; i++) {
			lt8713sx->bank_crc_value[i] =
				calculate_12K_crc(lt8713sx->fw_buffer + FW_64K_SIZE +
						  i * FW_12K_SIZE,
						  FW_12K_SIZE);
			pr_debug("Bank number:%d; Firmware Data  Crc:0x%02X\n",
				 i, lt8713sx->bank_crc_value[i]);
		}
	}
	return 0;
}

static void lt8713sx_config_parameters(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xFF, 0xE0);
	regmap_write(lt8713sx->regmap, 0xEE, 0x01);
	regmap_write(lt8713sx->regmap, 0x5E, 0xC1);
	regmap_write(lt8713sx->regmap, 0x58, 0x00);
	regmap_write(lt8713sx->regmap, 0x59, 0x50);
	regmap_write(lt8713sx->regmap, 0x5A, 0x10);
	regmap_write(lt8713sx->regmap, 0x5A, 0x00);
	regmap_write(lt8713sx->regmap, 0x58, 0x21);
}

static void lt8713sx_wren(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xff, 0xe1);
	regmap_write(lt8713sx->regmap, 0x03, 0xbf);
	regmap_write(lt8713sx->regmap, 0x03, 0xff);
	regmap_write(lt8713sx->regmap, 0xff, 0xe0);
	regmap_write(lt8713sx->regmap, 0x5a, 0x04);
	regmap_write(lt8713sx->regmap, 0x5a, 0x00);
}

static void lt8713sx_wrdi(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0x5A, 0x08);
	regmap_write(lt8713sx->regmap, 0x5A, 0x00);
}

static void lt8713sx_fifo_reset(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xff, 0xe1);
	regmap_write(lt8713sx->regmap, 0x03, 0xbf);
	regmap_write(lt8713sx->regmap, 0x03, 0xff);
}

static void lt8713sx_disable_sram_write(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xff, 0xe0);
	regmap_write(lt8713sx->regmap, 0x55, 0x00);
}

static void lt8713sx_sram_to_flash(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0x5a, 0x30);
	regmap_write(lt8713sx->regmap, 0x5a, 0x00);
}

static void lt8713sx_i2c_to_sram(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0x55, 0x80);
	regmap_write(lt8713sx->regmap, 0x5e, 0xc0);
	regmap_write(lt8713sx->regmap, 0x58, 0x21);
}

static u8 lt8713sx_read_flash_status(struct lt8713sx *lt8713sx)
{
	u32 flash_status = 0;

	regmap_write(lt8713sx->regmap,  0xFF, 0xE1);//fifo_rst_n
	regmap_write(lt8713sx->regmap,  0x03, 0x3F);
	regmap_write(lt8713sx->regmap,  0x03, 0xFF);

	regmap_write(lt8713sx->regmap,  0xFF, 0xE0);
	regmap_write(lt8713sx->regmap,  0x5e, 0x40);
	regmap_write(lt8713sx->regmap,  0x56, 0x05);//opcode=read status register
	regmap_write(lt8713sx->regmap,  0x55, 0x25);
	regmap_write(lt8713sx->regmap,  0x55, 0x01);
	regmap_write(lt8713sx->regmap,  0x58, 0x21);

	regmap_read(lt8713sx->regmap, 0x5f, &flash_status);
	pr_debug("flash_status:%x\n", flash_status);

	return flash_status;
}

static void lt8713sx_block_erase(struct lt8713sx *lt8713sx)
{
	u32 i = 0;
	u8 flash_status = 0;
	u8 blocknum = 0x00;
	u32 flashaddr = 0x00;

	for (blocknum = 0; blocknum < 8; blocknum++) {
		flashaddr = blocknum * 0x008000;
		regmap_write(lt8713sx->regmap,  0xFF, 0xE0);
		regmap_write(lt8713sx->regmap,  0xEE, 0x01);
		regmap_write(lt8713sx->regmap,  0x5A, 0x04);
		regmap_write(lt8713sx->regmap,  0x5A, 0x00);
		regmap_write(lt8713sx->regmap,  0x5B, flashaddr >> 16);//set flash address[23:16]
		regmap_write(lt8713sx->regmap,  0x5C, flashaddr >> 8);//set flash address[15:8]
		regmap_write(lt8713sx->regmap,  0x5D, flashaddr);//set flash address[7:0]
		regmap_write(lt8713sx->regmap,  0x5A, 0x01);
		regmap_write(lt8713sx->regmap,  0x5A, 0x00);
		msleep(100); //delay 100ms
		i = 0;
		while (1) {
			flash_status = lt8713sx_read_flash_status(lt8713sx); //wait erase finish
			if ((flash_status & 0x01) == 0)
				break;

			if (i > 50)
				break;

			i++;
			msleep(50); //delay 50ms
		}
	}
	pr_debug("erase flash done.\n");
}

static void lt8713sx_load_main_fw_to_sram(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xff, 0xe0);
	regmap_write(lt8713sx->regmap, 0xee, 0x01);
	regmap_write(lt8713sx->regmap, 0x68, 0x00);
	regmap_write(lt8713sx->regmap, 0x69, 0x00);
	regmap_write(lt8713sx->regmap, 0x6a, 0x00);
	regmap_write(lt8713sx->regmap, 0x65, 0x00);
	regmap_write(lt8713sx->regmap, 0x66, 0xff);
	regmap_write(lt8713sx->regmap, 0x67, 0xff);
	regmap_write(lt8713sx->regmap, 0x6b, 0x00);
	regmap_write(lt8713sx->regmap, 0x6c, 0x00);
	regmap_write(lt8713sx->regmap, 0x60, 0x01);
	msleep(200);
	regmap_write(lt8713sx->regmap, 0x60, 0x00);
}

static void lt8713sx_load_bank_fw_to_sram(struct lt8713sx *lt8713sx, u64 addr)
{
	regmap_write(lt8713sx->regmap, 0xff, 0xe0);
	regmap_write(lt8713sx->regmap, 0xee, 0x01);
	regmap_write(lt8713sx->regmap, 0x68, ((addr & 0xFF0000) >> 16));
	regmap_write(lt8713sx->regmap, 0x69, ((addr & 0x00FF00) >> 8));
	regmap_write(lt8713sx->regmap, 0x6a, (addr & 0x0000FF));
	regmap_write(lt8713sx->regmap, 0x65, 0x00);
	regmap_write(lt8713sx->regmap, 0x66, 0x30);
	regmap_write(lt8713sx->regmap, 0x67, 0x00);
	regmap_write(lt8713sx->regmap, 0x6b, 0x00);
	regmap_write(lt8713sx->regmap, 0x6c, 0x00);
	regmap_write(lt8713sx->regmap, 0x60, 0x01);
	msleep(50);
	regmap_write(lt8713sx->regmap, 0x60, 0x00);
}

static int lt8713sx_write_data(struct lt8713sx *lt8713sx, const u8 *data, u64 filesize)
{
	int page = 0, num = 0, i = 0, val;

	page = (filesize % LT8713SX_PAGE_SIZE) ?
			((filesize / LT8713SX_PAGE_SIZE) + 1) : (filesize / LT8713SX_PAGE_SIZE);

	pr_debug("Writing to Sram=%u pages, total size = %llu bytes\n", page, filesize);

	for (num = 0; num < page; num++) {
		pr_debug("page[%d]\n", num);
		lt8713sx_i2c_to_sram(lt8713sx);

		for (i = 0; i < LT8713SX_PAGE_SIZE; i++) {
			if ((num * LT8713SX_PAGE_SIZE + i) < filesize)
				val = *(data + (num * LT8713SX_PAGE_SIZE + i));
			else
				val = 0xFF;
			regmap_write(lt8713sx->regmap, 0x59, val);
		}

		lt8713sx_wren(lt8713sx);
		lt8713sx_sram_to_flash(lt8713sx);
	}

	lt8713sx_wrdi(lt8713sx);
	lt8713sx_disable_sram_write(lt8713sx);

	return 0;
}

static void lt8713sx_main_upgrade_result(struct lt8713sx *lt8713sx)
{
	u32 main_crc_result;

	regmap_write(lt8713sx->regmap, 0xff, 0xe0);
	regmap_read(lt8713sx->regmap, 0x23, &main_crc_result);

	pr_debug("Main CRC HW: 0x%02X\n", main_crc_result);
	pr_debug("Main CRC FW: 0x%02X\n", lt8713sx->main_crc_value);

	if (main_crc_result == lt8713sx->main_crc_value)
		pr_debug("Main Firmware Upgrade Success.\n");
	else
		pr_err("Main Firmware Upgrade Failed.\n");
}

static void lt8713sx_bank_upgrade_result(struct lt8713sx *lt8713sx, u8 banknum)
{
	u32 bank_crc_result;

	regmap_write(lt8713sx->regmap, 0xff, 0xe0);

	regmap_read(lt8713sx->regmap, 0x23, &bank_crc_result);

	pr_debug("Bank %d CRC Result: 0x%02X\n", banknum, bank_crc_result);

	if (bank_crc_result == lt8713sx->bank_crc_value[banknum])
		pr_debug("Bank %d Firmware Upgrade Success.\n", banknum);
	else
		pr_err("Bank %d Firmware Upgrade Failed.\n", banknum);
}

static void lt8713sx_bank_result_check(struct lt8713sx *lt8713sx)
{
	int i;
	u64 addr = 0x010000;

	for (i = 0; i < lt8713sx->bank_num; i++) {
		lt8713sx_load_bank_fw_to_sram(lt8713sx, addr);
		lt8713sx_bank_upgrade_result(lt8713sx, i);
		addr += 0x3000;
	}
}

static int lt8713sx_firmware_upgrade(struct lt8713sx *lt8713sx)
{
	int ret;

	lt8713sx_config_parameters(lt8713sx);

	lt8713sx_block_erase(lt8713sx);

	if (lt8713sx->fw->size < FW_64K_SIZE) {
		ret = lt8713sx_write_data(lt8713sx, lt8713sx->fw_buffer, FW_64K_SIZE);
		if (ret < 0) {
			pr_err("Failed to write firmware data: %d\n", ret);
			return ret;
		}
	} else {
		ret = lt8713sx_write_data(lt8713sx, lt8713sx->fw_buffer, lt8713sx->fw->size);
		if (ret < 0) {
			pr_err("Failed to write firmware data: %d\n", ret);
			return ret;
		}
	}

	pr_debug("Write Data done.\n");

	return 0;
}

static int lt8713sx_firmware_update(struct lt8713sx *lt8713sx)
{
	int ret = 0;

	mutex_lock(&lt8713sx->ocm_lock);
	lt8713sx_i2c_enable(lt8713sx);

	ret = lt8713sx_prepare_firmware_data(lt8713sx);
	if (ret < 0) {
		pr_err("Failed to prepare firmware data: %d\n", ret);
		goto error;
	}

	ret = lt8713sx_firmware_upgrade(lt8713sx);
	if (ret < 0) {
		pr_err("Upgrade failure.\n");
		goto error;
	} else {
		/* Validate CRC */
		lt8713sx_load_main_fw_to_sram(lt8713sx);
		lt8713sx_main_upgrade_result(lt8713sx);
		lt8713sx_wrdi(lt8713sx);
		lt8713sx_fifo_reset(lt8713sx);
		lt8713sx_bank_result_check(lt8713sx);
		lt8713sx_wrdi(lt8713sx);
	}

error:
	lt8713sx_i2c_disable(lt8713sx);
	if (!ret)
		lt8713sx_reset(lt8713sx);

	kfree(lt8713sx->fw_buffer);
	lt8713sx->fw_buffer = NULL;

	if (lt8713sx->fw) {
		release_firmware(lt8713sx->fw);
		lt8713sx->fw = NULL;
	}
	mutex_unlock(&lt8713sx->ocm_lock);

	return ret;
}

static void lt8713sx_reset(struct lt8713sx *lt8713sx)
{
	pr_debug("reset bridge.\n");
	gpiod_set_value_cansleep(lt8713sx->reset_gpio, 1);
	msleep(20);

	gpiod_set_value_cansleep(lt8713sx->reset_gpio, 0);
	msleep(20);

	gpiod_set_value_cansleep(lt8713sx->reset_gpio, 1);
	msleep(20);
	pr_debug("reset done.\n");
}

static int lt8713sx_regulator_init(struct lt8713sx *lt8713sx)
{
	int ret;

	lt8713sx->supplies[0].supply = "vdd";
	lt8713sx->supplies[1].supply = "vcc";

	ret = devm_regulator_bulk_get(lt8713sx->dev, 2, lt8713sx->supplies);
	if (ret < 0)
		return dev_err_probe(lt8713sx->dev, ret, "failed to get regulators\n");

	ret = regulator_set_load(lt8713sx->supplies[0].consumer, 200000);
	if (ret < 0)
		return dev_err_probe(lt8713sx->dev, ret, "failed to set regulator load\n");

	return 0;
}

static int lt8713sx_regulator_enable(struct lt8713sx *lt8713sx)
{
	int ret;

	ret = regulator_enable(lt8713sx->supplies[0].consumer);
	if (ret < 0)
		return dev_err_probe(lt8713sx->dev, ret, "failed to enable vdd regulator\n");

	usleep_range(1000, 10000);

	ret = regulator_enable(lt8713sx->supplies[1].consumer);
	if (ret < 0) {
		regulator_disable(lt8713sx->supplies[0].consumer);
		return dev_err_probe(lt8713sx->dev, ret, "failed to enable vcc regulator\n");
	}
	return 0;
}

static int lt8713sx_gpio_init(struct lt8713sx *lt8713sx)
{
	struct device *dev = lt8713sx->dev;

	lt8713sx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lt8713sx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(lt8713sx->reset_gpio),
				     "failed to acquire reset gpio\n");

	/* power enable gpio */
	lt8713sx->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(lt8713sx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(lt8713sx->enable_gpio),
				     "failed to acquire enable gpio\n");
	return 0;
}

static ssize_t lt8713sx_firmware_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct lt8713sx *lt8713sx = dev_get_drvdata(dev);
	int ret;

	ret = lt8713sx_firmware_update(lt8713sx);
	if (ret < 0)
		return ret;
	return len;
}

static DEVICE_ATTR_WO(lt8713sx_firmware);

static struct attribute *lt8713sx_attrs[] = {
	&dev_attr_lt8713sx_firmware.attr,
	NULL,
};

static const struct attribute_group lt8713sx_attr_group = {
	.attrs = lt8713sx_attrs,
};

static const struct attribute_group *lt8713sx_attr_groups[] = {
	&lt8713sx_attr_group,
	NULL,
};

static int lt8713sx_probe(struct i2c_client *client)
{
	struct lt8713sx *lt8713sx;
	struct device *dev = &client->dev;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return dev_err_probe(dev, -ENODEV, "device doesn't support I2C\n");

	lt8713sx = devm_kzalloc(dev, sizeof(*lt8713sx), GFP_KERNEL);
	if (!lt8713sx)
		return dev_err_probe(dev, -ENOMEM, "failed to allocate lt8713sx struct\n");

	lt8713sx->dev = dev;
	lt8713sx->client = client;
	i2c_set_clientdata(client, lt8713sx);

	mutex_init(&lt8713sx->ocm_lock);

	lt8713sx->regmap = devm_regmap_init_i2c(client, &lt8713sx_regmap_config);
	if (IS_ERR(lt8713sx->regmap))
		return dev_err_probe(dev, PTR_ERR(lt8713sx->regmap), "regmap i2c init failed\n");

	ret = lt8713sx_gpio_init(lt8713sx);
	if (ret < 0)
		goto err_of_put;

	ret = lt8713sx_regulator_init(lt8713sx);
	if (ret < 0)
		goto err_of_put;

	ret = lt8713sx_regulator_enable(lt8713sx);
	if (ret)
		goto err_of_put;

	lt8713sx_reset(lt8713sx);

	return 0;

err_of_put:
	return ret;
}

static void lt8713sx_remove(struct i2c_client *client)
{
	struct lt8713sx *lt8713sx = i2c_get_clientdata(client);

	mutex_destroy(&lt8713sx->ocm_lock);

	regulator_bulk_disable(ARRAY_SIZE(lt8713sx->supplies), lt8713sx->supplies);
}

static struct i2c_device_id lt8713sx_id[] = {
	{ "lontium,lt8713sx", 0 },
	{ /* sentinel */ }
};

static const struct of_device_id lt8713sx_match_table[] = {
	{ .compatible = "lontium,lt8713sx" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lt8713sx_match_table);

static struct i2c_driver lt8713sx_driver = {
	.driver = {
		.name = "lt8713sx",
		.of_match_table = lt8713sx_match_table,
		.dev_groups = lt8713sx_attr_groups,
	},
	.probe = lt8713sx_probe,
	.remove = lt8713sx_remove,
	.id_table = lt8713sx_id,
};

module_i2c_driver(lt8713sx_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("lt8713sx drm bridge driver");
MODULE_AUTHOR("Tony <syyang@lontium.com>");
MODULE_FIRMWARE(FW_FILE);
