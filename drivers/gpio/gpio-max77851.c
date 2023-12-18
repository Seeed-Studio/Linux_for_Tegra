// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Maxim MAX77851 GPIO driver
 */

#include <nvidia/conftest.h>

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/mfd/max77851.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define GPIO_CNFG_OFFSET (5)
#define GPIO_CNFG0_REG_ADDR(offset) (GPIO0_CFG0_REG + (offset * GPIO_CNFG_OFFSET))
#define GPIO_CNFG1_REG_ADDR(offset) (GPIO0_CFG1_REG + (offset * GPIO_CNFG_OFFSET))

#define SUPPORTED_IRQ_TYPE (IRQ_TYPE_EDGE_BOTH | IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW)

#define GPIO_INT_RISING (BIT_IS_ZERO | BIT_IS_ZERO)
#define GPIO_INT_FALLING (BIT_IS_ZERO | BIT(2))

#define GPIO_INT_LEVEL_HIGH (BIT(3) | BIT_IS_ZERO)
#define GPIO_INT_LEVEL_LOW (BIT(3) | BIT(2))
#define GPIO_INT_MASK BITS(3, 2)

struct max77851_gpio {
	struct gpio_chip	gpio_chip;
	struct regmap		*rmap;
	struct device		*dev;
};

/* Interrupts */
enum {
	/* Addr : 0x2B */
	MAX77851_IRQ_GPIO0_FALLING,
	MAX77851_IRQ_GPIO1_FALLING,
	MAX77851_IRQ_GPIO2_FALLING,
	MAX77851_IRQ_GPIO3_FALLING,
	MAX77851_IRQ_GPIO4_FALLING,
	MAX77851_IRQ_GPIO5_FALLING,
	MAX77851_IRQ_GPIO6_FALLING,
	MAX77851_IRQ_GPIO7_FALLING,

	/* Addr : 0x2C */
	MAX77851_IRQ_GPIO0_RISING,
	MAX77851_IRQ_GPIO1_RISING,
	MAX77851_IRQ_GPIO2_RISING,
	MAX77851_IRQ_GPIO3_RISING,
	MAX77851_IRQ_GPIO4_RISING,
	MAX77851_IRQ_GPIO5_RISING,
	MAX77851_IRQ_GPIO6_RISING,
	MAX77851_IRQ_GPIO7_RISING
};

static const struct regmap_irq max77851_gpio_irqs[] = {
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO0_FALLING, 0, GPIO_INT0_GPIO0_FL_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO1_FALLING, 0, GPIO_INT0_GPIO1_FL_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO2_FALLING, 0, GPIO_INT0_GPIO2_FL_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO3_FALLING, 0, GPIO_INT0_GPIO3_FL_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO4_FALLING, 0, GPIO_INT0_GPIO4_FL_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO5_FALLING, 0, GPIO_INT0_GPIO5_FL_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO6_FALLING, 0, GPIO_INT0_GPIO6_FL_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO7_FALLING, 0, GPIO_INT0_GPIO7_FL_I),

	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO0_RISING, 1, GPIO_INT1_GPIO0_RH_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO1_RISING, 1, GPIO_INT1_GPIO1_RH_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO2_RISING, 1, GPIO_INT1_GPIO2_RH_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO3_RISING, 1, GPIO_INT1_GPIO3_RH_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO4_RISING, 1, GPIO_INT1_GPIO4_RH_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO5_RISING, 1, GPIO_INT1_GPIO5_RH_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO6_RISING, 1, GPIO_INT1_GPIO6_RH_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_GPIO7_RISING, 1, GPIO_INT1_GPIO7_RH_I),
};

static int max77851_gpio_irq_global_mask(void *irq_drv_data)
{
	int ret = 0;

	return ret;
}

static int max77851_gpio_irq_global_unmask(void *irq_drv_data)
{
	int ret = 0;

	return ret;
}

static const struct regmap_irq_chip max77851_gpio_irq_chip = {
	.name		= "max77851-gpio",
	.status_base	= GPIO_INT0_REG,
	.mask_base	= GPIO_MSK0_REG,
	.num_regs	= 2,
	.irqs		= max77851_gpio_irqs,
	.num_irqs	= ARRAY_SIZE(max77851_gpio_irqs),
	.handle_pre_irq = max77851_gpio_irq_global_mask,
	.handle_post_irq = max77851_gpio_irq_global_unmask,
};

