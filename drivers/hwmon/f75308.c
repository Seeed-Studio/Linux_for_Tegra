// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/version.h>

#define DEVICE_NAME "f75308"
#define DEVICE_VID_ADDR 0xC0
#define DEVICE_PID_ADDR 0xC2

#define DEVICE_VID 0x1934
#define DEVICE_PID_64PIN 0x1012
#define DEVICE_PID_48PIN 0x1022
#define DEVICE_PID_28PIN 0x1032

#define F75308_MAX_FAN_IN 14
#define F75308_MAX_FAN_CTRL_CNT 11
#define F75308_MAX_FAN_SEG_CNT 5

#define F75308_REG_BANK 0x00
#define F75308_REG_VOLT(index) (0x30 + (index))
#define F75308_REG_TEMP_READ(index) (0x40 + (index)*2)
#define F75308_REG_FAN_READ(index) (0x80 + (index)*2)
#define F75308_REG_FAN_DUTY_READ(index) (0xA0 + (index))
#define F75308_REG_FAN_DUTY_WRITE(index) (0x11 + (index)*0x10)
#define F75308_REG_FAN_TYPE(index) (0x70 + (index) / 4)
#define F75308_REG_FAN_MODE(index) (0x74 + (index) / 4)
#define F75308_REG_FAN_SEG(index) (0x18 + (index)*0x10)
#define F75308_REG_FAN_MAP(index) (0x50 + (index))

#define F75308_BIT_PAIR_SHIFT(index) ((index) % 4 * 2)

enum chip {
	f75308a_28,
	f75308b_48,
	f75308c_64,
};

struct f75308_priv {
	struct mutex locker;
	struct i2c_client *client;
	struct device *hwmon_dev;
	enum chip chip_id;
};

static inline int temp_from_s16(s16 val)
{
	/*
	 * For F75308 series, the format of the temperature reading is
	 * signed 11-bit with LSB = 0.125 degree Celsius, left-justified
	 * in 16-bit register. To align with the unit used in the thermal
	 * framework, convert it to millidegree Celsius.
	 */
	return val / 32 * 125;
}

static int f75308_read8(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int f75308_write8(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

/*
 * It is assumed that client->locker is held (unless we are in detection or
 * initialization steps).
 */
static int f75308_read16(struct i2c_client *client, u8 reg)
{
	int hi, lo;

	hi = f75308_read8(client, reg);
	if (hi < 0)
		return hi;

	lo = f75308_read8(client, reg + 1);
	if (lo < 0)
		return lo;

	return hi << 8 | lo;
}

/*
 * It is assumed that client->locker is held (unless we are in detection or
 * initialization steps).
 */
static int f75308_write_mask8(struct i2c_client *client, u8 reg, u8 mask,
			      u8 value)
{
	int status;

	status = f75308_read8(client, reg);
	if (status < 0)
		return status;

	status = (status & ~mask) | (value & mask);
	return f75308_write8(client, reg, status);
}

/*
 * It is assumed that client->locker is held (unless we are in detection or
 * initialization steps).
 */
static bool f75308_manual_duty_enabled(struct i2c_client *client, int index)
{
	int status, data;

	status = f75308_write8(client, F75308_REG_BANK, 4);
	if (status)
		return status;

	data = f75308_read8(client, F75308_REG_FAN_MODE(index));
	if (data < 0)
		return data;

	data >>= F75308_BIT_PAIR_SHIFT(index);
	data &= GENMASK(1, 0);

	return data == 0x03;
}

static ssize_t f75308_in_input_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int index = to_sensor_dev_attr(attr)->index;
	int status, data;

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 0);
	if (status)
		goto release_lock;

	data = f75308_read8(client, F75308_REG_VOLT(index));
	if (data < 0) {
		status = data;
		goto release_lock;
	}

	mutex_unlock(&priv->locker);

	/* Scale of the voltage is 8mV */
	data *= 8;
	status = sprintf(buf, "%d\n", data);
	return status;

release_lock:
	mutex_unlock(&priv->locker);
	return status;
}

static ssize_t f75308_temp_input_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int index = to_sensor_dev_attr(attr)->index;
	int status, data;

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 0);
	if (status)
		goto release_lock;

	data = f75308_read16(client, F75308_REG_TEMP_READ(index));
	if (data < 0) {
		status = data;
		goto release_lock;
	}

	mutex_unlock(&priv->locker);

	data = temp_from_s16(data);
	status = sprintf(buf, "%d\n", data);
	return status;

release_lock:
	mutex_unlock(&priv->locker);
	return status;
}

