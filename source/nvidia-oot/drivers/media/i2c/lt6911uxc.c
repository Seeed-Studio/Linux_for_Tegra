// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Lontium LT6911UXC HDMI-CSI bridge driver
 */

#include <nvidia/conftest.h>

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/version.h>

#include <media/tegra_v4l2_camera.h>
#include <media/tegracam_core.h>

#include "../platform/tegra/camera/camera_gpio.h"

#define lt6911uxc_reg struct reg_8
#define LT6911_TABLE_WAIT_MS	0
#define LT6911_TABLE_END        1

static lt6911uxc_reg lt_enable_i2c[] = {
	{0xff, 0x80},
	{0xee, 0x01},
	{0x10, 0x00},
	{LT6911_TABLE_END, 0x00}
};

static lt6911uxc_reg lt_configure_param[] = {
	{0x5e, 0xdf},
	{0x58, 0x00},
	{0x59, 0x50},
	{0x5a, 0x10},
	{LT6911_TABLE_WAIT_MS, 1},
	{0x5a, 0x00},
	{0x58, 0x21},
	{LT6911_TABLE_END, 0x00}
};

static lt6911uxc_reg lt_block_erase[] = {
	{0x5a, 0x04},
	{LT6911_TABLE_WAIT_MS, 1},
	{0x5a, 0x00},
	{0x5b, 0x01},
	{0x5c, 0x80},
	{0x5d, 0x00},
	{0x5a, 0x01},
	{LT6911_TABLE_WAIT_MS, 1},
	{0x5a, 0x00},
	{LT6911_TABLE_END, 0x00}
};

static lt6911uxc_reg lt_fifo_rst[] = {
	{0xff, 0x81},
	{0x08, 0xbf},
	{LT6911_TABLE_WAIT_MS, 1},
	{0x08, 0xff},
	{LT6911_TABLE_END, 0x00}
};

static lt6911uxc_reg lt_write_enable[] = {
	{0xff, 0x80},
	{0x5a, 0x04},
	{LT6911_TABLE_WAIT_MS, 1},
	{0x5a, 0x00},
	{LT6911_TABLE_END, 0x00}
};

static lt6911uxc_reg lt_data_to_fifo[] = {
	{0x5e, 0xdf},
	{0x5a, 0x20},
	{LT6911_TABLE_WAIT_MS, 1},
	{0x5a, 0x00},
	{LT6911_TABLE_END, 0x00}
};

static lt6911uxc_reg lt_write_disable[] = {
	{0xff, 0x80},
	{0x5a, 0x08},
	{LT6911_TABLE_WAIT_MS, 1},
	{0x5a, 0x00},
	{0x58, 0x00},
	{LT6911_TABLE_END, 0x00}
};

