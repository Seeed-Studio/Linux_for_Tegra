// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <nvidia/conftest.h>

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/debugfs.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>

#include <media/camera_common.h>
#include <media/tegracam_core.h>


#define MAX96712_ID 0xA0
#define MAX96712_DPLL_FREQ 1000
#define DT_RAW_10           0x2B

enum max96712_pattern {
	MAX96712_PATTERN_CHECKERBOARD = 0,
	MAX96712_PATTERN_GRADIENT,
};

struct max96712_priv {
	struct i2c_client *client;
	struct regmap *regmap;
	struct gpio_desc *gpiod_pwdn[2];

	bool cphy;

	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler ctrl_handler;
	struct media_pad pads[1];

	enum max96712_pattern pattern;

	const struct i2c_device_id *id;
	struct v4l2_subdev      *subdev;
	u32     frame_length;
	struct camera_common_data       *s_data;
	struct tegracam_device          *tc_dev;
	u32                             channel;
	const char      *sensor_name;

	/* Debugfs interface */
	struct {
		struct dentry *dir;
		struct dentry *reg;
		struct dentry *val;
		u16 reg_addr;
		u8 reg_val;
	} debugfs;
};

struct index_reg_8 {
	u16 addr;
	u16 val;
};

enum {
	MAX96712_MODE_1920X1080_CROP_30FPS,
	MAX96712_MODE_3840X2160_CROP_30FPS,
	MAX96712_MODE_START_STREAM,
	MAX96712_MODE_STOP_STREAM,
	MAX96712_MODE_QUADLINK_DSER_SER,
	MAX96712_MODE_TEST_PATTERN
};

#define MAX96712_TABLE_WAIT_MS	0xff00
#define MAX96712_TABLE_END	0xff01

static const struct of_device_id max96712_of_table[] = {
	{ .compatible = "maxim,max96712-tpg" },
	{ /* sentinel */ },
};

static int max96712_raw16_3840_2160_x4(struct max96712_priv *priv);

/* Global definition begin */
static const int max96712_30fps[] = {
	30,
};

#define MAX96712_FRAME_RATE_30_FPS		30000000	/* 30 fps */
#define MAX96712_FRAME_RATE_DEFAULT		MAX96712_FRAME_RATE_30_FPS
static const struct camera_common_frmfmt max96712_frmfmt[] = {
	{{3840, 2160},
	 max96712_30fps,
	 1,
	 0,
	 MAX96712_MODE_3840X2160_CROP_30FPS,
	},
};

/* Global definition end */

static int max96712_read(struct max96712_priv *priv, int reg)
{
	int ret, val;

	if (!priv || !priv->regmap) {
		pr_err("Invalid regmap pointer\n");
		return -EINVAL;
	}

	dev_dbg(&priv->client->dev, "to read 0x%04x\n", reg);

	ret = regmap_read(priv->regmap, reg, &val);
	if (ret) {
		dev_err(&priv->client->dev, "read 0x%04x failed\n", reg);
		return ret;
	}

	return val;
}

static int max96712_write(struct max96712_priv *priv, unsigned int reg, u8 val)
{
	int ret;

	if (!priv || !priv->regmap) {
		pr_err("Invalid regmap pointer\n");
		return -EINVAL;
	}

	dev_dbg(&priv->client->dev, "to write 0x%04x with 0x%x\n", reg, val);

	ret = regmap_write(priv->regmap, reg, val);
	if (ret)
		dev_err(&priv->client->dev, "write 0x%04x failed\n", reg);

	return ret;
}

/* Debugfs interface implementation */
static int max96712_debugfs_reg_get(void *data, u64 *val)
{
	struct max96712_priv *priv = data;
	int ret;

	ret = max96712_read(priv, priv->debugfs.reg_addr);
	if (ret < 0) {
		dev_err(&priv->client->dev, "Failed to read reg 0x%04x\n",
				priv->debugfs.reg_addr);
		return ret;
	}

	*val = ret;
	return 0;
}

