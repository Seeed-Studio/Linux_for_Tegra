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
#include "pva_kmd_pfsd.h"

struct pva_kmd_device;

/**
 * @brief This struct manages a user context in KMD.
 *
 * @details One KMD user context is uniquely mapped to a UMD user context. Each context
 * is assigned a unique CCQ block and, on QNX and Linux, a unique file
 * descriptor. This structure contains all the resources and state information
 * needed to manage operations for a specific user context, including resource
 * tables, command queues, memory allocations, and synchronization primitives.
 */
struct pva_kmd_context {
	/**
	 * @brief Pointer to the parent PVA device
	 * Valid value: non-null
	 */
	struct pva_kmd_device *pva;

	/**
	 * @brief Resource table ID assigned to this context
	 * Valid range: [0 .. PVA_KMD_MAX_NUM_KMD_RESOURCES-1]
	 */
	uint8_t resource_table_id;

	/**
	 * @brief CCQ (Command and Control Queue) ID assigned to this context
	 * Valid range: [0 .. PVA_MAX_NUM_CCQ-1]
	 */
	uint8_t ccq_id;

	/**
	 * @brief SMMU context ID for memory protection
	 * Valid range: [0 .. PVA_MAX_NUM_SMMU_CONTEXTS-1]
	 */
	uint8_t smmu_ctx_id;

	/**
	 * @brief Flag indicating if context has been initialized
	 * Valid values: true, false
	 */
	bool inited;

	/**
	 * @brief Context-specific resource table for managing user resources
	 */
	struct pva_kmd_resource_table ctx_resource_table;

	/**
	 * @brief Command submission handler for context operations
	 */
	struct pva_kmd_submitter submitter;

	/**
	 * @brief Lock protecting submission operations and post fence increment
	 *
	 * @details The lock protects the submission to the queue, including
	 * incrementing the post fence
	 */
	pva_kmd_mutex_t submit_lock;

	/**
	 * @brief Device memory allocation for context queue operations
	 *
	 * @details Privileged queue owned by this context. It uses the privileged
	 * resource table (ID 0).
	 */
	struct pva_kmd_device_memory *ctx_queue_mem;

	/**
	 * @brief Context-owned privileged command queue
	 *
	 * @details Privileged queue owned by the context
	 */
	struct pva_kmd_queue ctx_queue;

	/**
	 * @brief Memory allocation for context submission operations
	 *
	 * @details Memory needed for submission: including command buffer chunks and fences
	 */
	struct pva_kmd_device_memory *submit_memory;

	/**
	 * @brief Resource ID of submission memory in privileged resource table
	 *
	 * @details Resource ID of the submission memory, registered with the
	 * privileged resource table (ID 0)
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t submit_memory_resource_id;

	/**
	 * @brief Offset of fence within submission memory
	 * Valid range: [0 .. submit_memory_size-1]
	 */
	uint64_t fence_offset;

	/**
	 * @brief Lock protecting command buffer chunk pool operations
	 */
	pva_kmd_mutex_t chunk_pool_lock;

	/**
	 * @brief Pool of command buffer chunks for efficient allocation
	 */
	struct pva_kmd_cmdbuf_chunk_pool chunk_pool;

	/**
	 * @brief Maximum number of queues supported by this context
	 * Valid range: [0 .. PVA_MAX_QUEUES_PER_CONTEXT]
	 */
	uint32_t max_n_queues;

	/**
	 * @brief Memory allocation for queue allocator bookkeeping
	 */
	void *queue_allocator_mem;

	/**
	 * @brief Block allocator for managing queue allocation within context
	 */
	struct pva_kmd_block_allocator queue_allocator;

	/**
	 * @brief Platform-specific private data pointer
	 */
	void *plat_data;

	/**
	 * @brief Shared memory handle for CCQ communication
	 * Valid range: [0 .. UINT64_MAX]
	 */
	uint64_t ccq_shm_handle;

	/**
	 * @brief Lock protecting OCB (On-Chip Buffer) operations
	 */
	pva_kmd_mutex_t ocb_lock;

	/** Status memory for this context. KMD shares ownership of this memory
	 * with the UMD so that FW can write safely. */
	struct pva_kmd_device_memory *status_mem;

	/**
	 * @brief PFSD (Power Functional Safety Diagnostic) resource IDs
	 *
	 * @details Stores resource IDs for PFSD buffers, executables, and configurations
	 * that are registered with the resource table for this context
	 */
	struct pva_pfsd_resource_ids pfsd_resource_ids;
};

/**
 * @brief Allocate a KMD context.
 *
 * @details This function performs the following operations:
 * - Allocates memory for a new @ref pva_kmd_context structure
 * - Initializes basic context fields including device pointer
 * - Allocates a unique CCQ ID from the device's available CCQ pool
 * - Sets up initial resource table and queue allocator configurations
 * - Initializes synchronization primitives including mutexes
 * - Configures platform-specific context data if required
 * - Prepares the context for initialization via @ref pva_kmd_context_init()
 *
 * The allocated context must be initialized using @ref pva_kmd_context_init()
 * before it can be used for operations. The context should be destroyed using
 * @ref pva_kmd_context_destroy() when no longer needed.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null
 *
 * @retval non-null  Pointer to successfully allocated @ref pva_kmd_context
 * @retval NULL      Context allocation failed due to memory shortage,
 *                   CCQ unavailability, or initialization failure
 */
