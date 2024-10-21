// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Maxim MAX77851 Watchdog Driver
 */

#include <nvidia/conftest.h>

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/max77851.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/watchdog.h>
#include <linux/regulator/consumer.h>

static bool nowayout = WATCHDOG_NOWAYOUT;

struct max77851_wdt {
	struct device			*dev;
	struct regmap			*rmap;
	struct watchdog_device		wdt_dev;
};

static int max77851_wdt_start(struct watchdog_device *wdt_dev)
{
	struct max77851_wdt *wdt = watchdog_get_drvdata(wdt_dev);

	return regmap_update_bits(wdt->rmap, SYS_WD_CFG_REG,
				  SYS_WD_CFG_SYS_WD_EN, SYS_WD_CFG_SYS_WD_EN);
}

static int max77851_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct max77851_wdt *wdt = watchdog_get_drvdata(wdt_dev);

	return regmap_update_bits(wdt->rmap, SYS_WD_CFG_REG,
				  SYS_WD_CFG_SYS_WD_EN, 0);
}

static int max77851_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct max77851_wdt *wdt = watchdog_get_drvdata(wdt_dev);

	return regmap_update_bits(wdt->rmap, SYS_WD_CLR_REG,
				  SYS_WD_CLR_SYS_WD_C, SYS_WD_CLR_COMMAND);
}

static int max77851_wdt_set_timeout(struct watchdog_device *wdt_dev,
				    unsigned int timeout)
{
	struct max77851_wdt *wdt = watchdog_get_drvdata(wdt_dev);
	unsigned int wdt_timeout;
	u8 regval;
	int ret;

	switch (timeout) {
	case 0 ... 2:
		regval = MAX77851_TWD_2_SEC;
		wdt_timeout = 2;
		break;

	case 3 ... 16:
		regval = MAX77851_TWD_16_SEC;
		wdt_timeout = 16;
		break;

	case 17 ... 64:
		regval = MAX77851_TWD_64_SEC;
		wdt_timeout = 64;
		break;

	default:
		regval = MAX77851_TWD_128_SEC;
		wdt_timeout = 128;
		break;
	}

	ret = regmap_update_bits(wdt->rmap, SYS_WD_CFG_REG,
				 SYS_WD_CLR_SYS_WD_C, SYS_WD_CLR_COMMAND);

	if (ret < 0)
		return ret;

	ret = regmap_update_bits(wdt->rmap, SYS_WD_CFG_REG,
				 SYS_WD_CLR_SYS_WD_C, regval);
	if (ret < 0)
		return ret;

	wdt_dev->timeout = wdt_timeout;

	return 0;
}

static const struct watchdog_info max77851_wdt_info = {
	.identity = "max77851-watchdog",
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops max77851_wdt_ops = {
	.start		= max77851_wdt_start,
	.stop		= max77851_wdt_stop,
	.ping		= max77851_wdt_ping,
	.set_timeout	= max77851_wdt_set_timeout,
};

static int max77851_wdt_probe(struct platform_device *pdev)
{
	struct max77851_wdt *wdt;
	struct watchdog_device *wdt_dev;
	struct device_node *np;
	unsigned int regval;
	int ret;

	np = of_get_child_by_name(pdev->dev.parent->of_node, "watchdog");
	if (np && !of_device_is_available(np))
		return -ENODEV;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->dev = &pdev->dev;
	wdt->rmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!wdt->rmap) {
		dev_err(wdt->dev, "Failed to get parent regmap\n");
		return -ENODEV;
	}

	wdt_dev = &wdt->wdt_dev;
	wdt_dev->info = &max77851_wdt_info;
	wdt_dev->ops = &max77851_wdt_ops;
	wdt_dev->min_timeout = 2;
	wdt_dev->max_timeout = 128;
	wdt_dev->max_hw_heartbeat_ms = 128 * 1000;
	wdt_dev->parent = wdt->dev;

	platform_set_drvdata(pdev, wdt);
	watchdog_set_drvdata(wdt_dev, wdt);

	/* Set WDT clear in OFF and sleep mode */
	ret = regmap_update_bits(wdt->rmap, SYS_WD_CFG_REG,
				 SYS_WD_CFG_SYS_WD_SLPC, SYS_WD_CFG_SYS_WD_SLPC);
	if (ret < 0) {
		dev_err(wdt->dev, "Failed to set WDT OFF mode: %d\n", ret);
		return ret;
	}

	/* start watchdog when "wdt-boot-init" flag is set */
	if (of_property_read_bool(np, "maxim,wdt-boot-init")) {
		if (max77851_wdt_start(wdt_dev) < 0)
			dev_err(wdt->dev, "Failed to start watchdog on booting\n");
	}

	/* Check if WDT running and if yes then set flags properly */
	ret = regmap_read(wdt->rmap, SYS_WD_CFG_REG, &regval);
	if (ret < 0) {
		dev_err(wdt->dev, "Failed to read WDT CFG register: %d\n", ret);
		return ret;
	}

	switch (regval & SYS_WD_CFG_SYS_WD) {
	case MAX77851_TWD_2_SEC:
		wdt_dev->timeout = 2;
		break;
	case MAX77851_TWD_16_SEC:
		wdt_dev->timeout = 16;
		break;
	case MAX77851_TWD_64_SEC:
		wdt_dev->timeout = 64;
		break;
	default:
		wdt_dev->timeout = 128;
		break;
	}

	if (regval & SYS_WD_CFG_SYS_WD_EN)
		set_bit(WDOG_HW_RUNNING, &wdt_dev->status);

	watchdog_set_nowayout(wdt_dev, nowayout);

	/* Stop watchdog on reboot */
	watchdog_stop_on_reboot(wdt_dev);

	ret = devm_watchdog_register_device(wdt->dev, wdt_dev);
	if (ret < 0) {
		dev_err(wdt->dev, "watchdog registration failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int max77851_wdt_remove(struct platform_device *pdev)
{
	struct max77851_wdt *wdt = platform_get_drvdata(pdev);

	max77851_wdt_stop(&wdt->wdt_dev);
	watchdog_unregister_device(&wdt->wdt_dev);

	return 0;
}

static struct platform_device_id max77851_wdt_devtype[] = {
	{ .name = "max77851-watchdog", },
	{ },
};

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void max77851_wdt_remove_wrapper(struct platform_device *pdev)
{
	max77851_wdt_remove(pdev);
}
#else
static int max77851_wdt_remove_wrapper(struct platform_device *pdev)
{
	return max77851_wdt_remove(pdev);
}
#endif

static struct platform_driver max77851_wdt_driver = {
	.driver	= {
		.name	= "max77851-watchdog",
	},
	.probe	= max77851_wdt_probe,
	.remove	= max77851_wdt_remove_wrapper,
	.id_table = max77851_wdt_devtype,
};

module_platform_driver(max77851_wdt_driver);

MODULE_DESCRIPTION("Max77851 watchdog timer driver");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

MODULE_DESCRIPTION("MAX77851 watchdog driver");
MODULE_AUTHOR("Shubhi Garg<shgarg@nvidia.com>");
MODULE_AUTHOR("Joan Na<Joan.na@maximintegrated.com>");
MODULE_ALIAS("platform:max77851-watchdog");
MODULE_LICENSE("GPL v2");
