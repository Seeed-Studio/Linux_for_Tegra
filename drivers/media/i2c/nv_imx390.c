// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2018-2024, NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.
/*
 * imx390.c - imx390 sensor driver
 * Copyright (c) 2020, RidgeRun. All rights reserved.
 *
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

#include <media/tegra_v4l2_camera.h>
#include <media/tegracam_core.h>

#include "../platform/tegra/camera/camera_gpio.h"
#include "imx390_mode_tbls.h"

/* imx390 - sensor parameters */
#define IMX390_MIN_GAIN         (0)
#define IMX390_MAX_GAIN         (978)
#define IMX390_ANALOG_GAIN_C0   (1024)
#define IMX390_SHIFT_8_BITS     (8)
#define IMX390_MIN_COARSE_EXPOSURE  (1)
#define IMX390_MAX_COARSE_DIFF  (10)
#define IMX390_MASK_LSB_2_BITS  0x0003
#define IMX390_MASK_LSB_8_BITS  0x00ff
#define IMX390_MIN_FRAME_LENGTH (3092)
#define IMX390_MAX_FRAME_LENGTH (0x1FFFF)

/* imx390 sensor register address */
#define IMX390_MODEL_ID_ADDR_MSB	        0x0000
#define IMX390_MODEL_ID_ADDR_LSB	        0x0001
#define IMX390_ANALOG_GAIN_ADDR_MSB	        0x0204
#define IMX390_ANALOG_GAIN_ADDR_LSB	        0x0205
#define IMX390_DIGITAL_GAIN_ADDR_MSB		0x020e
#define IMX390_DIGITAL_GAIN_ADDR_LSB		0x020f
#define IMX390_FRAME_LENGTH_ADDR_MSB		0x0340
#define IMX390_FRAME_LENGTH_ADDR_LSB		0x0341
#define IMX390_COARSE_INTEG_TIME_ADDR_MSB	0x0202
#define IMX390_COARSE_INTEG_TIME_ADDR_LSB	0x0203
#define IMX390_FINE_INTEG_TIME_ADDR_MSB		0x0200
#define IMX390_FINE_INTEG_TIME_ADDR_LSB		0x0201
#define IMX390_GROUP_HOLD_ADDR              0x0104

static const struct of_device_id imx390_of_match[] = {
	{.compatible = "sony,imx390",},
	{},
};
MODULE_DEVICE_TABLE(of, imx390_of_match);

static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_HDR_EN,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

struct imx390 {
	struct i2c_client *i2c_client;
	struct v4l2_subdev *subdev;
	u16 fine_integ_time;
	u32 frame_length;
	struct camera_common_data *s_data;
	struct tegracam_device *tc_dev;
};

static const struct regmap_config sensor_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
};

static inline void imx390_get_frame_length_regs(imx390_reg *regs,
		u32 frame_length)
{
	regs->addr = IMX390_FRAME_LENGTH_ADDR_MSB;
	regs->val = (frame_length >> 8) & 0xff;
	(regs + 1)->addr = IMX390_FRAME_LENGTH_ADDR_LSB;
	(regs + 1)->val = (frame_length) & 0xff;
}

static inline void imx390_get_coarse_integ_time_regs(imx390_reg *regs,
		u32 coarse_time)
{
	regs->addr = IMX390_COARSE_INTEG_TIME_ADDR_MSB;
	regs->val = (coarse_time >> 8) & 0xff;
	(regs + 1)->addr = IMX390_COARSE_INTEG_TIME_ADDR_LSB;
	(regs + 1)->val = (coarse_time) & 0xff;
}

static inline void imx390_get_gain_reg(imx390_reg *reg, u16 gain)
{
	reg->addr = IMX390_ANALOG_GAIN_ADDR_MSB;
	reg->val = (gain >> IMX390_SHIFT_8_BITS) & IMX390_MASK_LSB_2_BITS;

	(reg + 1)->addr = IMX390_ANALOG_GAIN_ADDR_LSB;
	(reg + 1)->val = (gain) & IMX390_MASK_LSB_8_BITS;
}

