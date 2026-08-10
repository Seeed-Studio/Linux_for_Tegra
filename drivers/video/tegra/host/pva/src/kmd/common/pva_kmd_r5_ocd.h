/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_R5_OCD_H
#define PVA_KMD_R5_OCD_H

#include "pva_kmd_device.h"
#include "pva_kmd.h"

/**
 * @brief Read data from R5 on-chip debug (OCD) interface
 *
 * @details This function performs read operations from the R5 processor's
 * on-chip debug interface. It provides access to R5 debug registers, memory,
 * and other debug-accessible resources for debugging and development purposes.
 * The function reads data from the specified offset within the R5 debug space
 * and copies it to the provided buffer.
 *
 * @param[in]  dev       Pointer to @ref pva_kmd_device structure
 *                       Valid value: non-null, must be initialized
 * @param[in]  file_data Pointer to file-specific data context
 *                       Valid value: platform-specific pointer or NULL
 * @param[out] data      Buffer to store read data
 *                       Valid value: non-null, must have capacity >= size
 * @param[in]  offset    Byte offset within R5 debug space to read from
 *                       Valid range: [0 .. R5_DEBUG_SPACE_SIZE-1]
 * @param[in]  size      Number of bytes to read
 *                       Valid range: [1 .. remaining_space_from_offset]
 *
 * @retval >=0                      Number of bytes successfully read
 * @retval PVA_INVAL                Invalid argument provided
 * @retval PVA_INTERNAL             Device not in proper state for debug access
 * @retval PVA_EACCES               Debug access not permitted
 * @retval PVA_TIMEDOUT             Timeout during debug operation
 * @retval PVA_INTERNAL             Hardware error during debug access
 */
int64_t pva_kmd_r5_ocd_read(struct pva_kmd_device *dev, void *file_data,
			    uint8_t *data, uint64_t offset, uint64_t size);

/**
 * @brief Write data to R5 on-chip debug (OCD) interface
 *
 * @details This function performs write operations to the R5 processor's
 * on-chip debug interface. It provides access to write to R5 debug registers,
 * memory, and other debug-accessible resources for debugging and development
 * purposes. The function writes data from the provided buffer to the specified
 * offset within the R5 debug space.
 *
 * @param[in] dev       Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null, must be initialized
 * @param[in] file_data Pointer to file-specific data context
 *                      Valid value: platform-specific pointer or NULL
 * @param[in] data      Buffer containing data to write
 *                      Valid value: non-null, must contain valid data
 * @param[in] offset    Byte offset within R5 debug space to write to
 *                      Valid range: [0 .. R5_DEBUG_SPACE_SIZE-1]
 * @param[in] size      Number of bytes to write
 *                      Valid range: [1 .. remaining_space_from_offset]
 *
 * @retval >=0                      Number of bytes successfully written
 * @retval PVA_INVAL                Invalid argument provided
 * @retval PVA_INTERNAL             Device not in proper state for debug access
 * @retval PVA_EACCES               Debug access not permitted
 * @retval PVA_TIMEDOUT             Timeout during debug operation
 * @retval PVA_INTERNAL             Hardware error during debug access
 */
int64_t pva_kmd_r5_ocd_write(struct pva_kmd_device *dev, void *file_data,
			     const uint8_t *data, uint64_t offset,
			     uint64_t size);

/**
 * @brief Open R5 on-chip debug (OCD) interface for access
 *
 * @details This function initializes and opens the R5 processor's on-chip
 * debug interface for subsequent debug operations. It performs necessary
 * hardware setup, validation checks, and prepares the debug interface for
 * read/write operations. This function must be called before any debug
 * access operations can be performed.
 *
 * @param[in] dev Pointer to @ref pva_kmd_device structure
 *                Valid value: non-null, must be initialized
 *
 * @retval PVA_SUCCESS              R5 OCD interface opened successfully
 * @retval PVA_INVAL                Invalid device pointer provided
 * @retval PVA_INTERNAL             Device not in proper state for debug access
 * @retval PVA_EACCES               Debug access not permitted
 * @retval PVA_AGAIN                Debug interface already in use
 * @retval PVA_INTERNAL             Hardware initialization failure
 */
int pva_kmd_r5_ocd_open(struct pva_kmd_device *dev);

/**
 * @brief Release R5 on-chip debug (OCD) interface and cleanup resources
 *
 * @details This function releases the R5 processor's on-chip debug interface
 * and performs necessary cleanup operations. It ensures proper shutdown of
 * the debug interface, releases any allocated resources, and restores the
 * device to normal operation mode. This function should be called when debug
 * access is no longer needed.
 *
 * @param[in] dev Pointer to @ref pva_kmd_device structure
 *                Valid value: non-null, must be initialized
 *
 * @retval PVA_SUCCESS              R5 OCD interface released successfully
 * @retval PVA_INVAL                Invalid device pointer provided
 * @retval PVA_INTERNAL             Device not in proper state for release
 * @retval PVA_INTERNAL             Hardware shutdown failure
 */
int pva_kmd_r5_ocd_release(struct pva_kmd_device *dev);

#endif