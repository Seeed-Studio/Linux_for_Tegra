// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA device implementation as Host1x client.
 */

#include <nvidia/conftest.h>

#include "../nvdla_device.h"

#include "../../dla_queue.h"
#include "../../nvdla_debug.h"
#include "../fw/nvdla_falcon.h"

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/host1x-next.h>
#include <linux/nvhost.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/reset.h>

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
	unsigned int num_clks;

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

	err = devm_clk_bulk_get_all(&pdev->dev, &pdata->clks);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to get clocks %d\n", err);
		return err;
	}
	pdata->num_clks = err;
	num_clks = err;

	for (i = 0; i < num_clks; i++) {
		err = clk_set_rate(pdata->clks[i].clk, ULONG_MAX);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to set clock rate!\n");
			return err;
		}
	}

	pdata->reset_control = devm_reset_control_get_exclusive_released(&pdev->dev, NULL);
	if (IS_ERR(pdata->reset_control)) {
		dev_err(&pdev->dev, "failed to get reset\n");
		return PTR_ERR(pdata->reset_control);
	}

	err = reset_control_acquire(pdata->reset_control);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to acquire reset: %d\n", err);
		return err;
	}

	err = clk_bulk_prepare_enable(num_clks, pdata->clks);
	if (err < 0) {
		reset_control_release(pdata->reset_control);
		dev_err(&pdev->dev, "failed to enabled clocks: %d\n", err);
		return err;
	}

	reset_control_reset(pdata->reset_control);
	clk_bulk_disable_unprepare(num_clks, pdata->clks);
	reset_control_release(pdata->reset_control);

	if (pdata->autosuspend_delay) {
		pm_runtime_set_autosuspend_delay(&pdev->dev, pdata->autosuspend_delay);
		pm_runtime_use_autosuspend(&pdev->dev);
	}

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev))
		return -EOPNOTSUPP;

	pdata->debugfs = debugfs_create_dir(pdev->dev.of_node->name, NULL);

	err = nvdla_client_device_init(pdev);
	if (err) {
		nvdla_dbg_err(pdev, "Failed to client device (err: %x)", err);
		/* Clean up in case of error */
		pm_runtime_disable(&pdev->dev);
		debugfs_remove_recursive(pdata->debugfs);
		return err;
	}

	return 0;
}

void nvdla_module_deinit(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct falcon *falcon = pdata->falcon_data;

	nvdla_client_device_release(pdev);
	pm_runtime_disable(&pdev->dev);

	if (falcon) {
		dma_free_coherent(&pdev->dev, falcon->firmware.size,
			  falcon->firmware.virt, falcon->firmware.iova);
		falcon_exit(falcon);
	}

	debugfs_remove_recursive(pdata->debugfs);
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
	int err;

	err = pm_runtime_get_sync(&pdev->dev);
	if (err < 0) {
		pm_runtime_put_noidle(&pdev->dev);
		return err;
	}

	return 0;
}

void nvdla_module_idle(struct platform_device *pdev)
{
	nvdla_module_idle_mult(pdev, 1);
}

void nvdla_module_idle_mult(struct platform_device *pdev, int32_t refs)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	while (refs--) {
		pm_runtime_mark_last_busy(&pdev->dev);
		if (pdata->autosuspend_delay)
			pm_runtime_put_autosuspend(&pdev->dev);
		else
			pm_runtime_put(&pdev->dev);
	}
}

static void nvdla_module_load_regs(struct platform_device *pdev, bool prod)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvhost_gating_register *regs = pdata->engine_cg_regs;

	if (!regs)
		return;

	while (regs->addr) {
		if (prod) {
			void __iomem *addr = pdata->aperture[0] + regs->addr;

			writel(regs->prod, addr);
		} else {
			void __iomem *addr = pdata->aperture[0] + regs->addr;

			writel(regs->disable, addr);
		}
		regs++;
	}
}