static int max96712_debugfs_reg_set(void *data, u64 val)
{
	struct max96712_priv *priv = data;
	int ret;

	if (val > 0xFF) {
		dev_err(&priv->client->dev, "Value 0x%llx out of range (max 0xFF)\n", val);
		return -EINVAL;
	}

	ret = max96712_write(priv, priv->debugfs.reg_addr, val);
	if (ret) {
		dev_err(&priv->client->dev, "Failed to wr reg 0x%04x = 0x%02llx\n",
				priv->debugfs.reg_addr, val);
		return ret;
	}

	dev_info(&priv->client->dev, "Successfully wr reg 0x%04x = 0x%02llx\n",
			priv->debugfs.reg_addr, val);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(max96712_debugfs_reg_fops,
			max96712_debugfs_reg_get,
			max96712_debugfs_reg_set,
			"0x%02llx\n");

static void max96712_debugfs_init(struct max96712_priv *priv)
{
	/* Create debugfs directory */
	priv->debugfs.dir = debugfs_create_dir("max96712", NULL);
	if (IS_ERR(priv->debugfs.dir)) {
		dev_err(&priv->client->dev, "Failed to create debugfs directory\n");
		return;
	}

	/* Create register address entry */
	debugfs_create_x16("reg_addr", 0644, priv->debugfs.dir,
			   &priv->debugfs.reg_addr);

	/* Create register value entry */
	debugfs_create_file("reg_val", 0644, priv->debugfs.dir,
			   priv, &max96712_debugfs_reg_fops);
}

static void max96712_debugfs_remove(struct max96712_priv *priv)
{
	debugfs_remove_recursive(priv->debugfs.dir);
}

static int max96712_update_bits(struct max96712_priv *priv, unsigned int reg,
				u8 mask, u8 val)
{
	int ret;

	dev_dbg(&priv->client->dev, "%s: update 0x%04x mask 0x%x, val 0x%x\n",
					__func__, reg, mask, val);

	ret = regmap_update_bits(priv->regmap, reg, mask, val);
	if (ret)
		dev_err(&priv->client->dev, "update 0x%04x failed\n", reg);

	return ret;
}

static void max96712_reset(struct max96712_priv *priv)
{
	pr_info("MAX96712 reset\n");

	max96712_update_bits(priv, 0x13, 0x40, 0x40);
	msleep(20);
}

static int max96712_raw16_3840_2160_x4(struct max96712_priv *priv)
{
	int regBpp = 0xE0;
	int dt = DT_RAW_10;
	int bpp = 10;
	int freq = 19; /* 1900 / 100 => DPLL */

	pr_info("%s: Configuring MAX96712 for RAW10 3840x2160@30fps\n", __func__);

	/* Enable Video PG 3, Generate VS, HS, DE, Invert VS */
	max96712_write(priv, 0x1050, 0xF3);
	/* Set Pattern to be checkerboard pattern. */
	max96712_write(priv, 0x1051, 0x10);
	/* VS (Vertical Sync) Configuration */
	/* Set VS_DLY = 0x000000 */
	max96712_write(priv, 0x1052, 0x00);
	max96712_write(priv, 0x1053, 0x00);
	max96712_write(priv, 0x1054, 0x00);
	/* Adjust VS timing for proper frame rate */
	max96712_write(priv, 0x1055, 0x00);
	max96712_write(priv, 0x1056, 0x44); /* Reduced from 0x52 */
	max96712_write(priv, 0x1057, 0x04); /* Reduced from 0x08 */
	/* Set VS Low = 0x8ED280, 9,360,000 */
	max96712_write(priv, 0x1058, 0x8E);
	max96712_write(priv, 0x1059, 0xD2);
	max96712_write(priv, 0x105A, 0x80);
	/* HS (Horizontal Sync) Configuration */
	/* Set HS Delay V2H = 0x015FC4, 90,052 */
	max96712_write(priv, 0x105B, 0x01);
	max96712_write(priv, 0x105C, 0x5F);
	max96712_write(priv, 0x105D, 0xC4);

	/* Adjust horizontal timing */
	max96712_write(priv, 0x105E, 0x0F);	/* HS_HIGH = 0x0F80 (reduced) */
	max96712_write(priv, 0x105F, 0x80);

	/* Optimize blanking periods */
	max96712_write(priv, 0x1060, 0x00);
	max96712_write(priv, 0x1061, 0x18);	/* Reduced H-sync width */

	/* Set HS_CNT = 0x094C, 2,380, V total */
	max96712_write(priv, 0x1062, 0x09);
	max96712_write(priv, 0x1063, 0x4C);
	/* DE (Data Enable) Configuration */
	/* Set DE Delay V2D = 0x015F90, 90,000, VS delay, VS width, plus Vertical back porch. */
	max96712_write(priv, 0x1064, 0x01);
	max96712_write(priv, 0x1065, 0x5F);
	max96712_write(priv, 0x1066, 0x90);

	/* Adjust DE timing for proper active region */
	max96712_write(priv, 0x1067, 0x0F);	/* DE_HIGH = 0x0F00 (3,840 pixels) */
	max96712_write(priv, 0x1068, 0x00);
	max96712_write(priv, 0x1069, 0x01);	/* DE_LOW = 0x0180 (reduced blanking) */
	max96712_write(priv, 0x106A, 0x80);

	/* Set proper vertical resolution */
	max96712_write(priv, 0x106B, 0x08); /* DE_CNT = 0x0870 (2,160) */
	max96712_write(priv, 0x106C, 0x70);

	/* Set color gradient increment ratio */
	max96712_write(priv, 0x106D, 0x06);
	/* Checkerboard mode color A low/mid/high byte, RGB888 color R/G/B (0-255) */
	max96712_write(priv, 0x106E, 0x80);
	max96712_write(priv, 0x106F, 0x00);
	max96712_write(priv, 0x1070, 0x04);
	/* Checkerboard mode color B low/mid/high byte, RGB888 color R/G/B (0-255) */
	max96712_write(priv, 0x1071, 0x00);
	max96712_write(priv, 0x1072, 0x08);
	max96712_write(priv, 0x1073, 0x80);
	/* Checkerboard mode color A repeat count. */
	max96712_write(priv, 0x1074, 0x50);
	/* Checkerboard mode color B repeat count */
	max96712_write(priv, 0x1075, 0xA0);
	/* Checkerboard mode alternate line count */
	max96712_write(priv, 0x1076, 0x50);
	/* Pattern CLK Freq Register:
	 * Bit [1:0] : Set video pattern PCLK frequency.
	 * 2'b00 = 25MHz
	 * 2'b01 = 75MHz
	 * 2'b10 = 150MHz (PATGEN_CLK_SRC = 1'b0)
	 * 2'b11 = 375MHz (PATGEN_CLK_SRC = 1'b1, by default)
	 * For 3840x2160@30fps with RAW10, let's verify the required pixel clock:
	 * Data Rate = 3840 * 2160 * 30 * 10 = 2,488,320,000 bits/sec
	 * Minimum Pixel Clock = 2,488,320,000 / 10 = 248.832 MHz
	 */
	max96712_write(priv, 0x0009, 0x03);	/* new 0x03, old 0x01 */
	/* 0b0: GMSL1, 0b1: GMSL2 */
	max96712_write(priv, 0x0006, 0xF0);	/* GMSL2 for Links D/C/B/A */

	/* dt >= DT_RAW_8 && dt <= DT_RAW_20 */
	regBpp = 0xE0;
	max96712_write(priv, 0x40B, 0x02 | (bpp << 3));			/* 10bpp 0x52 */
	max96712_write(priv, 0x40C, 0x00);					/* VC0 */
	max96712_write(priv, 0x40D, 0x00);					/* VC0 */
	/* Data type RAW10 0xAB */
	max96712_write(priv, 0x40E, dt | ((dt & 0x30)<<2));
	max96712_write(priv, 0x40F, (dt & 0x0F) | ((dt & 0x3C) << 2));	/* Data type RAW10 0xAB */
	max96712_write(priv, 0x410, (dt & 0x03) | (dt << 2));		/* Data type RAW10 0xAF */
	max96712_write(priv, 0x411, bpp | ((bpp & 0x1C) << 3));		/* 10bpp 0x4A */
	max96712_write(priv, 0x412, (bpp & 0x03) | bpp << 2);		/* 10bpp 0x2A */

	/* s_sensor_cfg.DataRate <= 3100 && s_sensor_cfg.DataRate >= 100 */
	max96712_write(priv, 0x415, regBpp + freq);		/* Port A Override DT/VC/BPP */
	max96712_write(priv, 0x418, regBpp + freq);		/* Port B Override DT/VC/BPP */
	max96712_write(priv, 0x41B, regBpp + freq);		/* Port C */
	max96712_write(priv, 0x41E, 0x20 + freq);		/* Port D */

	max96712_write(priv, 0x1DD, 0x9B);	/* Port A pipe config */
	max96712_write(priv, 0x1FD, 0x9B);	/* Port B pipe config */
	max96712_write(priv, 0x21D, 0x9B);	/* Port C pipe config */
	max96712_write(priv, 0x23D, 0x9B);	/* Port D pipe config */

	pr_info("Enable Deskew in Sensor\n");
	max96712_write(priv, 0x903, 0x87);	/* Port A Deskew */
	max96712_write(priv, 0x943, 0x87);	/* Port B Deskew */
	max96712_write(priv, 0x983, 0x87);	/* Port C Deskew */
	max96712_write(priv, 0x9c3, 0x87);	/* Port D Deskew */

	/* s_sensor_cfg.LaneMode == Lanes_4x2 */
	max96712_write(priv, 0x90A, 0xC0);	/* Port A Lane count */
	max96712_write(priv, 0x94A, 0xC0);	/* Port B Lane count */
	max96712_write(priv, 0x98A, 0xC0);	/* Port C Lane count */
	max96712_write(priv, 0x9CA, 0xC0);	/* Port D Lane count */
	max96712_write(priv, 0x8A3, 0xE4);	/* lane mapping */
	max96712_write(priv, 0x8A4, 0xE4);
	/* PHY Mode 2x4, force clock on PHY0, CIL A, (required by RCE FW) */
	max96712_write(priv, 0x8A0, 0xA4);

	max96712_write(priv, 0x8A1, 0x55);	/* Optimize HS timing */

	max96712_write(priv, 0x8AD, 0x28);
	max96712_write(priv, 0x8AE, 0x1c);
	max96712_write(priv, 0x8A2, 0xF0);
	max96712_write(priv, 0x018, 0x01);

	return 0;
}

static int max96712_s_stream(struct v4l2_subdev *sd, int enable)
{
	pr_info("%s: enable %d\n", __func__, enable);
	return 0;
}

static const struct v4l2_subdev_video_ops max96712_video_ops = {
	.s_stream = max96712_s_stream,
};

static int max96712_get_pad_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_format *format)
{
	format->format.width = 3840;
	format->format.height = 2160;
	format->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	format->format.field = V4L2_FIELD_NONE;

	return 0;
}