static ssize_t f75308_fan_input_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int index = to_sensor_dev_attr(attr)->index;
	int status, lsb, msb, data;

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 0);
	if (status)
		goto release_lock;

	msb = f75308_read8(client, F75308_REG_FAN_READ(index) + 0);
	if (msb < 0) {
		status = msb;
		goto release_lock;
	}

	lsb = f75308_read8(client, F75308_REG_FAN_READ(index) + 1);
	if (lsb < 0) {
		status = lsb;
		goto release_lock;
	}

	mutex_unlock(&priv->locker);

	dev_dbg(dev, "%s: index: %d, msb: %x, lsb: %x\n", __func__, index, msb,
		lsb);
	if (msb == 0x1F && lsb == 0xFF)
		data = 0;
	else if (msb || lsb)
		data = 1500000 / (msb * 256 + lsb);
	else
		data = 0;

	status = sprintf(buf, "%d\n", data);
	return status;

release_lock:
	mutex_unlock(&priv->locker);
	return status;
}

static ssize_t f75308_pwm_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int index = to_sensor_dev_attr(attr)->index;
	int status, data;

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 0);
	if (status)
		goto release_lock;

	data = f75308_read8(client, F75308_REG_FAN_DUTY_READ(index));
	if (data < 0) {
		status = data;
		goto release_lock;
	}

	mutex_unlock(&priv->locker);

	status = sprintf(buf, "%d\n", data);
	return status;

release_lock:
	mutex_unlock(&priv->locker);
	return status;
}

static ssize_t f75308_pwm_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int index = to_sensor_dev_attr(attr)->index;
	int status, pwm;

	status = kstrtoint(buf, 0, &pwm);
	if (status)
		return status;

	pwm = clamp_val(pwm, 0, 255);

	mutex_lock(&priv->locker);

	if (!f75308_manual_duty_enabled(client, index)) {
		dev_err(dev, "%s: Only manual_duty mode supports PWM write!\n",
			__func__);
		status = -EOPNOTSUPP;
		goto release_lock;
	}

	status = f75308_write8(client, F75308_REG_BANK, 5);
	if (status)
		goto release_lock;

	status = f75308_write8(client, F75308_REG_FAN_DUTY_WRITE(index), pwm);

release_lock:
	mutex_unlock(&priv->locker);
	return status ? status : count;
}

static ssize_t f75308_fan_type_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int index = to_sensor_dev_attr(attr)->index;
	int status, data;

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 4);
	if (status)
		goto release_lock;

	data = f75308_read8(client, F75308_REG_FAN_TYPE(index));
	if (data < 0) {
		status = data;
		goto release_lock;
	}

	mutex_unlock(&priv->locker);

	data >>= F75308_BIT_PAIR_SHIFT(index);
	data &= GENMASK(1, 0);

	switch (data) {
	case 0:
		status = sprintf(buf, "pwm\n");
		break;
	case 1:
		status = sprintf(buf, "linear\n");
		break;
	case 2:
		status = sprintf(buf, "pwm_opendrain\n");
		break;
	default:
		status = sprintf(buf,
				 "%s: invalid data: index: %d, data: %xh\n",
				 __func__, index, data);
		break;
	}

	return status;

release_lock:
	mutex_unlock(&priv->locker);
	return status;
}

static int __f75308_fan_type_store(struct device *dev, int index,
				   const char *buf)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int status, data, shift;

	shift = F75308_BIT_PAIR_SHIFT(index);
	if (!strncmp(buf, "pwm_opendrain", strlen("pwm_opendrain"))) {
		data = 0x02;
	} else if (!strncmp(buf, "linear", strlen("linear"))) {
		data = 0x01;
	} else if (!strncmp(buf, "pwm", strlen("pwm"))) {
		data = 0x00;
	} else {
		dev_err(dev, "%s: support only pwm/linear/pwm_opendrain\n",
			__func__);
		return -EINVAL;
	}

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 4);
	if (status)
		goto release_lock;

	status = f75308_write_mask8(client, F75308_REG_FAN_TYPE(index),
				    GENMASK(1, 0) << shift, data << shift);

release_lock:
	mutex_unlock(&priv->locker);
	return status;
}

static ssize_t f75308_fan_type_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(attr)->index;
	int status;

	status = __f75308_fan_type_store(dev, index, buf);
	return status ? status : count;
}

static ssize_t f75308_fan_mode_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int index = to_sensor_dev_attr(attr)->index;
	int status, data;

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 4);
	if (status)
		goto release_lock;

	data = f75308_read8(client, F75308_REG_FAN_MODE(index));
	if (data < 0) {
		status = data;
		goto release_lock;
	}

	mutex_unlock(&priv->locker);

	data >>= F75308_BIT_PAIR_SHIFT(index);
	data &= GENMASK(1, 0);

	switch (data) {
	case 0:
		status = sprintf(buf, "auto_rpm\n");
		break;
	case 1:
		status = sprintf(buf, "auto_duty\n");
		break;
	case 2:
		status = sprintf(buf, "manual_rpm\n");
		break;
	case 3:
		status = sprintf(buf, "manual_duty\n");
		break;
	default:
		status = sprintf(buf,
				 "%s: invalid data: index: %d, data: %xh\n",
				 __func__, index, data);
		break;
	}

	return status;