static int max77851_gpio_dir_input(struct gpio_chip *gc, unsigned int offset)
{
	struct max77851_gpio *mgpio = gpiochip_get_data(gc);
	int ret;

	ret = regmap_update_bits(mgpio->rmap, GPIO_CNFG1_REG_ADDR(offset),
				 GPIO_CFG1_MODE,
				 GPIO_PINMUX_GPIO_INPUT);
	if (ret < 0)
		dev_err(mgpio->dev, "CNFG_GPIOx dir update failed: %d\n", ret);

	return ret;
}

static int max77851_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct max77851_gpio *mgpio = gpiochip_get_data(gc);
	unsigned int val;
	int ret;

	ret = regmap_read(mgpio->rmap, GPIO_STAT0_REG, &val);
	if (ret < 0) {
		dev_err(mgpio->dev, "CNFG_GPIOx read failed: %d\n", ret);
		return ret;
	}

	if  (val & BIT(offset))
		return 1;
	else
		return 0;
}

static int max77851_gpio_dir_output(struct gpio_chip *gc, unsigned int offset,
				    int value)
{
	struct max77851_gpio *mgpio = gpiochip_get_data(gc);
	u8 val;
	int ret;

	val = (value) ? GPIO_OUTPUT_VAL_HIGH : GPIO_OUTPUT_VAL_LOW;

	ret = regmap_update_bits(mgpio->rmap, GPIO_CNFG1_REG_ADDR(offset),
				 GPIO_CFG1_OUTPUT, val);
	if (ret < 0) {
		dev_err(mgpio->dev, "CNFG_GPIOx val update failed: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(mgpio->rmap, GPIO_CNFG1_REG_ADDR(offset),
				 GPIO_CFG1_MODE,
				 GPIO_PINMUX_GPIO_OUTPUT);
	if (ret < 0)
		dev_err(mgpio->dev, "CNFG_GPIOx dir update failed: %d\n", ret);

	return ret;
}

static int max77851_gpio_set_debounce(struct max77851_gpio *mgpio,
				      unsigned int offset,
				      unsigned int debounce)
{
	u8 val;
	int ret;

	switch (debounce) {
	case 0:
		val = GPIO_DBNC_NONE;
		break;
	case 1 ... 100:
		val = GPIO_DBNC_100US;
		break;
	case 101 ... 1000:
		val = GPIO_DBNC_1MS;
		break;
	case 1001 ... 4000:
		val = GPIO_DBNC_4MS;
		break;
	case 4001 ... 8000:
		val = GPIO_DBNC_8MS;
		break;
	case 8001 ... 16000:
		val = GPIO_DBNC_16MS;
		break;
	case 16001 ... 32000:
		val = GPIO_DBNC_32MS;
		break;
	default:
		dev_err(mgpio->dev, "Illegal value %u\n", debounce);
		return -EINVAL;
	}

	ret = regmap_update_bits(mgpio->rmap, GPIO_CNFG0_REG_ADDR(offset),
				 GPIO_CFG0_IFILTER, val);
	if (ret < 0)
		dev_err(mgpio->dev, "CNFG_GPIOx_DBNC update failed: %d\n", ret);

	return ret;
}

static void max77851_gpio_set(struct gpio_chip *gc, unsigned int offset,
			      int value)
{
	struct max77851_gpio *mgpio = gpiochip_get_data(gc);
	u8 val;
	int ret;

	val = (value) ? GPIO_OUTPUT_VAL_HIGH : GPIO_OUTPUT_VAL_LOW;

	ret = regmap_update_bits(mgpio->rmap, GPIO_CNFG1_REG_ADDR(offset),
				GPIO_CFG1_OUTPUT, val);
	if (ret < 0)
		dev_err(mgpio->dev, "CNFG_GPIO_OUT update failed: %d\n", ret);
}

static int max77851_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
				    unsigned long config)
{
	struct max77851_gpio *mgpio = gpiochip_get_data(gc);

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		return regmap_update_bits(mgpio->rmap, GPIO_CNFG1_REG_ADDR(offset),
					  GPIO_CFG1_DRV, GPIO_DRV_OPENDRAIN);
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return regmap_update_bits(mgpio->rmap, GPIO_CNFG1_REG_ADDR(offset),
					  GPIO_CFG1_DRV, GPIO_DRV_PUSHPULL);
	case PIN_CONFIG_INPUT_DEBOUNCE:
		return max77851_gpio_set_debounce(mgpio, offset,
			pinconf_to_config_argument(config));
	default:
		break;
	}

	return -ENOTSUPP;
}

