// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra PCIe GPIO Hotplug driver
 *
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/sysfs.h>


#define GPIO_ASSERT	1
#define GPIO_DEASSERT	0

// by default we do not want to allow unloading of this module
// as it is required for PCIe controller to know accurately
// whether FPGA is hot-plugged or not
int unlock_module_removal;
module_param(unlock_module_removal, int, 0644);
MODULE_PARM_DESC(
	unlock_module_removal,
	"Set to 1 to unlock unload of module (only for debug purposes)");

struct tegra_pcie_gpio_hpd {
	unsigned int prsnt_irq;
	struct gpio_desc *pcie_prsnt;
	struct platform_device *pcie_gpio_hpd_pdev;
	struct platform_device *pcie_rp_pdev;
};

static const struct of_device_id tegra_pcie_gpio_hpd_of_match[] = {
	{.compatible = "nvidia,tegra_pcie_gpio_hotplug",},
	{},
};

static irqreturn_t pcie_hpd_prsnt_irq_handler(int irq, void *arg)
{
	struct tegra_pcie_gpio_hpd *pcie_hpd = arg;
	struct platform_device *pcie_rp_pdev = pcie_hpd->pcie_rp_pdev;
	struct platform_device *pcie_gpio_hpd_pdev =
	    pcie_hpd->pcie_gpio_hpd_pdev;
	int gpio_state = gpiod_get_value(pcie_hpd->pcie_prsnt);
	int ret;

	if (gpio_state < 0) {
		dev_err(&pcie_gpio_hpd_pdev->dev,
			"Unable to get pcie prsnt gpio %d", gpio_state);
		return IRQ_HANDLED;
	}

	if (gpio_state == GPIO_DEASSERT) {
		dev_info(&pcie_gpio_hpd_pdev->dev, "FPGA Hot Unplug");
		device_release_driver(&pcie_rp_pdev->dev);
	} else {
		dev_info(&pcie_gpio_hpd_pdev->dev, "FPGA Hotplug");
		ret = device_attach(&pcie_rp_pdev->dev);
		if (ret <= 0) {
			dev_err(&pcie_gpio_hpd_pdev->dev,
				"Unable to hotplug fpga %d", ret);
		}
	}

	return IRQ_HANDLED;
}

static ssize_t pcie_prsnt_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct tegra_pcie_gpio_hpd *pcie_hpd = dev_get_drvdata(dev);
	int state = gpiod_get_value(pcie_hpd->pcie_prsnt);

	if (state < 0) {
		dev_err(dev, "Failed to read PCIe present GPIO: %d\n", state);
		return state;
	}

	return sysfs_emit(buf, "%d\n", state);
}

static DEVICE_ATTR_RO(pcie_prsnt);

static struct attribute *dev_entries[] = {
	&dev_attr_pcie_prsnt.attr,
	NULL,
};

static struct attribute_group dev_attr_group = {
	.name = "pcie_gpio_hpd",
	.attrs = dev_entries,
};

