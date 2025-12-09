/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SHIM_VPU_APP_AUTH_H
#define PVA_KMD_SHIM_VPU_APP_AUTH_H

#include "pva_api_types.h"
struct pva_kmd_device;

/**
 * @brief Get the default allowlist file path.
 *
 * @details This function performs the following operations:
 * - Returns a pointer to the default allowlist file path string
 * - Provides platform-specific default location for VPU application allowlist
 * - Enables fallback allowlist configuration when custom path is not specified
 * - Returns a compile-time constant string path
 *
 * The allowlist file contains cryptographic hashes of authorized VPU
 * applications that are permitted to execute on the PVA hardware.
 * This function provides the default location where the system expects
 * to find the allowlist file.
 *
 * @retval file_path  Pointer to null-terminated string containing default
 *                    allowlist file path. Never returns NULL.
 */
const char *pva_kmd_get_default_allowlist(void);

/**
 * @brief Load VPU application allowlist from file.
 *
 * @details This function performs the following operations:
 * - Opens and reads the specified allowlist file from the file system
 * - Parses the allowlist file format to extract hash keys
 * - Allocates memory to store the hash key data
 * - Validates the allowlist file format and content
 * - Returns the hash key data and size for use in authentication
 * - Provides error reporting for file access or format issues
 *
 * The allowlist file contains cryptographic hashes of VPU applications
 * that are authorized to execute. This function loads and prepares
 * the hash data for use in VPU application authentication operations.
 * The caller is responsible for freeing the allocated hash key data.
 *
 * @param[in, out] pva            Pointer to @ref pva_kmd_device structure
 *                                Valid value: non-null
 * @param[in] file_name           Path to allowlist file to load
 *                                Valid value: non-null, null-terminated string
 * @param[out] hash_keys_data     Pointer to store allocated hash key data
 *                                Valid value: non-null pointer to uint8_t*
 * @param[out] psize              Pointer to store size of hash key data
 *                                Valid value: non-null pointer to uint64_t
 *
 * @retval PVA_SUCCESS            Allowlist loaded successfully
 * @retval PVA_NOENT              Allowlist file not found
 * @retval PVA_NOMEM              Insufficient memory for allowlist data
 * @retval PVA_INVAL              Invalid allowlist file format
 * @retval PVA_INTERNAL           File system I/O error
 * @retval PVA_INVAL              Invalid parameters provided
 */
enum pva_error pva_kmd_auth_allowlist_load(struct pva_kmd_device *pva,
					   const char *file_name,
					   uint8_t **hash_keys_data,
					   uint64_t *psize);

/**
 * @brief Update the allowlist file path for the PVA device.
 *
 * @details This function performs the following operations:
 * - Updates the allowlist file path configuration for the PVA device
 * - Stores the new allowlist path in device-specific configuration
 * - Enables runtime configuration of allowlist location
 * - Validates the provided path string
 * - Prepares the device for loading allowlist from the new location
 *
 * This function allows dynamic configuration of the allowlist file
 * location, enabling flexibility in deployment scenarios where the
 * allowlist may be located in different paths. The updated path
 * will be used for subsequent allowlist loading operations.
 *
 * @param[in, out] pva          Pointer to @ref pva_kmd_device structure
 *                              Valid value: non-null
 * @param[in] allowlist_path    New path to allowlist file
 *                              Valid value: non-null, null-terminated string
 */
void pva_kmd_update_allowlist_path(struct pva_kmd_device *pva,
				   const char *allowlist_path);

#endif