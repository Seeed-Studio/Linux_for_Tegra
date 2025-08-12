/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_CONTEXT_H
#define PVA_KMD_CONTEXT_H
#include "pva_api.h"
#include "pva_constants.h"
#include "pva_kmd_block_allocator.h"
#include "pva_kmd_resource_table.h"
#include "pva_kmd_queue.h"
#include "pva_kmd_mutex.h"
#include "pva_kmd_submitter.h"

struct pva_kmd_device;

/**
 * @brief This struct manages a user context in KMD.
 *
 * One KMD user context is uniquely mapped to a UMD user context. Each context
 * is assigned a unique CCQ block and, on QNX and Linux, a unique file
 * descriptor.
 */
struct pva_kmd_context {
	struct pva_kmd_device *pva;
	uint8_t resource_table_id;
	uint8_t ccq_id;
	uint8_t smmu_ctx_id;

	bool inited;

	struct pva_kmd_resource_table ctx_resource_table;

	struct pva_kmd_submitter submitter;
	/** The lock protects the submission to the queue, including
	 * incrementing the post fence */
	pva_kmd_mutex_t submit_lock;
	/** Privileged queue owned by this context. It uses the privileged
	 * resource table (ID 0). */
	struct pva_kmd_device_memory *ctx_queue_mem;

	/** Privileged queue owned by the context */
	struct pva_kmd_queue ctx_queue;

	/** memory needed for submission: including command buffer chunks and fences */
	struct pva_kmd_device_memory *submit_memory;
	/** Resource ID of the submission memory, registered with the privileged resource table (ID 0) */
	uint32_t submit_memory_resource_id;
	uint64_t fence_offset; /**< fence offset within submit_memory*/

	pva_kmd_mutex_t chunk_pool_lock;
	struct pva_kmd_cmdbuf_chunk_pool chunk_pool;

	uint32_t max_n_queues;
	void *queue_allocator_mem;
	struct pva_kmd_block_allocator queue_allocator;

	void *plat_data;
	uint64_t ccq_shm_handle;

	pva_kmd_mutex_t ocb_lock;
};

/**
 * @brief Allocate a KMD context.
 */
struct pva_kmd_context *pva_kmd_context_create(struct pva_kmd_device *pva);

/**
 * @brief Destroy a KMD context.
 *
 * This function first notify FW of context destruction. If successful, it
 * calls pva_kmd_free_context() to free the context. Otherwise, the
 * free is deferred until PVA is powered off.
 */
void pva_kmd_context_destroy(struct pva_kmd_context *client);

/**
 * @brief Free a KMD context.
 *
 * This function frees the context without notifying FW. We need to make sure FW
 * will not access any context resources before calling this function.
 */
void pva_kmd_free_context(struct pva_kmd_context *ctx);

/**
 * @brief Initialize a KMD context.
 *
 * The user provides a CCQ range (inclusive on both ends) and the KMD will pick
 * one CCQ from this range.
 */
enum pva_error pva_kmd_context_init(struct pva_kmd_context *ctx,
				    uint32_t res_table_capacity);

struct pva_kmd_context *pva_kmd_get_context(struct pva_kmd_device *pva,
					    uint8_t alloc_id);

#endif // PVA_KMD_CONTEXT_H
