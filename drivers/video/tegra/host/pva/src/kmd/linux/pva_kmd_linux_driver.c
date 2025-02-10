// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
/* Auto-detected configuration depending kernel version */
#include <nvidia/conftest.h>

/* Linux headers */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/atomic.h>
#include <linux/nvhost.h>
#include <linux/version.h>
#include <linux/iommu.h>
#include <uapi/linux/tegra-soc-hwpm-uapi.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>

#if KERNEL_VERSION(5, 14, 0) > LINUX_VERSION_CODE
#include <linux/tegra-ivc.h>
#else
#include <soc/tegra/virt/hv-ivc.h>
#endif

/* PVA headers */
#include "pva_api.h"
#include "pva_kmd_device.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_linux_device.h"
#include "pva_kmd_debugfs.h"
#include "pva_kmd_silicon_hwpm.h"
#include "pva_kmd_pm.h"

#define PVA_KMD_LINUX_DRIVER_NAME "pva_kmd"
#if PVA_DEV_MAIN_COMPATIBLE == 1
#define PVA_KMD_LINUX_T23X_FIRMWARE_NAME "nvpva_020.fw"
#define PVA_KMD_LINUX_T26X_FIRMWARE_NAME "nvpva_030.fw"
#else
#define PVA_KMD_LINUX_T23X_FIRMWARE_NAME "pvafw_t23x.fw"
#define PVA_KMD_LINUX_T26X_FIRMWARE_NAME "pvafw_t26x.fw"
#endif

extern struct platform_driver pva_kmd_linux_smmu_context_driver;
extern atomic_t g_num_smmu_ctxs;
static bool load_from_gsc = PVA_KMD_LOAD_FROM_GSC_DEFAULT;
static bool app_authenticate = PVA_KMD_APP_AUTH_DEFAULT;

module_param(load_from_gsc, bool, 0);
MODULE_PARM_DESC(load_from_gsc, "Load V3 FW from GSC");

module_param(app_authenticate, bool, 0);
MODULE_PARM_DESC(app_authenticate, "Enable app authentication");

struct nvhost_device_data t23x_pva0_props = {
	.version = PVA_CHIP_T23X,
	.ctrl_ops = &tegra_pva_ctrl_ops,
	.class = NV_PVA0_CLASS_ID,
	/* We should not enable autosuspend here as this logic is handled in
	 * common code. When poweroff is called, common code expects PVA to be
	 * _really_ powered off. If we enable autosuspend, PVA will stay on for
	 * a while. */
	.autosuspend_delay = 0,
	.firmware_name = PVA_KMD_LINUX_T23X_FIRMWARE_NAME
};

struct nvhost_device_data t26x_pva0_props = {
	.version = PVA_CHIP_T26X,
	.ctrl_ops = &tegra_pva_ctrl_ops,
	.class = NV_PVA0_CLASS_ID,
	/* We should not enable autosuspend here as this logic is handled in
	 * common code. When poweroff is called, common code expects PVA to be
	 * _really_ powered off. If we enable autosuspend, PVA will stay on for
	 * a while. */
	.autosuspend_delay = 0,
	.firmware_name = PVA_KMD_LINUX_T26X_FIRMWARE_NAME
};

/* Map PVA-A and PVA-B to respective configuration items in nvhost */
static struct of_device_id tegra_pva_of_match[] = {
	{ .name = "pva0",
	  .compatible = "nvidia,tegra234-pva",
	  .data = (struct nvhost_device_data *)&t23x_pva0_props },
	{ .name = "pva0",
	  .compatible = "nvidia,tegra234-pva-hv",
	  .data = (struct nvhost_device_data *)&t23x_pva0_props },
	{ .name = "pva0",
	  .compatible = "nvidia,tegra264-pva",
	  .data = (struct nvhost_device_data *)&t26x_pva0_props },
	{},
};

MODULE_DEVICE_TABLE(of, tegra_pva_of_match);