static inline int imx390_read_reg(struct camera_common_data *s_data,
		u16 addr, u8 *val)
{
	int err = 0;
	u32 reg_val = 0;

	err = regmap_read(s_data->regmap, addr, &reg_val);
	*val = reg_val & 0xff;

	return err;
}

static inline int imx390_write_reg(struct camera_common_data *s_data,
		u16 addr, u8 val)
{
	int err = 0;

	err = regmap_write(s_data->regmap, addr, val);
	if (err)
		dev_err(s_data->dev, "%s: i2c write failed, 0x%x = %x",
				__func__, addr, val);

	return err;
}

static int imx390_write_table(struct imx390 *priv, const imx390_reg table[])
{
	struct tegracam_device *tc_dev = priv->tc_dev;
	struct device *dev = tc_dev->dev;
	int i = 0;
	int ret = 0;
	int retry;

	while (table[i].addr != IMX390_TABLE_END) {
		retry = 5;

		if (table[i].addr == IMX390_TABLE_WAIT_MS) {
			dev_dbg(dev, "%s: sleep %d\n", __func__, table[i].val);
			msleep(table[i].val);
			i++;
			continue;
		}

		do {
			ret = imx390_write_reg(priv->s_data, table[i].addr, table[i].val);
			if (!ret)
				break;
			dev_warn(dev, "imx390_write_reg: try %d\n", retry);
			msleep(4);
			retry--;
		} while (retry > 0);

		if (retry == 0 && ret)
			return -EINVAL;

		i++;
	}

	return 0;
}

static int imx390_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	return 0;
}

static int imx390_set_gain(struct tegracam_device *tc_dev, s64 val)
{
	return 0;
}

static int imx390_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	struct imx390 *priv = (struct imx390 *)tc_dev->priv;

	priv->frame_length = IMX390_MIN_FRAME_LENGTH;
	return 0;
}

static int imx390_set_exposure(struct tegracam_device *tc_dev, s64 val)
{
	return 0;
}

static struct tegracam_ctrl_ops imx390_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.set_gain = imx390_set_gain,
	.set_exposure = imx390_set_exposure,
	.set_frame_rate = imx390_set_frame_rate,
	.set_group_hold = imx390_set_group_hold,
};

static int imx390_power_on(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;

	dev_err(dev, "%s: power on\n", __func__);
	if (pdata && pdata->power_on) {
		err = pdata->power_on(pw);
		if (err)
			dev_err(dev, "%s failed.\n", __func__);
		else
			pw->state = SWITCH_ON;
		return err;
	}

	if (gpio_is_valid(pw->reset_gpio)) {
		if (gpiod_cansleep(gpio_to_desc(pw->reset_gpio)))
			gpio_set_value_cansleep(pw->reset_gpio, 0);
		else
			gpio_set_value(pw->reset_gpio, 0);
	}

	if (unlikely(!(pw->avdd || pw->iovdd || pw->dvdd)))
		goto skip_power_seqn;

	usleep_range(10, 20);

	if (pw->avdd) {
		err = regulator_enable(pw->avdd);
		if (err)
			goto imx390_avdd_fail;
	}

	if (pw->iovdd) {
		err = regulator_enable(pw->iovdd);
		if (err)
			goto imx390_iovdd_fail;
	}

	if (pw->dvdd) {
		err = regulator_enable(pw->dvdd);
		if (err)
			goto imx390_dvdd_fail;
	}

	usleep_range(10, 20);

skip_power_seqn:
	if (gpio_is_valid(pw->reset_gpio)) {
		if (gpiod_cansleep(gpio_to_desc(pw->reset_gpio)))
			gpio_set_value_cansleep(pw->reset_gpio, 1);
		else
			gpio_set_value(pw->reset_gpio, 1);
	}

	/* Need to wait for t4 + t5 + t9 + t10 time as per the data sheet */
	/* t4 - 200us, t5 - 21.2ms, t9 - 1.2ms t10 - 270 ms */
	usleep_range(300000, 300100);

	pw->state = SWITCH_ON;

	return 0;

imx390_dvdd_fail:
	regulator_disable(pw->iovdd);

imx390_iovdd_fail:
	regulator_disable(pw->avdd);

imx390_avdd_fail:
	dev_err(dev, "%s failed.\n", __func__);

	return -ENODEV;
}