static const struct v4l2_subdev_pad_ops max96712_pad_ops = {
	.get_fmt = max96712_get_pad_format,
	.set_fmt = max96712_get_pad_format,
};

static const struct v4l2_subdev_ops max96712_subdev_ops = {
	.video = &max96712_video_ops,
	.pad = &max96712_pad_ops,
};

static const char * const max96712_test_pattern[] = {
	"Checkerboard",
	"Gradient",
};

static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_FRAME_RATE
	/* TEGRA_CAMERA_CID_GROUP_HOLD */
};

static int max96712_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	pr_info("%s: val %lld\n", __func__, val);

	return 0;
}

static int max96712_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	/* Group hold is not used for this sensor */
	return 0;
}

static struct tegracam_ctrl_ops max96712_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.set_frame_rate = max96712_set_frame_rate,
	.set_group_hold = max96712_set_group_hold,
};

static const struct regmap_config max96712_i2c_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x1f00,
};

static int max96712_power_on(struct camera_common_data *s_data)
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
	if (pw->reset_gpio > 0)
		gpio_set_value(pw->reset_gpio, 1);
	usleep_range(1000, 2000);
	pw->state = SWITCH_ON;
	return err;
}
static int max96712_power_off(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;

	if (pdata && pdata->power_off) {
		err = pdata->power_off(pw);
		if (!err)
			goto power_off_done;
		else
			dev_err(dev, "%s failed.\n", __func__);
		return err;
	}
power_off_done:
	pw->state = SWITCH_OFF;
	return 0;
}