static u8 LT6911_EDIDs[][256] = {
	// EDID with default 1920x1080 60fps
	{
		0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x3A,0xC4,0x04,0xED,0x01,0x01,0x01,0x01,
		0x1E,0x21,0x01,0x03,0x80,0x6F,0x3E,0x78,0x0A,0xEE,0x91,0xA3,0x54,0x4C,0x99,0x26,
		0x0F,0x50,0x54,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
		0x01,0x01,0x01,0x01,0x01,0x01,0x02,0x3A,0x80,0x18,0x71,0x38,0x2D,0x40,0x58,0x2C,
		0x45,0x00,0xBA,0x88,0x21,0x00,0x00,0x1E,0x02,0x3A,0x80,0x18,0x71,0x38,0x2D,0x40,
		0x58,0x2C,0x45,0x00,0xBA,0x88,0x21,0x00,0x00,0x1E,0x00,0x00,0x00,0xFD,0x00,0x18,
		0x4B,0x0F,0x87,0x22,0x00,0x0A,0x20,0x20,0x20,0x20,0x20,0x20,0x00,0x00,0x00,0xFC,
		0x00,0x44,0x75,0x61,0x6C,0x20,0x50,0x6F,0x72,0x74,0x20,0x52,0x47,0x42,0x01,0x50,
		0x02,0x03,0x18,0x71,0x43,0x04,0x01,0x03,0x23,0x09,0x07,0x01,0x83,0x01,0x00,0x00,
		0x67,0x03,0x0C,0x00,0x10,0x00,0x00,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xAD
	},
	// EDID with default 3840x2160 60fps
	{
		0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x3A,0xC4,0x01,0xED,0x01,0x01,0x01,0x01,
		0x1E,0x21,0x01,0x03,0x80,0x6F,0x3E,0x78,0x0A,0xEE,0x91,0xA3,0x54,0x4C,0x99,0x26,
		0x0F,0x50,0x54,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
		0x01,0x01,0x01,0x01,0x01,0x01,0x08,0xE8,0x00,0x30,0xF2,0x70,0x5A,0x80,0xB0,0x58,
		0x8A,0x00,0x50,0x1D,0x74,0x00,0x00,0x1E,0x08,0xE8,0x00,0x30,0xF2,0x70,0x5A,0x80,
		0xB0,0x58,0x8A,0x00,0x50,0x1D,0x74,0x00,0x00,0x1E,0x00,0x00,0x00,0xFD,0x00,0x18,
		0x4B,0x0F,0x87,0x3C,0x00,0x0A,0x20,0x20,0x20,0x20,0x20,0x20,0x00,0x00,0x00,0xFC,
		0x00,0x44,0x75,0x61,0x6C,0x20,0x50,0x6F,0x72,0x74,0x20,0x52,0x47,0x42,0x01,0xC7,
		0x02,0x03,0x20,0x71,0x43,0x61,0x01,0x03,0x23,0x09,0x07,0x01,0x83,0x01,0x00,0x00,
		0x67,0x03,0x0C,0x00,0x10,0x00,0x00,0x3C,0x67,0xD8,0x5D,0xC4,0x01,0x78,0x80,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xEF
	},
	// EDID with default 1280x720 60fps
	{
		0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x3A,0xC4,0x05,0xED,0x01,0x01,0x01,0x01,
		0x1E,0x21,0x01,0x03,0x80,0x6F,0x3E,0x78,0x0A,0xEE,0x91,0xA3,0x54,0x4C,0x99,0x26,
		0x0F,0x50,0x54,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
		0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x1D,0x00,0x72,0x51,0xD0,0x1E,0x20,0x6E,0x28,
		0x55,0x00,0xC4,0x8E,0x21,0x00,0x00,0x1E,0x01,0x1D,0x00,0x72,0x51,0xD0,0x1E,0x20,
		0x6E,0x28,0x55,0x00,0xC4,0x8E,0x21,0x00,0x00,0x1E,0x00,0x00,0x00,0xFD,0x00,0x18,
		0x4B,0x0F,0x87,0x22,0x00,0x0A,0x20,0x20,0x20,0x20,0x20,0x20,0x00,0x00,0x00,0xFC,
		0x00,0x44,0x75,0x61,0x6C,0x20,0x50,0x6F,0x72,0x74,0x20,0x52,0x47,0x42,0x01,0xE1,
		0x02,0x03,0x18,0x71,0x43,0x04,0x01,0x03,0x23,0x09,0x07,0x01,0x83,0x01,0x00,0x00,
		0x67,0x03,0x0C,0x00,0x10,0x00,0x00,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xAD
	}
};

static const struct of_device_id lt6911uxc_of_match[] = {
	{ .compatible = "nvidia,lt6911uxc", },
	{ },
};
MODULE_DEVICE_TABLE(of, lt6911uxc_of_match);

static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

static const int lt6911uxc_60fps[] = {
	60,
};

static const int lt6911uxc_30fps[] = {
	30,
};

static const u8 lt6911uxc_HDMI_Signal = 0xa3;

static const u8 lt6911uxc_FW_version[] = { 0xa7, 0xa8, 0xa9, 0xaa };

struct lt6911uxc {
	struct i2c_client		*i2c_client;
	struct v4l2_subdev		*subdev;
	u16				fine_integ_time;
	u32				frame_length;
	struct camera_common_data	*s_data;
	struct tegracam_device		*tc_dev;
	struct regmap 			*regmap;
};

static const struct camera_common_frmfmt lt6911uxc_frmfmt[] = {
	{{1920, 1080}, lt6911uxc_60fps, 1, 0, 0},
	{{3840, 2160}, lt6911uxc_60fps, 1, 0, 1},
	{{1280,  720}, lt6911uxc_60fps, 1, 0, 2},
};

