// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/err.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_data/ina2xx.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/util_macros.h>

/* INA232 register definitions */
#define INA232_CONFIG			0x0
#define INA232_SHUNT_VOLTAGE		0x1
#define INA232_BUS_VOLTAGE		0x2
#define INA232_POWER			0x3
#define INA232_CURRENT			0x4
#define INA232_CALIBRATION		0x5
#define INA232_ENABLE			0x6
#define INA232_ALERT_LIMIT		0x7
#define INA232_REGISTERS		0x8

#define INA232_VBUSCT_MASK		0x01C0
#define INA232_VSHCT_MASK		0x0038
#define INA232_ALERT_CONFIG_MASK        0xFC00

#define INA232_ALERT_FUNCTION_FLAG      BIT(4)
#define INA232_POWER_OVER_LIMIT_FLAG	BIT(11)
#define INA232_CONFIG_ADCRANGE_FLAG	BIT(12)

#define INA232_RSHUNT_DEFAULT		500 /* uOhm */
#define INA232_SHUNT_GAIN_DEFAULT	1
#define INA232_MAX_EXPECTED_CURRENT_DEFAULT	5000000 /* 5A */

/* Default configuration of device on reset. */
#define INA232_RESET_VALUE		0x8000
#define INA232_CONFIG_DEFAULT		0x4d27

#define INA232_BUS_VOLTAGE_LSB		1600 /* 1.6 mV/lsb */

static struct regmap_config ina232_regmap_config = {
	.max_register = INA232_REGISTERS,
	.reg_bits = 8,
	.val_bits = 16,
};

struct ina232_data {
	struct i2c_client *client;
	struct mutex config_lock;
	struct regmap *regmap;
	struct notifier_block nb;
	unsigned int cache[INA232_REGISTERS];
	u32 max_expected_current;
	u32 current_lsb;
	u32 rshunt;
	int gain;
};

static int ina232_regmap_update_bits(struct device *dev,
				     struct regmap *map,
				     unsigned int reg,
				     unsigned int mask,
				     unsigned int val)
{
	struct ina232_data *data = dev_get_drvdata(dev);
	int ret;

	ret = regmap_update_bits(map, reg, mask, val);
	if (ret)
		return ret;

	return regmap_read(map, reg, &data->cache[reg]);
}

static int ina232_regmap_write(struct device *dev,
			       struct regmap *map,
			       unsigned int reg,
			       unsigned int val)
{
	struct ina232_data *data = dev_get_drvdata(dev);
	int ret;

	ret = regmap_write(map, reg, val);
	if (ret)
		return ret;

	return regmap_read(map, reg, &data->cache[reg]);
}

/* Extra attribute groups */
static ssize_t ina232_shunt_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ina232_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", data->rshunt);
}

static ssize_t ina232_power_alert_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct ina232_data *data = dev_get_drvdata(dev);
	unsigned long power_factor = 32 * data->current_lsb;
	unsigned int val;
	int regval;
	int ret;

	ret = regmap_read(data->regmap, INA232_ALERT_LIMIT, &regval);
	if (ret)
		return ret;

	val = regval * power_factor;

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t ina232_power_alert_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct ina232_data *data = dev_get_drvdata(dev);
	unsigned long power_factor = 32 * data->current_lsb;
	unsigned long val;
	unsigned int regval, flag;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	ret = ina232_regmap_update_bits(dev, data->regmap,
					INA232_ENABLE,
					INA232_ALERT_CONFIG_MASK, 0);
	if (ret < 0)
		return ret;

	regval = DIV_ROUND_CLOSEST(val, power_factor) & 0xffff;
	ret = ina232_regmap_write(dev, data->regmap, INA232_ALERT_LIMIT, regval);
	if (ret < 0)
		return ret;

	flag = regval ? INA232_POWER_OVER_LIMIT_FLAG : 0;
	ret = ina232_regmap_update_bits(dev, data->regmap,
					INA232_ENABLE,
					INA232_POWER_OVER_LIMIT_FLAG, flag);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t ina232_power_alarm_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct ina232_data *data = dev_get_drvdata(dev);
	int alarm, regval, ret;

	ret = regmap_read(data->regmap, INA232_ENABLE, &regval);
	if (ret)
		return ret;

	alarm = (regval & INA232_ALERT_FUNCTION_FLAG);
	return sysfs_emit(buf, "%d\n", alarm ? 1 : 0);
}

/* Conversion time in usecs for vbus/vshunt ADC converion */
static const unsigned long ina232_conv_times[] = {
	140, 204, 332, 588, 1100, 2116, 4156, 8244
};

static ssize_t ina232_power_interval_show(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	struct ina232_data *data = dev_get_drvdata(dev);
	unsigned long conv1, conv2;
	int index1, index2;
	int regval;
	int ret;

	ret = regmap_read(data->regmap, INA232_CONFIG, &regval);
	if (ret)
		return ret;

	index1 = (regval & INA232_VBUSCT_MASK) >> 6;
	index2 = (regval & INA232_VSHCT_MASK) >> 3;

	conv1 = ina232_conv_times[index1];
	conv2 = ina232_conv_times[index2];

	return sysfs_emit(buf, "%lu\n", conv1 + conv2);
}

