// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include <linux/version.h>
#include <linux/iommu.h>
#include <uapi/linux/tegra-soc-hwpm-uapi.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/dma-mapping.h>
#include <soc/tegra/fuse-helper.h>

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
#include "pva_kmd_linux_device_api.h"

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
static bool pva_test_mode; //false by default

module_param(load_from_gsc, bool, 0);
MODULE_PARM_DESC(load_from_gsc, "Load V3 FW from GSC");

#if SYSTEM_TESTS_ENABLED == 1
module_param(pva_test_mode, bool, 0);
MODULE_PARM_DESC(pva_test_mode, "Enable test mode");
#endif

struct nvpva_device_data t23x_pva0_props = {
	.version = PVA_CHIP_T23X,
	.ctrl_ops = &tegra_pva_ctrl_ops,
	.class = NV_PVA0_CLASS_ID,
	.autosuspend_delay = 500,
	.firmware_name = PVA_KMD_LINUX_T23X_FIRMWARE_NAME
};

struct nvpva_device_data t26x_pva0_props = {
	.version = PVA_CHIP_T26X,
	.ctrl_ops = &tegra_pva_ctrl_ops,
	.class = NV_PVA0_CLASS_ID,
	.autosuspend_delay = 500,
	.firmware_name = PVA_KMD_LINUX_T26X_FIRMWARE_NAME
};

/* Map PVA-A and PVA-B to respective configuration items in nvhost */
static struct of_device_id tegra_pva_of_match[] = {
	{ .name = "pva0",
	  .compatible = "nvidia,tegra234-pva",
	  .data = (struct nvpva_device_data *)&t23x_pva0_props },
	{ .name = "pva0",
	  .compatible = "nvidia,tegra234-pva-hv",
	  .data = (struct nvpva_device_data *)&t23x_pva0_props },
	{ .name = "pva0",
	  .compatible = "nvidia,tegra264-pva",
	  .data = (struct nvpva_device_data *)&t26x_pva0_props },
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

static int pva_kmd_linux_register_hwpm(struct pva_kmd_device *pva)
{
	struct tegra_soc_hwpm_ip_ops *hwpm_ip_ops =
		pva_kmd_zalloc(sizeof(*hwpm_ip_ops));

	if (hwpm_ip_ops == NULL) {
		return -ENOMEM;
	}

	hwpm_ip_ops->ip_dev = pva;
	hwpm_ip_ops->ip_base_address = safe_addu64(
		pva->reg_phy_base[0], (uint64_t)pva->regspec.cfg_perf_mon);
	hwpm_ip_ops->resource_enum = TEGRA_SOC_HWPM_RESOURCE_PVA;
	hwpm_ip_ops->hwpm_ip_pm = &pva_kmd_hwpm_ip_pm;
	hwpm_ip_ops->hwpm_ip_reg_op = &pva_kmd_hwpm_ip_reg_op;
	tegra_soc_hwpm_ip_register(hwpm_ip_ops);
	pva->debugfs_context.data_hwpm = hwpm_ip_ops;
	return 0;
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
	struct nvpva_device_data *pdata =
		container_of(kobj, struct nvpva_device_data, clk_cap_kobj);
	/* i is indeed 'index' here after type conversion */
	int ret, i = attr - pdata->clk_cap_attrs;
	struct clk_bulk_data *clks = &pdata->clks[i];
	struct clk *clk = clks->clk;
	unsigned long freq_cap;
	long freq_cap_signed;

	ret = kstrtoul(buf, 0, &freq_cap);
	if (ret)
		return -EINVAL;

	/* Remove previous freq cap to get correct rounded rate for new cap */
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
	ret = clk_set_rate(clks->clk, freq_cap);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t clk_cap_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buf)
{
	struct nvpva_device_data *pdata =
		container_of(kobj, struct nvpva_device_data, clk_cap_kobj);
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

static enum pva_error pva_kmd_get_co_info(struct platform_device *pdev)
{
	struct device_node *np;
	const char *status = NULL;
	uint32_t reg[4] = { 0 };
	enum pva_error err = PVA_SUCCESS;
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);
	struct pva_kmd_device *pva = pdata->pva_kmd_dev;

	np = of_find_compatible_node(NULL, NULL, "nvidia,pva-carveout");
	if (np == NULL) {
		dev_err(&pdev->dev, "find node failed\n");
		goto err_out;
	}

	if (of_property_read_string(np, "status", &status)) {
		dev_err(&pdev->dev, "read status failed\n");
		goto err_out;
	}

	if (strcmp(status, "okay")) {
		dev_err(&pdev->dev, "status compare failed\n");
		goto err_out;
	}

	if (of_property_read_u32_array(np, "reg", reg, 4)) {
		dev_err(&pdev->dev, "read_32_array failed\n");
		goto err_out;
	}

	pva->fw_carveout.base_pa = ((u64)reg[0] << 32 | (u64)reg[1]);
	pva->fw_carveout.size = ((u64)reg[2] << 32 | (u64)reg[3]);

	if (iommu_get_domain_for_dev(&pdev->dev)) {
		pva->fw_carveout.base_va =
			dma_map_resource(&pdev->dev, pva->fw_carveout.base_pa,
					 pva->fw_carveout.size,
					 DMA_BIDIRECTIONAL,
					 DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(&pdev->dev, pva->fw_carveout.base_va)) {
			dev_err(&pdev->dev, "Failed to pin fw_bin_mem CO\n");
			goto err_out;
		}
	} else {
		pva->fw_carveout.base_va = pva->fw_carveout.base_pa;
	}

	printk(KERN_INFO "Allocated pva->fw_carveout\n");
	return err;

err_out:
	dev_err(&pdev->dev, "get co fail\n");
	return PVA_INVAL;
}

static void pva_kmd_free_co_mem(struct platform_device *pdev)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);
	struct pva_kmd_device *pva = pdata->pva_kmd_dev;

	if (iommu_get_domain_for_dev(&pdev->dev)) {
		dma_unmap_resource(&pdev->dev, pva->fw_carveout.base_va,
				   pva->fw_carveout.size, DMA_BIDIRECTIONAL,
				   DMA_ATTR_SKIP_CPU_SYNC);
	}
}