static const struct regmap_config sensor_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
};

static inline int lt6911uxc_read_reg(struct camera_common_data *s_data,
	u16 addr, u8 *val)
{
	int err = 0;
	u32 reg_val = 0;

	err = regmap_read(s_data->regmap, addr, &reg_val);
	if (err) {
		dev_err(s_data->dev, "%s: i2c read , 0x%x = %x",
			__func__, addr, reg_val);
		return err;
	}

	*val = reg_val & 0xff;

	return err;
}

static inline int lt6911uxc_write_reg(struct camera_common_data *s_data,
					u16 addr, u8 val)
{
	int err = 0;
	err = regmap_write(s_data->regmap, addr, val);
	if (err)
		dev_err(s_data->dev, "%s: i2c write failed, 0x%x = %x",
			__func__, addr, val);
	return err;
}

static int lt6911uxc_write_table(struct lt6911uxc *priv,
				const lt6911uxc_reg table[])
{
	return regmap_util_write_table_8(priv->s_data->regmap,
					 table,
					 NULL, 0,
					 LT6911_TABLE_WAIT_MS,
					 LT6911_TABLE_END);
}

static int lt6911uxc_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);

	return 0;
}
static int lt6911uxc_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	/* lt6911uxc does not support group hold */
	return 0;
}

/* As soon as the reset pin is released, the bridge starts streaming */
static int lt6911uxc_start_streaming(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;

	if (pw->reset_gpio) {
		if (gpiod_cansleep(gpio_to_desc(pw->reset_gpio)))
			gpio_set_value_cansleep(pw->reset_gpio, 1);
		else
			gpio_set_value(pw->reset_gpio, 1);
	}
	msleep(300);

	return 0;
}

static int lt6911uxc_stop_streaming(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;

	if (pw->reset_gpio) {
		if (gpiod_cansleep(gpio_to_desc(pw->reset_gpio)))
			gpio_set_value_cansleep(pw->reset_gpio, 0);
		else
			gpio_set_value(pw->reset_gpio, 0);
	}
	return 0;
}



static int lt6911uxc_power_get(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct clk *parent;
	int err = 0, ret = 0;

	if (!pdata) {
		dev_err(dev, "pdata missing\n");
		return -EFAULT;
	}

	/* Sensor MCLK (aka. INCK) */
	if (pdata->mclk_name) {
		pw->mclk = devm_clk_get(dev, pdata->mclk_name);
		if (IS_ERR(pw->mclk)) {
			dev_err(dev, "unable to get clock %s\n",
				pdata->mclk_name);
			return PTR_ERR(pw->mclk);
		}

		if (pdata->parentclk_name) {
			parent = devm_clk_get(dev, pdata->parentclk_name);
			if (IS_ERR(parent)) {
				dev_err(dev, "unable to get parent clock %s",
					pdata->parentclk_name);
			} else {
				ret = clk_set_parent(pw->mclk, parent);
				if (ret < 0)
					dev_dbg(dev,
						"%s unable to set parent clock %d\n",
						__func__, ret);
			}
		}
	}

	/* analog 3.3v */
	if (pdata->regulators.avdd)
		err |= camera_common_regulator_get(dev,
				&pw->avdd, pdata->regulators.avdd);
	if (err) {
		dev_err(dev, "%s: unable to get regulator(s)\n", __func__);
		goto done;
	}

	/* dig 1.2v */
	if (pdata->regulators.dvdd)
		err |= camera_common_regulator_get(dev,
				&pw->dvdd, pdata->regulators.dvdd);
	if (err) {
		dev_err(dev, "%s: unable to get regulator(s)\n", __func__);
		goto done;
	}

	/* vdd 1.8v */
	if (pdata->regulators.iovdd)
		err |= camera_common_regulator_get(dev,
				&pw->avdd, pdata->regulators.iovdd);
	if (err) {
		dev_err(dev, "%s: unable to get regulator(s)\n", __func__);
		goto done;
	}

	/* Reset or ENABLE GPIO */
	pw->reset_gpio = pdata->reset_gpio;
	err = gpio_request(pw->reset_gpio, "cam_reset_gpio");
	if (err < 0) {
		dev_err(dev, "%s: unable to request reset_gpio (%d)\n",
			__func__, err);
		goto done;
	}

done:
	pw->state = SWITCH_OFF;

	return err;
}

