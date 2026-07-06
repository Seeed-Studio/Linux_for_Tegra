/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA Corporation.
 */

#ifndef _TEGRA_DRM_HWPM_H_
#define _TEGRA_DRM_HWPM_H_

#include <linux/types.h>

struct tegra_drm_hwpm {
	struct device *dev;
	void __iomem *regs;
};

enum tegra_drm_hwpm_ip {
	TEGRA_DRM_HWPM_IP_INVALID,
	TEGRA_DRM_HWPM_IP_OFA,
	TEGRA_DRM_HWPM_IP_NVDEC,
	TEGRA_DRM_HWPM_IP_NVENC,
	TEGRA_DRM_HWPM_IP_VIC
};

void tegra_drm_hwpm_register(struct tegra_drm_hwpm *drm_hwpm, u64 resource_base,
	enum tegra_drm_hwpm_ip hwpm_ip);
void tegra_drm_hwpm_unregister(struct tegra_drm_hwpm *drm_hwpm, u64 resource_base,
	enum tegra_drm_hwpm_ip hwpm_ip);

#endif /* _TEGRA_DRM_HWPM_H_ */