static struct camera_common_pdata *max96712_parse_dt(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct device_node *node = dev->of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;
	int err;

	if (!node)
		return NULL;
	match = of_match_device(max96712_of_table, dev);
	if (!match) {
		dev_err(dev, "Failed to find matching dt id\n");
		return NULL;
	}
	board_priv_pdata = devm_kzalloc(dev, sizeof(*board_priv_pdata), GFP_KERNEL);
	err = of_property_read_string(node, "mclk",
			&board_priv_pdata->mclk_name);
	if (err)
		dev_err(dev, "mclk not in DT\n");

	board_priv_pdata->reset_gpio = of_get_named_gpio(node, "reset-gpios", 0);
	if (board_priv_pdata->reset_gpio > 0)
		gpio_direction_output(board_priv_pdata->reset_gpio, 1);
	else
		dev_err(dev, "failed to read reset_gpio\n");

	board_priv_pdata->pwdn_gpio = 0;
	if (board_priv_pdata->pwdn_gpio > 0) {
		dev_err(dev, "board_priv_pdata->pwdn_gpio %d\n", board_priv_pdata->pwdn_gpio);
		gpio_direction_output(board_priv_pdata->pwdn_gpio, 1);
	} else
		dev_warn(dev, "failed to read pwdn_gpio\n");

	return board_priv_pdata;
}

