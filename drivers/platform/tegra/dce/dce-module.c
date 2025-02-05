// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <dce.h>
#include <dce-linux-device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <dce-hsp-t234.h>
#include <dce-hsp-t264.h>

/**
 * The following platform info is needed for backdoor
 * booting of dce.
 */
static const struct dce_platform_data t234_dce_platform_data = {
	.stream_id = 0x08,
	.phys_stream_id = 0x7f,
	.fw_carveout_id = 9,
	.hsp_id = 0x0,
	.fw_vmindex = 0,
	.fw_name = "display-t234-dce.bin",
	.fw_dce_addr = 0x40000000,
	.fw_info_valid = true,
	.use_physical_id = false,
};

static const struct dce_platform_data t256_dce_platform_data = {
	.stream_id = 0x0,
	.hsp_id = 0x0,
	.fw_name = "display-t256-dce.bin",
	.fw_info_valid = false,
	.use_physical_id = false,
};

static const struct dce_platform_data t264_dce_platform_data = {
	.stream_id = 0x0,
	.hsp_id = 0x0,
	.fw_info_valid = false,
	.use_physical_id = false,
};

const struct of_device_id tegra_dce_of_match[] = {
	{
		.compatible = "nvidia,tegra234-dce",
		.data = (struct dce_platform_data *)&t234_dce_platform_data
	},
	{
		.compatible = "nvidia,tegra256-dce",
		.data = (struct dce_platform_data *)&t256_dce_platform_data
	},
	{
		.compatible = "nvidia,tegra264-dce",
		.data = (struct dce_platform_data *)&t264_dce_platform_data
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_dce_of_match);

/**
 * dce_get_pdata_dce - inline function to get the tegra_dce pointer
 *						from platform devicve.
 *
 * @pdev : Pointer to the platform device data structure.
 *
 * Return :  Pointer pointing to tegra_dce data structure.
 */
static inline struct tegra_dce *dce_get_pdata_dce(struct platform_device *pdev)
{
	return (&((struct dce_linux_device *)dev_get_drvdata(&pdev->dev))->d);
}

/**
 * dce_get_tegra_dce_from_dev - inline function to get the tegra_dce pointer
 *						from devicve struct.
 *
 * @pdev : Pointer to the device data structure.
 *
 * Return :  Pointer pointing to tegra_dce data structure.
 */
static inline struct tegra_dce *dce_get_tegra_dce_from_dev(struct device *dev)
{
	return (&((struct dce_linux_device *)dev_get_drvdata(dev))->d);
}

/**
 * dce_init_dev_data - Function to initialize the dce device data structure.
 *
 * @pdev : Pointer to Linux's platform device used for registering DCE.
 *
 * Primarily used during initialization sequence and is expected to be called
 * from probe only.
 *
 * Return : 0 if success else the corresponding error value.
 */
static int dce_init_dev_data(struct platform_device *pdev, struct dce_platform_data *pdata)
{
	struct device *dev = &pdev->dev;
	struct dce_linux_device *d_dev = NULL;

	d_dev = devm_kzalloc(dev, sizeof(*d_dev), GFP_KERNEL);
	if (!d_dev)
		return -ENOMEM;

	d_dev->dev = dev;
	d_dev->pdata = pdata;
	d_dev->regs = of_iomap(dev->of_node, 0);
	if (!d_dev->regs) {
		dev_err(dev, "failed to map dce cluster IO space\n");
		return -EINVAL;
	}

	dev_set_drvdata(dev, d_dev);
	return 0;
}

/**
 * dce_isr - Handles dce interrupts
 */
static irqreturn_t dce_isr(int irq, void *data)
{
	struct tegra_dce *d = data;

	dce_mailbox_isr(d);

	return IRQ_HANDLED;
}

static void dce_set_irqs(struct platform_device *pdev, bool en)
{
	int i = 0;
	struct tegra_dce *d;
	struct dce_linux_device *d_dev = NULL;

	d_dev = dev_get_drvdata(&pdev->dev);
	d = dce_get_pdata_dce(pdev);

	for (i = 0; i < d_dev->max_cpu_irqs; i++) {
		if (en)
			enable_irq(d->irq[i]);
		else
			disable_irq(d->irq[i]);
	}
}

/**
 * dce_req_interrupts - function to initialize CPU irqs for DCE cpu driver.
 *
 * @pdev : Pointet to Dce Linux Platform Device.
 *
 * Return : 0 if success else the corresponding error value.
 */
static int dce_req_interrupts(struct platform_device *pdev)
{
	int i = 0;
	int ret = 0;
	int no_ints = 0;
	struct tegra_dce *d;
	struct dce_linux_device *d_dev = NULL;

	d_dev = dev_get_drvdata(&pdev->dev);
	d = dce_get_pdata_dce(pdev);

	no_ints = platform_irq_count(pdev);
	if (no_ints == 0) {
		dev_err(&pdev->dev,
			"Invalid number of interrupts configured = %d",
			no_ints);
		return -EINVAL;
	}

	d_dev->max_cpu_irqs = no_ints;

	for (i = 0; i < no_ints; i++) {
		ret = platform_get_irq(pdev, i);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"Getting dce intr lines failed with ret = %d",
				ret);
			return ret;
		}
		d->irq[i] = ret;
		ret = devm_request_threaded_irq(&pdev->dev, d->irq[i],
				NULL, dce_isr, IRQF_ONESHOT, "tegra_dce_isr",
				d);
		if (ret) {
			dev_err(&pdev->dev,
				"failed to request irq @ with ret = %d\n",
				ret);
		}
		disable_irq(d->irq[i]);
	}
	return ret;
}

