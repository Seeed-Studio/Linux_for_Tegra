// SPDX-License-Identifier: GPL-2.0-only
/*
 * Capture support for syncpoint and GoS management
 *
 * SPDX-FileCopyrightText: Copyright (c) 2017-2025, NVIDIA Corporation.  All rights reserved.
 */

#include <nvidia/conftest.h>

#include "capture-support.h"
#include <linux/host1x-next.h>
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
#include <linux/reset.h>
#include <linux/clk.h>

#ifdef CONFIG_TEGRA_HWPM_CAM
#include <uapi/linux/tegra-soc-hwpm-uapi.h>
#include <linux/tegra-camera-rtcpu.h>

struct host_capture_data {
	void __iomem *reg_base;
};

static int tegra_cam_hwpm_ip_pm(void *ip_dev, bool disable)
{
	int err = 0;

#if defined(CONFIG_MEDIA_SUPPORT) && !defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
	// check if rtcpu is powered on
	bool is_powered_on = tegra_camrtc_is_rtcpu_powered();

	if (!is_powered_on)
		err = -EIO;
#endif

	return err;
}

static int tegra_cam_hwpm_ip_reg_op(void *ip_dev,
	enum tegra_soc_hwpm_ip_reg_op reg_op,
	u32 inst_element_index, u64 reg_offset, u32 *reg_data)
{
	struct platform_device *dev = (struct platform_device *)ip_dev;
	struct nvhost_device_data *info;
	struct host_capture_data *capture_data;
	void __iomem *addr;

	if (reg_offset > UINT_MAX)
		return -EINVAL;

	info = (struct nvhost_device_data *)platform_get_drvdata(dev);

	if (!info)
		return -1;

	capture_data = (struct host_capture_data *)(info->private_data);

	addr = capture_data->reg_base + (reg_offset);

	dev_err(&dev->dev, "%s:reg_op %d reg_offset %llu", __func__, reg_op, reg_offset);

	if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_READ)
		*reg_data = readl(addr);
	else if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_WRITE)
		writel(*reg_data, addr);
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
	struct host_capture_data *capture_data;
	struct resource *r;
	int err = 0;
#endif

	info = (void *)of_device_get_match_data(dev);
	if (WARN_ON(info == NULL))
		return -ENODATA;

	info->pdev = pdev;
	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);

	(void) dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(39));

#ifdef CONFIG_TEGRA_HWPM_CAM
	err = of_property_read_u64(dev->of_node, "reg", &base_address);
	if (err) {
		err = -ENODEV;
		goto error;
	}

	capture_data = (struct host_capture_data *) devm_kzalloc(dev,
			sizeof(*capture_data), GFP_KERNEL);
	if (!capture_data) {
		err = -ENOMEM;
		goto error;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	capture_data->reg_base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(capture_data->reg_base)) {
		err = PTR_ERR(capture_data->reg_base);
		goto error;
	}

	info->private_data = capture_data;

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
		goto error;
	}
	hwpm_ip_ops.hwpm_ip_pm = &tegra_cam_hwpm_ip_pm;
	hwpm_ip_ops.hwpm_ip_reg_op = &tegra_cam_hwpm_ip_reg_op;
	tegra_soc_hwpm_ip_register(&hwpm_ip_ops);
#endif

	return 0;

#ifdef CONFIG_TEGRA_HWPM_CAM
error:
	if (err != -EPROBE_DEFER)
		dev_err(dev, "probe failed: %d\n", err);
	return err;
#endif
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

static int capture_support_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *info = platform_get_drvdata(pdev);

	clk_bulk_disable_unprepare(info->num_clks, info->clks);

	return 0;
}

static int capture_support_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	int err;

	err = clk_bulk_prepare_enable(pdata->num_clks, pdata->clks);
	if (err) {
		dev_warn(dev, "failed to enable clocks: %d\n", err);
		return err;
	}

	return 0;
}

const struct dev_pm_ops capture_support_pm_ops = {
	SET_RUNTIME_PM_OPS(capture_support_runtime_suspend, capture_support_runtime_resume, NULL)
};

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
		.pm = &capture_support_pm_ops,
	},
};

module_platform_driver(capture_support_driver);
MODULE_LICENSE("GPL");
