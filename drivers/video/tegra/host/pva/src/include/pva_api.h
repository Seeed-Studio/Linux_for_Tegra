/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#ifndef PVA_API_H
#define PVA_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pva_api_types.h"
#include "pva_api_dma.h"
#include "pva_api_vpu.h"
#include "pva_api_cmdbuf.h"

/* Core APIs */

/**
 * @brief Create a PVA context.
 *
 * @param[in] pva_index Select which PVA instance to use if there are multiple PVAs
 * in the SOC.
 * @param[in] max_resource_count Maximum number of resources this context can have.
 * @param[out] ctx Pointer to the created context.
 */
enum pva_error pva_context_create(uint32_t pva_index,
				  uint32_t max_resource_count,
				  struct pva_context **ctx);

/**
 * @brief Destroy a PVA context.
 *
 * A context can only be destroyed after all queues are destroyed.
 *
 * @param[in] ctx Pointer to the context to destroy.
 */
void pva_context_destroy(struct pva_context *ctx);

/**
 * @brief Create a PVA queue.
 *
 * @param[in] ctx Pointer to the context.
 * @param[in] max_submission_count Max number of submissions that can be queued.
 * @param[out] queue Pointer to the created queue.
 */
enum pva_error pva_queue_create(struct pva_context *ctx,
				uint32_t max_submission_count,
				struct pva_queue **queue);

/**
 * @brief Destroy a PVA queue.
 *
 * @param[in] queue Pointer to the queue to destroy.
 */
void pva_queue_destroy(struct pva_queue *queue);

/**
 * @brief Allocate DRAM memory that can be mapped PVA's device space
 *
 * @param[in] size Size of the memory to allocate.
 * @param[out] out_mem Pointer to the allocated memory.
 */
enum pva_error pva_memory_alloc(uint64_t size, struct pva_memory **out_mem);

/**
 * @brief Map the memory to CPU's virtual space.
 *
 * @param[in] mem Pointer to the memory to map.
 * @param[in] access_mode Access mode for the memory. PVA_ACCESS_RD or
 *                        PVA_ACCESS_RW.
 * @param[out] out_va Pointer to the virtual address of the mapped memory.
 */
enum pva_error pva_memory_cpu_map(struct pva_memory *mem, uint32_t access_mode,
				  void **out_va);

/**
 *  @brief Unmap the memory from CPU's virtual space.
 *
 *  @param[in] mem Pointer to the memory to unmap.
 *  @param[in] va Previously mapped virtual address.
 */
enum pva_error pva_memory_cpu_unmap(struct pva_memory *mem, void *va);

/**
 * @brief Free the memory.
 *
 * Freeing a registered memory is okay since KMD holds a reference to the memory.
 *
 * @param mem Pointer to the memory to free.
 */
void pva_memory_free(struct pva_memory *mem);

/**
 * @brief Wait for a syncpoint to reach a value.
 *
 * @param[in] ctx Pointer to the context.
 * @param[in] syncpiont_id Syncpoint ID to wait on.
 * @param[in] value Value to wait for.
 * @param[in] timeout_us Timeout in microseconds. PVA_TIMEOUT_INF for infinite.
 */
enum pva_error pva_syncpoint_wait(struct pva_context *ctx,
				  uint32_t syncpiont_id, uint32_t value,
				  uint64_t timeout_us);

/**
 * @brief Submit a batch of command buffers.
 *
 * @param[in] queue Pointer to the queue.
 * @param[in] submit_infos Array of submit info structures.
 * @param[in] count Number of submit info structures.
 * @param[in] timeout_us Timeout in microseconds. PVA_TIMEOUT_INF for infinite.
 *
 * @note Concurrent submission to the same queue needs to be serialized by the
 *       caller.
 */
enum pva_error
pva_cmdbuf_batch_submit(struct pva_queue *queue,
			struct pva_cmdbuf_submit_info *submit_infos,
			uint32_t count, uint64_t timeout_us);

