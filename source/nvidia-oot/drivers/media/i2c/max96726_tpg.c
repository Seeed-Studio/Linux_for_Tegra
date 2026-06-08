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
#include <linux/moduleparam.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>

#include <media/camera_common.h>
#include <media/tegracam_core.h>

#define MAX96726_I2C_SLAVE_ADDR_CHIP1	0x21
#define MAX96726_I2C_SLAVE_ADDR_CHIP2	0x33
#define MAX96726_I2C_SLAVE_ADDR_BRD_MUX	0x25

/*
 * Resolution and FPS modes:
 * 0b000: 852 x 480 @ 30fps RAW16
 * 0b001: 1280 x 720 @ 30fps RAW16
 * 0b010: 1920 x 1080 @ 30fps RAW16 (default)
 * 0b011: 3840 x 2160 @ 30fps RAW16
 * 0b100: 852 x 480 @ 60fps RAW16
 * 0b101: 1280 x 720 @ 60fps RAW16
 * 0b110: 1920 x 1080 @ 60fps RAW16
 */
#define MAX96726_RESO_FPS_852x480_30	0x0
#define MAX96726_RESO_FPS_1280x720_30	0x1
#define MAX96726_RESO_FPS_1920x1080_30	0x2
#define MAX96726_RESO_FPS_3840x2160_30	0x3
#define MAX96726_RESO_FPS_852x480_60	0x4
#define MAX96726_RESO_FPS_1280x720_60	0x5
#define MAX96726_RESO_FPS_1920x1080_60	0x6


#define MAX96726_DATA_RATE_2500	2500
#define MAX96726_DATA_RATE_4500	4500

#define MAX96726_ID		0x30
#define MAX96726_ID1		0x31

#define MAX96726_DPLL_FREQ	1000

#define MAX96726_DT_RAW_10	43

#define MAX96726_PHY_MODE_DPHY	0
#define MAX96726_PHY_MODE_CPHY	1

enum max96726_pattern {
	MAX96726_PATTERN_CHECKERBOARD = 0,
	MAX96726_PATTERN_GRADIENT,
};

struct max96726_priv {
	struct i2c_client *client;
	struct regmap *regmap;
	struct gpio_desc *gpiod_pwdn[2];

	bool cphy;

	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler ctrl_handler;
	struct media_pad pads[1];

	enum max96726_pattern pattern;

	const struct i2c_device_id *id;
	struct v4l2_subdev *subdev;
	u32 frame_length;
	struct camera_common_data *s_data;
	struct tegracam_device *tc_dev;
	u32 channel;
	const char *sensor_name;
};

struct index_reg_8 {
	u16 addr;
	u16 val;
};

enum {
	MAX96726_MODE_3840X2160_CROP_30FPS,
	MAX96726_MODE_START_STREAM,
	MAX96726_MODE_STOP_STREAM,
	MAX96726_MODE_QUADLINK_DSER_SER,
	MAX96726_MODE_TEST_PATTERN
};

#define MAX96726_TABLE_WAIT_MS	0xff00
#define MAX96726_TABLE_END	0xff01

static const struct of_device_id max96726_of_table[] = {
	{ .compatible = "maxim,max96726-tpg" },
	{ /* sentinel */ },
};

/* Module parameters */
static int phy_mode = MAX96726_PHY_MODE_CPHY;
module_param(phy_mode, int, 0644);
MODULE_PARM_DESC(phy_mode, "PHY mode (0:DPHY,1:CPHY)");

static int data_rate;
module_param(data_rate, int, 0644);
MODULE_PARM_DESC(data_rate, "Data rate (0:2500, 1:4500)");

static int reso_fps = MAX96726_RESO_FPS_3840x2160_30;
module_param(reso_fps, int, 0644);
MODULE_PARM_DESC(reso_fps, "Resolution and FPS (3:3840x2160_30)");

/* Global definition begin */
static const int max96726_30fps[] = {
	30,
};

#define MAX96726_FRAME_RATE_30_FPS	30000000	/* 30 fps */
#define MAX96726_FRAME_RATE_DEFAULT	MAX96726_FRAME_RATE_30_FPS