static int max77851_gpio_to_irq(struct gpio_chip *gc, unsigned int offset)
{
#if defined(CONFIG_REGMAP_IRQ)
	struct max77851_gpio *mgpio = gpiochip_get_data(gc);
	struct max77851_chip *chip = dev_get_drvdata(mgpio->dev->parent);

	return regmap_irq_get_virq(chip->gpio_irq_data, offset);
#else
	return 0;
#endif
}

static int max77851_gpio_probe(struct platform_device *pdev)
{
	struct max77851_chip *chip =  dev_get_drvdata(pdev->dev.parent);
	struct max77851_gpio *mgpio;
	int gpio_irq;
	int ret;

	gpio_irq = platform_get_irq(pdev, 0);
	if (gpio_irq <= 0)
		return -ENODEV;

	mgpio = devm_kzalloc(&pdev->dev, sizeof(*mgpio), GFP_KERNEL);
	if (!mgpio)
		return -ENOMEM;

	mgpio->rmap = chip->rmap;
	mgpio->dev = &pdev->dev;

	mgpio->gpio_chip.label = pdev->name;
	mgpio->gpio_chip.parent = &pdev->dev;
	mgpio->gpio_chip.direction_input = max77851_gpio_dir_input;
	mgpio->gpio_chip.get = max77851_gpio_get;
	mgpio->gpio_chip.direction_output = max77851_gpio_dir_output;
	mgpio->gpio_chip.set = max77851_gpio_set;
	mgpio->gpio_chip.set_config = max77851_gpio_set_config;
	mgpio->gpio_chip.to_irq = max77851_gpio_to_irq;
	mgpio->gpio_chip.ngpio = MAX77851_GPIO_NR;
	mgpio->gpio_chip.can_sleep = 1;
	mgpio->gpio_chip.base = -1;
#ifdef CONFIG_OF_GPIO
#if defined(NV_GPIO_CHIP_STRUCT_HAS_OF_NODE_PRESENT) /* Linux 6.2 */
	mgpio->gpio_chip.of_node = pdev->dev.parent->of_node;
#endif
#endif

	platform_set_drvdata(pdev, mgpio);

	ret = devm_gpiochip_add_data(&pdev->dev, &mgpio->gpio_chip, mgpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "gpio_init: Failed to add max77851_gpio\n");
		return ret;
	}

#if defined(CONFIG_REGMAP_IRQ)
	ret = devm_regmap_add_irq_chip(&pdev->dev, chip->rmap, gpio_irq,
				       IRQF_ONESHOT, -1,
				       &max77851_gpio_irq_chip,
				       &chip->gpio_irq_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add gpio irq_chip %d\n", ret);
		return ret;
	}
#endif

	return 0;
}

static const struct platform_device_id max77851_gpio_devtype[] = {
	{ .name = "max77851-gpio", },
	{},
};
MODULE_DEVICE_TABLE(platform, max77851_gpio_devtype);

static struct platform_driver max77851_gpio_driver = {
	.driver.name	= "max77851-gpio",
	.probe		= max77851_gpio_probe,
	.id_table	= max77851_gpio_devtype,
};

module_platform_driver(max77851_gpio_driver);

MODULE_DESCRIPTION("MAX77851 GPIO driver");
MODULE_AUTHOR("Shubhi Garg<shgarg@nvidia.com>");
MODULE_AUTHOR("Joan Na<Joan.na@maximintegrated.com>");
MODULE_ALIAS("platform:max77851-gpio");
MODULE_LICENSE("GPL v2");
