/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef _CORE_COMMON_LIB_RM_OS_H_
#define _CORE_COMMON_LIB_RM_OS_H_

#include <linux/kernel.h>
#include <linux/types.h>

#include "core_common_lib_rm.h"

/**
 * @brief Extracts the lower 32 bits of a 64-bit address.
 *
 * @param addr The 64-bit address to extract the lower 32 bits from.
 *
 * @return The lower 32 bits of the address.
 */
static inline uint32_t EXTRACT_LOWER_32bits(uint64_t addr)
{
	return ((addr) & 0xFFFFFFFFU);
}

/**
 * @brief Extracts the upper 32 bits of a 64-bit address.
 *
 * @param addr The 64-bit address to extract the upper 32 bits from.
 *
 * @return The upper 32 bits of the address.
 */
static inline uint32_t EXTRACT_UPPER_32bits(uint64_t addr)
{
	return ((addr) >> 32);
}

#endif
