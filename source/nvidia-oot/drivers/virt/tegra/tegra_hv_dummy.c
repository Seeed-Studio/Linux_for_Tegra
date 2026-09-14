// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/module.h>

static int __init tegra_hv_dummy_init(void)
{
	pr_info("Inserting dummy tegra_hv.ko module");
	return 0;
}
module_init(tegra_hv_dummy_init);

static void __exit tegra_hv_dummy_exit(void)
{
	pr_info("Removing dummy tegra_hv.ko module");
}
module_exit(tegra_hv_dummy_exit);

MODULE_AUTHOR("Manish Bhardwaj <mbhardwaj@nvidia.com>");
MODULE_DESCRIPTION("Tegra HV Dummy Driver");
MODULE_LICENSE("GPL v2");