static int max96712_power_get(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	int err = 0;

	pw->reset_gpio = pdata->reset_gpio;

	pw->state = SWITCH_OFF;

	return err;
}

static int max96712_power_put(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;

	if (unlikely(!pw))
		return -EFAULT;
	return 0;
}

static int max96712_set_mode(struct tegracam_device *tc_dev)
{
	return 0;
}

static int max96712_start_streaming(struct tegracam_device *tc_dev)
{
	struct max96712_priv *priv = (struct max96712_priv *)tegracam_get_privdata(tc_dev);

	dev_dbg(tc_dev->dev, ">> %s on channel %d\n", __func__, priv->channel);

	return 0;
}
static int max96712_stop_streaming(struct tegracam_device *tc_dev)
{
	struct max96712_priv *priv = (struct max96712_priv *)tegracam_get_privdata(tc_dev);

	dev_dbg(tc_dev->dev, ">> %s on channel %d\n", __func__, priv->channel);

	return 0;
}

static struct camera_common_sensor_ops max96712_common_ops = {
	.numfrmfmts = ARRAY_SIZE(max96712_frmfmt),
	.frmfmt_table = max96712_frmfmt,
	.power_on = max96712_power_on,
	.power_off = max96712_power_off,
	.parse_dt = max96712_parse_dt,
	.power_get = max96712_power_get,
	.power_put = max96712_power_put,
	.set_mode = max96712_set_mode,
	.start_streaming = max96712_start_streaming,
	.stop_streaming = max96712_stop_streaming,
};

static int max96712_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_internal_ops max96712_subdev_internal_ops = {
	.open = max96712_open,
};

