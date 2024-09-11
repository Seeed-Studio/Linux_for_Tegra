// SPDX-License-Identifier: GPL-2.0-only
/*
 * MAXIM HDMI Serializer driver for MAXIM GMSL Serializers
 *
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/device.h>
#include <linux/fwnode.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/version.h>

#define MAX_GMSL_HDMI_SER_INTR2                 0x1A
#define MAX_GMSL_HDMI_SER_INTR3                 0x1B

#define MAX_GMSL_HDMI_SER_REG_13                0xD

#define MAX_GMSL_HDMI_SER_RESET                 0x10
#define MAX_GMSL_HDMI_SER_RCLKOUT               0x03
#define MAX_GMSL_HDMI_SER_HDMI                  0x01
#define MAX_GMSL_HDMI_SER_HPD                   0xF5

#define MAX_GMSL_HDMI_SERIALIZER_MAX96751F      0x83
#define MAX_GMSL_HDMI_SERIALIZER_MAX96753F      0x85

#define MAX_GMSL_HDMI_TRANSMIT_CRC              0x50

#define ENABLE_REMOTE_SIDE_ERROR_STATUS         0x2B
#define REMOTE_ERROR_STATUS                     (1 << 5)

#define RESET_SERIALIZER                        0x80
#define ENABLE_RCLKOUT                          0x30
#define ENABLE_AUTO_HDMI                        0x88
#define TOGGLE_HPD                              0x01

struct maxim_gmsl_hdmi_ser_priv {
	struct i2c_client *client;
	struct gpio_desc *gpiod_pwrdn;
	struct mutex mutex;
	struct regmap *regmap;
	int ser_errb;
	int ser_pwrdn;
	int ser_irq;
	bool enable_rclkout;
};

static const struct regmap_config maxim_gmsl_hdmi_ser_i2c_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
};

static int maxim_gmsl_hdmi_ser_read(struct maxim_gmsl_hdmi_ser_priv *priv, int reg)
{
	int ret, val = 0;

	ret = regmap_read(priv->regmap, reg, &val);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"%s: register 0x%02x read failed (%d)\n",
			__func__, reg, ret);
	return val;
}

static int maxim_gmsl_hdmi_ser_write(struct maxim_gmsl_hdmi_ser_priv *priv, u32 reg, u8 val)
{
	int ret;

	ret = regmap_write(priv->regmap, reg, val);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"%s: register 0x%02x write failed (%d)\n",
			__func__, reg, ret);
	return ret;
}


static void maxim_gmsl_hdmi_detect_remote_error(struct maxim_gmsl_hdmi_ser_priv *priv,
								struct device *dev)
{
	int ret = 0;

	ret = maxim_gmsl_hdmi_ser_read(priv, MAX_GMSL_HDMI_SER_INTR3);

	if ((ret & REMOTE_ERROR_STATUS) != 0)
		dev_err(dev, "%s: Remote side error detected..\n", __func__);

}

static irqreturn_t maxim_gmsl_hdmi_ser_irq_handler(int irq, void *dev_id)
{
	struct maxim_gmsl_hdmi_ser_priv *priv;
	struct device *dev;

	priv = dev_id;
	dev = &priv->client->dev;

	dev_err(dev, "%s: Interrupt came to MAX96751 Serializer, IRQ: %d\n", __func__, irq);

	/* Detect remote error across GMSL link */
	maxim_gmsl_hdmi_detect_remote_error(priv, dev);

	/* returning just irq handled */
	return IRQ_HANDLED;
}