static int tegra_pcie_gpio_hpd_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *pcie_rp_node = NULL;
	struct device *dev = &pdev->dev;
	struct tegra_pcie_gpio_hpd *pcie_hpd =
	    devm_kzalloc(dev, sizeof(struct tegra_pcie_gpio_hpd), GFP_KERNEL);
	int ret = 0;

	if (pcie_hpd == NULL)
		return -ENOMEM;

	pcie_hpd->pcie_gpio_hpd_pdev = pdev;
	dev_set_drvdata(dev, pcie_hpd);

	pcie_hpd->pcie_prsnt = devm_gpiod_get(dev, "pcie-prsnt", GPIOD_IN);
	if (IS_ERR(pcie_hpd->pcie_prsnt)) {
		ret = PTR_ERR(pcie_hpd->pcie_prsnt);
		dev_err(dev, "Failed to get pcie_prsnt GPIO: %d\n", ret);
		return ret;
	}

	ret = gpiod_to_irq(pcie_hpd->pcie_prsnt);
	if (ret < 0) {
		dev_err(dev, "Failed to get PRSNT IRQ: %d\n", ret);
		return ret;
	}
	pcie_hpd->prsnt_irq = (unsigned int)ret;

	pcie_rp_node = of_parse_phandle(node, "pcie-host-controller", 0);
	if (!pcie_rp_node) {
		dev_err(dev, "Failed to get pcie-host-controller phandle\n");
		return -ENODEV;
	}
	pcie_hpd->pcie_rp_pdev = of_find_device_by_node(pcie_rp_node);
	if (!pcie_hpd->pcie_rp_pdev) {
		dev_err(dev, "Failed to find PCIe platform device\n");
		ret = -ENODEV;
		goto of_node_cleanup;
	}

	ret = devm_request_threaded_irq(dev,
					pcie_hpd->prsnt_irq,
					NULL,
					pcie_hpd_prsnt_irq_handler,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING |
					IRQF_ONESHOT,
					"pcie_hpd_prsnt_irq", pcie_hpd);
	if (ret < 0) {
		dev_err(dev, "Failed to request IRQ for PRSNT: %d\n", ret);
		goto device_cleanup;
	}

	ret = sysfs_create_group(&dev->kobj, &dev_attr_group);
	if (ret) {
		dev_err(dev, "failed to expose device attributes: %d\n", ret);
		goto device_cleanup;
	}
	if (unlock_module_removal != 1) {
		if (!try_module_get(THIS_MODULE)) {
			dev_err(dev, "Unable to lock driver\n");
			goto sysfs_cleanup;
		}
	}

	if (gpiod_get_value(pcie_hpd->pcie_prsnt) == GPIO_ASSERT) {
		ret = device_attach(&pcie_hpd->pcie_rp_pdev->dev);
		if (ret <= 0) {
			dev_err(dev, "device attach failed: %d\n", ret);
			// maybe controller driver has not been loaded yet?
			ret = 0;
		}
	}

	goto of_node_cleanup;

sysfs_cleanup:
	sysfs_remove_group(&dev->kobj, &dev_attr_group);

device_cleanup:
	platform_device_put(pcie_hpd->pcie_rp_pdev);

of_node_cleanup:
	of_node_put(pcie_rp_node);
	return ret;
}

static int tegra_pcie_gpio_hpd_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra_pcie_gpio_hpd *pcie_hpd = dev_get_drvdata(dev);
	struct platform_device *pcie_rp_pdev = pcie_hpd->pcie_rp_pdev;

	sysfs_remove_group(&dev->kobj, &dev_attr_group);
	device_release_driver(&pcie_rp_pdev->dev);
	platform_device_put(pcie_hpd->pcie_rp_pdev);
	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID)	/* Linux v6.11 */
static void tegra_pcie_gpio_hpd_remove_wrapper(struct platform_device *pdev)
{
	tegra_pcie_gpio_hpd_remove(pdev);
}
#else
static int tegra_pcie_gpio_hpd_remove_wrapper(struct platform_device *pdev)
{
	return tegra_pcie_gpio_hpd_remove(pdev);
}
#endif

static struct platform_driver tegra_pcie_gpio_hpd_driver = {
	.probe = tegra_pcie_gpio_hpd_probe,
	.remove = tegra_pcie_gpio_hpd_remove_wrapper,
	.driver = {
		   .name = "tegra_pcie_gpio_hpd",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(tegra_pcie_gpio_hpd_of_match),
		   },
};

MODULE_AUTHOR("Guruprashanth Krishnakumar <gkrishnakuma@nvidia.com>");
MODULE_DESCRIPTION("Tegra PCIe GPIO Hotplug driver");
MODULE_LICENSE("GPL");

module_platform_driver(tegra_pcie_gpio_hpd_driver);
