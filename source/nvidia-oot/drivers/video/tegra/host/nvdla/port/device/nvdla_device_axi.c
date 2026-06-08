// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * NVDLA device implementation as AXI client.
 */

#include <nvidia/conftest.h>

#include "../nvdla_device.h"
#include "../nvdla_pm.h"

#include "../../dla_queue.h"
#include "../../nvdla_debug.h"

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/nvhost-emu-type.h>
#include <linux/host1x-dispatch.h>

#define NVDLA_NUM_CDEV 1

static uint32_t s_powerref;

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

static int32_t s_nvdla_module_get_platform_resources(
	struct platform_device *pdev)
{
	int32_t err;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	int32_t i;

	pdata->host1x = nvhost_get_host1x(pdev);
	if (pdata->host1x == NULL) {
		nvdla_dbg_err(pdev, "Failed to get private data\n");
		err = -ENODEV;
		goto fail;
	}

	/* Get resources. */
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
#if defined(BUG_5054810) && (BUG_5054810 == 1)
			nvdla_dbg_err(pdev, "Failed to map the resources. Continuing as WAR.\n");
			continue;
#else
			goto fail;
#endif
		}

		pdata->aperture[i] = regs;
	}

	return 0;

fail:
	return err;
}

static int32_t s_nvdla_module_pm_enable(struct platform_device *pdev)
{
	int32_t err;

	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	uint32_t i;

	if ((pdata->class == NV_DLA0_SIM_CLASS_ID) ||
		(pdata->class == NV_DLA1_SIM_CLASS_ID)) {
		nvdla_dbg_warn(pdev, "skipping PM for simulator\n");
		err = 0;
		goto fail;
	}

	err = devm_clk_bulk_get_all(&pdev->dev, &pdata->clks);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get clocks %d\n", err);
		goto fail;
	}
	pdata->num_clks = err;

	for (i = 0; i < pdata->num_clks; i++) {
		err = clk_set_rate(pdata->clks[i].clk, ULONG_MAX);
		if (err < 0) {
			nvdla_dbg_err(pdev, "failed to set clock rate!\n");
			goto fail;
		}
	}


	pdata->reset_control =
		devm_reset_control_get_optional_exclusive_released(
				&pdev->dev, NULL);
	if (!pdata->reset_control)
		nvdla_dbg_warn(pdev, "No reset controller.\n");

	if (pdata->reset_control && pdata->num_clks > 0U) {
		err = reset_control_acquire(pdata->reset_control);
		if (err < 0) {
			nvdla_dbg_err(pdev, "failed to acquire reset: %d\n", err);
			goto fail;
		}

		err = clk_bulk_prepare_enable(pdata->num_clks, pdata->clks);
		if (err < 0) {
			reset_control_release(pdata->reset_control);
			nvdla_dbg_err(pdev, "failed to enable clocks: %d\n", err);
			goto fail;
		}

		reset_control_reset(pdata->reset_control);
		clk_bulk_disable_unprepare(pdata->num_clks, pdata->clks);
		reset_control_release(pdata->reset_control);
	}

	if (pdata->autosuspend_delay) {
		pm_runtime_set_autosuspend_delay(&pdev->dev,
			pdata->autosuspend_delay);
		pm_runtime_use_autosuspend(&pdev->dev);
	}

	/* Enable the power module. */
	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		nvdla_dbg_err(pdev, "failed to enable pm_runtime\n");
		err = -EOPNOTSUPP;
		goto fail;
	}

	return 0;

fail:
	return err;
}

static void s_nvdla_module_pm_disable(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if ((pdata->class == NV_DLA0_SIM_CLASS_ID) ||
		(pdata->class == NV_DLA1_SIM_CLASS_ID)) {
		nvdla_dbg_warn(pdev, "skipping PM for simulator\n");
		goto fail;
	}

	pm_runtime_disable(&pdev->dev);

fail:
	return;
}

static int32_t s_nvdla_module_device_create(struct platform_device *pdev)
{
	int32_t err;

	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct device *dev;
	struct class *dla_class;
	dev_t devno;

	err = alloc_chrdev_region(&devno, 0, NVDLA_NUM_CDEV, "nvdla");
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to reserve chrdev region\n");
		goto fail;
	}

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	dla_class = class_create(pdata->devfs_name);
#else
	dla_class = class_create(THIS_MODULE, pdata->devfs_name);