static int maxim_gmsl_hdmi_ser_init(struct device *dev)
{
	struct maxim_gmsl_hdmi_ser_priv *priv;
	struct i2c_client *client;
	int ret = 0, val = 0;

	dev_info(dev, "%s: serializer initialization function:\n", __func__);

	client = to_i2c_client(dev);
	priv = i2c_get_clientdata(client);

	ret = devm_gpio_request_one(&client->dev, priv->ser_pwrdn,
				    GPIOF_OUT_INIT_LOW, "GPIO_MAXIM_SER_PWRDN");
	if (ret < 0) {
		dev_err(dev, "%s: GPIO request failed ret: %d\n", __func__, ret);
		return ret;
	}

	/* powering on the serializer */
	gpio_set_value(priv->ser_pwrdn, 1);

	/* Wait ~1.2ms for powerup to complete */
	usleep_range(1200, 1300);

	/* read the register to detect the serializer on board */
	val = maxim_gmsl_hdmi_ser_read(priv, MAX_GMSL_HDMI_SER_REG_13);

	if (val == MAX_GMSL_HDMI_SERIALIZER_MAX96751F) {
		dev_info(dev, "%s: MAXIM Serializer MAX96751F detected\n", __func__);
	} else if (val == MAX_GMSL_HDMI_SERIALIZER_MAX96753F) {
		dev_info(dev, "%s: MAXIM Serializer MAX96753F detected\n", __func__);
	} else {
		dev_err(dev, "%s: MAXIM Serializer Not detected\n", __func__);
		return -ENODEV;
	}

	/* Reset the serializer */
	ret = maxim_gmsl_hdmi_ser_write(priv, MAX_GMSL_HDMI_SER_RESET, RESET_SERIALIZER);

	if (ret != 0) {
		dev_err(dev, "%s: error resetting the serializer\n", __func__);
		return ret;
	}

	/* Wait ~1.2ms for powerup to complete */
	usleep_range(1200, 1300);

	if (priv->enable_rclkout) {
		/*If DT has this property, Enable display serializer RCLKOUT*/
		dev_info(dev, "%s: RCLKOUT Enable from DT\n", __func__);
		ret = maxim_gmsl_hdmi_ser_write(priv, MAX_GMSL_HDMI_SER_RCLKOUT, ENABLE_RCLKOUT);
		if (ret != 0) {
			dev_err(dev, "%s: error enabling rclkout\n", __func__);
			return ret;
		}
	}

	/* Enable HDMI Auto Start */
	ret = maxim_gmsl_hdmi_ser_write(priv, MAX_GMSL_HDMI_SER_HDMI, ENABLE_AUTO_HDMI);
	if (ret != 0) {
		dev_err(dev, "%s: error enabling HDMI auto start\n", __func__);
		return ret;
	}

	/* Toggle HPD */
	ret = maxim_gmsl_hdmi_ser_write(priv, MAX_GMSL_HDMI_SER_HPD, TOGGLE_HPD);
	if (ret != 0) {
		dev_err(dev, "%s: error in powering on HPD\n", __func__);
		return ret;
	}

	dev_info(dev, "%s: driver initialization done successfully\n", __func__);

	return ret;
}

static int maxim_gmsl_hdmi_ser_parse_dt(struct maxim_gmsl_hdmi_ser_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *ser = dev->of_node;
	int err = 0;

	dev_info(dev, "%s: parsing serializer device tree:\n", __func__);

	priv->ser_pwrdn = of_get_named_gpio(ser, "maxim_gmsl_hdmi_ser-pwrdn", 0);

	if (priv->ser_pwrdn < 0) {
		err = priv->ser_pwrdn;
		dev_err(dev, "%s: ser_pwrdn is not present in the DT: %d\n",
			__func__, err);
		return err;
	}

	priv->enable_rclkout = of_property_read_bool(ser, "enable_rclkout");
	priv->ser_errb = of_get_named_gpio(ser, "ser-errb", 0);

	if (priv->ser_errb < 0) {
		err = priv->ser_errb;
		dev_err(dev, "%s: ser_errb is not present in the DT: %d\n",
			__func__, err);
		return err;
	}

	return 0;
}

