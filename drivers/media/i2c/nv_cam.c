// SPDX-License-Identifier: GPL-2.0
/*
 * Common Nvidia V4L2 Sensor Driver
 *
 * Copyright (C) 2023 Analog Devices Inc.
 */

#include <linux/property.h>
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

static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_HDR_EN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_EXPOSURE_SHORT,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

#define MAX_CHIP_ID_REGS		3
#define MAX_GAIN_REGS			3

struct nv_cam_cmd {
	u32 *data;
	unsigned int len;
};

struct nv_cam_simple_gain {
	unsigned int num_regs;
	u32 min;
	u32 max;
	u32 regs[MAX_GAIN_REGS];
	u32 muls[MAX_GAIN_REGS];
	u32 divs[MAX_GAIN_REGS];
	u32 source_masks[MAX_GAIN_REGS];
	u32 target_masks[MAX_GAIN_REGS];
};

struct nv_cam_ad_gain {
	struct nv_cam_simple_gain analog;
	struct nv_cam_simple_gain digital;
};

struct nv_cam_mode {
	struct nv_cam_cmd mode_cmd;
	struct nv_cam_simple_gain simple_gain;
	struct nv_cam_ad_gain ad_gain;
	const char *gain_type;
};




struct nv_cam {
	struct i2c_client		*i2c_client;
	struct v4l2_subdev		*subdev;
	struct camera_common_data	*s_data;
	struct tegracam_device		*tc_dev;

	unsigned int			reg_bits;
	unsigned int			val_bits;

	unsigned int			num_chip_id_regs;
	u32				chip_id_regs[MAX_CHIP_ID_REGS];
	u32				chip_id_masks[MAX_CHIP_ID_REGS];
	u32				chip_id_vals[MAX_CHIP_ID_REGS];
	u32				pwdn_sleep_us;

	u32				wait_ms_cmd;
	struct nv_cam_cmd		mode_common_cmd;
	struct nv_cam_cmd		start_stream_cmd;
	struct nv_cam_cmd		stop_stream_cmd;

	struct nv_cam_mode		*modes;
	unsigned int			num_modes;
};

static const struct regmap_config sensor_regmap_config = {
	.use_single_read = true,
	.use_single_write = true,
};


static unsigned int nv_cam_field_get(unsigned int val, unsigned int mask)
{
	return (val & mask) >> __ffs(mask);
}

static unsigned int nv_cam_field_prep(unsigned int val, unsigned int mask)
{
	return (val << __ffs(mask)) & mask;
}

static inline int nv_cam_read_reg(struct camera_common_data *s_data,
				  u16 addr, u8 *val)
{
	unsigned int reg_val;
	int ret;

	ret = regmap_read(s_data->regmap, addr, &reg_val);
	if (ret) {
		dev_err(s_data->dev, "%s: i2c read 0x%x failed: %d",
			__func__, addr, ret);
		return ret;
	}

	*val = reg_val;

	return 0;
}

static inline int nv_cam_write_reg(struct camera_common_data *s_data,
				   u16 addr, u8 val)
{
	int ret;

	ret = regmap_write(s_data->regmap, addr, val);
	if (ret) {
		dev_err(s_data->dev, "%s: i2c write 0x%x = 0x%x failed: %d",
			__func__, addr, val, ret);
		return ret;
	}

	return 0;
}

static int nv_cam_write_cmd(struct nv_cam *priv, struct nv_cam_cmd *cmd)
{
	unsigned int i;
	int ret;

	for (i = 0; i < cmd->len;) {
		if (cmd->data[i] == priv->wait_ms_cmd) {
			msleep_range(cmd->data[i + 1]);

			i += 2;
		} else {
			ret = nv_cam_write_reg(priv->s_data, cmd->data[i], cmd->data[i + 1]);
			if (ret)
				return ret;

			i += 2;
		}
	}

	return 0;
}

static int nv_cam_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	return 0;
}

