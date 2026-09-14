// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA Corporation.
 */

#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>

#include <uapi/linux/tegra-soc-hwpm-uapi.h>

#include "hwpm.h"

#ifdef CONFIG_TEGRA_SYSTEM_TYPE_ACK
void tegra_drm_hwpm_register(struct tegra_drm_hwpm *drm_hwpm, u64 resource_base,
	enum tegra_drm_hwpm_ip hwpm_ip)
{

}
void tegra_drm_hwpm_unregister(struct tegra_drm_hwpm *drm_hwpm, u64 resource_base,
	enum tegra_drm_hwpm_ip hwpm_ip)
{

}
#else
static u32 tegra_drm_hwpm_readl(struct tegra_drm_hwpm *hwpm, u32 offset)
{
	return readl(hwpm->regs + offset);
}

static void tegra_drm_hwpm_writel(struct tegra_drm_hwpm *hwpm, u32 value, u32 offset)
{
	writel(value, hwpm->regs + offset);
}

static int tegra_drm_hwpm_ip_pm(void *ip_dev, bool disable)
{
	int err = 0;
	struct tegra_drm_hwpm *hwpm = (struct tegra_drm_hwpm *)ip_dev;

	if (disable) {
		err = pm_runtime_resume_and_get(hwpm->dev);
		if (err < 0) {
			dev_err(hwpm->dev, "runtime resume failed %d", err);
		}
	} else {
		err = pm_runtime_put_autosuspend(hwpm->dev);
		if (err < 0) {
			dev_err(hwpm->dev, "runtime suspend failed %d", err);
		}
	}

	return err;
}

static int tegra_drm_hwpm_ip_reg_op(void *ip_dev,
	enum tegra_soc_hwpm_ip_reg_op reg_op,
	u32 inst_element_index, u64 reg_offset, u32 *reg_data)
{
	struct tegra_drm_hwpm *hwpm = (struct tegra_drm_hwpm *)ip_dev;

	if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_READ) {
		*reg_data = tegra_drm_hwpm_readl(hwpm, reg_offset);
	} else if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_WRITE) {
		tegra_drm_hwpm_writel(hwpm, *reg_data, reg_offset);
	}
	return 0;
}

static u32 tegra_drm_hwpm_get_resource_index(enum tegra_drm_hwpm_ip hwpm_ip)
{
	switch (hwpm_ip) {
	case TEGRA_DRM_HWPM_IP_OFA:
		return TEGRA_SOC_HWPM_RESOURCE_OFA;
		break;
	case TEGRA_DRM_HWPM_IP_NVDEC:
		return TEGRA_SOC_HWPM_RESOURCE_NVDEC;
		break;
	case TEGRA_DRM_HWPM_IP_NVENC:
		return TEGRA_SOC_HWPM_RESOURCE_NVENC;
		break;
	case TEGRA_DRM_HWPM_IP_VIC:
		return TEGRA_SOC_HWPM_RESOURCE_VIC;
		break;
	case TEGRA_DRM_HWPM_IP_INVALID:
	default:
		return TERGA_SOC_HWPM_NUM_RESOURCES;
		break;
	}
}

void tegra_drm_hwpm_register(struct tegra_drm_hwpm *drm_hwpm, u64 resource_base,
	enum tegra_drm_hwpm_ip hwpm_ip)
{
	struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;

	hwpm_ip_ops.ip_dev = (void *)drm_hwpm;
	hwpm_ip_ops.ip_base_address = resource_base;
	hwpm_ip_ops.resource_enum = tegra_drm_hwpm_get_resource_index(hwpm_ip);
	hwpm_ip_ops.hwpm_ip_pm = &tegra_drm_hwpm_ip_pm;
	hwpm_ip_ops.hwpm_ip_reg_op = &tegra_drm_hwpm_ip_reg_op;
	tegra_soc_hwpm_ip_register(&hwpm_ip_ops);
}

void tegra_drm_hwpm_unregister(struct tegra_drm_hwpm *drm_hwpm, u64 resource_base,
	enum tegra_drm_hwpm_ip hwpm_ip)
{
	struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;

	hwpm_ip_ops.ip_dev = (void *)drm_hwpm;
	hwpm_ip_ops.ip_base_address = resource_base;
	hwpm_ip_ops.resource_enum = tegra_drm_hwpm_get_resource_index(hwpm_ip);
	tegra_soc_hwpm_ip_unregister(&hwpm_ip_ops);
}

#endif