static int maxim_gmsl_hdmi_ser_probe(struct i2c_client *client)
{
	struct maxim_gmsl_hdmi_ser_priv *priv;
	struct device *dev;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	mutex_init(&priv->mutex);

	priv->client = client;
	i2c_set_clientdata(client, priv);

	priv->regmap = devm_regmap_init_i2c(client, &maxim_gmsl_hdmi_ser_i2c_regmap);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	dev = &priv->client->dev;

	/* parse device-tree */
	ret = maxim_gmsl_hdmi_ser_parse_dt(priv);
	if (ret < 0) {
		dev_err(dev, "%s: error parsing device tree\n", __func__);
		return -EFAULT;
	}

	/* driver init function */
	ret = maxim_gmsl_hdmi_ser_init(&client->dev);
	if (ret < 0) {
		dev_err(dev, "%s: hdmi serializer init failed\n", __func__);
		gpio_set_value(priv->ser_pwrdn, 0);
		return -EFAULT;
	}

	ret = devm_gpio_request_one(&client->dev, priv->ser_errb, GPIOF_IN,
				    "GPIO_MAXIM_SER");
	if (ret < 0) {
		dev_err(dev, "%s: GPIO request failed\n ret: %d",
			__func__, ret);

		gpio_set_value(priv->ser_pwrdn, 0);
		return ret;
	}

	dev_info(dev, "%s: mapping gpio to irq and interrupt handler:\n", __func__);

	/* maps gpio to irq and interrupt handler */
	if (gpio_is_valid(priv->ser_errb)) {
		priv->ser_irq = gpio_to_irq(priv->ser_errb);
		if (priv->ser_irq < 0) {
			ret = priv->ser_irq;
			dev_err(dev, "%s: GPIO not configure for interrupts: %d\n",
				__func__, ret);
			return ret;
		}
		ret = request_threaded_irq(priv->ser_irq, NULL,
					maxim_gmsl_hdmi_ser_irq_handler,
					IRQF_TRIGGER_FALLING
					| IRQF_ONESHOT, "SER", priv);
		if (ret < 0) {
			dev_err(dev, "%s: Unable to register IRQ handler ret: %d\n",
				__func__, ret);
			return ret;
		}

	}

	/* Enable Remote Side Error */
	ret = maxim_gmsl_hdmi_ser_write(priv, MAX_GMSL_HDMI_SER_INTR2,
						ENABLE_REMOTE_SIDE_ERROR_STATUS);
	if (ret != 0) {
		dev_err(dev, "%s: Enabling Remote error reporting failed : %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int maxim_gmsl_hdmi_ser_remove(struct i2c_client *client)
#else
static void maxim_gmsl_hdmi_ser_remove(struct i2c_client *client)
#endif
{
	struct maxim_gmsl_hdmi_ser_priv *priv = i2c_get_clientdata(client);

	i2c_unregister_device(client);
	gpio_set_value(priv->ser_pwrdn, 0);

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

#ifdef CONFIG_PM
static int maxim_gmsl_hdmi_ser_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct maxim_gmsl_hdmi_ser_priv *priv = i2c_get_clientdata(client);

	dev_err(dev, "%s: gmsl hdmi serializer suspended\n", __func__);

	/* Drive PWRDNB pin low to power down the serializer */
	gpio_set_value(priv->ser_pwrdn, 0);
	gpio_free(priv->ser_pwrdn);
	return 0;
}

static int maxim_gmsl_hdmi_ser_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct maxim_gmsl_hdmi_ser_priv *priv = i2c_get_clientdata(client);
	int ret = 0;

	dev_err(dev, "%s: gmsl hdmi serializer resumed\n", __func__);

	/* Drive PWRDNB pin high to power up the serializer and initialize all registers */
	ret = maxim_gmsl_hdmi_ser_init(&client->dev);
	if (ret < 0)
		dev_err(&priv->client->dev, "%s: hdmi serializer init failed\n", __func__);

	return ret;
}

const struct dev_pm_ops maxim_gmsl_hdmi_ser_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(
		maxim_gmsl_hdmi_ser_suspend, maxim_gmsl_hdmi_ser_resume)
};

#endif

static const struct of_device_id maxim_gmsl_hdmi_ser_dt_ids[] = {
	{ .compatible = "maxim,maxim_gmsl_hdmi_ser" },
	{},
};

MODULE_DEVICE_TABLE(of, maxim_gmsl_hdmi_ser_dt_ids);

static struct i2c_driver maxim_gmsl_hdmi_ser_i2c_driver = {
	.driver	= {
		.name		= "maxim_gmsl_hdmi_ser",
		.of_match_table	= of_match_ptr(maxim_gmsl_hdmi_ser_dt_ids),
#ifdef CONFIG_PM
		.pm	= &maxim_gmsl_hdmi_ser_pm_ops,
#endif
	},
#if defined(NV_I2C_DRIVER_STRUCT_HAS_PROBE_NEW) /* Dropped on Linux 6.6 */
	.probe_new	= maxim_gmsl_hdmi_ser_probe,
#else
	.probe		= maxim_gmsl_hdmi_ser_probe,
#endif
	.remove		= maxim_gmsl_hdmi_ser_remove,
};

module_i2c_driver(maxim_gmsl_hdmi_ser_i2c_driver);

MODULE_DESCRIPTION("Maxim MAX96751 Serializer Driver");
MODULE_AUTHOR("Sagar Tyagi");
MODULE_LICENSE("GPL");
