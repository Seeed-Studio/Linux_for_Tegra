/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_OP_HANDLER_H
#define PVA_KMD_OP_HANDLER_H

#include "pva_kmd_context.h"
#include "pva_fw.h"
#include "pva_kmd.h"

/**
 * @brief Handler for PVA KMD operations.
 *
 * @details This function performs the following operations:
 * - Receives operation requests from UMD through platform-specific shim layers
 * - Validates the operation buffer format and size for correctness
 * - Parses the operations buffer to extract individual operation commands
 * - Executes the requested operations using the provided KMD context
 * - Manages submission mode (synchronous or asynchronous) based on parameters
 * - Handles postfence configuration for operation completion signaling
 * - Generates appropriate response data for successful operations
 * - Handles error conditions and generates error responses
 * - Ensures proper resource cleanup in case of operation failures
 * - Maintains operation isolation and security within the context
 *
 * This function implements the only runtime interface with UMD. Shim layers
 * receive the input data from UMD and call this function to execute the
 * operations. Then, shim layers send the response back to UMD. The function
 * assumes that the operations buffer is private to KMD and will dereference
 * it directly without making a copy. On Linux platforms, the operations
 * buffer should point to a private kernel space buffer rather than user
 * space to ensure memory safety and security.
 *
 * @param[in] ctx                     Pointer to @ref pva_kmd_context structure
 *                                    Valid value: non-null, must be initialized
 * @param[in] mode                    Submission mode for operation execution
 *                                    Valid values: @ref pva_ops_submit_mode enumeration values
 * @param[in, out] postfence          Pointer to @ref pva_fw_postfence structure for
 *                                    completion signaling
 *                                    Valid value: can be null if no postfence required
 * @param[in] ops_buffer              Pointer to operations buffer containing commands to execute
 *                                    Valid value: non-null, must point to KMD-private buffer
 * @param[in] ops_size                Size of the operations buffer in bytes
 *                                    Valid range: [1 .. UINT32_MAX]
 * @param[out] response               Pointer to buffer where response will be written
 *                                    Valid value: non-null, must not alias ops_buffer
 * @param[in] response_buffer_size    Size of the response buffer in bytes
 *                                    Valid range: [1 .. UINT32_MAX]
 * @param[out] out_response_size      Pointer to variable for actual response size
 *                                    Valid value: non-null
 * @param[in] priv                    Flag indicating privileged operation mode
 *                                    Valid values: true for privileged operations, false otherwise
 *
 * @retval PVA_SUCCESS                Operations executed successfully
 * @retval PVA_INVAL                  Invalid parameter values or buffer pointers
 * @retval PVA_ENOSPC                 Response buffer too small for result data
 * @retval PVA_NOT_IMPL               Invalid or unsupported operation in buffer
 * @retval PVA_INTERNAL               Context not properly initialized
 * @retval PVA_TIMEDOUT               Communication with firmware failed
 * @retval PVA_AGAIN                  Required resources are not available
 *
 * @note The input buffer and output buffer should never alias.
 */
enum pva_error pva_kmd_ops_handler(struct pva_kmd_context *ctx,
				   enum pva_ops_submit_mode mode,
				   struct pva_fw_postfence *postfence,
				   void const *ops_buffer, uint32_t ops_size,
				   void *response,
				   uint32_t response_buffer_size,
				   uint32_t *out_response_size, bool priv);

#endif // PVA_KMD_OP_HANDLER_H