/**
 * @brief Get the symbol table for a registered executable.
 *
 * @param[in] ctx Pointer to the context.
 * @param[in] exe_resource_id Resource ID of the executable.
 * @param[out] out_info Pointer to the symbol info array.
 * @param[in] max_num_symbols Maximum number of symbols to return.
 */
enum pva_error pva_executable_get_symbols(struct pva_context *ctx,
					  uint32_t exe_resource_id,
					  struct pva_symbol_info *out_info,
					  uint32_t max_num_symbols);

/**
 * @brief Submit a list of asynchronous registration operations to KMD.
 *
 * The operations can be:
 * - Memory registration
 * - Executable registration
 * - DMA config registration
 *
 * The response buffer will contain the resource IDs of the registered
 * resources. Any command buffers that use these resources should wait on the
 * returned post fence.
 *
 * @param[in] ctx Pointer to the context.
 * @param[in] fence Pointer to the post fence to wait on. If NULL, it means the
 * caller is not interested in waiting. This usually only applies to unregister
 * operations.
 * @param[in] Input buffer containing the list of operations.
 * @param[out] Output buffer to store the response.
 *
 * @note Input and output buffer may be the same buffer.
 */
enum pva_error pva_ops_submit_async(struct pva_context *ctx,
				    struct pva_fence *fence,
				    struct pva_ops_buffer const *input_buffer,
				    struct pva_ops_buffer *output_buffer);

/**
 * @brief Perform a list of registration operations synchronously.
 *
 * The operations can be:
 * - Memory registration
 * - Executable registration
 * - DMA config registration
 *
 * The response buffer will contain the resource IDs of the registered
 * resources.
 *
 * @param[in] ctx Pointer to the context.
 * @param[in] Input buffer containing the list of operations.
 * @param[out] Output buffer to store the response.
 *
 * @note Input and output buffer may be the same buffer.
 *
 */
enum pva_error pva_ops_submit(struct pva_context *ctx,
			      struct pva_ops_buffer const *input_buffer,
			      struct pva_ops_buffer *output_buffer);

/** Size of the ops buffer header. When user allocates memory for ops buffer,
 * this size needs to be added. */
#define PVA_OPS_BUFFER_HEADER_SIZE 64
/**
 * @brief Initialize pva_ops_buffer to keep track of the state of
 * operations buffer during preparation.
 *
 * @param[out] buf_handle Pointer to the pva_ops_buffer object to initialize.
 * @param[in] buf Pointer to the buffer that will store the operations.
 * @param[in] size Size of the buffer.
 */
enum pva_error pva_ops_buffer_init(struct pva_ops_buffer *buf_handle, void *buf,
				   uint32_t size);

#define PVA_OPS_MEMORY_REG_SIZE 64
/**
 * @brief Append a memory registration operation to the operations buffer.
 *
 * @param[in] ctx Pointer to the context.
 * @param[in] mem Pointer to the memory to register.
 * @param[in] segment Memory segment to register.
 * @param[in] access_flags Access flags for the memory.
 * @param[out] op_buf Pointer to the operations buffer.
 */
enum pva_error pva_ops_append_memory_register(struct pva_context *ctx,
					      struct pva_memory *mem,
					      enum pva_memory_segment segment,
					      uint32_t access_flags,
					      struct pva_ops_buffer *op_buf);
#define PVA_OPS_EXEC_REG_HEADER_SIZE 16
/**
 * @brief Append an executable registration operation to the operations.
 *
 * @param[in] ctx Pointer to the context.
 * @param[in] executable Pointer to the executable binary content.
 * @param[in] executable_size Size of the executable.
 * @param[out] op_buf Pointer to the operations buffer.
 */
enum pva_error pva_ops_append_executable_register(
	struct pva_context *ctx, void const *executable,
	uint32_t executable_size, struct pva_ops_buffer *op_buf);

#define PVA_OPS_DMA_CONFIG_REG_SIZE (24 * 1024)
/**
 * @brief Append a DMA config registration operation to the operations.
 * @param[in] ctx Pointer to the context.
 * @param[in] dma_config Pointer to the DMA config.
 * @param[out] op_buf Pointer to the operations buffer.
 */