static bool pva_kmd_in_test_mode(struct device *dev, bool param_test_mode)
{
#if SYSTEM_TESTS_ENABLED == 1
	const char *dt_test_mode = NULL;

	if (!tegra_platform_is_silicon())
		return true;

	if (of_property_read_string(dev->of_node, "nvidia,test_mode_enable",
				    &dt_test_mode)) {
		return param_test_mode;
	}

	if (strcmp(dt_test_mode, "true")) {
		return param_test_mode;
	}

	return true;
#else
	return false;
#endif
}

static struct kobj_type nvpva_kobj_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
};

/**
 * Read VPU authentication property from device tree
 *
 * @param dev Pointer to the device structure
 * @return true if authentication should be enabled, false otherwise
 */
static bool pva_kmd_linux_read_vpu_auth(const struct device *dev)
{
	bool auth_enabled = false;
	int len;
	const __be32 *val;

	val = of_get_property(dev->of_node, "nvidia,vpu-auth", &len);
	if ((val != NULL) && (len >= (int)sizeof(__be32))) {
		u32 value = (u32)be32_to_cpu(*val);
		if (value != 0U) {
			auth_enabled = true;
			dev_dbg(dev, "VPU authentication enabled\n");
		} else {
			auth_enabled = false;
			dev_dbg(dev, "VPU authentication disabled\n");
		}
	} else {
		dev_dbg(dev,
			"No VPU authentication property found, using default: %d\n",
			auth_enabled);
	}

	return auth_enabled;
}

/**
 * @brief Optimized PVA device probe function with unified device management
 *
 * @param pdev Platform device being probed
 * @return 0 on success, negative error code on failure
 */