static int lt6911uxc_power_put(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;

	if (unlikely(!pw))
		return -EFAULT;

	if (likely(pw->dvdd))
		devm_regulator_put(pw->dvdd);

	if (likely(pw->avdd))
		devm_regulator_put(pw->avdd);

	if (likely(pw->iovdd))
		devm_regulator_put(pw->iovdd);

	pw->dvdd = NULL;
	pw->avdd = NULL;
	pw->iovdd = NULL;

	if (likely(pw->reset_gpio))
		gpio_free(pw->reset_gpio);

	return 0;
}

static struct camera_common_pdata *lt6911uxc_parse_dt(
	struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct device_node *np = dev->of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;
	struct camera_common_pdata *ret = NULL;
	int err = 0;
	int gpio;

	if (!np)
		return NULL;

	match = of_match_device(lt6911uxc_of_match, dev);
	if (!match) {
		dev_err(dev, "Failed to find matching dt id\n");
		return NULL;
	}

	board_priv_pdata = devm_kzalloc(dev,
		sizeof(*board_priv_pdata), GFP_KERNEL);
	if (!board_priv_pdata)
		return NULL;

	gpio = of_get_named_gpio(np, "reset-gpios", 0);
	if (gpio < 0) {
		if (gpio == -EPROBE_DEFER)
			ret = ERR_PTR(-EPROBE_DEFER);
		dev_err(dev, "reset-gpios not found\n");
		goto error;
	}
	board_priv_pdata->reset_gpio = (unsigned int)gpio;

	err = of_property_read_string(np, "mclk", &board_priv_pdata->mclk_name);
	if (err)
		dev_err(dev, "mclk absent,assuming sensor driven externally\n");

	err = of_property_read_string(np, "avdd-reg",
		&board_priv_pdata->regulators.avdd);
	err |= of_property_read_string(np, "iovdd-reg",
		&board_priv_pdata->regulators.iovdd);
	err |= of_property_read_string(np, "dvdd-reg",
		&board_priv_pdata->regulators.dvdd);
	if (err)
		dev_dbg(dev, "avdd, iovdd and/or dvdd reglrs. not present, \
			assume sensor powered independently\n");

	board_priv_pdata->has_eeprom =
		of_property_read_bool(np, "has-eeprom");

	return board_priv_pdata;

error:
	devm_kfree(dev, board_priv_pdata);

	return ret;
}

static int lt6911uxc_power_on(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;

	if (pdata && pdata->power_on) {
		err = pdata->power_on(pw);
		if (err)
			dev_err(dev, "%s failed.\n", __func__);
		else
			pw->state = SWITCH_ON;
		return err;
	}

	if (pw->reset_gpio) {
		if (gpiod_cansleep(gpio_to_desc(pw->reset_gpio)))
			gpio_set_value_cansleep(pw->reset_gpio, 1);
		else
			gpio_set_value(pw->reset_gpio, 1);
	}

	usleep_range(10, 20);

	if (pw->avdd) {
		err = regulator_enable(pw->avdd);
		if (err)
			goto lt6911uxc_avdd_fail;
	}

	if (pw->iovdd) {
		err = regulator_enable(pw->iovdd);
		if (err)
			goto lt6911uxc_iovdd_fail;
	}
	/* Need to wait for atleat 1 ms before turning up 1.2v */
	usleep_range(1000, 1200);
	if (pw->dvdd) {
		err = regulator_enable(pw->dvdd);
		if (err)
			goto lt6911uxc_dvdd_fail;
	}

	msleep(300);

	pw->state = SWITCH_ON;

	return 0;

lt6911uxc_dvdd_fail:
	regulator_disable(pw->iovdd);

lt6911uxc_iovdd_fail:
	regulator_disable(pw->avdd);

lt6911uxc_avdd_fail:
	dev_err(dev, "%s failed.\n", __func__);

	return -ENODEV;
}

