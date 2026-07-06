// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA context driver
 */

#include <nvidia/conftest.h>
#include "nvdla_ctx.h"

#include "nvdla.h"
#include "nvdla_debug.h"
#include "port/nvdla_device.h"

#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#if defined(NVDLA_HAVE_CONFIG_AXI) && (NVDLA_HAVE_CONFIG_AXI == 1)
static struct of_device_id tegra_nvdla_ctx_of_match[] = {
	{
		.name = "nvdla0_ctx0",
		.compatible = "nvidia,tegra25x-nvdla-ctx",
		.data = (struct nvhost_device_data *)&t25x_nvdla0_ctx0_info },
	{
		.name = "nvdla0_ctx1",
		.compatible = "nvidia,tegra25x-nvdla-ctx",
		.data = (struct nvhost_device_data *)&t25x_nvdla0_ctx1_info },
	{
		.name = "nvdla0_ctx2",
		.compatible = "nvidia,tegra25x-nvdla-ctx",
		.data = (struct nvhost_device_data *)&t25x_nvdla0_ctx2_info },
	{
		.name = "nvdla0_ctx3",
		.compatible = "nvidia,tegra25x-nvdla-ctx",
		.data = (struct nvhost_device_data *)&t25x_nvdla0_ctx3_info },
	{ },
};
#else
static struct of_device_id tegra_nvdla_ctx_of_match[] = {
	{ },
};
#endif // NVDLA_HAVE_CONFIG_AXI

static int32_t nvdla_ctx_probe(struct platform_device *pdev)
{
	int32_t err;
	struct device *dev = &pdev->dev;
	struct nvhost_device_data *pdata = NULL;
	struct nvdla_device *nvdla_dev;
	struct device_node *of_node = pdev->dev.of_node;

	if (of_node) {
		const struct of_device_id *match;

		match = of_match_device(tegra_nvdla_ctx_of_match, dev);
		if (match)
			pdata = (struct nvhost_device_data *) match->data;
	}

	WARN_ON(!pdata);
	if (!pdata) {
		dev_err(dev, "No platform data\n");
		err = -ENODATA;
		goto err_get_pdata;
	}

	pdata->pdev = pdev;
	mutex_init(&pdata->lock);
	platform_set_drvdata(pdev, pdata);

	// initialize device data
	err = nvdla_device_data_init(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to init device data");
		goto destroy_pdata_lock;
	}

	// register context device with its parent.
	nvdla_dev = pdata->private_data;
	err = nvdla_module_context_register(nvdla_dev->parent_pdev, nvdla_dev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to register context");
		goto deinit_device_data;
	}

	return 0;

deinit_device_data:
	nvdla_device_data_deinit(pdev);
destroy_pdata_lock:
	mutex_destroy(&pdata->lock);
err_get_pdata:
	return err;
}

static int32_t __exit nvdla_ctx_remove(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	// unregister context device with its parent.
	nvdla_module_context_unregister(nvdla_dev->parent_pdev, nvdla_dev);

	// free device data.
	nvdla_device_data_deinit(pdev);
	mutex_destroy(&pdata->lock);

	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void __exit nvdla_ctx_remove_wrapper(struct platform_device *pdev)
{
	nvdla_ctx_remove(pdev);
}
#else
static int __exit nvdla_ctx_remove_wrapper(struct platform_device *pdev)
{
	return nvdla_ctx_remove(pdev);
}
#endif

struct platform_driver nvdla_ctx_driver = {
	.probe = nvdla_ctx_probe,
	.remove = __exit_p(nvdla_ctx_remove_wrapper),
	.driver = {
		.owner = THIS_MODULE,
		.name = "nvdla_ctx",
#ifdef CONFIG_OF
		.of_match_table = tegra_nvdla_ctx_of_match,
#endif
	},
};
