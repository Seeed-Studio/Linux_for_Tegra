// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/module.h>

static int __init ivc_driver_dummy_init(void)
{
	pr_info("Inserting dummy ivc_ext.ko module");
	return 0;
}
module_init(ivc_driver_dummy_init);

static void __exit ivc_driver_dummy_exit(void)
{
	pr_info("Removing dummy ivc_ext.ko module");
}
module_exit(ivc_driver_dummy_exit);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("Extended IVC Dummy Driver");
MODULE_LICENSE("GPL v2");
