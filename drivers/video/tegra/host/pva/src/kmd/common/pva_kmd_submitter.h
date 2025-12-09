/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SUBMITTER_H
#define PVA_KMD_SUBMITTER_H
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_mutex.h"
#include "pva_kmd_queue.h"

/**
 * @brief Thread-safe command buffer submission utility
 *
 * @details This structure provides a thread-safe interface for submitting
 * command buffers to PVA queues. It manages submission synchronization,
 * fence handling, and command buffer chunk allocation to ensure safe
 * concurrent access from multiple threads while maintaining proper
 * ordering and completion tracking.
 */
struct pva_kmd_submitter {
	/**
	 * @brief Lock protecting submission operations and post fence increment
	 *
	 * @details The lock protects the submission to the queue, including
	 * incrementing the post fence
	 */
	pva_kmd_mutex_t *submit_lock;

	/**
	 * @brief Pointer to the target queue for submissions
	 */
	struct pva_kmd_queue *queue;

	/**
	 * @brief Virtual address of post fence value in memory
	 */
	uint32_t *post_fence_va;

	/**
	 * @brief Post fence configuration for submission completion tracking
	 */
	struct pva_fw_postfence post_fence;

	/**
	 * @brief Next fence value to be assigned to submissions
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t fence_future_value;

	/**
	 * @brief Lock protecting chunk pool operations
	 *
	 * @details This lock protects the use of the chunk_pool
	 */
	pva_kmd_mutex_t *chunk_pool_lock;

	/**
	 * @brief Pointer to command buffer chunk pool for allocation
	 */
	struct pva_kmd_cmdbuf_chunk_pool *chunk_pool;
};

/**
 * @brief Initialize a PVA command buffer submitter
 *
 * @details This function performs the following operations:
 * - Initializes all fields of the submitter structure with provided parameters
 * - Associates the submitter with the specified queue and synchronization primitives
 * - Sets up fence management using the provided post fence configuration
 * - Configures command buffer chunk allocation through the provided chunk pool
 * - Prepares the submitter for thread-safe command buffer submissions
 * - Establishes proper locking mechanisms for concurrent operation
 *
 * The submitter structure must be allocated before calling this function.
 * After initialization, the submitter is ready for command buffer preparation
 * and submission operations using the other submitter functions.
 *
 * @param[out] submitter       Pointer to @ref pva_kmd_submitter structure to initialize
 *                             Valid value: non-null
 * @param[in] queue            Pointer to target @ref pva_kmd_queue for submissions
 *                             Valid value: non-null, must be initialized
 * @param[in] submit_lock      Pointer to mutex for submission synchronization
 *                             Valid value: non-null, must be initialized
 * @param[in] chunk_pool       Pointer to command buffer chunk pool
 *                             Valid value: non-null, must be initialized
 * @param[in] chunk_pool_lock  Pointer to mutex for chunk pool synchronization
 *                             Valid value: non-null, must be initialized
 * @param[in] post_fence_va    Virtual address of post fence memory location
 *                             Valid value: non-null
 * @param[in] post_fence       Pointer to post fence configuration
 *                             Valid value: non-null
 */
void pva_kmd_submitter_init(struct pva_kmd_submitter *submitter,
			    struct pva_kmd_queue *queue,
			    pva_kmd_mutex_t *submit_lock,
			    struct pva_kmd_cmdbuf_chunk_pool *chunk_pool,
			    pva_kmd_mutex_t *chunk_pool_lock,
			    uint32_t *post_fence_va,
			    struct pva_fw_postfence const *post_fence);