static int pva_probe(struct platform_device *pdev)
{
	int err = 0;
	struct device *dev = &pdev->dev;
	struct nvpva_device_data *pdata;
	const struct of_device_id *device_id;
	const struct nvpva_device_data *device_props_template;
	struct pva_kmd_device *pva_device;
	struct kobj_attribute *attr = NULL;
	int j = 0;
	struct clk_bulk_data *clks;
	struct clk *c;
	bool pva_enter_test_mode = false;
	bool app_authenticate;

	/* Match device tree entry */
	device_id = of_match_device(tegra_pva_of_match, dev);
	if (!device_id) {
		dev_err(dev, "no match for pva dev\n");
		return -ENODATA;
	}

	device_props_template =
		(const struct nvpva_device_data *)device_id->data;
	WARN_ON(!device_props_template);
	if (!device_props_template) {
		dev_info(dev, "no platform data\n");
		return -ENODATA;
	}

	app_authenticate = pva_kmd_linux_read_vpu_auth(dev);

	/* Create devices for child nodes of this device */
	of_platform_default_populate(dev->of_node, NULL, dev);

	/* Before probing PVA device, all of PVA's logical context devices
	 * must have been probed */
	if (!pva_kmd_linux_smmu_contexts_initialized(
		    device_props_template->version)) {
		dev_warn(dev,
			 "nvpva cntxt was not initialized, deferring probe.");
		return -EPROBE_DEFER;
	}

	/* Allocate unified device data structure */
	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "Failed to allocate device data\n");
		return -ENOMEM;
	}

	/* Initialize device data from template */
	pdata->pdev = pdev;
	pdata->version = device_props_template->version;
	pdata->class = device_props_template->class;
	pdata->ctrl_ops = device_props_template->ctrl_ops;
	pdata->autosuspend_delay = device_props_template->autosuspend_delay;
	pdata->firmware_name = device_props_template->firmware_name;
	mutex_init(&pdata->lock);

	/* Set as platform driver data early for use by helper functions */
	platform_set_drvdata(pdev, pdata);

	/* Create common KMD device, passing platform data for early linking */
	pva_enter_test_mode = pva_kmd_in_test_mode(dev, pva_test_mode);
	pva_device = pva_kmd_device_create(pdata->version, 0, app_authenticate,
					   pva_enter_test_mode, pdata);
	if (!pva_device) {
		dev_err(dev, "Failed to create PVA KMD device\n");
		err = -ENOMEM;
		goto err_create_device;
	}

	/* Store back-reference to common device */
	pdata->pva_kmd_dev = pva_device;

	/* Configure device properties */
	pva_device->is_hv_mode = is_tegra_hypervisor_mode();
	pva_device->is_silicon = tegra_platform_is_silicon();
	pva_device->load_from_gsc =
		pva_device->is_silicon ? load_from_gsc : false;
	pva_device->stream_ids[pva_device->r5_image_smmu_context_id] =
		pva_get_gsc_priv_hwid(pdev);

	/* Map MMIO ranges to kernel space */
	err = nvpva_device_get_resources(pdev);
	if (err < 0) {
		dev_err(dev, "nvpva_device_get_resources failed\n");
		goto err_get_resources;
	}

	/* Initialize clocks and power management */
	err = nvpva_module_init(pdev);
	if (err < 0) {
		dev_err(dev, "nvpva_module_init failed\n");
		goto err_get_car;
	}

	/* Initialize character device nodes */
	err = nvpva_device_init(pdev);
	if (err < 0) {
		dev_err(dev, "nvpva_device_init failed\n");
		goto err_cdev_init;
	}

	/* Initialize host1x syncpoint integration */
	err = pva_kmd_linux_host1x_init(pva_device);
	if (err < 0) {
		dev_err(dev, "pva_kmd_linux_host1x_init failed\n");
		goto err_host1x_init;
	}

	/* Create debugfs nodes */
	err = pva_kmd_debugfs_create_nodes(pva_device);
	if (err != PVA_SUCCESS) {
		dev_err(dev, "debugfs creation failed\n");
		goto err_debugfs;
	}

	/* Register with HWPM (Hardware Performance Monitor) */
	err = pva_kmd_linux_register_hwpm(pva_device);
	if (err != PVA_SUCCESS) {
		dev_err(dev, "pva_kmd_linux_register_hwpm failed\n");
		goto err_hwpm;
	}

	/* Get carveout info if loading from GSC */
	if (!pva_device->is_hv_mode && pva_device->load_from_gsc) {
		err = pva_kmd_get_co_info(pdev);
		if (err != PVA_SUCCESS) {
			dev_err(dev, "Failed to get CO info\n");
			goto err_co_info;
		}
	}

	/* Create clock cap sysfs entries */
	if (pdata->num_clks > 0) {
		err = kobject_init_and_add(&pdata->clk_cap_kobj,
					   &nvpva_kobj_ktype, &pdev->dev.kobj,
					   "%s", "clk_cap");
		if (err) {
			dev_err(dev, "Could not add dir 'clk_cap'\n");
			goto err_sysfs_init;
		}

		pdata->clk_cap_attrs = devm_kcalloc(dev, pdata->num_clks,
						    sizeof(*attr), GFP_KERNEL);
		if (!pdata->clk_cap_attrs) {
			err = -ENOMEM;
			goto err_cleanup_sysfs;
		}

		for (j = 0; j < pdata->num_clks; ++j) {
			clks = &pdata->clks[j];
			c = clks->clk;
			if (!c)
				continue;

			attr = &pdata->clk_cap_attrs[j];
			attr->attr.name = __clk_get_name(c);
			attr->attr.mode = 0644;
			attr->show = clk_cap_show;
			attr->store = clk_cap_store;
			sysfs_attr_init(&attr->attr);

			err = sysfs_create_file(&pdata->clk_cap_kobj,
						&attr->attr);
			if (err) {
				dev_err(dev,
					"Could not create sysfs attribute %s\n",
					__clk_get_name(c));
				goto err_cleanup_sysfs;
			}
		}
	}

	dev_info(dev, "PVA probe completed successfully\n");
	return 0;

	/* Error handling with proper cleanup order */