static int _nv_cam_set_gain_simple(struct tegracam_device *tc_dev,
				   struct nv_cam_simple_gain *gain,
				   s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	unsigned int i;
	int ret;

	for (i = 0; i < gain->num_regs; i++) {
		unsigned int reg_val = val * gain->muls[i] / gain->divs[i];

		reg_val = nv_cam_field_get(reg_val, gain->source_masks[i]);
		reg_val = nv_cam_field_prep(reg_val, gain->target_masks[i]);

		ret = nv_cam_write_reg(s_data, gain->regs[i], reg_val);
		if (ret)
			return ret;
	}

	return 0;
}

static int nv_cam_set_gain_simple(struct tegracam_device *tc_dev, s64 val)
{
	struct nv_cam *priv = tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	struct nv_cam_mode *mode = &priv->modes[s_data->mode];

	return _nv_cam_set_gain_simple(tc_dev, &mode->simple_gain, val);
}

static int nv_cam_set_gain_ad(struct tegracam_device *tc_dev, s64 val)
{
	struct nv_cam *priv = tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	struct nv_cam_mode *mode = &priv->modes[s_data->mode];
	unsigned int again;
	unsigned int dgain;
	int ret;

	again = DIV_ROUND_CLOSEST(val, mode->ad_gain.digital.min);
	if (again > mode->ad_gain.analog.max)
		again = mode->ad_gain.analog.max;

	dgain = DIV_ROUND_CLOSEST(val, again);
	if (dgain > mode->ad_gain.digital.max)
		dgain = mode->ad_gain.digital.max;

	ret = _nv_cam_set_gain_simple(tc_dev, &mode->ad_gain.analog, again);
	if (ret)
		return ret;

	ret = _nv_cam_set_gain_simple(tc_dev, &mode->ad_gain.digital, dgain);
	if (ret)
		return ret;

	return 0;
}

static int nv_cam_set_gain(struct tegracam_device *tc_dev, s64 val)
{
	struct nv_cam *priv = tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	struct nv_cam_mode *mode = &priv->modes[s_data->mode];

	if (!strcmp(mode->gain_type, "simple"))
		return nv_cam_set_gain_simple(tc_dev, val);
	else if (!strcmp(mode->gain_type, "ad"))
		return nv_cam_set_gain_ad(tc_dev, val);

	return -EINVAL;
}

static int nv_cam_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	return 0;
}

static int nv_cam_set_exposure(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = s_data->dev;

	dev_err(dev, "set exposure %lld\n", val);

	return 0;
}

static int nv_cam_set_exposure_short(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = s_data->dev;

	dev_err(dev, "set exposure short %lld\n", val);

	return 0;
}

static struct tegracam_ctrl_ops nv_cam_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.set_gain = nv_cam_set_gain,
	.set_exposure = nv_cam_set_exposure,
	.set_exposure_short = nv_cam_set_exposure_short,
	.set_frame_rate = nv_cam_set_frame_rate,
	.set_group_hold = nv_cam_set_group_hold,
};

static int nv_cam_power_on(struct camera_common_data *s_data)
{
	struct nv_cam *priv = (struct nv_cam *)s_data->priv;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	int ret = 0;

	if (pdata && pdata->power_on) {
		ret = pdata->power_on(pw);
		if (ret)
			dev_err(dev, "%s failed.\n", __func__);
		else
			pw->state = SWITCH_ON;
		return ret;
	}

	if (pw->pwdn_gpio) {
		if (gpio_cansleep(pw->pwdn_gpio))
			gpio_set_value_cansleep(pw->pwdn_gpio, 1);
		else
			gpio_set_value(pw->pwdn_gpio, 1);
		if (priv->pwdn_sleep_us)
			fsleep(priv->pwdn_sleep_us);
	}

	if (!pw->avdd && !pw->iovdd && !pw->dvdd)
		goto skip_power_seqn;

	if (pw->reset_gpio) {
		if (gpio_cansleep(pw->reset_gpio))
			gpio_set_value_cansleep(pw->reset_gpio, 0);
		else
			gpio_set_value(pw->reset_gpio, 0);
	}

	usleep_range(10, 20);

	if (pw->avdd) {
		ret = regulator_enable(pw->avdd);
		if (ret)
			goto nv_cam_avdd_fail;
	}

	if (pw->iovdd) {
		ret = regulator_enable(pw->iovdd);
		if (ret)
			goto nv_cam_iovdd_fail;
	}

	if (pw->dvdd) {
		ret = regulator_enable(pw->dvdd);
		if (ret)
			goto nv_cam_dvdd_fail;
	}

	usleep_range(10, 20);

skip_power_seqn:
	if (pw->reset_gpio) {
		if (gpio_cansleep(pw->reset_gpio))
			gpio_set_value_cansleep(pw->reset_gpio, 1);
		else
			gpio_set_value(pw->reset_gpio, 1);
	}

	usleep_range(10000, 10100);

	pw->state = SWITCH_ON;

	return 0;

nv_cam_dvdd_fail:
	regulator_disable(pw->iovdd);

nv_cam_iovdd_fail:
	regulator_disable(pw->avdd);

nv_cam_avdd_fail:
	dev_err(dev, "%s failed.\n", __func__);

	return -ENODEV;
}