release_lock:
	mutex_unlock(&priv->locker);
	return status;
}

static int __f75308_fan_mode_store(struct device *dev, int index,
				   const char *buf)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int status, data, shift;

	shift = F75308_BIT_PAIR_SHIFT(index);
	if (!strncmp(buf, "manual_rpm", strlen("manual_rpm"))) {
		data = 0x02;
	} else if (!strncmp(buf, "manual_duty", strlen("manual_duty"))) {
		data = 0x03;
	} else if (!strncmp(buf, "auto_rpm", strlen("auto_rpm"))) {
		data = 0x00;
	} else if (!strncmp(buf, "auto_duty", strlen("auto_duty"))) {
		data = 0x01;
	} else {
		dev_err(dev,
			"%s: support only manual_rpm/manual_duty/auto_rpm/auto_duty\n",
			__func__);

		return -EINVAL;
	}

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 4);
	if (status)
		goto release_lock;

	status = f75308_write_mask8(client, F75308_REG_FAN_MODE(index),
				    GENMASK(1, 0) << shift, data << shift);

release_lock:
	mutex_unlock(&priv->locker);
	return status;
}

static ssize_t f75308_fan_mode_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(attr)->index;
	int status;

	status = __f75308_fan_mode_store(dev, index, buf);
	return status ? status : count;
}

static ssize_t f75308_fan_5_seg_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int index = to_sensor_dev_attr(attr)->index;
	int status, data[F75308_MAX_FAN_SEG_CNT], i;

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 5);
	if (status)
		goto release_lock;

	for (i = 0; i < F75308_MAX_FAN_SEG_CNT; ++i) {
		data[i] = f75308_read8(client, F75308_REG_FAN_SEG(index) + i);
		if (data[i] < 0) {
			status = data[i];
			goto release_lock;
		}

		data[i] = data[i] * 100 / 255;
		dev_dbg(dev, "%s: reg: %x, data: %d%%\n", __func__,
			F75308_REG_FAN_SEG(index) + i, data[i]);
	}

	mutex_unlock(&priv->locker);

	status = sprintf(buf, "%d%% %d%% %d%% %d%% %d%%\n", data[0], data[1],
			 data[2], data[3], data[4]);
	return status;

release_lock:
	mutex_unlock(&priv->locker);
	return status;
}

static int __f75308_fan_5_seg_store(struct device *dev, int index,
				    u8 data[F75308_MAX_FAN_SEG_CNT])
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int status, i;

	for (i = 0; i < F75308_MAX_FAN_SEG_CNT; ++i)
		if (data[i] > 100)
			return -EINVAL;

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 5);
	if (status)
		goto release_lock;

	for (i = 0; i < F75308_MAX_FAN_SEG_CNT; ++i) {
		data[i] = 255 * data[i] / 100;

		status = f75308_write8(client, F75308_REG_FAN_SEG(index) + i,
				       data[i]);
		if (status)
			goto release_lock;

		dev_dbg(dev, "%s: reg: %x, data: %d\n", __func__,
			F75308_REG_FAN_SEG(index) + i, data[i]);
	}

release_lock:
	mutex_unlock(&priv->locker);
	return status;
}

static ssize_t f75308_fan_5_seg_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(attr)->index;
	u8 data[F75308_MAX_FAN_SEG_CNT];
	char *p;
	int status, i;

	for (i = 0; i < F75308_MAX_FAN_SEG_CNT; ++i) {
		p = strsep((char **)&buf, " ");
		if (!p)
			return -EINVAL;

		status = kstrtou8(p, 0, &data[i]);
		if (status)
			return status;
	}

	status = __f75308_fan_5_seg_store(dev, index, data);
	return status ? status : count;
}

static ssize_t f75308_fan_map_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int index = to_sensor_dev_attr(attr)->index;
	int status, data;

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 4);
	if (status)
		goto release_lock;

	data = f75308_read8(client, F75308_REG_FAN_MAP(index));
	if (data < 0) {
		status = data;
		goto release_lock;
	}

	mutex_unlock(&priv->locker);
	dev_dbg(dev, "%s: idx: %d, data: %x\n", __func__, index, data);
	status = sprintf(buf, "%d\n", data);
	return status;

release_lock:
	mutex_unlock(&priv->locker);
	return status;
}

static ssize_t f75308_fan_map_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct f75308_priv *priv = dev_get_drvdata(dev);
	struct i2c_client *client = priv->client;
	int index = to_sensor_dev_attr(attr)->index;
	int status, data;

	status = kstrtoint(buf, 0, &data);
	if (status)
		return status;

	mutex_lock(&priv->locker);

	status = f75308_write8(client, F75308_REG_BANK, 4);
	if (status)
		goto release_lock;

	status = f75308_write8(client, F75308_REG_FAN_MAP(index), data);
	if (status)
		goto release_lock;

	dev_dbg(dev, "%s: idx: %d, data: %x\n", __func__, index, data);