#endif
	if (IS_ERR(dla_class)) {
		nvdla_dbg_err(pdev, "failed to create class\n");
		err = PTR_ERR(dla_class);
		goto unregister_chrdev_region;
	}

	cdev_init(&pdata->ctrl_cdev, pdata->ctrl_ops);
	pdata->ctrl_cdev.owner = THIS_MODULE;

	err = cdev_add(&pdata->ctrl_cdev, devno, 1);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to add cdev\n");
		goto destroy_host1x_class;
	}

	dev = device_create(dla_class,
			&pdev->dev,
			devno,
			NULL,
			"nvhost-ctrl-%s",
			pdata->devfs_name);
	if (IS_ERR(dev)) {
		nvdla_dbg_err(pdev, "failed to create nvhost-ctrl-%s device\n",
			pdata->devfs_name);
		err = PTR_ERR(dev);
		goto delete_cdev;
	}

	pdata->cdev_region = devno;
	pdata->ctrl_node = dev;
	pdata->nvhost_class = dla_class;

	return 0;

delete_cdev:
	cdev_del(&pdata->ctrl_cdev);
destroy_host1x_class:
	class_destroy(dla_class);
unregister_chrdev_region:
	unregister_chrdev_region(devno, NVDLA_NUM_CDEV);
fail:
	return err;
}

static void s_nvdla_module_device_destroy(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(pdata->ctrl_node)) {
		device_destroy(pdata->nvhost_class, pdata->ctrl_cdev.dev);
		cdev_del(&pdata->ctrl_cdev);
		class_destroy(pdata->nvhost_class);
	}
	unregister_chrdev_region(pdata->cdev_region, NVDLA_NUM_CDEV);
}

int32_t nvdla_module_init(struct platform_device *pdev)
{
	int32_t err;

	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	err = s_nvdla_module_get_platform_resources(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get resources\n");
		goto fail;
	}

	err = s_nvdla_module_pm_enable(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to enable pm\n");
		goto fail;
	}

	err = s_nvdla_module_device_create(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to create device nodes\n");
		goto disable_pm;
	}

	if (pdev->num_resources > 1U) {
		err = nvdla_pm_init(pdev);
		if (err < 0) {
			nvdla_dbg_err(pdev, "failed to init pm. err %d\n", err);
			goto destroy_device;
		}
	}

	pdata->debugfs = debugfs_create_dir(pdata->devfs_name, NULL);

	return 0;

destroy_device:
	s_nvdla_module_device_destroy(pdev);
disable_pm:
	s_nvdla_module_pm_disable(pdev);
fail:
	return err;
}

void nvdla_module_deinit(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if ((pdata != NULL) && (pdata->debugfs != NULL))
		debugfs_remove_recursive(pdata->debugfs);

	nvdla_pm_deinit(pdev);
	s_nvdla_module_device_destroy(pdev);
	s_nvdla_module_pm_disable(pdev);
}

int32_t nvdla_module_client_register(struct platform_device *pdev,
	void *context)
{
	(void) pdev;
	(void) context;

	return 0;
}

void nvdla_module_client_unregister(struct platform_device *pdev,
	void *context)
{
	(void) pdev;
	(void) context;
}


static void nvdla_module_load_regs(struct platform_device *pdev, bool prod);

int32_t nvdla_module_busy(struct platform_device *pdev)
{
	int32_t err;

	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if ((pdata->class == NV_DLA0_SIM_CLASS_ID) ||
		(pdata->class == NV_DLA1_SIM_CLASS_ID)) {
		err = 0;

		nvdla_dbg_warn(pdev, "skipping PM for simulator\n");

		nvdla_module_load_regs(pdev, pdata->engine_can_cg);

		s_powerref++;
		if (pdata->finalize_poweron && (s_powerref == 1U))
			err = pdata->finalize_poweron(pdev);

		goto fail;
	}

#if defined(NVDLA_HAVE_CONFIG_FWSUSPEND) && (NVDLA_HAVE_CONFIG_FWSUSPEND == 1)
	if (atomic_read(&pdev->dev.power.usage_count) == 0) {
		/* Make sure that to hold reference for FW driven autosuspend */
		err = pm_runtime_get_sync(&pdev->dev);
		if (err < 0) {
			nvdla_dbg_err(pdev, "failed to get ref + resume (err=%d)\n",
					err);
			pm_runtime_put_noidle(&pdev->dev);
			goto fail;
		}
	}
#endif /* NVDLA_HAVE_CONFIG_FWSUSPEND */

	err = pm_runtime_get_sync(&pdev->dev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get ref + resume (err=%d)\n",
				err);
		pm_runtime_put_noidle(&pdev->dev);
		goto fail;
	}

	return 0;