static const struct camera_common_frmfmt max96726_frmfmt[] = {
	{{3840, 2160},
	 max96726_30fps,
	 1,
	 0,
	 MAX96726_MODE_3840X2160_CROP_30FPS,
	},
};

static int use_csi_standby;
module_param(use_csi_standby, int, 0644);

/* Global definition end */

static int max96726_read(struct max96726_priv *priv, int reg)
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

static int max96726_write(struct max96726_priv *priv, unsigned int reg, u8 val)
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

static int max96726_update_bits(struct max96726_priv *priv, unsigned int reg,
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

static void max96726_reset(struct max96726_priv *priv)
{
	pr_info("MAX96726 reset\n");

	max96726_update_bits(priv, 0x19, 0x40, 0x40);
	msleep(20);
}

/*
 * CamIndex:1/2/3/4
 * Bit[3]: Enable MIPI PHY3
 * Bit[2]: Enable MIPI PHY2
 * Bit[1]: Enable MIPI PHY1
 * Bit[0]: Enable MIPI PHY0
 * ~(0x3 << ((CamIndex-1) * 2))
 * Use the phy_Stdby_n bits to enable/disable the ports. Port A uses PHYs 0/1, Port B uses PHYs 2/3.
 * Reg 0x08B0 = 0x0F: All ports enabled
 * Reg 0x08B0 = 0x0C: Port A disabled
 * Reg 0x08B0 = 0x03: Port B disabled
 * Use CSI2_LANE_CNT bits to change number of lanes
 * Reg 0x0A06 for port A
 * Reg 0x0A46 for port B
 */
static int max96726_CsiPhyStandby(struct max96726_priv *priv,
				u32 CamIndex, int enable_phy_ports)
{
	int csiPhyShift = 0;
	int val = 0;

	if (CamIndex == 1 || CamIndex == 3)
		csiPhyShift = 0;
	else
		csiPhyShift = 2;

	val = max96726_read(priv, 0x08B0);
	dev_dbg(priv->tc_dev->dev,
			"channel: 0x%x, read, Register=0x08B0, Data=0x%02x\n",
			CamIndex, val);

	if (enable_phy_ports)
		val = val & ~(0x3 << csiPhyShift);
	else {
		if (true) /* not enable the PHYs while we want the Tx Off */
			val = val | (0x3 << csiPhyShift);
		else
			pr_info("Note: CSI TX forced to be off\n");
	}

	max96726_write(priv, 0x08B0, val);

	return 0;
}

/*
 * MAX96726A, 8pipes enabled
 * CSI Port A:  3 lanes, C-PHY, 2500 Msps/lane, RAW16 VC0
 * Pipe 0:		RAW16 VC0 -> RAW16 VC0  -> Port A
 * Pipe 1:		RAW16 VC0 -> RAW16 VC8  -> Port A
 * Pipe 2:		RAW16 VC0 -> RAW16 VC16 -> Port A
 * Pipe 3:		RAW16 VC0 -> RAW16 VC24 -> Port A
 * Pipe 4:		RAW16 VC0 -> RAW16 VC0  -> Port B
 * Pipe 5:		RAW16 VC0 -> RAW16 VC8  -> Port B
 * Pipe 6:		RAW16 VC0 -> RAW16 VC16 -> Port B
 * Pipe 7:		RAW16 VC0 -> RAW16 VC24 -> Port B
 */
static int max96726_I2cSequence_Raw16_8pipes(struct max96726_priv *priv)
{
	int val = 0, valwr = 0;
	int reg = 0;

	if (data_rate == 0) {
		if (phy_mode == MAX96726_PHY_MODE_DPHY)
			data_rate = MAX96726_DATA_RATE_2500;
		else
			data_rate = MAX96726_DATA_RATE_4500;
	}

	pr_info("%s: Dev 0x%x, phy_mode %d(0:DPHY,1:CPHY), data_rate %d, reso_fps %d\n",
		__func__, priv->client->addr, phy_mode, data_rate, reso_fps);

	/* [Number of bytes],[Slave adr],[Reg adr MSB],[Reg adr LSB],[Data] */
	/* RESET_ALL=1b with waitmsec 2000 to re-initiate the chip */
	/* 0x04,0x42,0x00,0x19,0x7F,  [6]RESET_ALL=1b */
	/* max96726_write(priv, 0x0019, 0x7F); */

	/*
	 * Select C-PHY/D-PHY mode with the CSI2_CPHY_EN bits
	 * in regs 0x0A06 (port A) and 0x0A46 (port B)
	 */
	if (phy_mode == MAX96726_PHY_MODE_DPHY) {
		/* 0x04,0x42,0x0A,0x06,0xC0, bit[7:6]=0b11 4lane, [5]=0b1 CPHY_EN */
		/* 0x04,0x42,0x0A,0x46,0xC0, bit[7:6]=0b11 4lane, [5]=0b1 CPHY_EN */
		max96726_write(priv, 0x0A06, 0xC0);
		max96726_write(priv, 0x0A46, 0xC0);

		/* Disable Periodic Deskew by default */
		pr_info("Disable Periodic Deskew on sensor by default\n");
		max96726_write(priv, 0x0A03, 0x0);
		max96726_write(priv, 0x0A43, 0x0);

		if (data_rate >= 1500) {
			pr_info("Enable Initial Deskew on sensor\n");
			valwr = 0x80;
		} else {
			pr_info("Disable Initial Deskew on sensor\n");
			valwr = 0x00;
		}

		reg = 0x0A02;
		val = max96726_read(priv, reg);
		max96726_write(priv, reg, valwr);
		dev_dbg(priv->tc_dev->dev,
			"sensor@0x%x, Reg(0x%x)=0x%02x, new 0x%02x to wr\n",
			priv->client->addr, reg, val, valwr);
		reg = 0x0A42;
		val = max96726_read(priv, reg);
		max96726_write(priv, reg, valwr);
		pr_info("sensor@0x%x, Reg(0x%x)=0x%02x, new 0x%02x to wr\n",
			priv->client->addr, reg, val, valwr);
	} else {
		/* 0x04,0x42,0x0A,0x06,0xE0, bit[7:6]=0b11 4lane, [5]=0b1 CPHY_EN */
		/* 0x04,0x42,0x0A,0x46,0xE0, bit[7:6]=0b11 4lane, [5]=0b1 CPHY_EN */
		max96726_write(priv, 0x0A06, 0xE0);
		max96726_write(priv, 0x0A46, 0xE0);
	}

	/* GM30 Patgen script, 4-lanes at 2500Mbps, PHY_COPY enabled */
	/* 0x04,0x42,0x09,0x8F,0x99, 0xAD:4500Mb/sps, 0x99:2500Mb/sps */
	/* 0x04,0x42,0x09,0x92,0x99, 0xAD:4500Mb/sps, 0x99:2500Mb/sps */
	/*
	 * bit7
	 * MIPI PHY1 software-override disable for frequency fine tuning
	 *    0b0: Enable software override for frequency fine tuning
	 *    0b1: Disable software override for frequency fine tuning
	 * bit [5:0]
	 *    0x00:   D-PHY: 80MHz DPLL, 80Mbps/lane
	 *            C-PHY: 80MHz DPLL, 182Mbps/lane
	 *    0x01:   D-PHY: 100MHz DPLL, 100Mbps/lane
	 *            C-PHY: 100MHz DPLL, 228Mbps/lane
	 *    0x02:   D-PHY: 200MHz DPLL, 200Mbps/lane
	 *            C-PHY: 200MHz DPLL, 456Mbps/lane
	 *    0x2D:   D-PHY: 4500MHz DPLL, 4.5Gbps/lane
	 *            C-PHY: 4500MHz DPLL, 10.26Gbps/lane
	 */
	 /* s_sensor_cfg.DataRate */
	max96726_write(priv, 0x098F, (1 << 7) | ((data_rate / 100) & 0x3F));
	max96726_write(priv, 0x0992, (1 << 7) | ((data_rate / 100) & 0x3F));

	/* DES patgen configuration
	 * Resolution and FPS modes:
	 * 0b000: 852 x 480 @ 30fps RAW16
	 * 0b001: 1280 x 720 @ 30fps RAW16
	 * 0b010: 1920 x 1080 @ 30fps RAW16 (default)
	 * 0b011: 3840 x 2160 @ 30fps RAW16
	 * 0b100: 852 x 480 @ 60fps RAW16
	 * 0b101: 1280 x 720 @ 60fps RAW16
	 * 0b110: 1920 x 1080 @ 60fps RAW16
	 */
	max96726_write(priv, 0x010F, reso_fps);	/* Pipe 0 PattGen */
	reg = 0x010E;
	val = max96726_read(priv, reg);
	val |= (1 << 4);
	pr_info("write, 0x%x <= 0x%x\n", reg, val);
	max96726_write(priv, reg, val);		/* Enable color bars in pipe 0 */
	max96726_write(priv, 0x0420, 0x41);	/* Pipe 0 to ctrl 1 (port A) */
	/* Override VC according to VC_MAP_SRC/DST registers */
	max96726_write(priv, 0x0426, 0x20);
	max96726_write(priv, 0x0427, 0x00);	/* remap0, src VC */
	max96726_write(priv, 0x0428, 0x00);	/* remap0, dst VC */
	max96726_write(priv, 0x0429, 0x08);	/* remap1, src VC */
	max96726_write(priv, 0x042A, 0x00);	/* remap1, dst VC */
	max96726_write(priv, 0x042B, 0x10);	/* remap2, src VC */
	max96726_write(priv, 0x042C, 0x00);	/* remap2, dst VC */
	max96726_write(priv, 0x042D, 0x18);	/* remap3, src VC */
	max96726_write(priv, 0x042E, 0x00);	/* remap3, dst VC */

	max96726_write(priv, 0x012B, reso_fps);	/* Pipe 1 PattGen */
	reg = 0x012A;
	val = max96726_read(priv, reg);
	val |= (1 << 4);
	dev_dbg(priv->tc_dev->dev, "write, 0x%x <= 0x%x\n", reg, val);
	max96726_write(priv, reg, val);	/* Enable color bars in pipe 1 */
	max96726_write(priv, 0x0438, 0x40);	/* Pipe 1 to ctrl 1 (port A) */

	max96726_write(priv, 0x0147, reso_fps);	/* Pipe 2 PattGen */
	reg = 0x0146;
	val = max96726_read(priv, reg);
	val |= (1 << 4);
	dev_dbg(priv->tc_dev->dev, "write, 0x%x <= 0x%x\n", reg, val);
	max96726_write(priv, reg, val);	/* Enable color bars in pipe 2 */
	max96726_write(priv, 0x0450, 0x40);	/* Pipe 2 to ctrl 1 (port A) */

	max96726_write(priv, 0x0163, reso_fps);	/* Pipe 3 PattGen */
	reg = 0x0162;
	val = max96726_read(priv, reg);
	val |= (1 << 4);
	dev_dbg(priv->tc_dev->dev, "write, 0x%x <= 0x%x\n", reg, val);
	max96726_write(priv, reg, val);	/* Enable color bars in pipe 3 */
	max96726_write(priv, 0x0468, 0x40);	/* Pipe 3 to ctrl 1 (port A) */

	max96726_write(priv, 0x017F, reso_fps);	/* Pipe 4 PattGen */
	reg = 0x017E;
	val = max96726_read(priv, reg);
	val |= (1 << 4);
	dev_dbg(priv->tc_dev->dev, "write, 0x%x <= 0x%x\n", reg, val);
	max96726_write(priv, reg, val);	/* Enable color bars in pipe 4 */
	max96726_write(priv, 0x0480, 0x42);	/* Pipe 4 to ctrl 2 (port B) */
	/* Override VC according to VC_MAP_SRC/DST registers */
	max96726_write(priv, 0x0486, 0x20);
	max96726_write(priv, 0x0487, 0x00);	/* remap0, src VC */
	max96726_write(priv, 0x0488, 0x00);	/* remap0, dst VC */
	max96726_write(priv, 0x0489, 0x08);	/* remap1, src VC */
	max96726_write(priv, 0x048A, 0x00);	/* remap1, dst VC */
	max96726_write(priv, 0x048B, 0x10);	/* remap2, src VC */
	max96726_write(priv, 0x048C, 0x00);	/* remap2, dst VC */
	max96726_write(priv, 0x048D, 0x18);	/* remap3, src VC */
	max96726_write(priv, 0x048E, 0x00);	/* remap3, dst VC */

	max96726_write(priv, 0x019B, reso_fps);	/* Pipe 5 PattGen */
	reg = 0x019A;
	val = max96726_read(priv, reg);
	val |= (1 << 4);
	dev_dbg(priv->tc_dev->dev, "write, 0x%x <= 0x%x\n", reg, val);
	max96726_write(priv, reg, val);	/* Enable color bars in pipe 5 */
	max96726_write(priv, 0x0498, 0x40);	/* Pipe 5 to ctrl 2 (port B) */

	max96726_write(priv, 0x01B7, reso_fps);	/* Pipe 6 PattGen */
	reg = 0x01B6;
	val = max96726_read(priv, reg);
	val |= (1 << 4);
	dev_dbg(priv->tc_dev->dev, "write, 0x%x <= 0x%x\n", reg, val);
	max96726_write(priv, reg, val);	/* Enable color bars in pipe 6 */
	max96726_write(priv, 0x04B0, 0x40);	/* Pipe 6 to ctrl 2 (port B) */

	max96726_write(priv, 0x01D3, reso_fps);	/* Pipe 7 PattGen */
	reg = 0x01D2;
	val = max96726_read(priv, reg);
	val |= (1 << 4);
	dev_dbg(priv->tc_dev->dev, "write, 0x%x <= 0x%x\n", reg, val);
	max96726_write(priv, reg, val);	/* Enable color bars in pipe 7 */
	max96726_write(priv, 0x04C8, 0x40);	/* Pipe 7 to ctrl 2 (port B) */

	/* 0x04,0x42,0x08,0xB0,0x8F, [7]=0b1 force_csi_out */
	max96726_write(priv, 0x08B0, 0x8F);	/* Enable port A */

	usleep_range(100000, 200000);

	return 0;
}

static int max96726_s_stream(struct v4l2_subdev *sd, int enable)
{
	pr_info("%s: enable %d\n", __func__, enable);
	return 0;
}

static const struct v4l2_subdev_video_ops max96726_video_ops = {
	.s_stream = max96726_s_stream,
};

static int max96726_get_pad_format(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_format *format)
{
	switch (reso_fps) {
	case MAX96726_RESO_FPS_852x480_30:
	case MAX96726_RESO_FPS_852x480_60:
		format->format.width = 852;
		format->format.height = 480;
		break;
	case MAX96726_RESO_FPS_1280x720_30:
	case MAX96726_RESO_FPS_1280x720_60:
		format->format.width = 1280;
		format->format.height = 720;
		break;
	case MAX96726_RESO_FPS_1920x1080_30:
	case MAX96726_RESO_FPS_1920x1080_60:
		format->format.width = 1920;
		format->format.height = 1080;
		break;
	case MAX96726_RESO_FPS_3840x2160_30:
		format->format.width = 3840;
		format->format.height = 2160;
		break;
	default:
		pr_warn("Invalid reso_fps %d, using default 3840x2160\n", reso_fps);
		format->format.width = 3840;
		format->format.height = 2160;
		break;
	}

	format->format.code = MEDIA_BUS_FMT_SBGGR16_1X16;
	format->format.field = V4L2_FIELD_NONE;

	return 0;
}

static const struct v4l2_subdev_pad_ops max96726_pad_ops = {
	.get_fmt = max96726_get_pad_format,
	.set_fmt = max96726_get_pad_format,
};

static const struct v4l2_subdev_ops max96726_subdev_ops = {
	.video = &max96726_video_ops,
	.pad = &max96726_pad_ops,
};

static const char * const max96726_test_pattern[] = {
	"Checkerboard",
	"Gradient",
};

static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_FRAME_RATE
};