static int pva_get_gsc_priv_hwid(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
	struct iommu_fwspec *fwspec = dev->iommu_fwspec;
#else
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
#endif
	if (!fwspec) {
		return -EINVAL;
	}
	return fwspec->ids[0] & 0xffff;
}

static void pva_kmd_linux_register_hwpm(struct pva_kmd_device *pva)
{
	struct tegra_soc_hwpm_ip_ops *hwpm_ip_ops =
		pva_kmd_zalloc(sizeof(*hwpm_ip_ops));

	hwpm_ip_ops->ip_dev = pva;
	hwpm_ip_ops->ip_base_address = safe_addu64(
		pva->reg_phy_base[0], (uint64_t)pva->regspec.cfg_perf_mon);
	hwpm_ip_ops->resource_enum = TEGRA_SOC_HWPM_RESOURCE_PVA;
	hwpm_ip_ops->hwpm_ip_pm = &pva_kmd_hwpm_ip_pm;
	hwpm_ip_ops->hwpm_ip_reg_op = &pva_kmd_hwpm_ip_reg_op;
	tegra_soc_hwpm_ip_register(hwpm_ip_ops);
	pva->debugfs_context.data_hwpm = hwpm_ip_ops;
}

static void pva_kmd_linux_unregister_hwpm(struct pva_kmd_device *pva)
{
	struct tegra_soc_hwpm_ip_ops *hwpm_ip_ops =
		(struct tegra_soc_hwpm_ip_ops *)pva->debugfs_context.data_hwpm;
	tegra_soc_hwpm_ip_unregister(hwpm_ip_ops);
	pva_kmd_free(hwpm_ip_ops);
}

static ssize_t clk_cap_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	struct nvhost_device_data *pdata =
		container_of(kobj, struct nvhost_device_data, clk_cap_kobj);
	/* i is indeed 'index' here after type conversion */
	int ret, i = attr - pdata->clk_cap_attrs;
	struct clk_bulk_data *clks = &pdata->clks[i];
	struct clk *clk = clks->clk;
	unsigned long freq_cap;
	long freq_cap_signed;

	ret = kstrtoul(buf, 0, &freq_cap);
	if (ret)
		return -EINVAL;
	/* Remove previous freq cap to get correct rounted rate for new cap */
	ret = clk_set_max_rate(clk, UINT_MAX);
	if (ret < 0)
		return ret;

	freq_cap_signed = clk_round_rate(clk, freq_cap);
	if (freq_cap_signed < 0)
		return -EINVAL;
	freq_cap = (unsigned long)freq_cap_signed;
	/* Apply new freq cap */
	ret = clk_set_max_rate(clk, freq_cap);
	if (ret < 0)
		return ret;

	/* Update the clock rate */
	clk_set_rate(clks->clk, freq_cap);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t clk_cap_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buf)
{
	struct nvhost_device_data *pdata =
		container_of(kobj, struct nvhost_device_data, clk_cap_kobj);
	/* i is indeed 'index' here after type conversion */
	int i = attr - pdata->clk_cap_attrs;
	struct clk_bulk_data *clks = &pdata->clks[i];
	struct clk *clk = clks->clk;
	long max_rate;

	max_rate = clk_round_rate(clk, UINT_MAX);
	if (max_rate < 0)
		return max_rate;

	return snprintf(buf, PAGE_SIZE, "%ld\n", max_rate);
}

static struct kobj_type nvpva_kobj_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
};