fail:
	return err;
}

void nvdla_module_idle(struct platform_device *pdev)
{
	nvdla_module_idle_mult(pdev, 1);
}

void nvdla_module_idle_mult(struct platform_device *pdev, int32_t refs)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if (refs == 0)
		goto fail;

	if ((pdata->class == NV_DLA0_SIM_CLASS_ID) ||
		(pdata->class == NV_DLA1_SIM_CLASS_ID)) {
		nvdla_dbg_warn(pdev, "skipping PM for simulator\n");
		if (s_powerref >= refs)
			s_powerref -= refs;
		else
			s_powerref = 0U;

		if (pdata->prepare_poweroff && (s_powerref == 0U))
			pdata->prepare_poweroff(pdev);

		goto fail;
	}

	while (refs--) {
		pm_runtime_mark_last_busy(&pdev->dev);
		if (pdata->autosuspend_delay)
			pm_runtime_put_autosuspend(&pdev->dev);
		else
			pm_runtime_put(&pdev->dev);
	}

fail:
	return;
}

static void nvdla_module_load_regs(struct platform_device *pdev, bool prod)
{
	(void) pdev;
	(void) prod;
}

void nvdla_module_reset(struct platform_device *pdev, bool reboot)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if (reboot)
		if (pdata->prepare_poweroff)
			pdata->prepare_poweroff(pdev);

	if (pdata->reset_control) {
		int err;

		mutex_lock(&pdata->lock);
		err = reset_control_acquire(pdata->reset_control);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to acquire reset: %d\n",
				err);
		} else {
			reset_control_reset(pdata->reset_control);
			reset_control_release(pdata->reset_control);
		}
		mutex_unlock(&pdata->lock);
	}

	if (reboot) {
		/* Load clockgating registers */
		nvdla_module_load_regs(pdev, pdata->engine_can_cg);

		if (pdata->finalize_poweron)
			pdata->finalize_poweron(pdev);
	}
}

int nvdla_module_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	int err = 0;

	if (pdata->prepare_poweroff) {
		err = pdata->prepare_poweroff(pdev);
		if (err) {
			nvdla_dbg_err(pdev, "failed to poweroff %d\n", err);
			goto fail;
		}
	}

	if ((pdata->class != NV_DLA0_SIM_CLASS_ID) &&
		(pdata->class != NV_DLA1_SIM_CLASS_ID))
		clk_bulk_disable_unprepare(pdata->num_clks, pdata->clks);

	return 0;

fail:
	return err;
}

int nvdla_module_runtime_resume(struct device *dev)
{
	int err = 0;

	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);

	if ((pdata->class != NV_DLA0_SIM_CLASS_ID) &&
		(pdata->class != NV_DLA1_SIM_CLASS_ID)) {
		err = clk_bulk_prepare_enable(pdata->num_clks, pdata->clks);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to enabled clocks: %d\n", err);
			goto fail;
		}
	}

	if (pdata->poweron_reset)
		nvdla_module_reset(pdev, false);

	/* Load clockgating registers */
	nvdla_module_load_regs(pdev, pdata->engine_can_cg);

	if (pdata->finalize_poweron) {
		err = pdata->finalize_poweron(pdev);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Failed to power-on, err=%d\n",
				err);
			goto fail;
		}
	}

	return 0;

fail:
	return err;
}

int nvdla_module_suspend(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	int err = 0;

	err = pm_runtime_force_suspend(dev);
	if (err != 0) {
		dev_err(dev, "(FAIL) PM suspend\n");
		goto fail_nvhost_module_suspend;
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

	err = pm_runtime_force_resume(dev);
	if (err != 0) {
		dev_err(dev, "(FAIL) PM resume\n");
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

	/* If we took an extra reference, drop it now to prevent
	 * the device from automatically resuming upon system
	 * resume.
	 */
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

	/* Retake reference dropped above */
	pm_runtime_get_noresume(dev);

	/* Module is no longer in suspend and has resumed successfully */
	nvdla_dev->is_suspended = false;
}

int32_t nvdla_driver_register(struct platform_driver *pdriver)
{
	return platform_driver_register(pdriver);
}

void nvdla_driver_unregister(struct platform_driver *pdriver)
{
	platform_driver_unregister(pdriver);
}
