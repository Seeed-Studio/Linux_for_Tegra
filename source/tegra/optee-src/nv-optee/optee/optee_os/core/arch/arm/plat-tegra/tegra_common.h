/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef __TEGRA_COMMON_H__
#define __TEGRA_COMMON_H__

#ifdef CFG_TEGRA_SIMULATION_SUPPORT
TEE_Result tegra_console_init(void);
#else
static inline TEE_Result tegra_console_init(void)
{
	return TEE_ERROR_NOT_IMPLEMENTED;
}
#endif /* CFG_TEGRA_SIMULATION_SUPPORT */
#endif