static int pva_probe(struct platform_device *pdev)
{
	int err = 0U;
	struct device *dev = &pdev->dev;
	struct pva_kmd_linux_device_data *pva_device_data;
	struct nvhost_device_data *pva_props;
	const struct of_device_id *device_id;
	struct pva_kmd_device *pva_device;
	struct kobj_attribute *attr = NULL;
	int j = 0;
	struct clk_bulk_data *clks;
	struct clk *c;

	device_id = of_match_device(tegra_pva_of_match, dev);
	if (!device_id) {
		dev_err(dev, "no match for pva dev\n");
		return -ENODATA;
	}

	pva_props = (struct nvhost_device_data *)device_id->data;
	WARN_ON(!pva_props);
	if (!pva_props) {
		dev_info(dev, "no platform data\n");
		return -ENODATA;
	}

	/* Create devices for child nodes of this device */
	of_platform_default_populate(dev->of_node, NULL, dev);

	/* Before probing PVA device, all of PVA's logical context devices
	 * must have been probed
	 */
	if (!pva_kmd_linux_smmu_contexts_initialized(pva_props->version)) {
		dev_warn(dev,
			 "nvpva cntxt was not initialized, deferring probe.");
		return -EPROBE_DEFER;
	}

	pva_props->pdev = pdev;
	mutex_init(&pva_props->lock);
	pva_device =
		pva_kmd_device_create(pva_props->version, 0, app_authenticate);

	pva_device->is_hv_mode = is_tegra_hypervisor_mode();

	/*Force to always boot from file in case of L4T*/
	if (!pva_device->is_hv_mode) {
		load_from_gsc = false;
	}

	pva_device->load_from_gsc = load_from_gsc;
	pva_device->stream_ids[pva_device->r5_image_smmu_context_id] =
		pva_get_gsc_priv_hwid(pdev);

	pva_props->private_data = pva_device;
	platform_set_drvdata(pdev, pva_props);

	/*
	 * pva_kmd_device_create allocates space for the platform data
	 * of this device. Update its property field to point to the platform
	 * data read using of_* APIs
	 */
	pva_device_data = pva_device->plat_data;
	pva_device_data->pva_device_properties = pva_props;

	/* Map MMIO range to kernel space */
	err = nvhost_client_device_get_resources(pdev);
	if (err < 0) {
		dev_err(dev, "nvhost_client_device_get_resources failed\n");
		goto err_get_resources;
	}

	/* Get clocks */
	err = nvhost_module_init(pdev);
	if (err < 0) {
		dev_err(dev, "nvhost_module_init failed\n");
		goto err_get_car;
	}

	/*
	 * Add this to nvhost device list, initialize scaling,
	 * setup memory management for the device, create dev nodes
	 */
	err = nvhost_client_device_init(pdev);
	if (err < 0) {
		dev_err(dev, "nvhost_client_device_init failed\n");
		goto err_cdev_init;
	}

	pva_kmd_linux_host1x_init(pva_device);

	pva_kmd_debugfs_create_nodes(pva_device);
	pva_kmd_linux_register_hwpm(pva_device);

	if (pva_props->num_clks > 0) {
		err = kobject_init_and_add(&pva_props->clk_cap_kobj,
					   &nvpva_kobj_ktype, &pdev->dev.kobj,
					   "%s", "clk_cap");
		if (err) {
			dev_err(dev, "Could not add dir 'clk_cap'\n");
			goto err_cdev_init;
		}

		pva_props->clk_cap_attrs = devm_kcalloc(
			dev, pva_props->num_clks, sizeof(*attr), GFP_KERNEL);
		if (!pva_props->clk_cap_attrs)
			goto err_cleanup_sysfs;

		for (j = 0; j < pva_props->num_clks; ++j) {
			clks = &pva_props->clks[j];
			c = clks->clk;
			if (!c)
				continue;

			attr = &pva_props->clk_cap_attrs[j];
			attr->attr.name = __clk_get_name(c);
			/* octal permission is preferred nowadays */
			attr->attr.mode = 0644;
			attr->show = clk_cap_show;
			attr->store = clk_cap_store;
			sysfs_attr_init(&attr->attr);
			if (sysfs_create_file(&pva_props->clk_cap_kobj,
					      &attr->attr)) {
				dev_err(dev,
					"Could not create sysfs attribute %s\n",
					__clk_get_name(c));
				err = -EIO;
				goto err_cleanup_sysfs;
			}
		}
	}
	/* return 0 as we would have jumped over this if an error was seen */
	return 0;

err_cleanup_sysfs:
	/* kobj of nvpva_kobj_ktype cleans up sysfs entries automatically */
	kobject_put(&pva_props->clk_cap_kobj);
err_cdev_init:
	nvhost_client_device_release(pdev);
err_get_car:
	nvhost_module_deinit(pdev);
err_get_resources:
	pva_kmd_device_destroy(pva_device);

	return err;
}