/**
 * @brief Prepare a command buffer builder for submission
 *
 * @details This function performs the following operations:
 * - Acquires the chunk pool lock for thread-safe chunk allocation
 * - Allocates command buffer chunks from the submitter's chunk pool
 * - Initializes the command buffer builder with allocated chunks
 * - Sets up the builder for command construction and submission
 * - Configures chunk linking and memory management for the builder
 * - Releases the chunk pool lock after successful preparation
 *
 * The prepared builder can be used to construct command sequences using
 * the command buffer builder API. After command construction is complete,
 * the builder should be submitted using @ref pva_kmd_submitter_submit().
 *
 * @param[in, out] submitter  Pointer to @ref pva_kmd_submitter structure
 *                            Valid value: non-null, must be initialized
 * @param[out] builder        Pointer to @ref pva_kmd_cmdbuf_builder to prepare
 *                            Valid value: non-null
 *
 * @retval PVA_SUCCESS                  Builder prepared successfully
 * @retval PVA_NOMEM                    Failed to allocate command buffer chunks
 * @retval PVA_ENOSPC                   Chunk pool has no available chunks
 */
enum pva_error
pva_kmd_submitter_prepare(struct pva_kmd_submitter *submitter,
			  struct pva_kmd_cmdbuf_builder *builder);

/**
 * @brief Submit a prepared command buffer to the queue
 *
 * @details This function performs the following operations:
 * - Acquires the submission lock for thread-safe queue operations
 * - Finalizes the command buffer and prepares submission information
 * - Assigns a unique fence value for completion tracking
 * - Submits the command buffer to the associated queue using @ref pva_kmd_queue_submit()
 * - Updates the post fence future value for subsequent submissions
 * - Returns the assigned fence value to the caller for synchronization
 * - Releases the submission lock after successful submission
 *
 * The command buffer builder must be properly prepared using
 * @ref pva_kmd_submitter_prepare() and populated with commands before
 * calling this function. The returned fence value can be used with
 * @ref pva_kmd_submitter_wait() to synchronize on completion.
 *
 * @param[in, out] submitter   Pointer to @ref pva_kmd_submitter structure
 *                             Valid value: non-null, must be initialized
 * @param[in, out] builder     Pointer to prepared @ref pva_kmd_cmdbuf_builder
 *                             Valid value: non-null, must be prepared
 * @param[out] out_fence_val   Pointer to store the assigned fence value
 *                             Valid value: non-null
 *
 * @retval PVA_SUCCESS              Submission completed successfully
 * @retval PVA_INVAL                Invalid submitter or builder parameters
 * @retval PVA_QUEUE_FULL           Target queue has no space for submission
 * @retval PVA_TIMEDOUT             Failed to notify firmware of submission
 */
enum pva_error pva_kmd_submitter_submit(struct pva_kmd_submitter *submitter,
					struct pva_kmd_cmdbuf_builder *builder,
					uint32_t *out_fence_val);

/**
 * @brief Wait for a submitted command buffer to complete
 *
 * @details This function performs the following operations:
 * - Polls the post fence memory location for the specified fence value
 * - Uses configurable polling intervals to balance CPU usage and latency
 * - Implements timeout mechanisms to prevent indefinite blocking
 * - Checks for fence value completion using memory-mapped reads
 * - Returns success when the fence value is reached or timeout on failure
 * - Provides blocking synchronization for submitted command buffers
 *
 * The fence value must be obtained from a previous call to
 * @ref pva_kmd_submitter_submit(). The function will block until the
 * command buffer associated with the fence value completes execution
 * or the timeout period expires.
 *
 * @param[in] submitter         Pointer to @ref pva_kmd_submitter structure
 *                              Valid value: non-null, must be initialized
 * @param[in] fence_val         Fence value to wait for completion
 *                              Valid range: [0 .. UINT32_MAX]
 * @param[in] poll_interval_ms  Polling interval in milliseconds
 *                              Valid range: [1 .. UINT32_MAX]
 * @param[in] timeout_ms        Timeout period in milliseconds
 *                              Valid range: [1 .. UINT32_MAX]
 *
 * @retval PVA_SUCCESS       Fence value reached, command buffer completed
 * @retval PVA_TIMEDOUT      Timeout expired before fence value reached
 */