release_lock:
	mutex_unlock(&priv->locker);
	return status ? status : count;
}

static SENSOR_DEVICE_ATTR_RO(in0_input, f75308_in_input, 0);
static SENSOR_DEVICE_ATTR_RO(in1_input, f75308_in_input, 1);
static SENSOR_DEVICE_ATTR_RO(in2_input, f75308_in_input, 2);
static SENSOR_DEVICE_ATTR_RO(in3_input, f75308_in_input, 3);
static SENSOR_DEVICE_ATTR_RO(in4_input, f75308_in_input, 4);
static SENSOR_DEVICE_ATTR_RO(in5_input, f75308_in_input, 5);
static SENSOR_DEVICE_ATTR_RO(in6_input, f75308_in_input, 6);
static SENSOR_DEVICE_ATTR_RO(in7_input, f75308_in_input, 7);
static SENSOR_DEVICE_ATTR_RO(in8_input, f75308_in_input, 8);
static SENSOR_DEVICE_ATTR_RO(in9_input, f75308_in_input, 9);
static SENSOR_DEVICE_ATTR_RO(in10_input, f75308_in_input, 10);
static SENSOR_DEVICE_ATTR_RO(in11_input, f75308_in_input, 11);
static SENSOR_DEVICE_ATTR_RO(in12_input, f75308_in_input, 12);
static SENSOR_DEVICE_ATTR_RO(in13_input, f75308_in_input, 13);
static SENSOR_DEVICE_ATTR_RO(in14_input, f75308_in_input, 14);

static SENSOR_DEVICE_ATTR_RO(temp0_input, f75308_temp_input, 0);
static SENSOR_DEVICE_ATTR_RO(temp1_input, f75308_temp_input, 1);
static SENSOR_DEVICE_ATTR_RO(temp2_input, f75308_temp_input, 2);
static SENSOR_DEVICE_ATTR_RO(temp3_input, f75308_temp_input, 3);
static SENSOR_DEVICE_ATTR_RO(temp4_input, f75308_temp_input, 4);
static SENSOR_DEVICE_ATTR_RO(temp5_input, f75308_temp_input, 5);
static SENSOR_DEVICE_ATTR_RO(temp6_input, f75308_temp_input, 6);

static SENSOR_DEVICE_ATTR_RO(fan1_input, f75308_fan_input, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, f75308_fan_input, 1);
static SENSOR_DEVICE_ATTR_RO(fan3_input, f75308_fan_input, 2);
static SENSOR_DEVICE_ATTR_RO(fan4_input, f75308_fan_input, 3);
static SENSOR_DEVICE_ATTR_RO(fan5_input, f75308_fan_input, 4);
static SENSOR_DEVICE_ATTR_RO(fan6_input, f75308_fan_input, 5);
static SENSOR_DEVICE_ATTR_RO(fan7_input, f75308_fan_input, 6);
static SENSOR_DEVICE_ATTR_RO(fan8_input, f75308_fan_input, 7);
static SENSOR_DEVICE_ATTR_RO(fan9_input, f75308_fan_input, 8);
static SENSOR_DEVICE_ATTR_RO(fan10_input, f75308_fan_input, 9);
static SENSOR_DEVICE_ATTR_RO(fan11_input, f75308_fan_input, 10);
static SENSOR_DEVICE_ATTR_RO(fan12_input, f75308_fan_input, 11);
static SENSOR_DEVICE_ATTR_RO(fan13_input, f75308_fan_input, 12);
static SENSOR_DEVICE_ATTR_RO(fan14_input, f75308_fan_input, 13);

static SENSOR_DEVICE_ATTR_RW(pwm1, f75308_pwm, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2, f75308_pwm, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3, f75308_pwm, 2);
static SENSOR_DEVICE_ATTR_RW(pwm4, f75308_pwm, 3);
static SENSOR_DEVICE_ATTR_RW(pwm5, f75308_pwm, 4);
static SENSOR_DEVICE_ATTR_RW(pwm6, f75308_pwm, 5);
static SENSOR_DEVICE_ATTR_RW(pwm7, f75308_pwm, 6);
static SENSOR_DEVICE_ATTR_RW(pwm8, f75308_pwm, 7);
static SENSOR_DEVICE_ATTR_RW(pwm9, f75308_pwm, 8);
static SENSOR_DEVICE_ATTR_RW(pwm10, f75308_pwm, 9);
static SENSOR_DEVICE_ATTR_RW(pwm11, f75308_pwm, 10);