static int __exit pva_remove(struct platform_device *pdev)
{
	struct nvhost_device_data *pva_props = platform_get_drvdata(pdev);
	struct pva_kmd_device *pva_device = pva_props->private_data;
	struct kobj_attribute *attr = NULL;
	int i;

	if (pva_props->clk_cap_attrs) {
		for (i = 0; i < pva_props->num_clks; i++) {
			attr = &pva_props->clk_cap_attrs[i];
			sysfs_remove_file(&pva_props->clk_cap_kobj,
					  &attr->attr);
		}

		kobject_put(&pva_props->clk_cap_kobj);
	}

	nvhost_client_device_release(pdev);
	pva_kmd_debugfs_destroy_nodes(pva_device);
	pva_kmd_linux_unregister_hwpm(pva_device);
	nvhost_module_deinit(pdev);
	pva_kmd_device_destroy(pva_device);

	return 0;
}

static int pva_kmd_linux_device_runtime_resume(struct device *dev)
{
	int err;
	struct nvhost_device_data *props = dev_get_drvdata(dev);

	dev_info(dev, "PVA: Calling runtime resume");
	reset_control_acquire(props->reset_control);

	err = clk_bulk_prepare_enable(props->num_clks, props->clks);
	if (err < 0) {
		reset_control_release(props->reset_control);
		dev_err(dev, "failed to enabled clocks: %d\n", err);
		return err;
	}

	reset_control_reset(props->reset_control);
	reset_control_release(props->reset_control);

	return 0;
}

static int pva_kmd_linux_device_runtime_suspend(struct device *dev)
{
	struct nvhost_device_data *props = dev_get_drvdata(dev);

	dev_info(dev, "PVA: Calling runtime suspend");

	reset_control_acquire(props->reset_control);
	reset_control_assert(props->reset_control);

	clk_bulk_disable_unprepare(props->num_clks, props->clks);

	reset_control_release(props->reset_control);

	return 0;
}
#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void __exit pva_remove_wrapper(struct platform_device *pdev)
{
	pva_remove(pdev);
}
#else
static int __exit pva_remove_wrapper(struct platform_device *pdev)
{
	pva_remove(pdev);
	return 0;
}
#endif

static int pva_kmd_linux_device_resume(struct device *dev)
{
	enum pva_error status = PVA_SUCCESS;
	int err = 0;
	struct nvhost_device_data *props = dev_get_drvdata(dev);
	struct pva_kmd_device *pva_device = props->private_data;

	if (pva_device->is_suspended == false) {
		dev_warn(dev, "PVA is not in suspend state.\n");
		goto fail_not_in_suspend;
	}

	dev_info(dev, "PVA: Calling resume");
	err = pm_runtime_force_resume(dev);

	if (err != 0) {
		goto fail_runtime_resume;
	}

	if (pva_device->refcount != 0u) {
		status = pva_kmd_init_fw(pva_device);
	}

	if (status != PVA_SUCCESS) {
		err = -EINVAL;
		goto fail_init_fw;
	}

fail_init_fw:
fail_runtime_resume:
fail_not_in_suspend:
	return err;
}

static int pva_kmd_linux_device_suspend(struct device *dev)
{
	int err = 0;
	struct nvhost_device_data *props = dev_get_drvdata(dev);
	struct pva_kmd_device *pva_device = props->private_data;

	if (pva_device->refcount != 0u) {
		pva_kmd_deinit_fw(pva_device);
	}

	dev_info(dev, "PVA: Calling suspend");
	err = pm_runtime_force_suspend(dev);
	if (err != 0) {
		dev_err(dev, "(FAIL) PM suspend\n");
		goto fail_nvhost_module_suspend;
	}

	pva_device->is_suspended = true;

fail_nvhost_module_suspend:
	return err;
}