static int imx390_power_off(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;

	dev_dbg(dev, "%s: power off\n", __func__);

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

		usleep_range(10, 10);

		if (pw->dvdd)
			regulator_disable(pw->dvdd);
		if (pw->iovdd)
			regulator_disable(pw->iovdd);
		if (pw->avdd)
			regulator_disable(pw->avdd);
	}

	pw->state = SWITCH_OFF;

	return 0;
}

static int imx390_power_put(struct tegracam_device *tc_dev)
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

static int imx390_power_get(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct clk *parent;
	int err = 0;

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
			} else
				clk_set_parent(pw->mclk, parent);
		}
	}

	/* analog 2.8v */
	if (pdata->regulators.avdd)
		err |= camera_common_regulator_get(dev,
				&pw->avdd,
				pdata->regulators.avdd);
	/* IO 1.8v */
	if (pdata->regulators.iovdd)
		err |= camera_common_regulator_get(dev,
				&pw->iovdd,
				pdata->regulators.iovdd);
	/* dig 1.2v */
	if (pdata->regulators.dvdd)
		err |= camera_common_regulator_get(dev,
				&pw->dvdd,
				pdata->regulators.dvdd);
	if (err)
		dev_err(dev, "%s: unable to get regulator(s)\n", __func__);

	return err;
}

static struct camera_common_pdata *imx390_parse_dt(struct tegracam_device
		*tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct device_node *np = dev->of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;
	int err = 0;

	if (!np)
		return NULL;

	match = of_match_device(imx390_of_match, dev);
	if (!match) {
		dev_err(dev, "Failed to find matching dt id\n");
		return NULL;
	}

	board_priv_pdata = devm_kzalloc(dev,
			sizeof(*board_priv_pdata), GFP_KERNEL);
	if (!board_priv_pdata)
		return NULL;

	err = of_property_read_string(np, "mclk", &board_priv_pdata->mclk_name);
	if (err)
		dev_dbg(dev, "mclk name not present, "
				"assume sensor driven externally\n");

	err = of_property_read_string(np, "avdd-reg",
			&board_priv_pdata->regulators.avdd);
	err |= of_property_read_string(np, "iovdd-reg",
			&board_priv_pdata->regulators.iovdd);
	err |= of_property_read_string(np, "dvdd-reg",
			&board_priv_pdata->regulators.dvdd);
	if (err)
		dev_dbg(dev, "avdd, iovdd and/or dvdd reglrs. not present, "
				"assume sensor powered independently\n");

	board_priv_pdata->has_eeprom = of_property_read_bool(np, "has-eeprom");

	return board_priv_pdata;
}

