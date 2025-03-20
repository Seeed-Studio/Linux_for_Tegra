/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_OP_HANDLER_H
#define PVA_KMD_OP_HANDLER_H

#include "pva_kmd_context.h"
#include "pva_fw.h"
#include "pva_kmd.h"

/** @brief Handler for PVA KMD operations.
*
* This function implements the only runtime interface with UMD. Shim layers
* receive the input data from UMD and call this function to execute the
* operations. Then, shim layers send the response back to UMD.
*
* @param ctx The KMD context.
* @param ops Pointer to the input buffer containing the operations to be
*            executed. The common layer assumes that this buffer is private to
*            KMD and will dereference it directly without making a copy.
*            Specifically on Linux, this parameter should point to a private
*            kernel space buffer instead of the user space buffer.
* @param ops_size Size of the input buffer.
* @param response Pointer to the buffer where the response will be written.
* @param response_buffer_size Size of the response buffer.
* @param out_response_size Pointer to a variable where the actual size of the
*                          response will be written.
*
* @return pva_error indicating the success or failure of the operation.
*
* @Note that the input buffer and output buffer should never alias.
*/
enum pva_error
pva_kmd_ops_handler(struct pva_kmd_context *ctx, enum pva_ops_submit_mode mode,
		    struct pva_fw_postfence *postfence, void const *ops_buffer,
		    uint32_t ops_size, void *response,
		    uint32_t response_buffer_size, uint32_t *out_response_size);

#endif // PVA_KMD_OP_HANDLER_H
