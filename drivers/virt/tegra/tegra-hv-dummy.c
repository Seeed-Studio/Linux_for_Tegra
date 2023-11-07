// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/module.h>

/* Dummy implementation for module */
static int __init tegra_hv_dummy_init(void)
{
	return 0;
}
device_initcall(tegra_hv_dummy_init);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("Dummy HV driver");
MODULE_LICENSE("GPL V2");
