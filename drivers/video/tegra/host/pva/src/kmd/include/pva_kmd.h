/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#ifndef PVA_KMD_H
#define PVA_KMD_H
#include "pva_api.h"
#include "pva_fw.h"
#include "pva_constants.h"
#include "pva_math_utils.h"

/* KMD API: context init */
struct pva_kmd_context_init_in_args {
	uint32_t resource_table_capacity;
};

struct pva_kmd_context_init_out_args {
	enum pva_error error;
	uint64_t ccq_shm_hdl;
};

struct pva_kmd_syncpt_register_out_args {
	enum pva_error error;
	uint32_t syncpt_ro_res_id;
	uint32_t syncpt_rw_res_id;
	uint32_t synpt_size;
	uint32_t synpt_ids[PVA_NUM_RW_SYNCPTS_PER_CONTEXT];
	uint32_t num_ro_syncpoints;
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
struct pva_kmd_queue_create_in_args {
	uint32_t max_submission_count;
	uint64_t queue_memory_handle;
	uint64_t queue_memory_offset;
};

struct pva_kmd_queue_create_out_args {
	enum pva_error error;
	uint32_t queue_id;
	uint32_t syncpt_fence_counter;
};

/* KMD API: queue destroy */
struct pva_kmd_queue_destroy_in_args {
	uint32_t queue_id;
};

struct pva_kmd_queue_destroy_out_args {
	enum pva_error error;
};

struct pva_kmd_memory_register_in_args {
	enum pva_memory_segment segment;
	uint32_t access_flags;
	uint64_t memory_handle;
	uint64_t offset;
	uint64_t size;
};

/* KMD API: executable */
struct pva_kmd_executable_register_in_args {
	uint32_t size;
};

struct pva_kmd_executable_get_symbols_in_args {
	uint32_t exec_resource_id;
};

struct pva_kmd_executable_get_symbols_out_args {
	enum pva_error error;
	uint32_t num_symbols;
	/* Followed by <num_symbols> of struct pva_symbol_info */
};

/* KMD API: DMA config */
struct pva_kmd_dma_config_register_in_args {
	struct pva_dma_config_header dma_config_header;
	/* Followed by hwseq words, channels, descriptors, etc. */
};

struct pva_kmd_register_out_args {
	enum pva_error error;
	uint32_t resource_id;
};

struct pva_kmd_exec_register_out_args {
	enum pva_error error;
	uint32_t resource_id;
	uint32_t num_symbols;
};

struct pva_kmd_unregister_in_args {
	uint32_t resource_id;
};

enum pva_kmd_op_type {
	PVA_KMD_OP_CONTEXT_INIT,
	PVA_KMD_OP_QUEUE_CREATE,
	PVA_KMD_OP_QUEUE_DESTROY,
	PVA_KMD_OP_EXECUTABLE_GET_SYMBOLS,
	PVA_KMD_OP_MEMORY_REGISTER,
	PVA_KMD_OP_SYNPT_REGISTER,
	PVA_KMD_OP_EXECUTABLE_REGISTER,
	PVA_KMD_OP_DMA_CONFIG_REGISTER,
	PVA_KMD_OP_UNREGISTER,
	PVA_KMD_OP_MAX,
};

/**
 * The header of a KMD operation
 */
struct pva_kmd_op_header {
	enum pva_kmd_op_type op_type; /**< Type of the KMD operation */
};

/**
 * The header of a KMD response
 */
struct pva_kmd_response_header {
	uint32_t rep_size; /** Size of the response, including the header */
};

enum pva_kmd_ops_mode {
	/**
	* Only one operation is allowed. The
	* operation will be done synchronously.
	* KMD will wait for the fence if
	* necessary. */
	PVA_KMD_OPS_MODE_SYNC,
	/**
	* A list of registration operations are allowed. These operations will
	* trigger a post fence. KMD will not wait for the fence.
	*/
	PVA_KMD_OPS_MODE_ASYNC,
};

/**
 * A buffer contains a list of KMD operations and a post fence.
 *
 * In general, the list of KMD operations contain jobs that need to be done by
 * the KMD and FW. KMD will first perform its part and then submit a privileged
 * command buffer to FW. FW will trigger the provided post fence when done.
 *
 * NOTE: Starting address of every struct/array in the buffer must be aligned to
 * 8 bytes.
 */
struct pva_kmd_operations {
	enum pva_kmd_ops_mode mode;
	struct pva_fw_postfence postfence;
	/** Followed by a list of KMD operation(s) */
};

/* Max op buffer sizer is 8 MB */
#define PVA_KMD_MAX_OP_BUFFER_SIZE (8 * 1024 * 1024)

/* Max respone size is 8 KB */
#define PVA_KMD_MAX_RESP_BUFFER_SIZE (8 * 1024)

#endif // PVA_KMD_H