static int max96726_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	pr_info("%s: val %lld\n", __func__, val);

	return 0;
}

static int max96726_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	/* Group hold is not used for this sensor */
	return 0;
}

static struct tegracam_ctrl_ops max96726_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.set_frame_rate = max96726_set_frame_rate,
	.set_group_hold = max96726_set_group_hold
};

static const struct regmap_config max96726_i2c_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x13aa,
};

static int max96726_power_on(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	int tmpid, tmpid0, tmpid1;
	struct max96726_priv *priv = (struct max96726_priv *) s_data->priv;

	dev_dbg(dev, "%s priv %p\n", __func__, priv);

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

	tmpid = max96726_read(priv, 0x0d);
	tmpid0 = max96726_read(priv, 0x0e);
	tmpid1 = max96726_read(priv, 0x0f);
	pr_info("max96726-tpg: tmpid (0xd): 0x%x\n", tmpid);
	pr_info("max96726-tpg: tmpid0(0xe): 0x%x\n", tmpid0);
	pr_info("max96726-tpg: tmpid1(0xf): 0x%x\n", tmpid1);

	return err;
}
static int max96726_power_off(struct camera_common_data *s_data)
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

static struct camera_common_pdata *max96726_parse_dt(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct device_node *node = dev->of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;
	int err;

	if (!node)
		return NULL;
	match = of_match_device(max96726_of_table, dev);
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

	board_priv_pdata->pwdn_gpio = 0; /* of_get_named_gpio(node, "pwdn-gpios", 0); */
	if (board_priv_pdata->pwdn_gpio > 0) {
		dev_err(dev, "board_priv_pdata->pwdn_gpio %d\n", board_priv_pdata->pwdn_gpio);
		gpio_direction_output(board_priv_pdata->pwdn_gpio, 1);
	} else
		dev_warn(dev, "failed to read pwdn_gpio\n");

	return board_priv_pdata;
}