enum pva_error pva_kmd_submitter_wait(struct pva_kmd_submitter *submitter,
				      uint32_t fence_val,
				      uint32_t poll_interval_ms,
				      uint32_t timeout_ms);

/**
 * @brief Submit a command buffer with custom fence configuration
 *
 * @details This function performs the following operations:
 * - Acquires the submission lock for thread-safe queue operations
 * - Finalizes the command buffer using the provided custom fence configuration
 * - Submits the command buffer to the associated queue with custom fence
 * - Uses the provided fence instead of the submitter's default post fence
 * - Enables custom synchronization mechanisms for specialized use cases
 * - Releases the submission lock after successful submission
 *
 * This function allows for custom fence configurations that may differ
 * from the submitter's default post fence. The custom fence can specify
 * different syncpoint targets, fence values, or synchronization behavior
 * for specialized submission requirements.
 *
 * @param[in, out] submitter  Pointer to @ref pva_kmd_submitter structure
 *                            Valid value: non-null, must be initialized
 * @param[in, out] builder    Pointer to prepared @ref pva_kmd_cmdbuf_builder
 *                            Valid value: non-null, must be prepared
 * @param[in] fence           Pointer to custom @ref pva_fw_postfence configuration
 *                            Valid value: non-null
 *
 * @retval PVA_SUCCESS              Submission completed successfully
 * @retval PVA_INVAL                Invalid submitter, builder, or fence parameters
 * @retval PVA_QUEUE_FULL           Target queue has no space for submission
 * @retval PVA_TIMEDOUT             Failed to notify firmware of submission
 */
enum pva_error
pva_kmd_submitter_submit_with_fence(struct pva_kmd_submitter *submitter,
				    struct pva_kmd_cmdbuf_builder *builder,
				    struct pva_fw_postfence *fence);

/**
 * @brief Submit commands synchronously and wait for completion
 *
 * @details This function performs the following operations:
 * - Prepares a command buffer builder with chunk allocation
 * - Reserves space in the command buffer for the provided commands
 * - Copies the command data into the reserved buffer space
 * - Submits the command buffer to the firmware via the queue
 * - Waits for command execution to complete using fence synchronization
 * - Automatically cleans up allocated chunks upon completion or error
 * - Provides a simplified interface for single-shot command submission
 *
 * This is a convenience function that combines command buffer preparation,
 * submission, and synchronous waiting into a single operation. The total
 * command size must fit within a single chunk. For larger command sequences
 * or asynchronous operation, use the individual submitter functions.
 *
 * @param[in] submitter        Pointer to @ref pva_kmd_submitter structure
 *                             Valid value: non-null, must be initialized
 * @param[in] cmds             Pointer to command data to submit
 *                             Valid value: non-null
 * @param[in] size             Size of command data in bytes
 *                             Valid range: [1 .. chunk_size]
 * @param[in] poll_interval_us Polling interval for fence checking in microseconds
 *                             Valid range: [1 .. UINT32_MAX]
 * @param[in] timeout_us       Maximum timeout for command completion in microseconds
 *                             Valid range: [poll_interval_us .. UINT32_MAX]
 *
 * @retval PVA_SUCCESS          Commands submitted and completed successfully
 * @retval PVA_NOMEM            Failed to allocate command buffer chunk
 * @retval PVA_INVAL            Command size exceeds chunk capacity
 * @retval PVA_QUEUE_FULL       Submission queue has no space available
 * @retval PVA_TIMEDOUT         Command execution or submission timed out
 * @retval PVA_ERR_FW_ABORTED   Firmware operation aborted during wait
 */
enum pva_error pva_kmd_submit_cmd_sync(struct pva_kmd_submitter *submitter,
				       void *cmds, uint32_t size,
				       uint32_t poll_interval_us,
				       uint32_t timeout_us);

#endif // PVA_KMD_SUBMITTER_H
