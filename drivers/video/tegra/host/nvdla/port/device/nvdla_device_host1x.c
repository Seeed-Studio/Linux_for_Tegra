// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA device implementation as Host1x client.
 */

#include <nvidia/conftest.h>

#include "../nvdla_device.h"

#include "../../dla_queue.h"
#include "../../nvdla_debug.h"

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/host1x-next.h>
#include <linux/nvhost.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>

uint32_t nvdla_device_register_read(struct platform_device *pdev,
	uint32_t reg)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	void __iomem *addr = pdata->aperture[0] + reg;

	return readl(addr);
}

void nvdla_device_register_write(struct platform_device *pdev,
	uint32_t reg,
	uint32_t value)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	void __iomem *addr = pdata->aperture[0] + reg;

	writel(value, addr);
}

static struct device *nvdla_client_device_create(struct platform_device *pdev,
						struct cdev *cdev,
						const char *cdev_name,
						dev_t devno,
						const struct file_operations *ops)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct device *dev;
	int err;

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	pdata->nvhost_class = class_create(pdev->dev.of_node->name);
#else
	pdata->nvhost_class = class_create(THIS_MODULE, pdev->dev.of_node->name);
#endif

	if (IS_ERR(pdata->nvhost_class)) {
		dev_err(&pdev->dev, "failed to create class\n");
		return ERR_CAST(pdata->nvhost_class);
	}

	cdev_init(cdev, ops);
	cdev->owner = THIS_MODULE;

	err = cdev_add(cdev, devno, 1);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to add cdev\n");
		class_destroy(pdata->nvhost_class);
		return ERR_PTR(err);
	}

	dev = device_create(pdata->nvhost_class, &pdev->dev, devno, NULL,
			(pdev->id <= 0) ? "nvhost-%s%s" : "nvhost-%s%s.%d",
			cdev_name, pdev->dev.of_node->name, pdev->id);

	if (IS_ERR(dev)) {
		dev_err(&pdev->dev, "failed to create %s device\n", cdev_name);
		class_destroy(pdata->nvhost_class);
		cdev_del(cdev);
	}

	return dev;
}

static int nvdla_client_device_init(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	dev_t devno;
	int err;

	err = alloc_chrdev_region(&devno, 0, 1, "nvhost");
	if (err < 0) {
		dev_err(&pdev->dev, "failed to reserve chrdev region\n");
		return err;
	}

	pdata->ctrl_node = nvdla_client_device_create(pdev, &pdata->ctrl_cdev,
						"ctrl-", devno,
						pdata->ctrl_ops);
	if (IS_ERR(pdata->ctrl_node)) {
		unregister_chrdev_region(devno, 1);
		return PTR_ERR(pdata->ctrl_node);
	}

	pdata->cdev_region = devno;

	return 0;
}

static void nvdla_client_device_release(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(pdata->ctrl_node)) {
		device_destroy(pdata->nvhost_class, pdata->ctrl_cdev.dev);
		cdev_del(&pdata->ctrl_cdev);
		class_destroy(pdata->nvhost_class);
	}

	unregister_chrdev_region(pdata->cdev_region, 1);
}

int32_t nvdla_module_init(struct platform_device *pdev)
{
	int32_t err;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	int i;

	pdata->host1x = dev_get_drvdata(pdev->dev.parent);
	if (!pdata->host1x) {
		dev_warn(&pdev->dev, "No platform data for host1x!\n");
		return -ENODEV;
	}

	/* Map device resources */
	for (i = 0; i < pdev->num_resources; i++) {
		void __iomem *regs = NULL;
		struct resource *r;

		r = platform_get_resource(pdev, IORESOURCE_MEM, i);
		/* We've run out of mem resources */
		if (!r)
			break;

		regs = devm_ioremap_resource(&pdev->dev, r);
		if (IS_ERR(regs)) {
			err = PTR_ERR(regs);
			dev_err(&pdev->dev, "failed to get register memory\n");
			return err;
		}

		pdata->aperture[i] = regs;
	}

	err = nvhost_module_init(pdev);
	if (err) {
		nvdla_dbg_err(pdev, "Failed to init module (err: %x)", err);
		return err;
	}

	err = nvdla_client_device_init(pdev);
	if (err) {
		nvdla_dbg_err(pdev, "Failed to client device (err: %x)", err);
		nvhost_module_deinit(pdev);
		return err;
	}

	return 0;
}

void nvdla_module_deinit(struct platform_device *pdev)
{
	nvdla_client_device_release(pdev);
	nvhost_module_deinit(pdev);
}

int32_t nvdla_module_client_register(struct platform_device *pdev,
	void *context)
{
	return nvhost_module_add_client(pdev, context);
}

void nvdla_module_client_unregister(struct platform_device *pdev,
	void *context)
{
	nvhost_module_remove_client(pdev, context);
}

int32_t nvdla_module_busy(struct platform_device *pdev)
{
	return nvhost_module_busy(pdev);
}

void nvdla_module_idle(struct platform_device *pdev)
{
	nvhost_module_idle(pdev);
}

void nvdla_module_idle_mult(struct platform_device *pdev, int32_t refs)
{
	nvhost_module_idle_mult(pdev, refs);
}

