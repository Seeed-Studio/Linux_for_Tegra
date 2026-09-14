/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_API_OPS_H
#define PVA_API_OPS_H
#include "pva_api_types.h"
#include "pva_api_dma.h"

/**
 * @brief Structure describing memory used by PVA KMD operations.
 */
struct pva_ops_memory {
	uint32_t handle; /**< Memory handle */
	uint64_t size; /**< Size of memory */
	void *va; /**< Virtual address */
};

/**
 * @brief Structure describing the state of the operation buffer being appended
 * to/parsed from.
 *
 * Valid data is between [start_offset, end_offset). The reader/consumer
 * advances the start_offset, while the writer/producer advances the end_offset.
 * Consequently, when used as an input buffer, PVA KMD reads from start_offset
 * to end_offset; when used as an output buffer, PVA KMD appends to end_offset
 * until memory->size is reached.
 */
struct pva_ops_buffer {
	struct pva_ops_memory *mem_ptr; /**< Pointer to buffer memory */
	uint64_t start_offset; /**< Start offset in buffer memory */
	uint64_t end_offset; /**< End offset (exclusive) in buffer memory */
};

/**
 * @brief Alignment requirement for PVA operations.
 *
 * All PVA operation starting offset must be 8 bytes aligned. The fixed-size
 * operation structs are already explictedly aligned. The starting offset of
 * arrays in variable-sized operations (e.g. DMA configuration) must be manually
 * aligned to 8 bytes.
 */
#define PVA_OPS_ALIGNMENT 8U

/**
 * @brief Header structure for PVA operations.
 */
struct pva_ops_header {
	uint64_t opcode; /**< Operation code identifying the operation type */
	/** Size of the operation in bytes. This size must be a multiple of 8 bytes. */
	uint64_t size;
};

/**
 * @brief Structure for executable registration operation.
 */
struct pva_ops_executable_register {
#define PVA_OPS_OPCODE_EXECUTABLE_REGISTER 1U
	struct pva_ops_header header; /**< Operation header */
	uint64_t exec_size; /**< Size of executable data */
	//followed by executable data
};

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

/**
 * @brief Structure for DMA configuration registration operation.
 */
struct pva_ops_dma_config_register {
#define PVA_OPS_OPCODE_DMA_CONFIG_REGISTER 2U
	struct pva_ops_header header; /**< Operation header */
	struct pva_dma_config_header
		dma_config_header; /**< DMA configuration header */
	uint32_t channels_offset;
	uint32_t descriptors_offset;
	uint32_t hwseq_words_offset;
	uint32_t static_bindings_offset;
	//followed by channels, descriptors, hwseq_words, static_bindings
};

/**
 * @brief Append a memory registration operation to the operations buffer.
 *
 * @param[in] ctx Pointer to the context.
 * @param[in] mem Pointer to the memory to register.
 * @param[in] import_id Import ID created by pva_memory_import_id_create
 * @param[in] segment Memory segment to register.
 * @param[in] access_flags Access flags for the memory.
 * @param[out] op_buf Pointer to the operations buffer.
 */
enum pva_error pva_ops_append_memory_register(struct pva_context *ctx,
					      struct pva_memory *mem,
					      uint64_t import_id,
					      enum pva_memory_segment segment,
					      uint32_t access_flags,
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
 * @brief Append a resource unregistration operation to the operations buffer.
 *
 * @param[in] ctx Pointer to the context.
 * @param[in] resource_id Resource ID to unregister.
 * @param[out] op_buf Pointer to the operations buffer.
 */
enum pva_error pva_ops_append_unregister(struct pva_context *ctx,
					 uint32_t resource_id,
					 struct pva_ops_buffer *op_buf);

/**
 * @brief Parse the response buffer to get the result of the unregister operation.
 *
 * @param[in] resp_buf Pointer to the response buffer.
 */
enum pva_error pva_ops_parse_unregister_resp(struct pva_ops_buffer *resp_buf);

/**
 * @brief Allocate memory for operations buffer.
 *
 * This memory is directly accessible by the PVA KMD.
 *
 * @param[in] ctx Pointer to PVA context.
 * @param[in] size Size of buffer to allocate.
 * @param[out] ops_buf Pointer to operations buffer memory structure.
 *
 * @return PVA_SUCCESS on success, appropriate error code otherwise.
 */
enum pva_error pva_ops_memory_alloc(struct pva_context *ctx, uint64_t size,
				    struct pva_ops_memory *ops_buf);

/**
 * @brief Free operations buffer memory.
 *
 * @param[in] ctx Pointer to PVA context.
 * @param[in] ops_buf Pointer to operations buffer memory to free.
 */
void pva_ops_memory_free(struct pva_context *ctx,
			 struct pva_ops_memory *ops_buf);

/**
 * @brief Submit operations buffer synchronously to PVA KMD for processing.
 *
 * This function submits a buffer of operations to the KMD and waits for FW
 * acknowledgement synchronously. The KMD will process each operation in the
 * input buffer sequentially and write responses to the output buffer in the
 * same order.
 *
 * @param[in] ctx Pointer to PVA context.
 * @param[in] input_buffer Input operations buffer containing operations to
 * process.
 * @param[out] output_buffer Output operations buffer where responses will be
 *                          written. Must have sufficient space for all
 *                          responses. It is guaranteed that each response will
 *                          not be longer than the corresponding operation.
 *
 * @retval PVA_SUCCESS All operations were processed by KMD and responses
 *                     written, though individual operations may have failed.
 *                     Parse output buffer to check per-operation status.
 * @return Other error codes if KMD was not able to process all operations or
 *         was not able to write all responses.
 */
enum pva_error pva_ops_submit(struct pva_context *ctx,
			      struct pva_ops_buffer *input_buffer,
			      struct pva_ops_buffer *output_buffer);
/**
 * @brief Submit operations buffer asynchronously to PVA KMD for processing.
 *
 * Identical to pva_ops_submit, but does not wait for FW acknowledgement.
 *
 * This function submits a buffer of operations to the KMD. The KMD will NOT
 * wait for FW acknowledgement. The KMD will process each operation in the input
 * buffer sequentially and write responses to the output buffer in the same
 * order. For any command buffers that wish to use the resource IDs returned by
 * this function, they must attach the fence as pre-fence.
 *
 * @param[in] ctx Pointer to PVA context.
 * @param[in] input_buffer Input operations buffer containing operations to
 *                         process.
 * @param[out] output_buffer Output operations buffer where responses will be
 *                           written. Must have sufficient space for all
 *                           responses. It is guaranteed that each response
 *                           will not be longer than the corresponding
 *                           operation.
 * @param[in] fence Optional fence to signal when operations complete. If NULL,
 *                  no fence will be signaled, but KMD still will not wait for
 *                  FW acknowledgement.
 *
* @retval PVA_SUCCESS All operations were processed by KMD and responses
 *                     written, though individual operations may have failed.
 *                     Parse output buffer to check per-operation status.
 * @return Other error codes if KMD was not able to process all operations or
 *         was not able to write all responses.
 */
enum pva_error pva_ops_submit_async(struct pva_context *ctx,
				    struct pva_ops_buffer *input_buffer,
				    struct pva_ops_buffer *output_buffer,
				    struct pva_fence *fence);

#endif // PVA_API_OPS_H