static int pva_kmd_linux_device_prepare_suspend(struct device *dev)
{
	struct nvhost_device_data *props = dev_get_drvdata(dev);
	struct pva_kmd_device *pva_device = props->private_data;
	enum pva_error status = PVA_SUCCESS;
	int err = 0;

	dev_info(dev, "PVA: Preparing to suspend");
	if (pva_device->is_suspended == true) {
		dev_info(dev, "PVA device already suspended");
		goto fail_already_in_suspend;
	}

	status = pva_kmd_prepare_suspend(pva_device);
	if (status != PVA_SUCCESS) {
		dev_info(dev, "PVA: Suspend FAIL");
		err = -EBUSY;
		goto fail;
	}

fail_already_in_suspend:
fail:
	return err;
}

static void pva_kmd_linux_device_complete_resume(struct device *dev)
{
	enum pva_error status = PVA_SUCCESS;
	struct nvhost_device_data *props = dev_get_drvdata(dev);
	struct pva_kmd_device *pva_device = props->private_data;

	dev_info(dev, "PVA: Completing resume");
	if (pva_device->is_suspended == false) {
		dev_info(dev, "PVA device not in suspend state");
		goto done;
	}

	status = pva_kmd_complete_resume(pva_device);
	if (status != PVA_SUCCESS) {
		dev_err(dev, "PVA: Resume failed");
		goto done;
	}

	dev_info(dev, "PVA: Resume complete");

done:
	pva_device->is_suspended = false;
	return;
}

static const struct dev_pm_ops pva_kmd_linux_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pva_kmd_linux_device_suspend,
				pva_kmd_linux_device_resume)
		SET_RUNTIME_PM_OPS(pva_kmd_linux_device_runtime_suspend,
				   pva_kmd_linux_device_runtime_resume, NULL)
			.prepare = pva_kmd_linux_device_prepare_suspend,
	.complete = pva_kmd_linux_device_complete_resume
};

static struct platform_driver pva_platform_driver = {
    .probe = pva_probe,
    .remove = pva_remove_wrapper,
    .driver = {
        .name = PVA_KMD_LINUX_DRIVER_NAME,
        .owner = THIS_MODULE,
#ifdef CONFIG_OF
	.of_match_table = tegra_pva_of_match,
#endif
	.pm	= &pva_kmd_linux_pm_ops,
    },
};

static struct host1x_driver host1x_nvpva_driver = {
	.driver = {
		.name = "host1x-nvpva",
	},
	.subdevs = tegra_pva_of_match,
};

static int __init nvpva_init(void)
{
	int err;

	atomic_set(&g_num_smmu_ctxs, 0);

	err = host1x_driver_register(&host1x_nvpva_driver);
	if (err < 0)
		goto err_out;

	err = platform_driver_register(&pva_kmd_linux_smmu_context_driver);
	if (err < 0)
		goto unreg_host1x_drv;

	err = platform_driver_register(&pva_platform_driver);
	if (err < 0)
		goto unreg_smmu_drv;

	printk(KERN_INFO "nvpva_init completed: %d. GSC boot: %d", err,
	       load_from_gsc);

	return err;

unreg_smmu_drv:
	platform_driver_unregister(&pva_kmd_linux_smmu_context_driver);
unreg_host1x_drv:
	host1x_driver_unregister(&host1x_nvpva_driver);
err_out:
	return err;
}

static void __exit nvpva_exit(void)
{
	platform_driver_unregister(&pva_platform_driver);
	platform_driver_unregister(&pva_kmd_linux_smmu_context_driver);
	host1x_driver_unregister(&host1x_nvpva_driver);
	printk(KERN_INFO "nvpva_exit completed");
}

module_init(nvpva_init);
module_exit(nvpva_exit);

#if defined(NV_MODULE_IMPORT_NS_CALLS_STRINGIFY)
MODULE_IMPORT_NS(DMA_BUF);
#else
MODULE_IMPORT_NS("DMA_BUF");
#endif
MODULE_LICENSE("GPL v2");