enum pva_error
pva_ops_append_dma_config_register(struct pva_context *ctx,
				   struct pva_dma_config const *dma_config,
				   struct pva_ops_buffer *op_buf);

#define PVA_OPS_UNREG_SIZE 16
enum pva_error pva_ops_append_unregister(struct pva_context *ctx,
					 uint32_t resource_id,
					 struct pva_ops_buffer *op_buf);

/**
 * @brief Parse the response buffer to get the resource ID of the registered
 * memory or DMA configuration.
 *
 * @param[in] resp_buf Pointer to the response buffer.
 * @param[out] resource_id output resource ID.
 */
enum pva_error pva_ops_parse_register_resp(struct pva_ops_buffer *resp_buf,
					   uint32_t *resource_id);

/**
 * @brief Parse the response buffer to get the resource ID of the registered
 * executable.
 *
 * @param[in] resp_buf Pointer to the response buffer.
 * @param[out] num_symbols Number of symbols in the executable.
 * @param[out] resource_id output resource ID.
 */
enum pva_error pva_ops_parse_exec_register_resp(struct pva_ops_buffer *op_buf,
						uint32_t *num_symbols,
						uint32_t *resource_id);

#define PVA_DATA_CHANNEL_HEADER_SIZE 32
/**
 * @brief Initialize VPU print buffer
 *
 * @param[in] data Pointer to VPU print buffer.
 * @param[in] size Size of VPU print buffer.
 */
struct pva_data_channel;
enum pva_error pva_init_data_channel(void *data, uint32_t size,
				     struct pva_data_channel **data_channel);

/**
 * @brief Read VPU print buffer
 *
 * @param[in]  data Pointer to VPU print buffer.
 * @param[out] read_buffer Pointer to output buffer in which data will be read.
 * @param[in]  bufferSize Size of output buffer.
 * @param[out] read_size Size of actual data read in output buffer.
 */
enum pva_error pva_read_data_channel(struct pva_data_channel *data_channel,
				     uint8_t *read_buffer, uint32_t bufferSize,
				     uint32_t *read_size);

/**
 * @brief Duplicate PVA memory object.
 *
 * This function duplicates a PVA memory object. The new object will have shared
 * ownership of the memory.
 *
 * @param[in] src Pointer to the source memory object.
 * @param[in] access_mode Access mode for the new memory object. It should be
 * more restrictive than the source memory. Passing 0 will use the same access
 * mode as the source memory.
 * @param[out] dst Resulting duplicated memory object.
 */
enum pva_error pva_memory_duplicate(struct pva_memory *src,
				    uint32_t access_mode,
				    struct pva_memory **dst);

/**
 * @brief Get memory attributes.
 *
 * @param[in] mem Pointer to the memory.
 * @param[out] out_attrs Pointer to the memory attributes.
 */
void pva_memory_get_attrs(struct pva_memory const *mem,
			  struct pva_memory_attrs *out_attrs);

/** \brief Specifies the PVA system software major version. */
#define PVA_SYSSW_MAJOR_VERSION (2U)

/** \brief Specifies the PVA system software minor version. */
#define PVA_SYSSW_MINOR_VERSION (7U)

/**
 * @brief Get PVA system software version.
 *
 * PVA system software version is defined as the latest version of cuPVA which is fully supported
 * by this version of the PVA system software.
 *
 * @param[out] version version of currently running system SW, computed as:
 	       (PVA_SYSSW_MAJOR_VERSION * 1000) + PVA_SYSSW_MINOR_VERSION
 * @return PVA_SUCCESS on success, else error code indicating the failure.
 */
enum pva_error pva_get_version(uint32_t *version);

/**
 * @brief Get the hardware characteristics of the PVA.
 *
 * @param[out] pva_hw_char Pointer to the hardware characteristics.
 */
enum pva_error
pva_get_hw_characteristics(struct pva_characteristics *pva_hw_char);

#ifdef __cplusplus
}
#endif

#endif // PVA_API_H