static int imx390_set_mode(struct tegracam_device *tc_dev)
{
	struct imx390 *priv = (struct imx390 *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	int err = 0;

	dev_dbg(tc_dev->dev, "%s:\n", __func__);

	err = imx390_write_table(priv, mode_table[s_data->mode]);
	if (err) {
		dev_err(tc_dev->dev, "%s: sensor write table failed with error %d\n",
				__func__, err);
		return err;
	}

	return 0;
}

static int imx390_start_streaming(struct tegracam_device *tc_dev)
{
	struct imx390 *priv = (struct imx390 *)tegracam_get_privdata(tc_dev);

	dev_dbg(tc_dev->dev, "%s:\n", __func__);
	return imx390_write_table(priv, mode_table[IMX390_MODE_START_STREAM]);
}

static int imx390_stop_streaming(struct tegracam_device *tc_dev)
{
	int err;
	struct imx390 *priv = (struct imx390 *)tegracam_get_privdata(tc_dev);

	dev_dbg(tc_dev->dev, "%s:\n", __func__);
	err = imx390_write_table(priv, mode_table[IMX390_MODE_STOP_STREAM]);

	return err;
}

static struct camera_common_sensor_ops imx390_common_ops = {
	.numfrmfmts = ARRAY_SIZE(imx390_frmfmt),
	.frmfmt_table = imx390_frmfmt,
	.power_on = imx390_power_on,
	.power_off = imx390_power_off,
	.write_reg = imx390_write_reg,
	.read_reg = imx390_read_reg,
	.parse_dt = imx390_parse_dt,
	.power_get = imx390_power_get,
	.power_put = imx390_power_put,
	.set_mode = imx390_set_mode,
	.start_streaming = imx390_start_streaming,
	.stop_streaming = imx390_stop_streaming,
};

static int imx390_board_setup(struct imx390 *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	struct device *dev = s_data->dev;
	int err = 0;

	/* Skip mclk enable as this camera has an internal oscillator */

	err = imx390_power_on(s_data);
	if (err)
		dev_err(dev, "error during power on sensor (%d)\n", err);

	return err;
}

static int imx390_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);

	return 0;
}

static const struct v4l2_subdev_internal_ops imx390_subdev_internal_ops = {
	.open = imx390_open,
};

#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int imx390_probe(struct i2c_client *client)
#else
static int imx390_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct tegracam_device *tc_dev;
	struct imx390 *priv;
	int err;

	dev_dbg(dev, "probing v4l2 sensor at addr 0x%0x\n", client->addr);

	if (!IS_ENABLED(CONFIG_OF) || !client->dev.of_node)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(struct imx390), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	tc_dev = devm_kzalloc(dev, sizeof(struct tegracam_device), GFP_KERNEL);
	if (!tc_dev)
		return -ENOMEM;

	priv->i2c_client = tc_dev->client = client;
	tc_dev->dev = dev;
	strscpy(tc_dev->name, "imx390", sizeof(tc_dev->name));
	tc_dev->dev_regmap_config = &sensor_regmap_config;
	tc_dev->sensor_ops = &imx390_common_ops;
	tc_dev->v4l2sd_internal_ops = &imx390_subdev_internal_ops;
	tc_dev->tcctrl_ops = &imx390_ctrl_ops;

	err = tegracam_device_register(tc_dev);
	if (err) {
		dev_err(dev, "tegra camera driver registration failed\n");
		return err;
	}
	priv->tc_dev = tc_dev;
	priv->s_data = tc_dev->s_data;
	priv->subdev = &tc_dev->s_data->subdev;
	tegracam_set_privdata(tc_dev, (void *)priv);

	err = imx390_board_setup(priv);
	if (err) {
		dev_err(dev, "board setup failed\n");
		return err;
	}

	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err) {
		tegracam_device_unregister(tc_dev);
		dev_err(dev, "tegra camera subdev registration failed\n");
		return err;
	}

	dev_dbg(dev, "detected imx390 sensor\n");

	return 0;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int imx390_remove(struct i2c_client *client)
#else
static void imx390_remove(struct i2c_client *client)
#endif
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct imx390 *priv = (struct imx390 *)s_data->priv;

	tegracam_v4l2subdev_unregister(priv->tc_dev);
	tegracam_device_unregister(priv->tc_dev);

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

static const struct i2c_device_id imx390_id[] = {
	{"imx390", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, imx390_id);

static struct i2c_driver imx390_i2c_driver = {
	.driver = {
		.name = "imx390",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(imx390_of_match),
	},
	.probe = imx390_probe,
	.remove = imx390_remove,
	.id_table = imx390_id,
};

module_i2c_driver(imx390_i2c_driver);

MODULE_DESCRIPTION("Media Controller driver for Sony IMX390");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL v2");
