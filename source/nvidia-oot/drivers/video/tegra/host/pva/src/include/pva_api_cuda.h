/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_API_CUDA_H
#define PVA_API_CUDA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cuda.h"
#include "pva_api_types.h"

#define PVA_CUEXTEND_MAX_NUM_PREFENCES 16
#define PVA_CUEXTEND_MAX_NUM_POSTFENCES 16

struct pva_cuextend_submit_events {
	struct pva_fence prefences[PVA_CUEXTEND_MAX_NUM_PREFENCES];
	struct pva_fence postfences[PVA_CUEXTEND_MAX_NUM_POSTFENCES];
	uint32_t num_prefences;
	uint32_t num_postfences;
};

/**
 * @brief Function type for cuExtend register memory callback
 *
 * @param[in] callback_args Pointer to the callback arguments provided by client during cuExtend initialization.
 * @param[in] mem The pointer to a \ref pva_memory object. This register memory callback shall transfer the
 *                ownership of the memory to the client, and it is client's responsibility to release the memory.
 * @param[in] cuda_ptr CUDA device pointer.
 * @param[in] cached_flags The cached flags for the memory.
 * @return \ref pva_error The completion status of register memory operation.
 */
typedef enum pva_error (*pva_cuextend_memory_register)(void *callback_args,
						       struct pva_memory *mem,
						       void *cuda_ptr,
						       uint32_t cached_flags);

/**
 *  @brief Function type for cuExtend unregister memory callback.
 *
 * @param[in] callback_args Pointer to the callback arguments provided by client during cuExtend initialization.
 * @param[in] cuda_ptr CUDA device pointer.
 * @return \ref pva_error The completion status of unregister memory operation.
 */
typedef enum pva_error (*pva_cuextend_memory_unregister)(void *callback_args,
							 void *cuda_ptr);

/**
 *  @brief Function type for cuExtend register stream callback.
 *
 * @param[in] callback_args Pointer to the callback arguments provided by client during cuExtend initialization.
 * @param[out] stream_payload Client data associated with a CUDA stream.
 * @param[in] flags Reserved for future. Must set to 0.
 * @return \ref pva_error The completion status of register stream operation.
 */
typedef enum pva_error (*pva_cuextend_stream_register)(void *callback_args,
						       void **stream_payload,
						       uint64_t flags);

/**
 *  @brief Function type for cuExtend unregister stream callback.
 *
 * @param[in] callback_args Pointer to the callback arguments provided by client during cuExtend initialization.
 * @param[in] stream_payload Client data returned by \ref pva_cuextend_stream_register.
 * @param[in] flags Reserved for future. Must set to 0.
 * @return \ref pva_error The completion status of unregister stream operation.
 */
typedef enum pva_error (*pva_cuextend_stream_unregister)(void *callback_args,
							 void *stream_payload,
							 uint64_t flags);

/**
 * @brief Function type for submitting a batch of command buffers via a CUDA stream.
 *
 * @param[in] callback_args Pointer to the callback arguments provided by client during cuExtend initialization.
 * @param[in] stream_payload Client data returned by \ref pva_cuextend_stream_register.
 * @param[in] submit_payload Pointer to the submit payload.
 * @return \ref pva_error The completion status of the submit operation.
 */
typedef enum pva_error (*pva_cuextend_stream_submit)(
	void *callback_args, void *stream_payload, void *submit_payload,
	struct pva_cuextend_submit_events *submit_events);

/**
 * @brief Function type for retrieving error code from cuExtend.
 *
 * @param[in] teardown_ctx Pointer to the cuExtend context pointer.
 */
typedef enum pva_error (*pva_cuextend_get_error)(void *teardown_ctx);

/**
 * @brief Function type for cuExtend teardown callback.
 *
 * It is expected that the client does the following necessary actions in this callback:
 * Blocking wait for all pending tasks on all queues. In the wait loop, periodically check for CUDA error by calling \ref pva_cuextend_get_error,
 * hop out then loop if there is an error.
 *
 * @param[in] callback_args Pointer to the callback arguments provided by client during cuExtend initialization.
 * @param[in] teardown_ctx Pointer to a teardown context passed by cuExtend teardown callback.
 * @param[in] get_error Function pointer to get CUDA error function.
 * @return \ref pva_error The completion status of release  queue operation.
 */