static SENSOR_DEVICE_ATTR_RW(fan1_type, f75308_fan_type, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_type, f75308_fan_type, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_type, f75308_fan_type, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_type, f75308_fan_type, 3);
static SENSOR_DEVICE_ATTR_RW(fan5_type, f75308_fan_type, 4);
static SENSOR_DEVICE_ATTR_RW(fan6_type, f75308_fan_type, 5);
static SENSOR_DEVICE_ATTR_RW(fan7_type, f75308_fan_type, 6);
static SENSOR_DEVICE_ATTR_RW(fan8_type, f75308_fan_type, 7);
static SENSOR_DEVICE_ATTR_RW(fan9_type, f75308_fan_type, 8);
static SENSOR_DEVICE_ATTR_RW(fan10_type, f75308_fan_type, 9);
static SENSOR_DEVICE_ATTR_RW(fan11_type, f75308_fan_type, 10);

static SENSOR_DEVICE_ATTR_RW(fan1_mode, f75308_fan_mode, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_mode, f75308_fan_mode, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_mode, f75308_fan_mode, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_mode, f75308_fan_mode, 3);
static SENSOR_DEVICE_ATTR_RW(fan5_mode, f75308_fan_mode, 4);
static SENSOR_DEVICE_ATTR_RW(fan6_mode, f75308_fan_mode, 5);
static SENSOR_DEVICE_ATTR_RW(fan7_mode, f75308_fan_mode, 6);
static SENSOR_DEVICE_ATTR_RW(fan8_mode, f75308_fan_mode, 7);
static SENSOR_DEVICE_ATTR_RW(fan9_mode, f75308_fan_mode, 8);
static SENSOR_DEVICE_ATTR_RW(fan10_mode, f75308_fan_mode, 9);
static SENSOR_DEVICE_ATTR_RW(fan11_mode, f75308_fan_mode, 10);

static SENSOR_DEVICE_ATTR_RW(fan1_5_seg, f75308_fan_5_seg, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_5_seg, f75308_fan_5_seg, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_5_seg, f75308_fan_5_seg, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_5_seg, f75308_fan_5_seg, 3);
static SENSOR_DEVICE_ATTR_RW(fan5_5_seg, f75308_fan_5_seg, 4);
static SENSOR_DEVICE_ATTR_RW(fan6_5_seg, f75308_fan_5_seg, 5);
static SENSOR_DEVICE_ATTR_RW(fan7_5_seg, f75308_fan_5_seg, 6);
static SENSOR_DEVICE_ATTR_RW(fan8_5_seg, f75308_fan_5_seg, 7);
static SENSOR_DEVICE_ATTR_RW(fan9_5_seg, f75308_fan_5_seg, 8);
static SENSOR_DEVICE_ATTR_RW(fan10_5_seg, f75308_fan_5_seg, 9);
static SENSOR_DEVICE_ATTR_RW(fan11_5_seg, f75308_fan_5_seg, 10);

static SENSOR_DEVICE_ATTR_RW(fan1_map, f75308_fan_map, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_map, f75308_fan_map, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_map, f75308_fan_map, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_map, f75308_fan_map, 3);
static SENSOR_DEVICE_ATTR_RW(fan5_map, f75308_fan_map, 4);
static SENSOR_DEVICE_ATTR_RW(fan6_map, f75308_fan_map, 5);
static SENSOR_DEVICE_ATTR_RW(fan7_map, f75308_fan_map, 6);
static SENSOR_DEVICE_ATTR_RW(fan8_map, f75308_fan_map, 7);
static SENSOR_DEVICE_ATTR_RW(fan9_map, f75308_fan_map, 8);
static SENSOR_DEVICE_ATTR_RW(fan10_map, f75308_fan_map, 9);
static SENSOR_DEVICE_ATTR_RW(fan11_map, f75308_fan_map, 10);

static struct attribute *f75308a_28_attributes[] = {
	&sensor_dev_attr_temp0_input.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,

	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,

	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,

	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm4.dev_attr.attr,

	&sensor_dev_attr_fan1_type.dev_attr.attr,
	&sensor_dev_attr_fan2_type.dev_attr.attr,
	&sensor_dev_attr_fan3_type.dev_attr.attr,
	&sensor_dev_attr_fan4_type.dev_attr.attr,

	&sensor_dev_attr_fan1_mode.dev_attr.attr,
	&sensor_dev_attr_fan2_mode.dev_attr.attr,
	&sensor_dev_attr_fan3_mode.dev_attr.attr,
	&sensor_dev_attr_fan4_mode.dev_attr.attr,

	&sensor_dev_attr_fan1_map.dev_attr.attr,
	&sensor_dev_attr_fan2_map.dev_attr.attr,
	&sensor_dev_attr_fan3_map.dev_attr.attr,
	&sensor_dev_attr_fan4_map.dev_attr.attr,

	&sensor_dev_attr_fan1_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan2_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan3_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan4_5_seg.dev_attr.attr,

	NULL
};

