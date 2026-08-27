// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.
/*
 * max929x.c - max929x IO Expander driver
 */

#include <nvidia/conftest.h>

#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <media/camera_common.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include "max929x.h"

struct max929x {
	struct i2c_client *i2c_client;
	struct regmap *regmap;
	unsigned int pwdn_gpio;
	unsigned short ser_addr;
};
struct max929x *priv;

static int max929x_write_reg(u8 slave_addr, u16 reg, u8 val)
{
	struct i2c_client *i2c_client = priv->i2c_client;
	int err;

	i2c_client->addr = slave_addr;
	err = regmap_write(priv->regmap, reg, val);
	if (err)
		dev_err(&i2c_client->dev, "%s:i2c write failed, slave_addr 0x%x, 0x%x = 0x%x\n",
			__func__, slave_addr, reg, val);

	return err;
}

static int max929x_write_reg_list(struct max929x_reg *table, int size)
{
	struct device dev = priv->i2c_client->dev;
	int err = 0;
	int i;
	u8 slave_addr;
	u16 reg;
	u8 val;

	for (i = 0; i < size; i++) {
		if (table[i].slave_addr == SER_SLAVE2)
			slave_addr = priv->ser_addr;
		else
			slave_addr = table[i].slave_addr;

		reg = table[i].reg;
		val = table[i].val;

		if (slave_addr == 0xf1) {
			msleep(val);
			msleep(2000);
			continue;
		}

		dev_dbg(&dev, "%s: size %d, slave_addr 0x%x, reg 0x%x, val 0x%x\n",
				__func__, size, slave_addr, reg, val);

		err = max929x_write_reg(slave_addr, reg, val);
		if (err != 0)
			break;
		mdelay(5);
	}

	return err;
}

static  struct regmap_config max929x_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
};

#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int max929x_probe(struct i2c_client *client)
#else
static int max929x_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
#endif
{
	struct device dev = client->dev;
	struct device_node *np = (&dev)->of_node;
	unsigned short ser_addr = SER_SLAVE2;
	int err;

	dev_dbg(&dev, "%s: enter\n", __func__);

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	priv->i2c_client = client;
	priv->regmap = devm_regmap_init_i2c(priv->i2c_client, &max929x_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(priv->regmap));
		return -ENODEV;
	}

	priv->pwdn_gpio = of_get_named_gpio(np, "pwdn-gpios", 0);
	if (priv->pwdn_gpio < 0) {
		dev_err(&dev, "pwdn-gpios not found\n");
		return -EINVAL;
	}

	if (priv->pwdn_gpio) {
		gpio_direction_output(priv->pwdn_gpio, 1);
		gpio_set_value(priv->pwdn_gpio, 1);
		msleep(100);
	}

	/*
	 * Try to find the I2C address of serializer by writing to register
	 * 0x00 at i2c slave address 0x62 and 0x40. When write is successful
	 * the slave address is saved and used when configuring serializer.
	 */
	if (max929x_write_reg(SER_SLAVE2, MAX9295_DEV_ADDR, SER_SLAVE2 << 1)) {
		if (max929x_write_reg(SER_SLAVE1, MAX9295_DEV_ADDR, SER_SLAVE1 << 1)) {
			dev_err(&dev, "%s: failed to find serializer at 0x%x or 0x%x\n",
					__func__, SER_SLAVE2, SER_SLAVE1);
			return -ENODEV;
		}
		ser_addr = SER_SLAVE1;
	}

	msleep(100);
	priv->ser_addr = ser_addr;

	err = max929x_write_reg_list(max929x_Double_Dser_Ser_init,
			sizeof(max929x_Double_Dser_Ser_init)/sizeof(struct max929x_reg));
	if (err == 0)
		dev_dbg(&dev, "%s: success\n", __func__);
	else
		dev_err(&dev, "%s: fail\n", __func__);

	dev_set_drvdata(&client->dev, priv);

	return 0;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int max929x_remove(struct i2c_client *client)
#else
static void max929x_remove(struct i2c_client *client)
#endif
{
	struct device dev = client->dev;

	gpio_set_value(priv->pwdn_gpio, 0);
	dev_dbg(&dev, "%s: \n", __func__);

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

static const struct i2c_device_id max929x_id[] = {
	{ "max929x", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, max929x_id);

const struct of_device_id max929x_of_match[] = {
	{ .compatible = "Maxim,max929x", },
	{ },
};
MODULE_DEVICE_TABLE(of, max929x_of_match);

static struct i2c_driver max929x_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "max929x",
		.of_match_table = of_match_ptr(max929x_of_match),
	},
	.probe = max929x_probe,
	.remove = max929x_remove,
	.id_table = max929x_id,
};

module_i2c_driver(max929x_i2c_driver);

MODULE_DESCRIPTION("IO Expander driver max929x");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL v2");
