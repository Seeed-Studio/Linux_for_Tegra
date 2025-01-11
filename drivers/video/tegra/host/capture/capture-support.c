// SPDX-License-Identifier: GPL-2.0-only
/*
 * Capture support for syncpoint and GoS management
 *
 * SPDX-FileCopyrightText: Copyright (c) 2017-2025, NVIDIA Corporation.  All rights reserved.
 */

#include <nvidia/conftest.h>

#include "capture-support.h"

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <soc/tegra/camrtc-capture.h>
#include <linux/version.h>
#include <soc/tegra/fuse.h>
#include <linux/nvhost.h>
#include <uapi/linux/nvhost_ioctl.h>

#ifdef CONFIG_TEGRA_HWPM_CAM
#include <uapi/linux/tegra-soc-hwpm-uapi.h>
#include <linux/tegra-camera-rtcpu.h>

static int tegra_cam_hwpm_ip_pm(void *ip_dev, bool disable)
{
	int err = 0;

#ifdef CONFIG_MEDIA_SUPPORT
#ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
	// check if rtcpu is powered on
	bool is_powered_on = tegra_camrtc_is_rtcpu_powered();

	if (!is_powered_on)
		err = -EIO;
#endif
#endif

	return err;
}

static int tegra_cam_hwpm_ip_reg_op(void *ip_dev,
	enum tegra_soc_hwpm_ip_reg_op reg_op,
	u32 inst_element_index, u64 reg_offset, u32 *reg_data)
{
	struct platform_device *dev = (struct platform_device *)ip_dev;

	if (reg_offset > UINT_MAX)
		return -EINVAL;

	dev_err(&dev->dev, "%s:reg_op %d reg_offset %llu", __func__, reg_op, reg_offset);

	if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_READ)
		*reg_data = host1x_readl(dev,
				(unsigned int)reg_offset);
	else if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_WRITE)
		host1x_writel(dev, (unsigned int)reg_offset,
				*reg_data);
	else
		return -1;

	return 0;
}
#endif

static int capture_support_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvhost_device_data *info;
#ifdef CONFIG_TEGRA_HWPM_CAM
	struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;
	uint64_t base_address;
#endif
	int err = 0;

	info = (void *)of_device_get_match_data(dev);
	if (WARN_ON(info == NULL))
		return -ENODATA;

	info->pdev = pdev;
	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);

	(void) dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(39));

	err = nvhost_client_device_get_resources(pdev);
	if (err)
		goto error;

	err = nvhost_module_init(pdev);
	if (err)
		goto error;

	err = nvhost_client_device_init(pdev);
	if (err) {
		nvhost_module_deinit(pdev);
		goto error;
	}

	err = nvhost_syncpt_unit_interface_init(pdev);
	if (err)
		goto device_release;

#ifdef CONFIG_TEGRA_HWPM_CAM
	err = of_property_read_u64(dev->of_node, "reg", &base_address);
	if (err) {
		err = -ENODEV;
		goto error;
	}

	hwpm_ip_ops.ip_dev = (void *)pdev;
	hwpm_ip_ops.ip_base_address = base_address;
	if ((strcmp(info->devfs_name, "vi0-thi") == 0U) ||
		(strcmp(info->devfs_name, "vi1-thi") == 0U)) {
		hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_VI;
		dev_info(dev,
			"%s: hwpm VI address: 0x%llx\n", __func__,
				base_address);
	} else if ((strcmp(info->devfs_name, "isp-thi") == 0U) ||
		(strcmp(info->devfs_name, "isp1-thi") == 0U)) {
		hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_ISP;
		dev_info(dev,
			"%s: hwpm ISP address: 0x%llx\n", __func__,
				base_address);
	} else {
		err = -EINVAL;
		dev_err(dev, "%s: hwpm: invalid resource name: %s\n", __func__, info->devfs_name);
		goto device_release;
	}
	hwpm_ip_ops.hwpm_ip_pm = &tegra_cam_hwpm_ip_pm;
	hwpm_ip_ops.hwpm_ip_reg_op = &tegra_cam_hwpm_ip_reg_op;
	tegra_soc_hwpm_ip_register(&hwpm_ip_ops);
