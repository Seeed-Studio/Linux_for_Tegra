/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_UTILS_H
#define PVA_KMD_UTILS_H
#include "pva_kmd.h"
#include "pva_api.h"
#include "pva_kmd_shim_utils.h"
#include "pva_bit.h"
#include "pva_utils.h"
#include "pva_plat_faults.h"
#include "pva_math_utils.h"

/**
 * @brief Size of a 4KB memory page in bytes
 *
 * @details This macro defines the standard 4KB page size commonly used in
 * memory management and allocation operations. It is calculated as 4 * 1024
 * bytes and is used for memory alignment and size calculations.
 */
#define SIZE_4KB (4 * 1024)

/**
 * @brief Allocate zero-initialized memory that cannot fail
 *
 * @details This function performs the following operations:
 * - Allocates memory of the specified size for KMD internal use
 * - Initializes all allocated memory to zero
 * - Uses platform-appropriate memory allocation mechanisms
 * - Guarantees that the allocation will not fail - if memory cannot be
 *   allocated, the system will halt or take appropriate recovery action
 * - Returns a pointer to the allocated and zero-initialized memory region
 * - Ensures memory is properly aligned for the target platform
 *
 * This function is designed for critical memory allocations where failure
 * is not acceptable. The allocated memory must be freed using the appropriate
 * platform-specific deallocation function to prevent memory leaks. The
 * "nofail" designation means the function will either succeed or take
 * system-level action to handle the out-of-memory condition.
 *
 * @param[in] size  Size of memory to allocate in bytes
 *                  Valid range: [1 .. UINT64_MAX]
 *
 * @retval non-null  Pointer to allocated and zero-initialized memory
 */
void *pva_kmd_zalloc_nofail(uint64_t size);

#endif // PVA_KMD_UTILS_H