typedef enum pva_error (*pva_cuextend_teardown)(
	void *callback_args, void *teardown_ctx,
	pva_cuextend_get_error get_error);

/**
 *  @brief Structure for cuExtend callbacks provided by the caller during cuExtend initialization.
 */
struct pva_cuextend_callbacks {
	/*! Holds the register memory callback */
	pva_cuextend_memory_register mem_reg;
	/*! Holds the unregister memory callback */
	pva_cuextend_memory_unregister mem_unreg;
	/*! Holds the register stream callback */
	pva_cuextend_stream_register stream_reg;
	/*! Holds the unregister stream callback */
	pva_cuextend_stream_unregister stream_unreg;
	/*! Holds the teardown callback */
	pva_cuextend_teardown teardown;
	/*! Holds the stream submit callback */
	pva_cuextend_stream_submit stream_submit;
	/*! Pointer to the callback arguments provided by client during cuExtend initialization */
	void *args;
};

/**
 * @brief Initialize cuExtend context.
 *
 * This function must be called before any other cuExtend functions. It does the following:
 *
 * 1. Load cuExtend library and retrieves function pointers to the library's exported functions.
 * 2. Add PVA to CUDA unified context model.
 * 3. Initialize the opaque cuExtend impl pointer.
 *
 * @param[in] ctx Pointer to a PVA context object.
 * @param[in] callbacks Pointer to CUDA interop callbacks.
 * @return \ref pva_error The completion status of the initialization operation.
 */
enum pva_error pva_cuextend_init(struct pva_context *ctx,
				 struct pva_cuextend_callbacks *callbacks);

/**
 * @brief De-initialize cuExtend context.
 *
 * This function must be called at the context destructor in the client. It does the following:
 *
 * 1. Clear the opaque cuExtend impl pointer in pva context object.
 * 2. Remove PVA to from cuExtend context.
 * 3. Unload cuExtend library and clear all the function pointers.
 *
 * @param[in] ctx Pointer to a PVA context object.
 * @return \ref pva_error The completion status of the de-initialization operation.
 */
enum pva_error pva_cuextend_deinit(struct pva_context *ctx);

/**
 * @brief Import a memory region from a CUDA context into a PVA context.
 *
 * @param[in] ctx Pointer to a PVA context structure.
 * @param[in] cuda_ptr Pointer to CUDA memory provided by client.
 * @param[in] size Size of the memory region.
 * @param[in] access_type Access flag provided by client.
 * @param[out] out_mem Pointer to the imported memory object.
 * @param[out] cached_flags Output cached flags for the memory.
 * @return \ref pva_error The completion status of the initialization operation.
 */
enum pva_error pva_cuextend_memory_import(struct pva_context *ctx,
					  void *cuda_ptr, uint64_t size,
					  uint32_t access_mode,
					  struct pva_memory **out_mem,
					  uint32_t *cached_flags);

/**
 * @brief Submit a batch of command buffers via a CUDA stream.
 *
 * @param[in] ctx Pointer to the PVA context.
 * @param[in] cuStream A CUDA stream.
 * @param[in] client_stream A client stream.
 * @param[in] submit_payload Pointer to the submit payload.
 * @return \ref pva_error The completion status of the submit operation.
 */
enum pva_error pva_cuextend_cmdbuf_batch_submit(struct pva_context *ctx,
						CUstream cuStream,
						void *client_stream,
						void *submit_payload);

/**
 * @brief Get the payload associated with a CUDA stream.
 *
 * Returns the payload which was associated with the CUDA stream during registration callback.
 *
 * @param[in] ctx Pointer to the PVA context.
 * @param[in] cuStream A CUDA stream.
 * @param[out] stream_payload Pointer to the stream payload.
 * @return PVA_SUCCESS if the stream payload is successfully retrieved
 *         PVA_BAD_PARAMETER_ERROR if any of the parameters are NULL
 *         PVA_CUDA_INIT_FAILED if the cuExtend was not initialized for the context
 */
enum pva_error pva_cuextend_get_stream_payload(struct pva_context *ctx,
					       CUstream cuStream,
					       void **stream_payload);

#ifdef __cplusplus
}
#endif

#endif // PVA_API_CUDA_H
