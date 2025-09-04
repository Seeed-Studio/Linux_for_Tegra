/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEGRA_DRIVER_RNG1_H__
#define __TEGRA_DRIVER_RNG1_H__

#if defined(PLATFORM_FLAVOR_t194)
#define TEGRA_RNG1_BASE      0x03ae0000
#define TEGRA_RNG1_SIZE      0x10000
#endif

#if defined(PLATFORM_FLAVOR_t234)
#define TEGRA_RNG1_BASE      0x03b70000
#define TEGRA_RNG1_SIZE      0x10000
#endif

TEE_Result tegra_rng1_map_regs(vaddr_t *va);

#endif
