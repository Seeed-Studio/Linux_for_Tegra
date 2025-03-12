/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_UTILS_H
#define PVA_UTILS_H
#include "pva_constants.h"
#include "pva_api.h"
#include "pva_bit.h"

#define PVA_ROUND_UP(val, align) ((((val) + ((align)-1U)) / (align)) * (align))
#define PVA_ALIGN4(n) PVA_ROUND_UP(n, 4)
#define PVA_ALIGN8(n) PVA_ROUND_UP(n, 8)

static inline uint64_t assemble_addr(uint8_t hi, uint32_t lo)
{
	return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline uint32_t iova_lo(uint64_t iova)
{
	return PVA_EXTRACT64(iova, 31, 0, uint32_t);
}

static inline uint8_t iova_hi(uint64_t iova)
{
	return PVA_EXTRACT64(iova, 39, 32, uint8_t);
}

static inline void *pva_offset_pointer(void *ptr, uintptr_t offset)
{
	return (void *)((uintptr_t)ptr + offset);
}

static inline void const *pva_offset_const_ptr(void const *ptr,
					       uintptr_t offset)
{
	return (void const *)((uintptr_t)ptr + offset);
}

static inline uint64_t pack64(uint32_t hi, uint32_t lo)
{
	uint64_t val = ((uint64_t)hi) << 32;
	val |= (uint64_t)lo;
	return val;
}

static inline bool pva_is_64B_aligned(uint64_t addr)
{
	return (addr & 0x3f) == 0;
}

static inline bool pva_is_512B_aligned(uint64_t addr)
{
	return (addr & 0x1ff) == 0;
}

static inline bool pva_is_reserved_desc(uint8_t desc_id)
{
	return ((desc_id >= (PVA_RESERVED_DESCRIPTORS_START)) &&
		(desc_id <= (PVA_RESERVED_DESCRIPTORS_END)));
}

#define PVA_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// clang-format off
/*
 * pva_dbg_printf is only available in the following two environments:
 * - Linux kernel (via printk)
 * - User space with c runtime (via printf)
 */
#if PVA_IS_DEBUG == 1
	/* For debug build, we allow printf */
	#if defined(__KERNEL__)
		/* Linux kernel */
		#define pva_dbg_printf printk
	#elif (PVA_BUILD_MODE == PVA_BUILD_MODE_BAREMETAL)
		#include "pva_fw_bm_utils.h"
		/* Firmware in silicon */
		#define pva_dbg_printf pva_fw_printf
	#else
		/* User space with c runtime */
		#include <stdio.h>
		#define pva_dbg_printf printf
	#endif
#else
	#if !(defined(__KERNEL__) || (PVA_BUILD_MODE == PVA_BUILD_MODE_BAREMETAL))
		#include <stdio.h>
	#endif
	/*For release build*/
	#define pva_dbg_printf(...)
#endif
// clang-format on

#endif // PVA_UTILS_H