err_cleanup_sysfs:
	kobject_put(&pdata->clk_cap_kobj);
err_sysfs_init:
	if (!pva_device->is_hv_mode && pva_device->load_from_gsc)
		pva_kmd_free_co_mem(pdev);
err_co_info:
	pva_kmd_linux_unregister_hwpm(pva_device);
err_hwpm:
	pva_kmd_debugfs_destroy_nodes(pva_device);
err_debugfs:
	pva_kmd_linux_host1x_deinit(pva_device);
err_host1x_init:
	nvpva_device_release(pdev);
err_cdev_init:
	nvpva_module_deinit(pdev);
err_get_car:
err_get_resources:
	pva_kmd_device_destroy(pva_device);
err_create_device:
	platform_set_drvdata(pdev, NULL);
	return err;
}

/**
 * @brief Remove PVA device with proper cleanup
 *
 * @details Performs orderly cleanup of all resources in reverse order of
 * initialization using the unified device structure
 *
 * @param pdev Platform device being removed
 * @return 0 on success
 */
static int __exit pva_remove(struct platform_device *pdev)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);
	struct pva_kmd_device *pva_device = pdata->pva_kmd_dev;
	struct kobj_attribute *attr = NULL;
	int i;

	/* Make sure PVA is powered off here by disabling auto suspend */
	pm_runtime_dont_use_autosuspend(&pdev->dev);

	/* At this point, PVA should be suspended in L4T. However, for AV+L,
	 * PVA will still be powered on since the system took an additional
	 * reference count. We need to temporary drop it to suspend and then
	 * restore the reference count. */
	if (pm_runtime_active(&pdev->dev)) {
		pm_runtime_put_sync(&pdev->dev);
		pm_runtime_get_noresume(&pdev->dev);
	}

	/* Clean up clock cap sysfs entries */
	if (pdata->clk_cap_attrs) {
		for (i = 0; i < pdata->num_clks; i++) {
			attr = &pdata->clk_cap_attrs[i];
			sysfs_remove_file(&pdata->clk_cap_kobj, &attr->attr);
		}
		kobject_put(&pdata->clk_cap_kobj);
	}

	/* Free carveout memory if applicable */
	if (!pva_device->is_hv_mode && pva_device->load_from_gsc)
		pva_kmd_free_co_mem(pdev);

	/* Cleanup in reverse order of initialization */
	pva_kmd_linux_unregister_hwpm(pva_device);
	pva_kmd_debugfs_destroy_nodes(pva_device);
	pva_kmd_linux_host1x_deinit(pva_device);
	nvpva_device_release(pdev);
	nvpva_module_deinit(pdev);
	pva_kmd_device_destroy(pva_device);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int runtime_resume(struct device *dev)
{
	int err;
	struct nvpva_device_data *pdata = dev_get_drvdata(dev);
	struct pva_kmd_device *pva = pdata->pva_kmd_dev;
	enum pva_error pva_err = PVA_SUCCESS;

	dev_info(dev, "Start runtime resume");

	err = clk_bulk_prepare_enable(pdata->num_clks, pdata->clks);
	if (err < 0) {
		dev_err(dev, "Runtime resume failed to enable clocks: %d\n",
			err);
		goto err_out;
	}

	err = reset_control_acquire(pdata->reset_control);
	if (err < 0) {
		dev_err(dev, "Runtime resume failed to acquire reset: %d\n",
			err);
		goto disable_clocks;
	}
	reset_control_reset(pdata->reset_control);
	reset_control_release(pdata->reset_control);

	pva_err = pva_kmd_init_fw(pva);
	if (pva_err != PVA_SUCCESS) {
		err = -EIO;
		dev_info(dev, "Runtime resume failed to init fw");
		goto disable_clocks;
	}

	dev_info(dev, "Runtime resume succeeded");
	return 0;

disable_clocks:
	clk_bulk_disable_unprepare(pdata->num_clks, pdata->clks);
err_out:
	return err;
}

