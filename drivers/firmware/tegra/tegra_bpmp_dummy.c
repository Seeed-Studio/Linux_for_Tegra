// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/module.h>

static int __init tegra_bpmp_dummy_init(void)
{
	pr_info("Inserting dummy tegra_bpmp.ko module");
	return 0;
}
module_init(tegra_bpmp_dummy_init);

static void __exit tegra_bpmp_dummy_exit(void)
{
	pr_info("Removing dummy tegra_bpmp.ko module");
}
module_exit(tegra_bpmp_dummy_exit);

MODULE_AUTHOR("Manish Bhardwaj <mbhardwaj@nvidia.com>");
MODULE_DESCRIPTION("Tegra BPMP Dummy Driver");
MODULE_LICENSE("GPL v2");