static struct attribute *f75308b_48_attributes[] = {
	&sensor_dev_attr_temp0_input.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,

	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan5_input.dev_attr.attr,
	&sensor_dev_attr_fan6_input.dev_attr.attr,
	&sensor_dev_attr_fan7_input.dev_attr.attr,
	&sensor_dev_attr_fan8_input.dev_attr.attr,
	&sensor_dev_attr_fan9_input.dev_attr.attr,

	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in10_input.dev_attr.attr,

	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm4.dev_attr.attr,
	&sensor_dev_attr_pwm5.dev_attr.attr,
	&sensor_dev_attr_pwm6.dev_attr.attr,
	&sensor_dev_attr_pwm7.dev_attr.attr,

	&sensor_dev_attr_fan1_type.dev_attr.attr,
	&sensor_dev_attr_fan2_type.dev_attr.attr,
	&sensor_dev_attr_fan3_type.dev_attr.attr,
	&sensor_dev_attr_fan4_type.dev_attr.attr,
	&sensor_dev_attr_fan5_type.dev_attr.attr,
	&sensor_dev_attr_fan6_type.dev_attr.attr,
	&sensor_dev_attr_fan7_type.dev_attr.attr,

	&sensor_dev_attr_fan1_mode.dev_attr.attr,
	&sensor_dev_attr_fan2_mode.dev_attr.attr,
	&sensor_dev_attr_fan3_mode.dev_attr.attr,
	&sensor_dev_attr_fan4_mode.dev_attr.attr,
	&sensor_dev_attr_fan5_mode.dev_attr.attr,
	&sensor_dev_attr_fan6_mode.dev_attr.attr,
	&sensor_dev_attr_fan7_mode.dev_attr.attr,

	&sensor_dev_attr_fan1_map.dev_attr.attr,
	&sensor_dev_attr_fan2_map.dev_attr.attr,
	&sensor_dev_attr_fan3_map.dev_attr.attr,
	&sensor_dev_attr_fan4_map.dev_attr.attr,
	&sensor_dev_attr_fan5_map.dev_attr.attr,
	&sensor_dev_attr_fan6_map.dev_attr.attr,
	&sensor_dev_attr_fan7_map.dev_attr.attr,

	&sensor_dev_attr_fan1_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan2_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan3_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan4_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan5_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan6_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan7_5_seg.dev_attr.attr,

	NULL
};

static struct attribute *f75308c_64_attributes[] = {
	&sensor_dev_attr_temp0_input.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,
	&sensor_dev_attr_temp6_input.dev_attr.attr,

	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan5_input.dev_attr.attr,
	&sensor_dev_attr_fan6_input.dev_attr.attr,
	&sensor_dev_attr_fan7_input.dev_attr.attr,
	&sensor_dev_attr_fan8_input.dev_attr.attr,
	&sensor_dev_attr_fan9_input.dev_attr.attr,
	&sensor_dev_attr_fan10_input.dev_attr.attr,
	&sensor_dev_attr_fan11_input.dev_attr.attr,
	&sensor_dev_attr_fan12_input.dev_attr.attr,
	&sensor_dev_attr_fan13_input.dev_attr.attr,
	&sensor_dev_attr_fan14_input.dev_attr.attr,

	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in10_input.dev_attr.attr,
	&sensor_dev_attr_in11_input.dev_attr.attr,
	&sensor_dev_attr_in12_input.dev_attr.attr,
	&sensor_dev_attr_in13_input.dev_attr.attr,
	&sensor_dev_attr_in14_input.dev_attr.attr,

	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm4.dev_attr.attr,
	&sensor_dev_attr_pwm5.dev_attr.attr,
	&sensor_dev_attr_pwm6.dev_attr.attr,
	&sensor_dev_attr_pwm7.dev_attr.attr,
	&sensor_dev_attr_pwm8.dev_attr.attr,
	&sensor_dev_attr_pwm9.dev_attr.attr,
	&sensor_dev_attr_pwm10.dev_attr.attr,
	&sensor_dev_attr_pwm11.dev_attr.attr,

	&sensor_dev_attr_fan1_type.dev_attr.attr,
	&sensor_dev_attr_fan2_type.dev_attr.attr,
	&sensor_dev_attr_fan3_type.dev_attr.attr,
	&sensor_dev_attr_fan4_type.dev_attr.attr,
	&sensor_dev_attr_fan5_type.dev_attr.attr,
	&sensor_dev_attr_fan6_type.dev_attr.attr,
	&sensor_dev_attr_fan7_type.dev_attr.attr,
	&sensor_dev_attr_fan8_type.dev_attr.attr,
	&sensor_dev_attr_fan9_type.dev_attr.attr,
	&sensor_dev_attr_fan10_type.dev_attr.attr,
	&sensor_dev_attr_fan11_type.dev_attr.attr,