void nvdla_module_reset(struct platform_device *pdev, bool reboot)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	int err;

	if (reboot)
		if (pdata->prepare_poweroff)
			pdata->prepare_poweroff(pdev);

	mutex_lock(&pdata->lock);
	err = reset_control_acquire(pdata->reset_control);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to acquire reset: %d\n", err);
	} else {
		reset_control_reset(pdata->reset_control);
		reset_control_release(pdata->reset_control);
	}
	mutex_unlock(&pdata->lock);

	if (reboot) {
		/* Load clockgating registers */
		nvdla_module_load_regs(pdev, pdata->engine_can_cg);

		/* ..and execute engine specific operations (i.e. boot) */
		if (pdata->finalize_poweron)
			pdata->finalize_poweron(pdev);
	}
}

/* Define our own runtime_suspend function for nvdla */
static int nvdla_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	int err;

	if (pdata->prepare_poweroff) {
		err = pdata->prepare_poweroff(pdev);
		if (err)
			return err;
	}

	clk_bulk_disable_unprepare(pdata->num_clks, pdata->clks);

	return 0;
}

/* Define our own runtime_resume function for nvdla */
static int nvdla_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	int err;

	err = clk_bulk_prepare_enable(pdata->num_clks, pdata->clks);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enabled clocks: %d\n", err);
		return err;
	}

	if (pdata->poweron_reset)
		nvdla_module_reset(pdev, false);

	/* Load clockgating registers */
	nvdla_module_load_regs(pdev, pdata->engine_can_cg);

	if (pdata->finalize_poweron)
		err = pdata->finalize_poweron(pdev);

	return err;
}

/* Define our own suspend function for nvdla */
static int nvdla_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	int err = 0;

	if (pdata->prepare_poweroff) {
		err = pdata->prepare_poweroff(pdev);
		if (err)
			return err;
	}

	clk_bulk_disable_unprepare(pdata->num_clks, pdata->clks);

	return 0;
}

/* Define our own resume function for nvdla */
static int nvdla_resume(struct device *dev)
{
	return nvdla_runtime_resume(dev);
}

/* Module runtime suspend implementation */
int nvdla_module_runtime_suspend(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla = pdata->private_data;
	int err;

	/* Call directly to our own runtime_suspend */
	err = nvdla_runtime_suspend(dev);
	if (!err && nvdla->icc_write) {
		err = icc_set_bw(nvdla->icc_write, 0, 0);
		if (err)
			dev_warn(&nvdla->pdev->dev,
				"failed to set icc_write bw: %d\n",
				err);
	}

	return err;
}

/* Module runtime resume implementation */
int nvdla_module_runtime_resume(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla = pdata->private_data;
	struct clk *clk = pdata->clks[0].clk;
	unsigned long rate;
	u32 emc_kbps;
	int err;

	/* Call directly to our own runtime_resume */
	err = nvdla_runtime_resume(dev);
	if (!err && nvdla->icc_write) {
		rate = clk_get_rate(clk);
		emc_kbps = rate * NVDLA_AXI_DBB_BW_BPC / 1024;
		err = icc_set_bw(nvdla->icc_write, kbps_to_icc(emc_kbps),
				0);
		if (err)
			dev_warn(&nvdla->pdev->dev,
				"failed to set icc_write bw: %d\n",
				err);
	}

	return err;
}

int nvdla_module_suspend(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	int err = 0;

	/* Call directly to our own suspend */
	err = nvdla_suspend(dev);
	if (err != 0) {
		dev_err(dev, "(FAIL) NVDLA suspend\n");
		goto fail_nvhost_module_suspend;
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

	/* Call directly to our own resume */
	err = nvdla_resume(dev);
	if (err != 0) {
		dev_err(dev, "(FAIL) NVDLA resume\n");
		goto fail_nvhost_module_resume;
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

	/* Drop runtime reference */
	pm_runtime_put_sync(dev);

	return 0;

fail_nvdla_queue_pool_prepare_suspend:
fail_already_in_suspend:
	return err;
}

void nvdla_module_complete_resume(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	/* Retake reference dropped in prepare */
	pm_runtime_get_noresume(dev);

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