void nvdla_module_reset(struct platform_device *pdev, bool reboot)
{
	nvhost_module_reset(pdev, reboot);
}

int nvdla_module_runtime_suspend(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla = pdata->private_data;
	int err;

	if (nvhost_module_pm_ops.runtime_suspend != NULL) {
		err = nvhost_module_pm_ops.runtime_suspend(dev);
		if (!err && nvdla->icc_write) {
			err = icc_set_bw(nvdla->icc_write, 0, 0);
			if (err)
				dev_warn(&nvdla->pdev->dev,
					"failed to set icc_write bw: %d\n",
					err);

			return 0;
		}
		return err;
	}

	return -EOPNOTSUPP;
}

int nvdla_module_runtime_resume(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla = pdata->private_data;
	struct clk *clk = pdata->clks[0].clk;
	unsigned long rate;
	u32 emc_kbps;
	int err;

	if (nvhost_module_pm_ops.runtime_resume != NULL) {
		err = nvhost_module_pm_ops.runtime_resume(dev);
		if (!err && nvdla->icc_write) {
			rate = clk_get_rate(clk);
			emc_kbps = rate * NVDLA_AXI_DBB_BW_BPC / 1024;
			err = icc_set_bw(nvdla->icc_write, kbps_to_icc(emc_kbps),
					0);
			if (err)
				dev_warn(&nvdla->pdev->dev,
					"failed to set icc_write bw: %d\n",
					err);

			return 0;
		}
		return err;
	}

	return -EOPNOTSUPP;
}

int nvdla_module_suspend(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	int err = 0;

	if (nvhost_module_pm_ops.suspend != NULL) {
		err = nvhost_module_pm_ops.suspend(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) NvHost suspend\n");
			goto fail_nvhost_module_suspend;
		}
	} else {
		err = pm_runtime_force_suspend(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) PM suspend\n");
			goto fail_nvhost_module_suspend;
		}
	}

	if (nvdla_dev->icc_write) {
		err = icc_set_bw(nvdla_dev->icc_write, 0, 0);
		if (err)
			dev_warn(&nvdla_dev->pdev->dev,
				 "failed to set icc_write bw: %d\n", err);
	}

	/* Mark module to be in suspend state. */
	nvdla_dev->is_suspended = true;

fail_nvhost_module_suspend:
	return err;
}

int nvdla_module_resume(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	int err;

	/* Confirm if module is in suspend state. */
	if (!nvdla_dev->is_suspended) {
		dev_warn(dev, "NvDla is not in suspend state.\n");
		goto fail_not_in_suspend;
	}

	if (nvhost_module_pm_ops.resume != NULL) {
		err = nvhost_module_pm_ops.resume(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) NvHost resume\n");
			goto fail_nvhost_module_resume;
		}
	} else {
		err = pm_runtime_force_resume(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) PM resume\n");
			goto fail_nvhost_module_resume;
		}
	}

	return 0;

fail_nvhost_module_resume:
fail_not_in_suspend:
	return err;
}

int nvdla_module_prepare_suspend(struct device *dev)
{
	int err = 0;
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	/* Confirm if module is not in suspend state. */
	if (nvdla_dev->is_suspended) {
		dev_warn(dev, "NvDla is already in suspend state.\n");
		goto fail_already_in_suspend;
	}

	/* Prepare for queue pool suspension. */
	err = nvdla_queue_pool_prepare_suspend(nvdla_dev->pool);
	if (err != 0) {
		dev_err(dev, "(FAIL) Queue suspend\n");
		goto fail_nvdla_queue_pool_prepare_suspend;
	}

	/* NvHost prepare suspend - callback */
	if (nvhost_module_pm_ops.prepare != NULL) {
		err = nvhost_module_pm_ops.prepare(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) NvHost prepare suspend\n");
			goto fail_nvhost_module_prepare_suspend;
		}
	} else {
		/* If we took an extra reference, drop it now to prevent
		 * the device from automatically resuming upon system
		 * resume.
		 */
		pm_runtime_put_sync(dev);
	}


	return 0;

fail_nvhost_module_prepare_suspend:
fail_nvdla_queue_pool_prepare_suspend:
fail_already_in_suspend:
	return err;
}

void nvdla_module_complete_resume(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	if (nvhost_module_pm_ops.complete != NULL) {
		nvhost_module_pm_ops.complete(dev);
	} else {
		/* Retake reference dropped above */
		pm_runtime_get_noresume(dev);
	}

	/* Module is no longer in suspend and has resumed successfully */
	nvdla_dev->is_suspended = false;
}

static struct host1x_driver host1x_nvdla_driver = {
	.driver = {
		.name = "host1x-nvdla",
	},
};

int32_t nvdla_driver_register(struct platform_driver *pdriver)
{
	int32_t err;

	host1x_nvdla_driver.subdevs = pdriver->driver.of_match_table;
	err = host1x_driver_register(&host1x_nvdla_driver);
	if (err < 0)
		return err;

	err = platform_driver_register(pdriver);
	if (err < 0)
		host1x_driver_unregister(&host1x_nvdla_driver);

	return err;
}

void nvdla_driver_unregister(struct platform_driver *pdriver)
{
	platform_driver_unregister(pdriver);
	host1x_driver_unregister(&host1x_nvdla_driver);
}