	&sensor_dev_attr_fan1_mode.dev_attr.attr,
	&sensor_dev_attr_fan2_mode.dev_attr.attr,
	&sensor_dev_attr_fan3_mode.dev_attr.attr,
	&sensor_dev_attr_fan4_mode.dev_attr.attr,
	&sensor_dev_attr_fan5_mode.dev_attr.attr,
	&sensor_dev_attr_fan6_mode.dev_attr.attr,
	&sensor_dev_attr_fan7_mode.dev_attr.attr,
	&sensor_dev_attr_fan8_mode.dev_attr.attr,
	&sensor_dev_attr_fan9_mode.dev_attr.attr,
	&sensor_dev_attr_fan10_mode.dev_attr.attr,
	&sensor_dev_attr_fan11_mode.dev_attr.attr,

	&sensor_dev_attr_fan1_map.dev_attr.attr,
	&sensor_dev_attr_fan2_map.dev_attr.attr,
	&sensor_dev_attr_fan3_map.dev_attr.attr,
	&sensor_dev_attr_fan4_map.dev_attr.attr,
	&sensor_dev_attr_fan5_map.dev_attr.attr,
	&sensor_dev_attr_fan6_map.dev_attr.attr,
	&sensor_dev_attr_fan7_map.dev_attr.attr,
	&sensor_dev_attr_fan8_map.dev_attr.attr,
	&sensor_dev_attr_fan9_map.dev_attr.attr,
	&sensor_dev_attr_fan10_map.dev_attr.attr,
	&sensor_dev_attr_fan11_map.dev_attr.attr,

	&sensor_dev_attr_fan1_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan2_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan3_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan4_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan5_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan6_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan7_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan8_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan9_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan10_5_seg.dev_attr.attr,
	&sensor_dev_attr_fan11_5_seg.dev_attr.attr,

	NULL
};

static const struct attribute_group f75308a_28_group = {
	.attrs = f75308a_28_attributes,
};

static const struct attribute_group f75308b_48_group = {
	.attrs = f75308b_48_attributes,
};

static const struct attribute_group f75308c_64_group = {
	.attrs = f75308c_64_attributes,
};

static const struct attribute_group *f75308a_28_groups[] = {
	&f75308a_28_group,
	NULL,
};

static const struct attribute_group *f75308b_48_groups[] = {
	&f75308b_48_group,
	NULL,
};

static const struct attribute_group *f75308c_64_groups[] = {
	&f75308c_64_group,
	NULL,
};

static const struct attribute_group **f75308_groups[] = {
	f75308a_28_groups,
	f75308b_48_groups,
	f75308c_64_groups,
};

static int f75308_get_devid(struct i2c_client *client, enum chip *chipid)
{
	u16 vendid, pid;
	int status;

	status = f75308_write8(client, F75308_REG_BANK, 0);
	if (status)
		return status;

	vendid = f75308_read16(client, DEVICE_VID_ADDR);
	pid = f75308_read16(client, DEVICE_PID_ADDR);
	if (vendid != DEVICE_VID)
		return -ENODEV;

	if (pid == DEVICE_PID_64PIN)
		*chipid = f75308c_64;
	else if (pid == DEVICE_PID_48PIN)
		*chipid = f75308b_48;
	else if (pid == DEVICE_PID_28PIN)
		*chipid = f75308a_28;
	else
		return -ENODEV;

	return 0;
}

static int f75308_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	enum chip chipid;
	const char *name;
	int status;

	status = f75308_get_devid(client, &chipid);
	if (status)
		return status;

	if (chipid == f75308a_28)
		name = "F75308AR";
	else if (chipid == f75308b_48)
		name = "F75308BD";
	else if (chipid == f75308c_64)
		name = "F75308CU";
	else
		return -ENODEV;

	strscpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static int f75308_init(struct i2c_client *client, enum chip chip_id)
{
	int status, data;

	/*
	 * The default temperature control source of FAN4 is T4. However, T4
	 * is only present in F75308BD and F75308CU, not in F75308AR. Hence,
	 * remap the temperature control source of FAN4 of F75308AR to T0 if
	 * T4 is selected.
	 */
	if (chip_id == f75308a_28) {
		status = f75308_write8(client, F75308_REG_BANK, 4);
		if (status)
			return status;

		data = f75308_read8(client, F75308_REG_FAN_MAP(3));
		if (data < 0)
			return data;

		if (data == 0x04) {
			status =
				f75308_write8(client, F75308_REG_FAN_MAP(3), 0);
			if (status)
				return status;
		}
	}

	return 0;
}

static int f75308_probe_child_from_dt(struct device *dev,
				      struct device_node *child,
				      struct i2c_client *client)
{
	int status;
	u32 reg_idx, seg_idx = 0, seg_val;
	u8 seg5[F75308_MAX_FAN_SEG_CNT];
	const char *val_str;
#if !defined(NV_OF_PROPERTY_FOR_EACH_REMOVED_INTERNAL_ARGS) /* Linux v6.11 */
	struct property *prop;
	const __be32 *p;
#endif

