/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEGRA_DRIVER_COMMON_H__
#define __TEGRA_DRIVER_COMMON_H__

#include <tee_api_types.h>
#include <types_ext.h>

TEE_Result iomap_pa2va(paddr_t pa, paddr_size_t size, vaddr_t *va);

#endif
