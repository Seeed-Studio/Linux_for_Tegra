/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_API_H
#define PVA_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pva_api_types.h"
#include "pva_api_dma.h"
#include "pva_api_vpu.h"
#include "pva_api_cmdbuf.h"
#include "pva_api_ops.h"

/* Core APIs */

#define PVA_MAX_NUM_RESOURCES_PER_CONTEXT (16U * 1024U)
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
 * @brief Get the value of a context attribute.
 *
 * @param[in] ctx Pointer to the context.
 * @param[in] attr Attribute to get.
 * @param[out] out_value Pointer to the value of the attribute.
 * @param[size] size of the attribute structure
 */
enum pva_error pva_get_attribute(struct pva_context *ctx, enum pva_attr attr,
				 void *out_value, uint64_t size);

#define PVA_MAX_NUM_SUBMISSIONS_PER_QUEUE (8U * 1024U)
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
 * @param[in] timeout_us Timeout in microseconds. PVA_SUBMIT_TIMEOUT_INF for infinite.
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
 * @param[in] timeout_us Timeout in microseconds. PVA_SUBMIT_TIMEOUT_INF for infinite.
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

/**
 * @brief Create an import ID for memory registration.
 *
 * The ID must be destroyed after registration.
 *
 * @param[in] ctx Pointer to the context.
 * @param[in] mem Pointer to the memory.
 * @param[out] out_import_id Pointer to the import ID.
 */
enum pva_error pva_memory_import_id_create(struct pva_context *ctx,
					   struct pva_memory *mem,
					   uint64_t *out_import_id);

/**
 * @brief Destroy an import ID.
 *
 * @param[in] import_id Import ID to destroy.
 */
enum pva_error pva_memory_import_id_destroy(uint64_t import_id);

/** \brief Specifies the PVA system software major version. */
#define PVA_SYSSW_MAJOR_VERSION (2U)

/** \brief Specifies the PVA system software minor version. */
#define PVA_SYSSW_MINOR_VERSION (8U)

#ifdef __cplusplus
}
#endif

#endif // PVA_API_H