static int nv_cam_power_off(struct camera_common_data *s_data)
{
	struct nv_cam *priv = (struct nv_cam *)s_data->priv;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	int ret = 0;

	if (pdata && pdata->power_off) {
		ret = pdata->power_off(pw);
		if (ret) {
			dev_err(dev, "%s failed.\n", __func__);
			return ret;
		}
	} else {
		if (pw->reset_gpio) {
			if (gpio_cansleep(pw->reset_gpio))
				gpio_set_value_cansleep(pw->reset_gpio, 0);
			else
				gpio_set_value(pw->reset_gpio, 0);
		}

		if (pw->pwdn_gpio) {
			if (gpio_cansleep(pw->pwdn_gpio))
				gpio_set_value_cansleep(pw->pwdn_gpio, 0);
			else
				gpio_set_value(pw->pwdn_gpio, 0);
			if (priv->pwdn_sleep_us)
				fsleep(priv->pwdn_sleep_us);
		}

		usleep_range(10, 20);

		if (pw->dvdd)
			regulator_disable(pw->dvdd);
		if (pw->iovdd)
			regulator_disable(pw->iovdd);
		if (pw->avdd)
			regulator_disable(pw->avdd);
	}

	usleep_range(5000, 5000);
	pw->state = SWITCH_OFF;

	return 0;
}

static int nv_cam_power_put(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;

	if (!pw)
		return -EFAULT;

	if (pw->dvdd)
		devm_regulator_put(pw->dvdd);

	if (pw->avdd)
		devm_regulator_put(pw->avdd);

	if (pw->iovdd)
		devm_regulator_put(pw->iovdd);

	pw->dvdd = NULL;
	pw->avdd = NULL;
	pw->iovdd = NULL;

	if (pw->reset_gpio)
		gpio_free(pw->reset_gpio);

	if (pw->pwdn_gpio)
		gpio_free(pw->pwdn_gpio);

	return 0;
}

static int nv_cam_power_get(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct clk *parent;
	int ret = 0;

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
		ret |= camera_common_regulator_get(dev,
				&pw->avdd, pdata->regulators.avdd);
	/* IO 1.8v */
	if (pdata->regulators.iovdd)
		ret |= camera_common_regulator_get(dev,
				&pw->iovdd, pdata->regulators.iovdd);
	/* dig 1.2v */
	if (pdata->regulators.dvdd)
		ret |= camera_common_regulator_get(dev,
				&pw->dvdd, pdata->regulators.dvdd);
	if (ret) {
		dev_err(dev, "%s: unable to get regulator(s)\n", __func__);
		goto done;
	}

	/* Reset GPIO */
	pw->reset_gpio = pdata->reset_gpio;
	if (!pdata->reset_gpio)
		goto skip_reset_gpio;
	ret = gpio_request(pw->reset_gpio, "cam_reset_gpio");
	if (ret < 0) {
		dev_err(dev, "%s: unable to request reset_gpio (%d)\n",
			__func__, ret);
		goto done;
	}

skip_reset_gpio:

	/* PWDN GPIO */
	if (!pdata->pwdn_gpio)
		goto skip_pwdn_gpio;
	pw->pwdn_gpio = pdata->pwdn_gpio;
	ret = gpio_request(pw->pwdn_gpio, "cam_pwdn_gpio");
	if (ret < 0) {
		dev_err(dev, "%s: unable to request pwdn_gpio (%d)\n",
			__func__, ret);
		goto done;
	}

skip_pwdn_gpio:
done:
	pw->state = SWITCH_OFF;

	return ret;
}