static int max96726_power_get(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	int err = 0;

	pw->reset_gpio = pdata->reset_gpio;

	pw->state = SWITCH_OFF;

	return err;
}

static int max96726_power_put(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;

	if (unlikely(!pw))
		return -EFAULT;
	return 0;
}

static int max96726_set_mode(struct tegracam_device *tc_dev)
{
	return 0;
}

static int max96726_start_streaming(struct tegracam_device *tc_dev)
{
	struct max96726_priv *priv = (struct max96726_priv *)tegracam_get_privdata(tc_dev);

	dev_dbg(tc_dev->dev, ">> %s on channel %d\n", __func__, priv->channel);

	/* CSI exit from standby */
	if (use_csi_standby)
		max96726_CsiPhyStandby(priv, priv->channel+1, false);

	return 0;
}
static int max96726_stop_streaming(struct tegracam_device *tc_dev)
{
	struct max96726_priv *priv = (struct max96726_priv *)tegracam_get_privdata(tc_dev);

	dev_dbg(tc_dev->dev, ">> %s on channel %d\n", __func__, priv->channel);

	/* CSI enter into standby */
	if (use_csi_standby)
		max96726_CsiPhyStandby(priv, priv->channel+1, true);

	return 0;
}

static struct camera_common_sensor_ops max96726_common_ops = {
	.numfrmfmts = ARRAY_SIZE(max96726_frmfmt),
	.frmfmt_table = max96726_frmfmt,
	.power_on = max96726_power_on,
	.power_off = max96726_power_off,
	.parse_dt = max96726_parse_dt,
	.power_get = max96726_power_get,
	.power_put = max96726_power_put,
	.set_mode = max96726_set_mode,
	.start_streaming = max96726_start_streaming,
	.stop_streaming = max96726_stop_streaming,
};

