// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/module.h>

/* Dummy implementation for module */
static int __init tegra_hv_vblk_dummy_init(void)
{
	return 0;
}
device_initcall(tegra_hv_vblk_dummy_init);

MODULE_AUTHOR("Jon Hunter <jonathanh@nvidia.com>");
MODULE_DESCRIPTION("Dummy Tegra HV VBLK driver");
MODULE_LICENSE("GPL");