struct pva_kmd_context *pva_kmd_context_create(struct pva_kmd_device *pva);

/**
 * @brief Destroy a KMD context.
 *
 * @details This function performs the following operations:
 * - Notifies firmware of context destruction using appropriate messaging
 * - Waits for firmware acknowledgment of context cleanup
 * - If firmware notification succeeds, calls @ref pva_kmd_free_context()
 *   to immediately free context resources
 * - If firmware notification fails, adds context to deferred free list
 *   using @ref pva_kmd_add_deferred_context_free()
 * - Ensures proper cleanup of context resources and CCQ assignment
 *
 * This function first notify FW of context destruction. If successful, it
 * calls @ref pva_kmd_free_context() to free the context. Otherwise, the
 * free is deferred until PVA is powered off. Deferred cleanup ensures
 * that contexts are properly cleaned up even when firmware communication
 * is not possible.
 *
 * @param[in] client  Pointer to @ref pva_kmd_context structure to destroy
 *                    Valid value: non-null, must be a valid context created by
 *                    @ref pva_kmd_context_create()
 */
void pva_kmd_context_destroy(struct pva_kmd_context *client);

/**
 * @brief Free a KMD context.
 *
 * @details This function performs the following operations:
 * - Frees all context-allocated memory including queues and resource tables
 * - Releases the assigned CCQ ID back to the device's available pool
 * - Destroys synchronization primitives including mutexes and semaphores
 * - Cleans up platform-specific context resources
 * - Deallocates command buffer chunks and submission memory
 * - Frees the context structure itself
 *
 * This function frees the context without notifying FW. We need to make sure FW
 * will not access any context resources before calling this function. This
 * function is typically called either after successful firmware notification
 * or during device shutdown when firmware communication is not required.
 *
 * @param[in] ctx  Pointer to @ref pva_kmd_context structure to free
 *                 Valid value: non-null, firmware must not be accessing
 *                 context resources
 */
void pva_kmd_free_context(struct pva_kmd_context *ctx);

/**
 * @brief Initialize a KMD context.
 *
 * @details This function performs the following operations:
 * - Allocates and configures the context's resource table with specified capacity
 * - Sets up context-specific command queues and submission infrastructure
 * - Initializes memory allocations for command buffers and synchronization
 * - Configures SMMU context for memory protection and isolation
 * - Establishes communication channels with firmware for this context
 * - Sets up queue allocator for managing multiple queues within the context
 * - Marks the context as initialized and ready for operation
 *
 * The user provides a CCQ range (inclusive on both ends) and the KMD will pick
 * one CCQ from this range. The context must be created using
 * @ref pva_kmd_context_create() before calling this function. After successful
 * initialization, the context is ready to accept command submissions and
 * resource registrations.
 *
 * @param[in, out] ctx                 Pointer to @ref pva_kmd_context structure
 *                                     Valid value: non-null, created but not yet initialized
 * @param[in] res_table_capacity       Maximum number of resources the context can manage
 *                                     Valid range: [1 .. PVA_MAX_RESOURCE_TABLE_ENTRIES]
 *
 * @retval PVA_SUCCESS                    Context initialized successfully
 * @retval PVA_INVAL                     Context is already initialized
 * @retval PVA_BAD_PARAMETER_ERROR       Invalid resource table capacity (zero)
 * @retval PVA_NOMEM                     Memory allocation failed for context resources
 * @retval PVA_NO_RESOURCE_ID            No available resource IDs for memory registration
 */
enum pva_error pva_kmd_context_init(struct pva_kmd_context *ctx,
				    uint32_t res_table_capacity,
				    uint64_t status_shm_hdl);

/**
 * @brief Retrieve a context by its allocation ID from the device
 *
 * @details This function performs the following operations:
 * - Validates the provided allocation ID against the device's context pool
 * - Uses the device's context allocator to locate the corresponding context
 * - Performs bounds checking to ensure the allocation ID is valid
 * - Returns the context pointer if found, or NULL if invalid
 * - Provides safe access to contexts using their allocation identifiers
 *
 * This function provides a safe way to retrieve context pointers using
 * allocation IDs, which are used internally by the KMD to track and
 * manage contexts. The allocation ID is typically assigned during context
 * creation and remains valid until context destruction.
 *
 * @param[in] pva       Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 * @param[in] alloc_id  Allocation ID of the context to retrieve
 *                      Valid range: [0 .. max_n_contexts-1]
 *
 * @retval non-null  Pointer to @ref pva_kmd_context if allocation ID is valid
 * @retval NULL      Invalid allocation ID or context not found
 */
struct pva_kmd_context *pva_kmd_get_context(struct pva_kmd_device *pva,
					    uint8_t alloc_id);

#endif // PVA_KMD_CONTEXT_H
