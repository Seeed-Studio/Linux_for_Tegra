// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES.
 */

#include <mm/core_mmu.h>
#include <mm/core_memprot.h>
#include <tegra_driver_common.h>

TEE_Result iomap_pa2va(paddr_t pa, paddr_size_t size, vaddr_t *va)
{
	if (!core_mmu_add_mapping(MEM_AREA_IO_SEC, pa, size))
		return TEE_ERROR_GENERIC;

	*va = (vaddr_t)phys_to_virt(pa, MEM_AREA_IO_SEC, size);
	if (*va)
		return TEE_SUCCESS;

	return TEE_ERROR_GENERIC;
}