static int lt6911uxc_power_off(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;

	if (pdata && pdata->power_off) {
		err = pdata->power_off(pw);
		if (err) {
			dev_err(dev, "%s failed.\n", __func__);
			return err;
		}
	} else {
		if (pw->reset_gpio) {
			if (gpiod_cansleep(gpio_to_desc(pw->reset_gpio)))
				gpio_set_value_cansleep(pw->reset_gpio, 0);
			else
				gpio_set_value(pw->reset_gpio, 0);
		}
	}

	usleep_range(10, 20);

	if (pw->dvdd)
		regulator_disable(pw->dvdd);
	if (pw->iovdd)
		regulator_disable(pw->iovdd);
	if (pw->avdd)
		regulator_disable(pw->avdd);

	pw->state = SWITCH_OFF;

	return 0;
}

static int lt6911uxc_set_mode(struct tegracam_device *tc_dev)
{
	int err = 0;
	int i, j;

	struct camera_common_data *s_data = tc_dev->s_data;
	struct lt6911uxc *priv = (struct lt6911uxc *)s_data->priv;
	struct camera_common_power_rail *pw = s_data->power;

	// Write the corresponding Shadow EDID
	err |= lt6911uxc_write_table(priv, lt_enable_i2c);
	err |= lt6911uxc_write_table(priv, lt_configure_param);
	err |= lt6911uxc_write_table(priv, lt_block_erase);
	msleep(600);

	for (i = 0; i < 8; i++)
	{
		err |= lt6911uxc_write_table(priv, lt_fifo_rst);
		err |= lt6911uxc_write_table(priv, lt_write_enable);
		err |= lt6911uxc_write_table(priv, lt_data_to_fifo);

		for(j = 0; j < 32; j++)
		{
			lt6911uxc_write_reg(priv->s_data, 0x59,
				LT6911_EDIDs[s_data->mode][(i * 32) + j]);
		}
		err |= lt6911uxc_write_reg(priv->s_data, 0x5b, 0x01);
		err |= lt6911uxc_write_reg(priv->s_data, 0x5c, 0x80);
		err |= lt6911uxc_write_reg(priv->s_data, 0x5d, (i) * 32);
		err |= lt6911uxc_write_reg(priv->s_data, 0x5a, 0x10);
		msleep(1);
		err |= lt6911uxc_write_reg(priv->s_data, 0x5a, 0x00);
		msleep(1);
	}
	err |= lt6911uxc_write_table(priv, lt_write_disable);
	if (err)
		dev_err(tc_dev->dev, "%s Error in writing shadow EDID \n", __func__);

	// Toggle the reset pin to low, stream on will again set it to high
	if (pw->reset_gpio) {
		if (gpiod_cansleep(gpio_to_desc(pw->reset_gpio)))
			gpio_set_value_cansleep(pw->reset_gpio, 0);
		else
			gpio_set_value(pw->reset_gpio, 0);
	}
	// Keep reset pin low for the changes to take effect
	msleep(800);

	return err;
}

static int lt6911uxc_board_setup(struct lt6911uxc *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	u8 version;
	int i = 0, fw_ver;
	int err = 0;

	if (pdata->mclk_name) {
		err = camera_common_mclk_enable(s_data);
		if (err) {
			dev_err(dev, "error turning on mclk (%d)\n", err);
			goto done;
		}
	}

	err = lt6911uxc_power_on(s_data);
	if (err) {
		dev_err(dev, "error during power on sensor (%d)\n", err);
		goto err_power_on;
	}
	msleep(300);

	err |= lt6911uxc_write_table(priv, lt_enable_i2c);
	err |= lt6911uxc_write_reg(s_data, 0xff, 0x86);

	for (i = 0; i < sizeof(lt6911uxc_FW_version); i++) {
		fw_ver = 0;
		err = lt6911uxc_read_reg(s_data, lt6911uxc_FW_version[i], &version);
		if (err)
			dev_err(s_data->dev, "%s: Error in reading FW version \n",
			__func__);
		fw_ver = (fw_ver << 1) | version;
	}
	dev_info(s_data->dev, "Lontium FW version %d \n",fw_ver);

err_power_on:
	if (pdata->mclk_name)
		camera_common_mclk_disable(s_data);

	err = lt6911uxc_power_off(s_data);
	if (err) {
		dev_err(dev, "error during power off sensor (%d)\n", err);
		goto err_power_on;
	}

done:
	return err;
}

