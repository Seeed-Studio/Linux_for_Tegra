// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/module.h>

/* Dummy implementation for module */
static int __init r8168_dummy_dummy_init(void)
{
	return 0;
}
device_initcall(r8168_dummy_dummy_init);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("Dummy R8168 dummy driver");
MODULE_LICENSE("GPL");