#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int max96712_probe(struct i2c_client *client)
#else
static int max96712_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct tegracam_device *tc_dev;
	struct max96712_priv *priv;
	int tmpid;
	int err;

	dev_info(dev, "%s: probing v4l2 sensor:%s\n", __func__, dev_name(dev));

	if (!IS_ENABLED(CONFIG_OF) || !node)
		return -EINVAL;

	/* Allocate private data */
	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	tc_dev = devm_kzalloc(dev,
			sizeof(struct tegracam_device), GFP_KERNEL);
	if (!tc_dev)
		return -ENOMEM;

	/* Initialize basic structures */
	priv->client = tc_dev->client = client;
	tc_dev->dev = dev;
	i2c_set_clientdata(client, priv);
	strncpy(tc_dev->name, "96712-tpg", sizeof(tc_dev->name));

	/* Configure tegracam device */
	tc_dev->dev_regmap_config = &max96712_i2c_regmap;
	tc_dev->sensor_ops = &max96712_common_ops;
	tc_dev->v4l2sd_internal_ops = &max96712_subdev_internal_ops;
	tc_dev->tcctrl_ops = &max96712_ctrl_ops;

	/* Register tegracam device */
	err = tegracam_device_register(tc_dev);
	if (err) {
		dev_err(dev, "tegra camera driver registration failed\n");
		return err;
	}

	priv->regmap = tc_dev->s_data->regmap;
	priv->tc_dev = tc_dev;
	priv->s_data = tc_dev->s_data;
	priv->subdev = &tc_dev->s_data->subdev;
	tegracam_set_privdata(tc_dev, (void *)priv);

	/* Initialize debugfs interface */
	max96712_debugfs_init(priv);

	/* GPIO setup */
	/* max96712_0_pd and max96712_1_pd */
	priv->gpiod_pwdn[0] = devm_gpiod_get_optional(&client->dev, "max96712_0_pd",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_pwdn[0])) {
		err = PTR_ERR(priv->gpiod_pwdn[0]);
		goto un_register;
	}

	gpiod_set_consumer_name(priv->gpiod_pwdn[0], "max96712-0-pwdn");
	gpiod_set_value_cansleep(priv->gpiod_pwdn[0], 1);

	priv->gpiod_pwdn[1] = devm_gpiod_get_optional(&client->dev, "max96712_1_pd",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_pwdn[1])) {
		err = PTR_ERR(priv->gpiod_pwdn[1]);
		goto un_register;
	}

	gpiod_set_consumer_name(priv->gpiod_pwdn[1], "max96712-1-pwdn");
	gpiod_set_value_cansleep(priv->gpiod_pwdn[1], 1);

	if (priv->gpiod_pwdn[0] || priv->gpiod_pwdn[1])
		usleep_range(4000, 5000);

	tmpid = max96712_read(priv, 0x0d);
	if (tmpid != MAX96712_ID) {
		pr_err("max96712-tpg: ID 0x%x mismatch\n", tmpid);
		return -ENODEV;
	}

	max96712_reset(priv);

	max96712_raw16_3840_2160_x4(priv);

	/* Register V4L2 subdevice */
	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err) {
		dev_err(dev, "tegra camera subdev registration failed\n");
		goto un_register;
	}
	dev_err(&client->dev, "Detected max96712 sensor\n");
	return 0;

un_register:
	max96712_debugfs_remove(priv);
	tegracam_device_unregister(tc_dev);
	return err;
}

/* Remove function with version-dependent return type */
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int max96712_remove(struct i2c_client *client)
#else
static void max96712_remove(struct i2c_client *client)
#endif
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct max96712_priv *priv;

	if (!s_data) {
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
		return -EINVAL;
#else
		return;
#endif
	}

	priv = (struct max96712_priv *)s_data->priv;

	/* Clean up debugfs interface */
	max96712_debugfs_remove(priv);

	tegracam_v4l2subdev_unregister(priv->tc_dev);
	tegracam_device_unregister(priv->tc_dev);

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

static const struct i2c_device_id max96712_id[] = {
	{ "max96712-tpg", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max96712_id);
static struct i2c_driver max96712_i2c_driver = {
	.driver = {
		.name = "max96712-tpg",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(max96712_of_table),
	},
	.probe = max96712_probe,
	.remove = max96712_remove,
	.id_table = max96712_id,
};

module_i2c_driver(max96712_i2c_driver);

MODULE_DESCRIPTION("Maxim MAX96712 Quad GMSL2 Deserializer TPG Driver");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL v2");
