/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_DEVICE_MEMORY_POOL_H
#define PVA_KMD_DEVICE_MEMORY_POOL_H
#include "pva_api_types.h"

struct pva_kmd_device;

struct pva_kmd_devmem_view {
	uint64_t iova;
	void *va;
};

struct pva_kmd_devmem_pool {
};

enum pva_error pva_kmd_devmem_pool_init(struct pva_kmd_device *dev,
					uint32_t smmu_context_id,
					uint32_t block_size,
					uint32_t alloc_step,
					struct pva_kmd_devmem_pool *pool);

enum pva_error pva_kmd_devmem_pool_acquire(struct pva_kmd_devmem_pool *pool,
					   struct pva_kmd_devmem_view *view);

enum pva_error pva_kmd_devmem_pool_release(struct pva_kmd_devmem_pool *pool,
					   struct pva_kmd_devmem_view *view);

enum pva_error pva_kmd_devmem_pool_deinit(struct pva_kmd_devmem_pool *pool);

#endif