static struct camera_common_pdata *nv_cam_parse_dt(struct tegracam_device *tc_dev)
{
	struct camera_common_pdata *board_priv_pdata;
	struct camera_common_pdata *ret = NULL;
	struct device *dev = tc_dev->dev;
	struct device_node *np = dev->of_node;
	int err = 0;
	int gpio;

	if (!np)
		return NULL;

	board_priv_pdata = devm_kzalloc(dev,
		sizeof(*board_priv_pdata), GFP_KERNEL);
	if (!board_priv_pdata)
		return NULL;

	gpio = of_get_named_gpio(np, "reset-gpios", 0);
	if (gpio < 0) {
		if (gpio == -EPROBE_DEFER)
			ret = ERR_PTR(-EPROBE_DEFER);
		dev_err(dev, "reset-gpios not found\n");
		gpio = 0;
	}
	board_priv_pdata->reset_gpio = (unsigned int)gpio;

	gpio = of_get_named_gpio(np, "pwdn-gpios", 0);
	if (gpio < 0) {
		if (gpio == -EPROBE_DEFER)
			ret = ERR_PTR(-EPROBE_DEFER);
		dev_err(dev, "pwdn-gpios not found\n");
		gpio = 0;
	}
	board_priv_pdata->pwdn_gpio = (unsigned int)gpio;

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

	board_priv_pdata->has_eeprom =
		of_property_read_bool(np, "has-eeprom");

	return board_priv_pdata;

	devm_kfree(dev, board_priv_pdata);

	return ret;
}

static int nv_cam_set_mode(struct tegracam_device *tc_dev)
{
	struct nv_cam *priv = tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = tc_dev->dev;
	struct sensor_mode_properties *mode =
	 	&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];

	//u16 link_speed = 0;
	u16 lane_rate = 0;



	if (s_data->mode < 0 || s_data->mode > priv->num_modes){
		dev_err(dev, "s_data->mode: %d   priv->num_modes: %d\n", s_data->mode, priv->num_modes);
		return -EINVAL;
	}

	lane_rate = mode->signal_properties.serdes_pixel_clock.val * 16 / 100000000 / mode->signal_properties.num_lanes;

	//for csi4&csi5-->vin4 and csi6&csi7-->vin5 lane rate must Sum  two csi prot rate 
	//rate = rate * depth / signal->num_lanes;
	//signal->mipi_clock.val = rate / 2;

	dev_err(dev, "==Total_lane_rate ,==lane_rate = 0x%x,  serdes_pixel_clock = %lld pixel_clock.val = %lld, mipi_clock.val = %lld\n", 
			lane_rate, mode->signal_properties.serdes_pixel_clock.val, 
			mode->signal_properties.pixel_clock.val, 
			mode->signal_properties.mipi_clock.val);


	// ret = nv_cam_write_cmd(priv, &priv->mode_common_cmd);
	// if (ret) {
	// 	dev_err(dev, "Failed to write common mode cmd: %d\n", ret);
	// 	return ret;
	// }

	// ret = nv_cam_write_cmd(priv, &priv->modes[s_data->mode].mode_cmd);
	// if (ret) {
	// 	dev_err(dev, "Failed to write mode cmd: %d\n", ret);
	// 	return ret;
	// }

	return 0;
}

static int nv_cam_start_streaming(struct tegracam_device *tc_dev)
{
	struct nv_cam *priv = tegracam_get_privdata(tc_dev);
	struct device *dev = &priv->i2c_client->dev;
	int ret;

	ret = nv_cam_write_cmd(priv, &priv->start_stream_cmd);
	if (ret) {
		dev_err(dev, "Failed to write start stream cmd: %d\n", ret);
		return ret;
	}

	return 0;
}