static int match_display_dev(struct device *dev, const void *data)
{
	if ((dev != NULL) && (dev->of_node != NULL)) {
		if (of_device_is_compatible(dev->of_node, "nvidia,tegra234-display"))
			return 1;
	}

	return 0;
}

/**
 * dce_init_hsp_hal_fn - Function to initialize DCE HSP hal functions.
 *
 * @pdev : Pointer to Linux's platform device used for registering DCE.
 * @pdata: pointer to dce_platform data structure
 *
 * Primarily used during initialization sequence and is expected to be called
 * from probe only.
 *
 * Return : 0 if success else the corresponding error value.
 */
static int dce_init_hsp_hal_fn(struct platform_device *pdev,
			       struct dce_platform_data *pdata)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct tegra_dce *d = NULL;
	int ret = 0;

	d = dce_get_pdata_dce(pdev);
	if (of_device_is_compatible(node, "nvidia,tegra234-dce")) {
		dev_info(&pdev->dev, "Setting DCE HSP functions for tegra234-dce");
		DCE_HSP_INIT_T234(d->hsp);
	} else if (of_device_is_compatible(node, "nvidia,tegra256-dce")) {
		dev_info(&pdev->dev, "Setting DCE HSP functions for tegra256-dce");
		DCE_HSP_INIT_T264(d->hsp);
	} else if (of_device_is_compatible(node, "nvidia,tegra264-dce")) {
		dev_info(&pdev->dev, "Setting DCE HSP functions for tegra234-dce");
		DCE_HSP_INIT_T264(d->hsp);
	} else {
		ret = -1;
		dev_err(&pdev->dev, "DCE SOC not supported");
	}

	/**
	 * TODO: Get HSP_ID from DT
	 */
	d->hsp_id = pdata->hsp_id;

	return ret;
}

