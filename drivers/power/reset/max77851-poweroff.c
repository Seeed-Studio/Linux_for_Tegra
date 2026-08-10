// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Maxim MAX77851 Power Driver
 */

#include <linux/errno.h>
#include <linux/mfd/max77851.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/slab.h>

struct max77851_poweroff {
	struct regmap *rmap;
	struct device *dev;
};

static struct max77851_poweroff *system_power_off;

static void max77851_pm_power_off(void)
{
	unsigned int val;
	int ret;

	// TOP_INT0 : read only
	// TOP_INT1 : read clear
	// In Any state, use SW_OFF with SRC_SW_OFF = 0b1 to PD.
	// Use SW_COLD_RST with SRC_SW_COLD_RST = 0b1 to reset O-Type registers after a PD and PU

	// TOP_INT1_REG : Read Clear
	ret = regmap_read(system_power_off->rmap, TOP_INT1_REG, &val);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to read %d\n", ret);

	// EN_INT_REG : Read Clear
	ret = regmap_read(system_power_off->rmap, EN_INT_REG, &val);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to read %d\n", ret);

	// GPIO_INT0_REG : Read Clear
	ret = regmap_read(system_power_off->rmap, GPIO_INT0_REG, &val);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to read %d\n", ret);

	// GPIO_INT1_REG : Read Clear
	ret = regmap_read(system_power_off->rmap, GPIO_INT1_REG, &val);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to read %d\n", ret);

	// FPS_INT0_REG : Read Clear
	ret = regmap_read(system_power_off->rmap, FPS_INT0_REG, &val);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to read %d\n", ret);

	// FPS_INT1_REG : Read Clear
	ret = regmap_read(system_power_off->rmap, FPS_INT1_REG, &val);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to read %d\n", ret);

	// LDO_INT0_REG : Read Clear
	ret = regmap_read(system_power_off->rmap, LDO_INT0_REG, &val);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to read %d\n", ret);

	// LDO_INT1_REG : Read Clear
	ret = regmap_read(system_power_off->rmap, LDO_INT1_REG, &val);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to read %d\n", ret);

	// BUCK_INT0_REG : Read Clear
	ret = regmap_read(system_power_off->rmap, BUCK_INT0_REG, &val);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to read %d\n", ret);

	// BUCK_INT1_REG : Read Clear
	ret = regmap_read(system_power_off->rmap, BUCK_INT1_REG, &val);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to read %d\n", ret);

	// BUCK_INT2_REG : Read Clear
	ret = regmap_read(system_power_off->rmap, BUCK_INT2_REG, &val);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to read %d\n", ret);

	// Source Software Off Enabled
	ret = regmap_update_bits(system_power_off->rmap, FPS_SRC_CFG1_REG,
				FPS_SRC_CFG1_SRC_SW_OFF, FPS_SRC_CFG1_SRC_SW_OFF);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to set Source SW Off %d\n", ret);

	// Software Off Event
	ret = regmap_write(system_power_off->rmap, FPS_SW_REG, FPS_SW_OFF);
	if (ret < 0)
		dev_err(system_power_off->dev, "Failed to set SW Off Event %d\n", ret);
}

static int max77851_poweroff_probe(struct platform_device *pdev)
{
	struct max77851_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct device_node *np = pdev->dev.parent->of_node;
	struct max77851_poweroff *poweroff;
	u8 data[3];
	unsigned int val;
	int ret;

	if (!of_device_is_system_power_controller(np))
		return 0;

	poweroff = devm_kzalloc(&pdev->dev, sizeof(*poweroff), GFP_KERNEL);
	if (!poweroff)
		return -ENOMEM;

	poweroff->rmap = chip->rmap;
	poweroff->dev = &pdev->dev;

	ret = regmap_read(poweroff->rmap, RLOGIC_INT0_REG, &val);
	if (ret < 0) {
		dev_err(poweroff->dev, "failed to read power event : %d\n", ret);
		return ret;
	}

	ret = regmap_bulk_read(poweroff->rmap, RLOGIC_INT0_REG, data, ARRAY_SIZE(data));
	if (ret < 0) {
		dev_err(poweroff->dev, "failed to read power event : %d\n", ret);
		return ret;
	}

	dev_dbg(poweroff->dev, "power event: 0x%#x, 0x%#x, 0x%#x\n",
			data[0], data[1], data[2]);

	system_power_off = poweroff;

	if (!pm_power_off)
		pm_power_off = max77851_pm_power_off;

	return 0;

}

static struct platform_device_id max77851_poweroff_devtype[] = {
	{ .name = "max77851-power",	},
	{},
};

static struct platform_driver max77851_poweroff_driver = {
	.driver = {
		.name = "max77851-power",
		.owner = THIS_MODULE,
	},
	.probe = max77851_poweroff_probe,
	.id_table = max77851_poweroff_devtype,
};

module_platform_driver(max77851_poweroff_driver);

MODULE_DESCRIPTION("MAX77851 power driver");
MODULE_AUTHOR("Shubhi Garg<shgarg@nvidia.com>");
MODULE_AUTHOR("Joan Na<Joan.na@maximintegrated.com>");
MODULE_ALIAS("platform:max77851-power");
MODULE_LICENSE("GPL v2");