static ssize_t ina232_power_interval_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct ina232_data *data = dev_get_drvdata(dev);
	unsigned long val, index;
	int ret, regval;

	ret = kstrtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	/*
	 * Power limit violation detection is followed after one vbus conversion
	 * and one vshunt conversion. Therefore, divide the passed in interval
	 * value by 2 to get the allocated time for vbus/vshunt conversion times
	 */
	val >>= 1;
	index = find_closest(val,
			     ina232_conv_times,
			     ARRAY_SIZE(ina232_conv_times));

	ret = regmap_read(data->regmap, INA232_CONFIG, &regval);
	if (ret < 0)
		return ret;

	ret = ina232_regmap_update_bits(dev, data->regmap,
					INA232_CONFIG,
					INA232_VBUSCT_MASK,
					index << 6);
	if (ret < 0)
		return ret;

	ret = ina232_regmap_update_bits(dev, data->regmap,
					INA232_CONFIG,
					INA232_VSHCT_MASK,
					index << 3);
	if (ret < 0)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR_RO(shunt_resistor, ina232_shunt, 0);
static SENSOR_DEVICE_ATTR_RW(power1_crit, ina232_power_alert, 0);
static SENSOR_DEVICE_ATTR_RO(power1_crit_alarm, ina232_power_alarm, 0);
static SENSOR_DEVICE_ATTR_RW(power1_update_interval, ina232_power_interval, 0);

static struct attribute *ina232_attrs[] = {
	&sensor_dev_attr_shunt_resistor.dev_attr.attr,
	&sensor_dev_attr_power1_crit.dev_attr.attr,
	&sensor_dev_attr_power1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_power1_update_interval.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ina232);

