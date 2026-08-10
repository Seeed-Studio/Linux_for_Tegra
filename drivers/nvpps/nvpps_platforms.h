// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef __NVPPS_PLATFORMS_H__
#define __NVPPS_PLATFORMS_H__

#include <linux/of_device.h>

#include "nvpps_t23x.h"
#include "nvpps_t26x.h"

static const struct of_device_id nvpps_of_table[] = {
	{ .compatible = "nvidia,tegra234-nvpps", .data = &tegra234_chip_ops },
	{ .compatible = "nvidia,tegra264-nvpps", .data = &tegra264_chip_ops },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nvpps_of_table);

#endif