static struct tegracam_ctrl_ops lt6911uxc_ctrl_ops = {
	.set_group_hold = lt6911uxc_set_group_hold,
};

static const struct v4l2_subdev_internal_ops lt6911uxc_subdev_internal_ops = {
	.open = lt6911uxc_open,
};

static struct camera_common_sensor_ops lt6911uxc_common_ops = {
	.numfrmfmts = ARRAY_SIZE(lt6911uxc_frmfmt),
	.frmfmt_table = lt6911uxc_frmfmt,
	.power_on = lt6911uxc_power_on,
	.power_off = lt6911uxc_power_off,
	.write_reg = lt6911uxc_write_reg,
	.read_reg = lt6911uxc_read_reg,
	.parse_dt = lt6911uxc_parse_dt,
	.power_get = lt6911uxc_power_get,
	.power_put = lt6911uxc_power_put,
	.set_mode = lt6911uxc_set_mode,
	.start_streaming = lt6911uxc_start_streaming,
	.stop_streaming = lt6911uxc_stop_streaming,
};


#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int lt6911uxc_probe(struct i2c_client *client)
#else
static int lt6911uxc_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct tegracam_device *tc_dev;
	struct lt6911uxc *priv;
	int err;

	dev_info(dev, "probing lt6911uxc v4l2 sensor at addr 0x%0x\n",
				client->addr);

	if (!IS_ENABLED(CONFIG_OF) || !client->dev.of_node)
		return -EINVAL;

	priv = devm_kzalloc(dev,
			sizeof(struct lt6911uxc), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	tc_dev = devm_kzalloc(dev,
			sizeof(struct tegracam_device), GFP_KERNEL);
	if (!tc_dev)
		return -ENOMEM;

	priv->i2c_client = tc_dev->client = client;
	tc_dev->dev = dev;
	strncpy(tc_dev->name, "lt6911uxc", sizeof(tc_dev->name));
	tc_dev->dev_regmap_config = &sensor_regmap_config;
	tc_dev->sensor_ops = &lt6911uxc_common_ops;
	tc_dev->v4l2sd_internal_ops = &lt6911uxc_subdev_internal_ops;
	tc_dev->tcctrl_ops = &lt6911uxc_ctrl_ops;

	err = tegracam_device_register(tc_dev);
	if (err) {
		dev_err(dev, "tegra camera driver registration failed\n");
		return err;
	}
	priv->tc_dev = tc_dev;
	priv->s_data = tc_dev->s_data;
	priv->subdev = &tc_dev->s_data->subdev;
	tegracam_set_privdata(tc_dev, (void *)priv);

	err = lt6911uxc_board_setup(priv);
	if (err) {
		tegracam_device_unregister(tc_dev);
		dev_err(dev, "board setup failed\n");
		return err;
	}

	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err) {
		dev_err(dev, "tegra camera subdev registration failed\n");
		return err;
	}

	dev_info(dev, "detected lt6911uxc sensor\n");

	return 0;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int lt6911uxc_remove(struct i2c_client *client)
#else
static void lt6911uxc_remove(struct i2c_client *client)
#endif
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct lt6911uxc *priv;

	if (!s_data)
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
		return -EINVAL;
#else
		return;
#endif

	priv = (struct lt6911uxc *)s_data->priv;
	tegracam_v4l2subdev_unregister(priv->tc_dev);
	tegracam_device_unregister(priv->tc_dev);

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

static const struct i2c_device_id lt6911uxc_id[] = {
	{ "lt6911uxc", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, lt6911uxc_id);

static struct i2c_driver lt6911uxc_i2c_driver = {
	.driver = {
		.name = "lt6911uxc",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(lt6911uxc_of_match),
	},
	.probe = lt6911uxc_probe,
	.remove = lt6911uxc_remove,
	.id_table = lt6911uxc_id,
};
module_i2c_driver(lt6911uxc_i2c_driver);
MODULE_DESCRIPTION("Media Controller driver for Lontium LTX6911UXC");
MODULE_AUTHOR("Anubhav Rai <arai@nvidia.com>");
MODULE_LICENSE("GPL v2");