static int ina232_read_in(struct device *dev, int channel, long *val)
{
	struct ina232_data *data = dev_get_drvdata(dev);
	long shunt_volt;
	int regval;
	int err;

	switch (channel) {
	// Shunt voltage reading
	case 0:
		err = regmap_read(data->regmap, INA232_SHUNT_VOLTAGE, &regval);
		if (err < 0)
			return err;

		shunt_volt = (long)regval;

		// Handle negative value in two's complement
		if (regval & BIT(15)) {
			shunt_volt--;
			shunt_volt = ~shunt_volt;
			shunt_volt &= 0xffff;
		}

		// Multiple LSB based on ADCRANGE to get nV
		if (data->gain == 4)
			shunt_volt *= 625;
		else
			shunt_volt *= 2500;

		// Handle negative value in two's complement
		if (regval & BIT(15))
			*val = -(shunt_volt / 1000000);
		else
			*val = shunt_volt / 1000000;

		break;
	// Bus voltage reading
	case 1:
		err = regmap_read(data->regmap, INA232_BUS_VOLTAGE, &regval);
		if (err < 0)
			return err;

		*val = regval * INA232_BUS_VOLTAGE_LSB;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ina232_read_current(struct device *dev, long *val)
{
	struct ina232_data *data = dev_get_drvdata(dev);
	long curr;
	int regval;
	int err;

	// Current reading directly from register and convert to uA
	err = regmap_read(data->regmap, INA232_CURRENT, &regval);
	if (err < 0)
		return err;

	curr = (long)regval;

	// Handle negative value in two's complement
	if (regval & BIT(15)) {
		curr--;
		curr = ~curr;
		curr &= 0xffff;
		*val = -(curr * data->current_lsb);
	} else {
		*val = curr * data->current_lsb;
	}

	return 0;
}

static int ina232_read_power(struct device *dev, long *val)
{
	struct ina232_data *data = dev_get_drvdata(dev);
	int regval;
	int err;

	// Power reading directly from register in uW
	err = regmap_read(data->regmap, INA232_POWER, &regval);
	if (err < 0)
		return err;

	*val = 32 * regval * data->current_lsb;

	return 0;
}

static int ina232_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_in:
		return ina232_read_in(dev, channel, val);
	case hwmon_curr:
		return ina232_read_current(dev, val);
	case hwmon_power:
		return ina232_read_power(dev, val);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int ina232_write(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long val)
{
	return -EOPNOTSUPP;
}

static umode_t ina232_is_visible(const void *drvdata,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			return 0444;
		default:
			return 0;
		}
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			return 0444;
		default:
			return 0;
		}
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
			return 0444;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

static const struct hwmon_channel_info *ina232_info[] = {
	HWMON_CHANNEL_INFO(in,
			   /* shunt voltage in mV */
			   HWMON_I_INPUT,
			   /* bus voltage in mV */
			   HWMON_I_INPUT),
	HWMON_CHANNEL_INFO(curr,
			   /* current in mA */
			   HWMON_C_INPUT),
	HWMON_CHANNEL_INFO(power,
			   /* power in uW */
			   HWMON_P_INPUT),
	NULL
};

static const struct hwmon_ops ina232_hwmon_ops = {
	.is_visible = ina232_is_visible,
	.read = ina232_read,
	.write = ina232_write,
};

static const struct hwmon_chip_info ina232_chip_info = {
	.ops = &ina232_hwmon_ops,
	.info = ina232_info,
};

static int
ina232_pm_notifier(struct notifier_block *nb, unsigned long action, void *data)
{
	struct ina232_data *ina232 = container_of(nb, struct ina232_data, nb);
	int i, ret;

	switch (action) {
	case PM_POST_SUSPEND:
		for (i = 0; i < INA232_REGISTERS; i++) {
			ret = regmap_write(ina232->regmap, i, ina232->cache[i]);
			if (ret < 0)
				return ret;
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int ina232_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct ina232_data *data;
	u32 calibration;
	int config;
	int i, ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->config_lock);

	data->regmap = devm_regmap_init_i2c(client, &ina232_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}

	/* load shunt value in uOhm */
	if (device_property_read_u32(dev, "nvidia,shunt-resistor", &data->rshunt) < 0)
		data->rshunt = INA232_RSHUNT_DEFAULT;
	if (data->rshunt == 0) {
		dev_err(dev, "invalid shunt resister value %u\n", data->rshunt);
		return -EINVAL;
	}

	/* load shunt gain value (either 1 or 4) */
	if (device_property_read_u32(dev, "nvidia,shunt-gain", &data->gain) < 0)
		data->gain = INA232_SHUNT_GAIN_DEFAULT; /* Default of ADCRANGE = 0 */
	if (data->gain != 1 && data->gain != 4) {
		dev_err(dev, "invalid shunt gain value %u\n", data->gain);
		return -EINVAL;
	}

	/* load maximum expected current in uA */
	if (device_property_read_u32(dev,
				     "nvidia,max-expected-current",
				     &data->max_expected_current) < 0)
		data->max_expected_current = INA232_MAX_EXPECTED_CURRENT_DEFAULT;

	/* Reset the device */
	ret = regmap_write(data->regmap, INA232_CONFIG, INA232_RESET_VALUE);
	if (ret < 0) {
		dev_err(dev, "error resetting the device: %d\n", ret);
		return -ENODEV;
	}

	/* Setup CONFIG register */
	config = INA232_CONFIG_DEFAULT;
	if (data->gain == 4)
		config |= INA232_CONFIG_ADCRANGE_FLAG; /* ADCRANGE = 1 is /4 */

	ret = regmap_write(data->regmap, INA232_CONFIG, config);
	if (ret < 0) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return -ENODEV;
	}

	/* Setup SHUNT_CALIBRATION register */
	data->current_lsb = (data->max_expected_current >> 15) << 3;
	calibration = ((5120 * 1000 / data->current_lsb) * 1000) / data->rshunt;
	if (config & INA232_CONFIG_ADCRANGE_FLAG)
		calibration >>= 2;

	ret = regmap_write(data->regmap, INA232_CALIBRATION, calibration);
	if (ret < 0) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return -ENODEV;
	}

	/* Initialize the cache */
	for (i = 0; i < INA232_REGISTERS; i++) {
		ret = regmap_read(data->regmap, i, &data->cache[i]);
		if (ret) {
			dev_err(dev, "fail to cache %d register: %d\n", i, ret);
			return -ENODEV;
		}
	}

	/* Register PM notifier */
	data->nb.notifier_call = ina232_pm_notifier;
	ret = register_pm_notifier(&data->nb);
	if (ret) {
		dev_err(dev, "fail to register PM notifier: %d\n", ret);
		return -EINVAL;
	}

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, data,
							 &ina232_chip_info,
							 ina232_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	return 0;
}

#if !defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static void ina232_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ina232_data *data = dev_get_drvdata(dev);

	unregister_pm_notifier(&data->nb);
}
#else
static int ina232_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ina232_data *data = dev_get_drvdata(dev);

	unregister_pm_notifier(&data->nb);

	return 0;
}
#endif

static const struct i2c_device_id ina232_id[] = {
	{ "ina232", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ina232_id);

static const struct of_device_id __maybe_unused ina232_of_match[] = {
	{ .compatible = "nvidia,ina232" },
	{ },
};
MODULE_DEVICE_TABLE(of, ina232_of_match);

static struct i2c_driver ina232_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "ina232",
		.of_match_table = of_match_ptr(ina232_of_match),
	},
#if defined(NV_I2C_DRIVER_STRUCT_HAS_PROBE_NEW)
	.probe_new	= ina232_probe,
#else
	.probe		= ina232_probe,
#endif
	.remove		= ina232_remove,
	.id_table	= ina232_id,
};

module_i2c_driver(ina232_driver);

MODULE_AUTHOR("Johnny Liu <johnliu@nvidia.com>");
MODULE_DESCRIPTION("ina232 driver");
MODULE_LICENSE("GPL");
