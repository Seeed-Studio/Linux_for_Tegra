// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Junction temperature thermal driver for Maxim Max77851.
 */

#include <nvidia/conftest.h>

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mfd/max77851.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#include <linux/of_device.h>

#define MAX77851_TJALARM1_TH BIT(0) // 85 Degree
#define MAX77851_TJALARM2_TH BIT(0) // 115 Degree
#define MAX77851_TJSHUTDOWN_TH BIT(0) // 140 Degree

#define MAX77851_NORMAL_OPERATING_TEMP	100000
#define MAX77851_TJALARM1_TEMP		105000
#define MAX77851_TJALARM2_TEMP		135000
#define MAX77851_TJSHUTDOWN_TEMP	150000

struct max77851_therm_info {
	struct device			*dev;
	struct regmap			*rmap;
	struct thermal_zone_device	*tz_device;

	bool			tjalarm_en;
	bool			tjshutdown_en;

	int				tjshutdown_th;
	int				tjalarm1_th;
	int				tjalarm2_th;

	int				irq_tjshutdown;
	int				irq_tjalarm1;
	int				irq_tjalarm2;
};

#if defined(NV_DEVM_THERMAL_OF_ZONE_REGISTER_PRESENT) /* Linux v6.1 */
static int max77851_thermal_read_temp(struct thermal_zone_device *data, int *temp)
#else
static int max77851_thermal_read_temp(void *data, int *temp)
#endif
{
#if defined(NV_DEVM_THERMAL_OF_ZONE_REGISTER_PRESENT) /* Linux v6.1 */
#if defined(NV_THERMAL_ZONE_DEVICE_PRIV_PRESENT) /* Linux v6.4 */
	struct max77851_therm_info *thermal = thermal_zone_device_priv(data);
#else
	struct max77851_therm_info *thermal = data->devdata;
#endif
#else
	struct max77851_therm_info *thermal = data;
#endif
	unsigned int val;
	int ret;

	ret = regmap_read(thermal->rmap, TOP_STAT1_REG, &val);
	if (ret < 0) {
		dev_err(thermal->dev, "Failed to read STATLBT: %d\n", ret);
		return ret;
	}
	if (val & TOP_STAT1_TJ_SHDN)
		*temp = MAX77851_TJSHUTDOWN_TEMP;
	else if (val & TOP_STAT1_TJ_ALM2)
		*temp = MAX77851_TJALARM2_TEMP;
	else if (val & TOP_STAT1_TJ_ALM1)
		*temp = MAX77851_TJALARM1_TEMP;
	else
		*temp = MAX77851_NORMAL_OPERATING_TEMP;

	return 0;
}

