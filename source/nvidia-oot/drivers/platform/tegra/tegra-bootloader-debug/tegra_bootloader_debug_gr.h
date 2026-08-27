/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (C) 2025 NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __TEGRA_BOOTLOADER_DEBUG_GR_H
#define __TEGRA_BOOTLOADER_DEBUG_GR_H

#include <linux/types.h>

struct platform_device;

int tegra_bootloader_debug_gr_init(struct platform_device *pdev);
int tegra_bl_args(char *options, phys_addr_t *tegra_bl_arg_size,
				phys_addr_t *tegra_bl_arg_start);
int tegra_bl_parse_command_line_debug_gr_args(struct platform_device *pdev);
void __exit tegra_bl_debug_gr_init_module_exit(void);

#endif
