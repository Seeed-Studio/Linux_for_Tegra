/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_API_HOSTMEM_H
#define PVA_API_HOSTMEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pva_api_types.h"

/**
 * @brief Create a PVA pointer from a libc allocated page-aligned host CPU pointer.
 * Linux only API.
 *
 * The caller is responsible for freeing the PVA memory object.
 *
 * @param[in] host_ptr Pointer to the host memory which needs to be imported to PVA.
 * @param[in] size Size of the buffer to be imported.
 * @param[in] access_mode Access mode for the buffer, determining the PVA's permissions for interaction.
 * @param[out] out_obj A pointer to the PVA memory object representing the imported buffer.
 */
enum pva_error pva_hostptr_import(void *host_ptr, size_t size,
				  uint32_t access_mode,
				  struct pva_memory **out_mem);

#ifdef __cplusplus
}
#endif

#endif // PVA_API_HOSTMEM_H