#if defined(NV_DEVM_THERMAL_OF_ZONE_REGISTER_PRESENT)
static const struct thermal_zone_device_ops max77851_thermal_ops = {
#else
static const struct thermal_zone_of_device_ops max77851_thermal_ops = {
#endif
	.get_temp = max77851_thermal_read_temp,
};

static irqreturn_t max77851_thermal_irq(int irq, void *data)
{
	struct max77851_therm_info *thermal = data;

	if (irq == thermal->irq_tjalarm1)
		dev_warn(thermal->dev, "Junction Temp Alarm1(105C) occurred\n");
	else if (irq == thermal->irq_tjalarm2)
		dev_crit(thermal->dev, "Junction Temp Alarm2(135C) occurred\n");
	else if (irq == thermal->irq_tjshutdown)
		dev_crit(thermal->dev, "Junction Temp Shutdown(150C) occurred\n");

	thermal_zone_device_update(thermal->tz_device,
				   THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

static int max77851_thermal_init(struct max77851_therm_info *thermal)
{
	int ret;

	u8 config;

	thermal->tjalarm_en = true;
	thermal->tjshutdown_en = false;

	thermal->tjalarm1_th = MAX77851_TJALARM1_TEMP;
	thermal->tjalarm2_th = MAX77851_TJALARM2_TEMP;
	thermal->tjshutdown_th = MAX77851_TJSHUTDOWN_TEMP;

	config = thermal->tjalarm_en ? TOP_CFG0_TJ_ALM_EN : 0;
	config |= thermal->tjshutdown_en ? TOP_CFG0_TJ_EN : 0;

	ret = regmap_update_bits(thermal->rmap, TOP_CFG0_REG,
				 TOP_CFG0_TJ_ALM_EN | TOP_CFG0_TJ_EN, config);
	if (ret < 0)
		dev_err(thermal->dev, "Failed to update TOP_CFG0_REG: %d\n", ret);

	config = MAX77851_TJALARM1_TH | MAX77851_TJALARM2_TH | MAX77851_TJSHUTDOWN_TH;

	ret = regmap_update_bits(thermal->rmap, TJ_SHDN_CFG_REG,
				 TJ_SHDN_CFG_TJ_ALM1_R | TJ_SHDN_CFG_TJ_ALM2_R | TJ_SHDN_CFG_TJ_SHDN_R, config);
	if (ret < 0)
		dev_err(thermal->dev, "Failed to update TJ_SHDN_CFG_REG: %d\n", ret);

	ret = regmap_update_bits(thermal->rmap, TOP_MSK1_REG,
				 TOP_MSK1_TJ_SHDN_M | TOP_MSK1_TJ_ALM1_M, 0);
	if (ret < 0)
		dev_err(thermal->dev, "Failed to update TOP_MSK1_REG: %d\n", ret);

	return ret;
}


static int max77851_thermal_probe(struct platform_device *pdev)
{
	struct max77851_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max77851_therm_info *thermal;
	int ret;

	thermal = devm_kzalloc(&pdev->dev, sizeof(*thermal), GFP_KERNEL);
	if (!thermal)
		return -ENOMEM;

	thermal->irq_tjshutdown = regmap_irq_get_virq(chip->top_irq_data, MAX77851_IRQ_TOP_TJ_SHDN);
	thermal->irq_tjalarm1 = regmap_irq_get_virq(chip->top_irq_data, MAX77851_IRQ_TOP_TJ_ALM1);
	thermal->irq_tjalarm2 = regmap_irq_get_virq(chip->top_irq_data, MAX77851_IRQ_TOP_TJ_ALM2);
	if ((thermal->irq_tjalarm1 < 0) || (thermal->irq_tjalarm2 < 0) || (thermal->irq_tjshutdown < 0)) {
		dev_err(&pdev->dev, "Alarm irq number not available\n");
		return -EINVAL;
	}

	thermal->dev = &pdev->dev;
	thermal->rmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!thermal->rmap) {
		dev_err(&pdev->dev, "Failed to get parent regmap\n");
		return -ENODEV;
	}

	of_node_put(pdev->dev.of_node);
	pdev->dev.of_node = of_node_get(pdev->dev.parent->of_node);

	max77851_thermal_init(thermal);

#if defined(NV_DEVM_THERMAL_OF_ZONE_REGISTER_PRESENT)
	thermal->tz_device = devm_thermal_of_zone_register(&pdev->dev, 0,
				thermal, &max77851_thermal_ops);
#else
	thermal->tz_device = devm_thermal_zone_of_sensor_register(&pdev->dev, 0,
				thermal, &max77851_thermal_ops);
#endif
	if (IS_ERR(thermal->tz_device)) {
		ret = PTR_ERR(thermal->tz_device);
		dev_err(&pdev->dev, "Failed to register thermal zone: %d\n",
			ret);
		return ret;
	}

	ret = request_threaded_irq(thermal->irq_tjshutdown, NULL, max77851_thermal_irq, 0,
				   "thermal-shutdown", thermal);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request thermal-shutdown IRQ: %d: %d\n",
			thermal->irq_tjshutdown, ret);
		return ret;
	}

	ret = request_threaded_irq(thermal->irq_tjalarm1, NULL, max77851_thermal_irq, 0,
				   "thermal-alarm1", thermal);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request thermal-alarm1 IRQ: %d: %d\n",
			thermal->irq_tjalarm1, ret);
		return ret;
	}

	ret = request_threaded_irq(thermal->irq_tjalarm2, NULL, max77851_thermal_irq, 0,
				   "thermal-alarm2", thermal);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request thermal-alarm2 IRQ: %d: %d\n",
			thermal->irq_tjalarm2, ret);
		return ret;
	}

	platform_set_drvdata(pdev, thermal);

	return 0;
}

static struct platform_device_id max77851_thermal_devtype[] = {
	{ .name = "max77851-thermal", },
	{},
};

static struct platform_driver max77851_thermal_driver = {
	.driver = {
		.name = "max77851-thermal",
	},
	.probe = max77851_thermal_probe,
	.id_table = max77851_thermal_devtype,
};

module_platform_driver(max77851_thermal_driver);

MODULE_DESCRIPTION("MAX77851 thermal driver");
MODULE_AUTHOR("Shubhi Garg<shgarg@nvidia.com>");
MODULE_AUTHOR("Joan Na<Joan.na@maximintegrated.com>");
MODULE_ALIAS("platform:max77851-thermal");
MODULE_LICENSE("GPL v2");