static int max96726_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_internal_ops max96726_subdev_internal_ops = {
	.open = max96726_open,
};

#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int max96726_probe(struct i2c_client *client)
#else
static int max96726_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct tegracam_device *tc_dev;
	struct max96726_priv *priv;
	int tmpid, tmpid0, tmpid1;
	int err;

	dev_info(dev, "%s: probing v4l2 sensor:%s\n", __func__, dev_name(dev));

	if (!IS_ENABLED(CONFIG_OF) || !node)
		return -EINVAL;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	tc_dev = devm_kzalloc(dev,
			sizeof(struct tegracam_device), GFP_KERNEL);
	if (!tc_dev)
		return -ENOMEM;

	priv->client = tc_dev->client = client;
	tc_dev->dev = dev;
	i2c_set_clientdata(client, priv);
	strncpy(tc_dev->name, "96726-tpg", sizeof(tc_dev->name));

	tc_dev->dev_regmap_config = &max96726_i2c_regmap;
	tc_dev->sensor_ops = &max96726_common_ops;
	tc_dev->v4l2sd_internal_ops = &max96726_subdev_internal_ops;
	tc_dev->tcctrl_ops = &max96726_ctrl_ops;

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

	/* max96726_0_pd and max96726_1_pd */
	priv->gpiod_pwdn[0] = devm_gpiod_get_optional(&client->dev, "max96726_0_pd",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_pwdn[0])) {
		err = PTR_ERR(priv->gpiod_pwdn[0]);
		goto un_register;
	}

	gpiod_set_consumer_name(priv->gpiod_pwdn[0], "max96726-0-pwdn");
	gpiod_set_value_cansleep(priv->gpiod_pwdn[0], 1);

	priv->gpiod_pwdn[1] = devm_gpiod_get_optional(&client->dev, "max96726_1_pd",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_pwdn[1])) {
		err = PTR_ERR(priv->gpiod_pwdn[1]);
		goto un_register;
	}

	gpiod_set_consumer_name(priv->gpiod_pwdn[1], "max96726-1-pwdn");
	gpiod_set_value_cansleep(priv->gpiod_pwdn[1], 1);

	if (priv->gpiod_pwdn[0] || priv->gpiod_pwdn[1])
		usleep_range(4000, 5000);

	tmpid = max96726_read(priv, 0x0d);
	tmpid0 = max96726_read(priv, 0x0e);
	tmpid1 = max96726_read(priv, 0x0f);
	pr_info("max96726-tpg: tmpid (0xd): 0x%x\n", tmpid);
	pr_info("max96726-tpg: tmpid0(0xe): 0x%x\n", tmpid0);
	pr_info("max96726-tpg: tmpid1(0xf): 0x%x\n", tmpid1);
	if ((tmpid0 != MAX96726_ID) && (tmpid0 != MAX96726_ID1)) {
		pr_err("max96726-tpg: ID 0x%x mismatch\n", tmpid0);
		return -ENODEV;
	}

	max96726_reset(priv);

	max96726_I2cSequence_Raw16_8pipes(priv);

	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err) {
		dev_err(dev, "tegra camera subdev registration failed\n");
		goto un_register;
	}
	dev_err(&client->dev, "Detected max96726 sensor\n");
	return 0;

un_register:
	tegracam_device_unregister(tc_dev);
	return err;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int max96726_remove(struct i2c_client *client)
#else
static void max96726_remove(struct i2c_client *client)
#endif
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct max96726_priv *priv = (struct max96726_priv *)s_data->priv;

	if (!s_data) {
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
		return -EINVAL;
#else
		return;
#endif
	}

	tegracam_v4l2subdev_unregister(priv->tc_dev);
	tegracam_device_unregister(priv->tc_dev);

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

static const struct i2c_device_id max96726_id[] = {
	{ "max96726-tpg", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max96726_id);
static struct i2c_driver max96726_i2c_driver = {
	.driver = {
		.name = "max96726-tpg",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(max96726_of_table),
	},
	.probe = max96726_probe,
	.remove = max96726_remove,
	.id_table = max96726_id,
};

module_i2c_driver(max96726_i2c_driver);

MODULE_DESCRIPTION("Maxim max96726 Quad GMSL2 Deserializer Driver");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL");