#endif

	return 0;

device_release:
	nvhost_client_device_release(pdev);
error:
	if (err != -EPROBE_DEFER)
		dev_err(dev, "probe failed: %d\n", err);
	return err;
}

static int capture_support_remove(struct platform_device *pdev)
{

#ifdef CONFIG_TEGRA_HWPM_CAM
	struct device *dev = &pdev->dev;
	struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;
	uint64_t base_address;
	struct nvhost_device_data *info;
	int err = 0;

	info = (void *)of_device_get_match_data(dev);
	if (WARN_ON(info == NULL))
		return -ENODATA;

	err = of_property_read_u64(dev->of_node, "reg", &base_address);
	if (err) {
		err = -ENODEV;
		goto error;
	}

	hwpm_ip_ops.ip_dev = (void *)pdev;
	hwpm_ip_ops.ip_base_address = base_address;
	if ((strcmp(info->devfs_name, "vi0-thi") == 0U) ||
		(strcmp(info->devfs_name, "vi1-thi") == 0U)) {
		hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_VI;
	} else if ((strcmp(info->devfs_name, "isp-thi") == 0U) ||
		(strcmp(info->devfs_name, "isp1-thi") == 0U)) {
		hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_ISP;
	} else {
		err = -EINVAL;
		dev_err(dev, "%s: hwpm: invalid resource name: %s\n", __func__, info->devfs_name);
		goto error;
	}
	hwpm_ip_ops.hwpm_ip_pm = NULL;
	hwpm_ip_ops.hwpm_ip_reg_op = NULL;
	tegra_soc_hwpm_ip_unregister(&hwpm_ip_ops);
#endif

	return 0;

#ifdef CONFIG_TEGRA_HWPM_CAM
error:
	dev_err(dev, "hwpm unregistration failed: %d\n", err);
	return err;
#endif
}

#ifdef CONFIG_TEGRA_HWPM_CAM
struct nvhost_device_data t264_vi0_thi_info = {
	.devfs_name		= "vi0-thi",
	.moduleid		= 2, //NVHOST_MODULE_VI,
};

struct nvhost_device_data t264_vi1_thi_info = {
	.devfs_name		= "vi1-thi",
	.moduleid		= 3, //NVHOST_MODULE_VI2,
};

struct nvhost_device_data t264_isp_thi_info = {
	.devfs_name             = "isp-thi",
	.moduleid               = 4, //NVHOST_MODULE_ISP
};

struct nvhost_device_data t264_isp1_thi_info = {
	.devfs_name             = "isp1-thi",
	.moduleid               = 5, //NVHOST_MODULE_ISPB
};
#endif

static const struct of_device_id capture_support_match[] = {
#ifdef CONFIG_TEGRA_HWPM_CAM
	{
		.name = "vi0-thi",
		.compatible = "nvidia,tegra264-vi-thi",
		.data = &t264_vi0_thi_info,
	},
	{
		.name = "vi1-thi",
		.compatible = "nvidia,tegra264-vi-thi",
		.data = &t264_vi1_thi_info,
	},
	{
		.name = "isp-thi",
		.compatible = "nvidia,tegra264-isp-thi",
		.data = &t264_isp_thi_info,
	},
	{
		.name = "isp1-thi",
		.compatible = "nvidia,tegra264-isp-thi",
		.data = &t264_isp1_thi_info,
	},
#endif
	{ },
};
MODULE_DEVICE_TABLE(of, capture_support_match);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void capture_support_remove_wrapper(struct platform_device *pdev)
{
	capture_support_remove(pdev);
}
#else
static int capture_support_remove_wrapper(struct platform_device *pdev)
{
	return capture_support_remove(pdev);
}
#endif

static struct platform_driver capture_support_driver = {
	.probe = capture_support_probe,
	.remove = capture_support_remove_wrapper,
	.driver = {
		/* Only suitable name for dummy falcon driver */
		.name = "scare-pigeon",
		.of_match_table = capture_support_match,
		.pm = &nvhost_module_pm_ops,
	},
};

module_platform_driver(capture_support_driver);
MODULE_LICENSE("GPL");