static int tegra_dce_probe(struct platform_device *pdev)
{
	int err = 0;
	struct tegra_dce *d = NULL;
	struct device *dev = &pdev->dev;
	struct dce_platform_data *pdata = NULL;
	const struct of_device_id *match = NULL;
	struct device *c_dev;
	struct device_link *link;

	match = of_match_device(tegra_dce_of_match, dev);
	if (!match) {
		dev_info(dev, "no device match found\n");
		return -ENODEV;
	}

	pdata = (struct dce_platform_data *)match->data;

	WARN_ON(!pdata);
	if (!pdata) {
		dev_info(dev, "no platform data\n");
		err = -ENODATA;
		goto err_get_pdata;
	}

	err = dce_init_dev_data(pdev, pdata);
	if (err) {
		dev_err(dev, "failed to init device data with err = %d\n",
			err);
		goto os_init_err;
	}

	/*
	 * Initialize SOC HSP HAL Functions
	 */
	err = dce_init_hsp_hal_fn(pdev, pdata);
	if (err) {
		dev_err(dev, "failed to init HSP functions err:%d\n", err);
		goto os_init_err;
	}

	err = dce_req_interrupts(pdev);
	if (err) {
		dev_err(dev, "failed to get interrupts with err = %d\n",
			err);
		goto req_intr_err;
	}

	d = dce_get_pdata_dce(pdev);

	err = dce_driver_init(d);
	if (err) {
		dce_os_err(d, "DCE Driver Init Failed");
		goto err_driver_init;
	}

	dce_driver_start(d);

	dce_set_irqs(pdev, true);

#ifdef CONFIG_DEBUG_FS
	dce_init_debug(d);
#endif

	c_dev = bus_find_device(&platform_bus_type, NULL, NULL, match_display_dev);
	if (c_dev != NULL) {
		dce_os_info(d, "Found display consumer device");
		link = device_link_add(c_dev, dev,
				       DL_FLAG_PM_RUNTIME | DL_FLAG_AUTOREMOVE_SUPPLIER);
		if (link == NULL) {
			dce_os_err(d, "Failed to create device link to %s\n", dev_name(c_dev));
			return -EINVAL;
		}
	}

	/**
	 * FIXME: Allow tegra_dce.ko unloading.
	 */
	if (!try_module_get(THIS_MODULE)) {
		dce_os_info(d, "Failed to get lock of DCE Module.\n");
		dce_os_info(d, "modprobe --remove of kernel modules depending on tegra_dce.ko will fail.\n");
	}

	return 0;

req_intr_err:
os_init_err:
err_get_pdata:
err_driver_init:
	return err;
}

static int tegra_dce_remove(struct platform_device *pdev)
{
	/* TODO */
	struct tegra_dce *d =
			dce_get_pdata_dce(pdev);

#ifdef CONFIG_DEBUG_FS
	dce_remove_debug(d);
#endif

	dce_set_irqs(pdev, false);
	dce_driver_stop(d);
	dce_driver_deinit(d);
	return 0;
}

#ifdef CONFIG_PM
static int dce_pm_suspend(struct device *dev)
{
	struct tegra_dce *d = dce_get_tegra_dce_from_dev(dev);

	return dce_pm_enter_sc7(d);
}

static int dce_pm_resume(struct device *dev)
{
	struct tegra_dce *d = dce_get_tegra_dce_from_dev(dev);

	return dce_pm_exit_sc7(d);
}

static const struct dev_pm_ops dce_pm_ops = {
	.suspend_late = dce_pm_suspend,
	.resume  = dce_pm_resume,
};
#endif

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_dce_remove_wrapper(struct platform_device *pdev)
{
	tegra_dce_remove(pdev);
}
#else
static int tegra_dce_remove_wrapper(struct platform_device *pdev)
{
	return tegra_dce_remove(pdev);
}
#endif

static struct platform_driver tegra_dce_driver = {
	.driver = {
		.name   = "tegra-dce",
		.of_match_table =
			of_match_ptr(tegra_dce_of_match),
#ifdef CONFIG_PM
		.pm	= &dce_pm_ops,
#endif
	},
	.probe = tegra_dce_probe,
	.remove = tegra_dce_remove_wrapper,
};
module_platform_driver(tegra_dce_driver);

MODULE_DESCRIPTION("DCE Linux driver");
MODULE_AUTHOR("NVIDIA");
MODULE_LICENSE("GPL v2");
