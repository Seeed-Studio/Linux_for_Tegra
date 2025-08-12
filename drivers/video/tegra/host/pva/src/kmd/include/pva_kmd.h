/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_H
#define PVA_KMD_H
#include "pva_api.h"
#include "pva_fw.h"
#include "pva_constants.h"
#include "pva_math_utils.h"

#define PVA_OPS_PRIVATE_OPCODE_FLAG (1U << 31U)

/* KMD API: context init */
struct pva_ops_context_init {
#define PVA_OPS_OPCODE_CONTEXT_INIT (1U | PVA_OPS_PRIVATE_OPCODE_FLAG)
	struct pva_ops_header header;
	uint32_t resource_table_capacity;
	uint32_t pad;
};

struct pva_ops_response_context_init {
	enum pva_error error;
	uint16_t max_cmdbuf_chunk_size;
	uint64_t ccq_shm_hdl;
};

/**
 * Calculates the total memory size required for a PVA submission queue.
 * This includes the size of the queue header and the combined size of all command buffer submission info structures.
 *
 * @param x The number of command buffer submission info structures.
 * @return The total memory size in bytes.
 */
static inline uint32_t pva_get_submission_queue_memory_size(uint32_t x)
{
	uint32_t submit_info_size =
		(uint32_t)sizeof(struct pva_fw_cmdbuf_submit_info);
	uint32_t num_submit_infos = safe_mulu32(x, submit_info_size);
	uint32_t header_size =
		(uint32_t)sizeof(struct pva_fw_submit_queue_header);
	return safe_addu32(header_size, num_submit_infos);
}

/* KMD API: queue create */
struct pva_ops_queue_create {
#define PVA_OPS_OPCODE_QUEUE_CREATE (3U | PVA_OPS_PRIVATE_OPCODE_FLAG)
	struct pva_ops_header header;
	uint32_t max_submission_count;
	uint64_t queue_memory_handle;
	uint64_t queue_memory_offset;
};

struct pva_ops_response_queue_create {
	enum pva_error error;
	uint32_t queue_id;
	uint32_t syncpt_id;
	uint32_t syncpt_current_value;
};

/* KMD API: queue destroy */
struct pva_ops_queue_destroy {
#define PVA_OPS_OPCODE_QUEUE_DESTROY (4U | PVA_OPS_PRIVATE_OPCODE_FLAG)
	struct pva_ops_header header;
	uint32_t queue_id;
	uint32_t pad;
};

struct pva_ops_response_queue_destroy {
	enum pva_error error;
	uint32_t pad;
};

struct pva_ops_executable_get_symbols {
#define PVA_OPS_OPCODE_EXECUTABLE_GET_SYMBOLS (5U | PVA_OPS_PRIVATE_OPCODE_FLAG)
	struct pva_ops_header header;
	uint32_t exec_resource_id;
	uint32_t pad;
};

struct pva_ops_response_executable_get_symbols {
	enum pva_error error;
	uint32_t num_symbols;
	/* Followed by <num_symbols> of struct pva_symbol_info */
};

/**
 * @brief Structure for memory registration operation.
 */
struct pva_ops_memory_register {
#define PVA_OPS_OPCODE_MEMORY_REGISTER (6U | PVA_OPS_PRIVATE_OPCODE_FLAG)
	struct pva_ops_header header; /**< Operation header */
	enum pva_memory_segment segment; /**< Memory segment to register */
	uint32_t access_flags; /**< Memory access flags */
	uint64_t import_id; /**< Import ID of the memory */
	uint64_t offset; /**< Offset into the memory */
	uint64_t size; /**< Size of memory to register */
	uint64_t serial_id; /**< Serial ID of the memory */
};

/**
 * @brief Response structure for memory registration operation.
 */
struct pva_ops_response_register {
	enum pva_error error; /**< Operation result status */
	uint32_t resource_id; /**< Assigned resource ID */
};

/**
 * @brief Structure for resource unregistration operation.
 */
struct pva_ops_unregister {
#define PVA_OPS_OPCODE_UNREGISTER (7U | PVA_OPS_PRIVATE_OPCODE_FLAG)
	struct pva_ops_header header; /**< Operation header */
	uint32_t resource_id; /**< ID of resource to unregister */
	uint32_t pad; /**< Padding for 8 bytes alignment */
};

/**
 * @brief Response structure for executable registration operation.
 */
struct pva_ops_response_executable_register {
	enum pva_error error; /**< Operation result status */
	uint32_t resource_id; /**< Assigned resource ID */
	uint32_t num_symbols; /**< Number of symbols in executable */
	uint32_t pad; /**< Padding for 8 bytes alignment */
};

/**
 * @brief Response structure for unregister operation.
 */
struct pva_ops_response_unregister {
	enum pva_error error; /**< Operation result status */
	uint32_t pad; /**< Padding for 8 bytes alignment */
};

enum pva_ops_submit_mode {
	PVA_OPS_SUBMIT_MODE_SYNC,
	PVA_OPS_SUBMIT_MODE_ASYNC,
};

struct pva_dma_config {
	struct pva_dma_config_header header;
	const uint32_t *hwseq_words;
	const struct pva_dma_channel *channels;
	const struct pva_dma_descriptor *descriptors;
	const struct pva_dma_static_binding *static_bindings;
};

#define PVA_OPS_CONTEXT_BUFFER_SIZE (1U * 1024U * 1024U) //1MB
#define PVA_KMD_MAX_OP_BUFFER_SIZE (8 * 1024 * 1024) //8MB
#define PVA_KMD_MAX_RESP_BUFFER_SIZE (8 * 1024) //8KB

#endif // PVA_KMD_H