static int runtime_suspend(struct device *dev)
{
	struct nvpva_device_data *pdata = dev_get_drvdata(dev);
	struct pva_kmd_device *pva = pdata->pva_kmd_dev;
	enum pva_error pva_err = PVA_SUCCESS;

	dev_info(dev, "Start runtime suspend");

	pva_err = pva_kmd_deinit_fw(pva);
	if (pva_err != PVA_SUCCESS) {
		/* These might be errors if PVA is aborted. It's safe to ignore them. */
		dev_err(dev, "Failed to deinit firmware");
	}

	clk_bulk_disable_unprepare(pdata->num_clks, pdata->clks);

	dev_info(dev, "Runtime suspend complete");
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

static int system_resume(struct device *dev)
{
	int err = 0;
	struct nvpva_device_data *pdata = dev_get_drvdata(dev);
	struct pva_kmd_device *pva_device = pdata->pva_kmd_dev;
	enum pva_error pva_err = PVA_SUCCESS;

	dev_info(dev, "System resume");
	err = pm_runtime_force_resume(dev);
	if (err != 0) {
		dev_err(dev, "Force resume failed");
		goto out;
	}

	/* Even after force resume, the PVA may still be powered off if the usage count is 0.
	 * Therefore, we need to skip restoring firmware state in this case.
	 */
	if (!pm_runtime_active(dev)) {
		dev_info(dev, "No active PVA users. Skipping resume.");
		goto out;
	}

	pva_err = pva_kmd_complete_resume(pva_device);
	if (pva_err != PVA_SUCCESS) {
		dev_err(dev, "Complete resume failed");
		err = -EIO;
		goto out;
	}

out:
	dev_info(dev, "Resume from system suspend completed: %d\n", err);
	return err;
}

static int system_suspend(struct device *dev)
{
	int err = 0;
	struct nvpva_device_data *pdata = dev_get_drvdata(dev);
	struct pva_kmd_device *pva_device = pdata->pva_kmd_dev;
	enum pva_error pva_err = PVA_SUCCESS;

	dev_info(dev, "System suspend");

	/* Synchronize with runtime suspend/resume calls */
	pm_runtime_barrier(dev);

	/* Now it's safe to check runtime status */
	if (!pm_runtime_active(dev)) {
		dev_info(
			dev,
			"PVA is powered off. Nothing to do for system suspend.");
		goto out;
	}

	pva_err = pva_kmd_prepare_suspend(pva_device);
	if (pva_err != PVA_SUCCESS) {
		dev_err(dev, "Prepare system suspend failed");
		err = -EBUSY;
		goto out;
	}

	err = pm_runtime_force_suspend(dev);
	if (err != 0) {
		dev_err(dev, "Force suspend failed");
		goto out;
	}

out:
	return err;
}

enum pva_error pva_kmd_simulate_enter_sc7(struct pva_kmd_device *pva)
{
	struct nvpva_device_data *pdata = pva_kmd_linux_device_get_data(pva);
	struct device *dev = &pdata->pdev->dev;
	int ret;

	/* The PM core increases the device usage count before calling prepare, so
	 * we need to emulate this behavior as well. */
	pm_runtime_get_noresume(dev);

	ret = system_suspend(dev);
	if (ret != 0) {
		pva_kmd_log_err("SC7 simulation: suspend failed");
		return PVA_INTERNAL;
	}

	return PVA_SUCCESS;
}

enum pva_error pva_kmd_simulate_exit_sc7(struct pva_kmd_device *pva)
{
	struct nvpva_device_data *pdata = pva_kmd_linux_device_get_data(pva);
	struct device *dev = &pdata->pdev->dev;
	int ret;

	dev_info(dev, "SC7 simulation: resume");

	ret = system_resume(dev);
	if (ret != 0) {
		pva_kmd_log_err("SC7 simulation: resume failed");
		return PVA_INTERNAL;
	}

	/* The PM core decreases the device usage count after calling complete, so
	 * we need to emulate this behavior as well. */
	pm_runtime_put(dev);

	return PVA_SUCCESS;
}

static const struct dev_pm_ops pva_kmd_linux_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(system_suspend, system_resume)
		SET_RUNTIME_PM_OPS(runtime_suspend, runtime_resume, NULL)
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

	printk(KERN_INFO "nvpva_init completed: %d. GSC boot: %d\n", err,
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