	status = of_property_read_u32(child, "reg", &reg_idx);
	if (status) {
		dev_err(dev, "missing reg property of %pOFn\n", child);
		return status;
	} else if (reg_idx >= F75308_MAX_FAN_CTRL_CNT) {
		dev_err(dev, "invalid reg %u of %pOFn\n", reg_idx, child);
		return -EINVAL;
	}

	status = of_property_read_string(child, "type", &val_str);
	if (status) {
		dev_err(dev, "invalid type property of %pOFn\n", child);
		return status;
	}

	status = __f75308_fan_type_store(dev, reg_idx, val_str);
	if (status)
		return status;

	status = of_property_read_string(child, "duty", &val_str);
	if (status) {
		dev_err(dev, "invalid duty property of %pOFn\n", child);
		return status;
	}

	status = __f75308_fan_mode_store(dev, reg_idx, val_str);
	if (status)
		return status;

#if defined(NV_OF_PROPERTY_FOR_EACH_REMOVED_INTERNAL_ARGS) /* Linux v6.11 */
	of_property_for_each_u32(child, "5seg", seg_val) {
#else
	of_property_for_each_u32(child, "5seg", prop, p, seg_val) {
#endif
		dev_dbg(dev, "%s: seg5[%u]: %u\n", __func__, seg_idx, seg_val);
		if (seg_idx < F75308_MAX_FAN_SEG_CNT)
			seg5[seg_idx++] = seg_val;
		else
			break;
	}

	if (seg_idx != F75308_MAX_FAN_SEG_CNT) {
		dev_err(dev, "invalid 5seg property of %pOFn\n", child);
		return -EINVAL;
	}

	status = __f75308_fan_5_seg_store(dev, reg_idx, seg5);
	if (status)
		return status;

	return status;
}

static int f75308_probe_from_dt(struct device *dev, struct i2c_client *client)
{
	const struct device_node *np = dev->of_node;
	struct device_node *child;
	int status;

	/* Compatible with non-DT platforms */
	if (!np)
		return 0;

	for_each_child_of_node(np, child) {
		status = f75308_probe_child_from_dt(dev, child, client);
		if (status) {
			of_node_put(child);
			return status;
		}
	}

	return 0;
}

static int f75308_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct f75308_priv *priv;
	int status;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->locker);
	priv->client = client;
	dev_set_drvdata(dev, priv);

	status = f75308_get_devid(client, &priv->chip_id);
	if (status) {
		dev_err(dev, "Unable to get f75308 id: %d\n", status);
		goto destroy_lock;
	}

	status = f75308_init(client, priv->chip_id);
	if (status) {
		dev_err(dev, "Unable to initialize f75308: %d\n", status);
		goto destroy_lock;
	}

	status = f75308_probe_from_dt(dev, priv->client);
	if (status) {
		dev_err(dev, "Unable to parse all dt entries: %d\n", status);
		goto destroy_lock;
	}

	priv->hwmon_dev = devm_hwmon_device_register_with_groups(
		dev, DEVICE_NAME, priv, f75308_groups[priv->chip_id]);
	if (IS_ERR(priv->hwmon_dev)) {
		status = PTR_ERR(priv->hwmon_dev);
		if (status)
			goto destroy_lock;
	}

	return 0;

destroy_lock:
	mutex_destroy(&priv->locker);
	return status;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int f75308_remove(struct i2c_client *client)
#else
static void f75308_remove(struct i2c_client *client)
#endif
{
	struct f75308_priv *priv = dev_get_drvdata(&client->dev);

	mutex_destroy(&priv->locker);
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

static const unsigned short f75308_addr[] = {
	0x2C, 0x2D, 0x2E, 0x2F, 0x4C, 0x4D, 0x4E, 0x4F, I2C_CLIENT_END,
};

static const struct i2c_device_id f75308_id[] = { { "F75308CU", f75308c_64 },
						  { "F75308BD", f75308b_48 },
						  { "F75308AR", f75308a_28 },
						  {} };
MODULE_DEVICE_TABLE(i2c, f75308_id);

#ifdef CONFIG_OF
static const struct of_device_id f75308_match_table[] = {
	{ .compatible = "fintek,f75308" },
	{},
};
MODULE_DEVICE_TABLE(of, f75308_match_table);
#else
#define f75308_match_table NULL
#endif

static struct i2c_driver f75308_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = DEVICE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(f75308_match_table),
	},
	.detect = f75308_detect,
#if defined(NV_I2C_DRIVER_STRUCT_HAS_PROBE_NEW) /* Dropped on Linux 6.6 */
	.probe_new = f75308_probe,
#else
	.probe     = f75308_probe,
#endif
	.remove = f75308_remove,
	.address_list = f75308_addr,
	.id_table = f75308_id,
};

module_i2c_driver(f75308_driver);

MODULE_AUTHOR("Ji-Ze Hong (Peter Hong) <hpeter+linux_kernel@gmail.com>");
MODULE_AUTHOR("Yi-Wei Wang <yiweiw@nvidia.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("F75308 hardware monitoring driver");