static int nv_cam_stop_streaming(struct tegracam_device *tc_dev)
{
	struct nv_cam *priv = tegracam_get_privdata(tc_dev);
	struct device *dev = &priv->i2c_client->dev;
	int ret;

	ret = nv_cam_write_cmd(priv, &priv->stop_stream_cmd);
	if (ret) {
		dev_err(dev, "Failed to write stop stream cmd: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct camera_common_sensor_ops nv_cam_common_ops = {
	.power_on = nv_cam_power_on,
	.power_off = nv_cam_power_off,
	.write_reg = nv_cam_write_reg,
	.read_reg = nv_cam_read_reg,
	.parse_dt = nv_cam_parse_dt,
	.power_get = nv_cam_power_get,
	.power_put = nv_cam_power_put,
	.set_mode = nv_cam_set_mode,
	.start_streaming = nv_cam_start_streaming,
	.stop_streaming = nv_cam_stop_streaming,
};

static int __nv_cam_check_id(struct nv_cam *priv, unsigned int i)
{
	struct camera_common_data *s_data = priv->s_data;
	struct device *dev = s_data->dev;
	unsigned int reg, mask, val;
	u8 reg_val;
	int ret;

	reg = priv->chip_id_regs[i];
	mask = priv->chip_id_masks[i];
	val = priv->chip_id_vals[i];

	ret = nv_cam_read_reg(s_data, reg, &reg_val);
	if (ret)
		return ret;

	val &= mask;
	reg_val &= mask;

	if (reg_val != val) {
		dev_err(dev, "Invalid chip id 0x%x, expected 0x%x\n",
			reg_val, val);
		return -EINVAL;
	}

	return 0;
}

static int __nv_cam_check_ids(struct nv_cam *priv)
{
	unsigned int i;
	int ret;

	for (i = 0; i < priv->num_chip_id_regs; i++) {
		ret = __nv_cam_check_id(priv, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int nv_cam_check_ids(struct nv_cam *priv)
{
	int retry = 100;
	int ret;

retry:
	ret = __nv_cam_check_ids(priv);
	if (ret && retry) {
		retry--;
		udelay(1000);
		goto retry;
	}

	return ret;
}

static int nv_cam_board_setup(struct nv_cam *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	int ret = 0;

	if (pdata->mclk_name) {
		ret = camera_common_mclk_enable(s_data);
		if (ret) {
			dev_err(dev, "error turning on mclk (%d)\n", ret);
			goto done;
		}
	}

	ret = nv_cam_power_on(s_data);
	if (ret) {
		dev_err(dev, "error during power on sensor (%d)\n", ret);
		goto err_power_on;
	}

	ret = nv_cam_check_ids(priv);

	nv_cam_power_off(s_data);

err_power_on:
	if (pdata->mclk_name)
		camera_common_mclk_disable(s_data);

done:
	return ret;
}

static int nv_cam_parse_dt_cmd(struct nv_cam *priv, struct fwnode_handle *fwnode,
			       struct nv_cam_cmd *cmd, const char *name)
{
	struct device *dev = &priv->i2c_client->dev;
	int ret;

	ret = fwnode_property_count_u32(fwnode, name);
	if (ret <= 0)
		return ret;

	cmd->len = ret;

	cmd->data = devm_kcalloc(dev, cmd->len, sizeof(*cmd->data), GFP_KERNEL);
	if (!cmd->data)
		return -ENOMEM;

	return fwnode_property_read_u32_array(fwnode, name, cmd->data, cmd->len);
}

#define nv_cam_format_gain_prop(name, end, prefix) \
	snprintf((name), sizeof(name), "nv,%s-" end, prefix);

static int _nv_cam_parse_dt_mode_gain_simple(struct nv_cam *priv, struct fwnode_handle *fwnode,
					     struct nv_cam_simple_gain *gain, const char *prefix,
					     bool need_min_max)
{
	struct device *dev = &priv->i2c_client->dev;
	unsigned int i;
	char name[32];
	int ret;

	nv_cam_format_gain_prop(name, "min", prefix);
	ret = fwnode_property_read_u32(fwnode, name, &gain->min);
	if (ret && need_min_max) {
		dev_err(dev, "Failed to read gain min: %d\n", ret);
		return ret;
	}

	nv_cam_format_gain_prop(name, "max", prefix);
	ret = fwnode_property_read_u32(fwnode, name, &gain->max);
	if (ret && need_min_max) {
		dev_err(dev, "Failed to read gain max: %d\n", ret);
		return ret;
	}

	nv_cam_format_gain_prop(name, "regs", prefix);
	ret = fwnode_property_count_u32(fwnode, name);
	if (ret <= 0) {
		dev_err(dev, "Failed to read gain max: %d\n", ret);
		return -EINVAL;
	}

	gain->num_regs = ret;

	ret = fwnode_property_read_u32_array(fwnode, name, gain->regs, gain->num_regs);
	if (ret) {
		dev_err(dev, "Failed to read gain regs: %d\n", ret);
		return ret;
	}

	nv_cam_format_gain_prop(name, "muls", prefix);
	ret = fwnode_property_read_u32_array(fwnode, name, gain->muls, gain->num_regs);
	if (ret) {
		dev_info(dev, "Gain muls missing, using default: %d\n", ret);

		for (i = 0; i < gain->num_regs; i++)
			gain->muls[i] = 1;
	}

	nv_cam_format_gain_prop(name, "divs", prefix);
	ret = fwnode_property_read_u32_array(fwnode, name, gain->divs, gain->num_regs);
	if (ret) {
		dev_info(dev, "Gain divs missing, using default: %d\n", ret);

		for (i = 0; i < gain->num_regs; i++)
			gain->divs[i] = 1;
	}

	nv_cam_format_gain_prop(name, "source-masks", prefix);
	ret = fwnode_property_read_u32_array(fwnode, name, gain->source_masks, gain->num_regs);
	if (ret) {
		dev_err(dev, "Failed to read gain source masks: %d\n", ret);
		return ret;
	}

	nv_cam_format_gain_prop(name, "target-masks", prefix);
	ret = fwnode_property_read_u32_array(fwnode, name, gain->target_masks, gain->num_regs);
	if (ret) {
		dev_err(dev, "Failed to read gain target masks: %d\n", ret);
		return ret;
	}

	return 0;
}

static int nv_cam_parse_dt_mode_gain_simple(struct nv_cam *priv, struct fwnode_handle *fwnode,
					    struct nv_cam_mode *mode)
{
	return _nv_cam_parse_dt_mode_gain_simple(priv, fwnode, &mode->simple_gain, "gain", false);
}

static int nv_cam_parse_dt_mode_gain_ad(struct nv_cam *priv, struct fwnode_handle *fwnode,
					struct nv_cam_mode *mode)
{
	int ret;

	ret = _nv_cam_parse_dt_mode_gain_simple(priv, fwnode, &mode->ad_gain.analog, "again", true);
	if (ret)
		return ret;

	ret = _nv_cam_parse_dt_mode_gain_simple(priv, fwnode, &mode->ad_gain.digital, "dgain", true);
	if (ret)
		return ret;

	return 0;
}

static int nv_cam_parse_dt_mode_gain(struct nv_cam *priv, struct fwnode_handle *fwnode,
				     struct nv_cam_mode *mode)
{
	const char *gain_type;
	int ret;

	ret = fwnode_property_read_string(fwnode, "nv,gain-type", &gain_type);
	if (ret)
		return 0;

	mode->gain_type = gain_type;

	if (!strcmp(gain_type, "simple"))
		return nv_cam_parse_dt_mode_gain_simple(priv, fwnode, mode);
	else if (!strcmp(gain_type, "ad"))
		return nv_cam_parse_dt_mode_gain_ad(priv, fwnode, mode);

	return -EINVAL;
}

static int nv_cam_parse_dt_mode(struct nv_cam *priv, struct fwnode_handle *fwnode,
				struct nv_cam_mode *mode)
{
	struct device *dev = &priv->i2c_client->dev;
	int ret;

	ret = nv_cam_parse_dt_cmd(priv, fwnode, &mode->mode_cmd,
				  "nv,mode-cmd");
	if (ret) {
		dev_err(dev, "Failed to read mode cmd: %d\n", ret);
		return ret;
	}

	ret = nv_cam_parse_dt_mode_gain(priv, fwnode, mode);
	if (ret) {
		dev_err(dev, "Failed to read mode gain: %d\n", ret);
		return ret;
	}

	return 0;
}

static int nv_cam_parse_dt_count_modes(struct nv_cam *priv)
{
	struct device *dev = &priv->i2c_client->dev;
	char temp_str[OF_MAX_STR_LEN];
	struct fwnode_handle *fwnode;
	unsigned int num_modes = 0;
	unsigned int i;
	int ret;

	for (i = 0; num_modes < MAX_NUM_SENSOR_MODES; i++) {
		ret = snprintf(temp_str, sizeof(temp_str), "%s%d",
			       OF_SENSORMODE_PREFIX, i);
		if (ret < 0)
			return -EINVAL;

		fwnode = device_get_named_child_node(dev, temp_str);
		if (!fwnode)
			break;

		fwnode_handle_put(fwnode);

		num_modes++;
	}

	priv->num_modes = num_modes;

	return 0;
}

static int nv_cam_parse_dt_modes(struct nv_cam *priv)
{
	struct device *dev = &priv->i2c_client->dev;
	char temp_str[OF_MAX_STR_LEN];
	struct fwnode_handle *fwnode;
	unsigned int i;
	int ret;

	if (!priv->num_modes)
		return 0;

	priv->modes = devm_kcalloc(dev, priv->num_modes,
				   sizeof(*priv->modes), GFP_KERNEL);
	if (!priv->modes)
		return -ENOMEM;

	for (i = 0; i < priv->num_modes; i++) {
		ret = snprintf(temp_str, sizeof(temp_str), "%s%d",
			       OF_SENSORMODE_PREFIX, i);
		if (ret < 0)
			return -EINVAL;

		fwnode = device_get_named_child_node(dev, temp_str);
		if (!fwnode)
			break;

		ret = nv_cam_parse_dt_mode(priv, fwnode, &priv->modes[i]);

		fwnode_handle_put(fwnode);

		if (ret) {
			dev_err(dev, "Failed to parse mode: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int nv_cam_parse_dt_cmds(struct nv_cam *priv)
{
	struct device *dev = &priv->i2c_client->dev;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	u32 val;
	int ret;

	val = 0x00;
	ret = device_property_read_u32(dev, "nv,wait-ms-cmd", &val);
	if (ret)
		dev_info(dev, "Failed to read wait cmd, using default: %d\n", ret);
	priv->wait_ms_cmd = val;

	ret = nv_cam_parse_dt_cmd(priv, fwnode, &priv->mode_common_cmd,
				  "nv,mode-common-cmd");
	if (ret)
		dev_err(dev, "Failed to read common mode cmd: %d\n", ret);

	ret = nv_cam_parse_dt_cmd(priv, fwnode, &priv->start_stream_cmd,
				  "nv,start-stream-cmd");
	if (ret)
		dev_err(dev, "Failed to read start stream cmd: %d\n", ret);

	ret = nv_cam_parse_dt_cmd(priv, fwnode, &priv->stop_stream_cmd,
				  "nv,stop-stream-cmd");
	if (ret)
		dev_err(dev, "Failed to read stop stream cmd: %d\n", ret);

	ret = nv_cam_parse_dt_count_modes(priv);
	if (ret) {
		dev_err(dev, "Failed to count number of modes: %d\n", ret);
		return ret;
	}

	ret = nv_cam_parse_dt_modes(priv);
	if (ret) {
		dev_err(dev, "Failed to parse modes: %d\n", ret);
		return ret;
	}

	return 0;
}

static int nv_cam_parse_dt_chip_ids(struct nv_cam *priv)
{
	struct device *dev = &priv->i2c_client->dev;
	int ret;

	ret = device_property_count_u32(dev, "nv,chip-id-regs");
	if (ret <= 0) {
		dev_err(dev, "Failed to reach chip ID regs: %d\n", ret);
		return ret;
	}

	priv->num_chip_id_regs = ret;

	ret = device_property_read_u32_array(dev, "nv,chip-id-regs",
					     priv->chip_id_regs,
					     priv->num_chip_id_regs);
	if (ret) {
		dev_err(dev, "Failed to read chip ID regs: %d\n", ret);
		return ret;
	}

	ret = device_property_read_u32_array(dev, "nv,chip-id-masks",
					     priv->chip_id_masks,
					     priv->num_chip_id_regs);
	if (ret) {
		dev_err(dev, "Failed to read chip ID masks: %d\n", ret);
		return ret;
	}

	ret = device_property_read_u32_array(dev, "nv,chip-id-vals",
					     priv->chip_id_vals,
					     priv->num_chip_id_regs);
	if (ret) {
		dev_err(dev, "Failed to read chip ID vals: %d\n", ret);
		return ret;
	}

	return 0;
}

static int nv_cam_parse_dt_extra(struct nv_cam *priv)
{
	struct device *dev = &priv->i2c_client->dev;
	int ret;
	u32 val;

	val = 8;
	ret = device_property_read_u32(dev, "nv,reg-bits", &val);
	if (ret)
		dev_info(dev, "Failed to read register bits, using default: %d\n", ret);
	priv->reg_bits = val;

	val = 8;
	ret = device_property_read_u32(dev, "nv,val-bits", &val);
	if (ret)
		dev_info(dev, "Failed to read value bits, using default: %d\n", ret);
	priv->val_bits = val;

	val = 0;
	ret = device_property_read_u32(dev, "nv,pwdn-sleep-us", &val);
	if (ret)
		dev_info(dev, "Failed to read pwdn sleep us, using default: %d\n", ret);
	priv->pwdn_sleep_us = val;

	ret = nv_cam_parse_dt_cmds(priv);
	if (ret)
		return ret;

	ret = nv_cam_parse_dt_chip_ids(priv);
	if (ret)
		return ret;

	return 0;
}

static int nv_cam_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct regmap_config regmap_config;
	struct device *dev = &client->dev;
	struct tegracam_device *tc_dev;
	struct nv_cam *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	tc_dev = devm_kzalloc(dev, sizeof(*tc_dev), GFP_KERNEL);
	if (!tc_dev)
		return -ENOMEM;

	priv->i2c_client = tc_dev->client = client;

	ret = nv_cam_parse_dt_extra(priv);
	if (ret)
		return ret;

	regmap_config = sensor_regmap_config;
	regmap_config.reg_bits = priv->reg_bits;
	regmap_config.val_bits = priv->val_bits;

	tc_dev->dev = dev;
	strncpy(tc_dev->name, "nv_cam", sizeof(tc_dev->name));
	tc_dev->dev_regmap_config = &regmap_config;
	tc_dev->sensor_ops = &nv_cam_common_ops;
	tc_dev->tcctrl_ops = &nv_cam_ctrl_ops;

	ret = tegracam_device_register(tc_dev);
	if (ret) {
		dev_err(dev, "tegra camera driver registration failed\n");
		return ret;
	}
	priv->tc_dev = tc_dev;
	priv->s_data = tc_dev->s_data;
	priv->subdev = &tc_dev->s_data->subdev;
	tegracam_set_privdata(tc_dev, (void *)priv);

	ret = nv_cam_board_setup(priv);
	if (ret) {
		tegracam_device_unregister(tc_dev);
		dev_err(dev, "board setup failed\n");
		return ret;
	}

	return tegracam_v4l2subdev_register(tc_dev, true);
}

static int nv_cam_remove(struct i2c_client *client)
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct nv_cam *priv = (struct nv_cam *)s_data->priv;

	tegracam_v4l2subdev_unregister(priv->tc_dev);
	tegracam_device_unregister(priv->tc_dev);

	return 0;
}

static const struct of_device_id nv_cam_of_match[] = {
	{ .compatible = "nv,nv-cam", },
	{ },
};
MODULE_DEVICE_TABLE(of, nv_cam_of_match);

static const struct i2c_device_id nv_cam_id[] = {
	{ "nv_cam", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nv_cam_id);

static struct i2c_driver nv_cam_i2c_driver = {
	.driver = {
		.name = "nv_cam",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nv_cam_of_match),
	},
	.probe = nv_cam_probe,
	.remove = nv_cam_remove,
	.id_table = nv_cam_id,
};
module_i2c_driver(nv_cam_i2c_driver);

MODULE_DESCRIPTION("Common Nvidia V4L2 Sensor Driver");
MODULE_AUTHOR("Analog Devices Inc.");
MODULE_LICENSE("GPL v2");
