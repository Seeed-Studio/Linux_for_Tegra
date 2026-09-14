/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_QUEUE_H
#define PVA_KMD_QUEUE_H
#include "pva_fw.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_mutex.h"

/**
 * @brief Structure for managing PVA command submission queues
 *
 * @details This structure represents a command queue used for submitting
 * command buffers to the PVA firmware. Each queue is associated with a
 * specific CCQ (Command and Control Queue) and maintains submission state,
 * memory management, and synchronization mechanisms for thread-safe operation.
 */
struct pva_kmd_queue {
	/**
	 * @brief Pointer to the parent PVA device
	 * Valid value: non-null
	 */
	struct pva_kmd_device *pva;

	/**
	 * @brief Device memory allocation for queue data structures
	 */
	struct pva_kmd_device_memory *queue_memory;

	/**
	 * @brief Pointer to firmware queue header for submission tracking
	 */
	struct pva_fw_submit_queue_header *queue_header;

	/**
	 * @brief CCQ (Command and Control Queue) identifier
	 * Valid range: [0 .. PVA_MAX_NUM_CCQ-1]
	 */
	uint8_t ccq_id;

	/**
	 * @brief Queue identifier within the CCQ
	 * Valid range: [0 .. PVA_MAX_QUEUES_PER_CCQ-1]
	 */
	uint8_t queue_id;

	/**
	 * @brief Maximum number of submissions this queue can hold
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t max_num_submit;
};

/**
 * @brief Initialize a PVA command queue structure
 *
 * @details This function performs the following operations:
 * - Initializes all fields of the queue structure with provided parameters
 * - Associates the queue with the specified PVA device and CCQ
 * - Sets up queue memory and header pointers for submission tracking
 * - Configures synchronization mechanisms using the provided CCQ lock
 * - Prepares the queue for command buffer submissions
 *
 * The queue structure must be allocated before calling this function.
 * After initialization, the queue is ready to accept command submissions
 * using @ref pva_kmd_queue_submit().
 *
 * @param[out] queue           Pointer to @ref pva_kmd_queue structure to initialize
 *                             Valid value: non-null
 * @param[in] pva              Pointer to @ref pva_kmd_device structure
 *                             Valid value: non-null
 * @param[in] ccq_id           CCQ identifier for this queue
 *                             Valid range: [0 .. PVA_MAX_NUM_CCQ-1]
 * @param[in] queue_id         Queue identifier within the CCQ
 *                             Valid range: [0 .. PVA_MAX_QUEUES_PER_CCQ-1]
 * @param[in] ccq_lock         Pointer to mutex for CCQ synchronization
 *                             Valid value: non-null, must be initialized
 * @param[in] queue_memory     Pointer to allocated queue memory
 *                             Valid value: non-null
 * @param[in] max_num_submit   Maximum number of submissions for this queue
 *                             Valid range: [1 .. UINT32_MAX]
 */
void pva_kmd_queue_init(struct pva_kmd_queue *queue, struct pva_kmd_device *pva,
			uint8_t ccq_id, uint8_t queue_id,
			struct pva_kmd_device_memory *queue_memory,
			uint32_t max_num_submit);

/**
 * @brief Create a new user queue within a context
 *
 * @details This function performs the following operations:
 * - Validates the queue creation parameters provided by the user
 * - Allocates a new queue identifier from the context's queue pool
 * - Initializes queue data structures and memory allocations
 * - Sets up the queue for the specified context and configuration
 * - Registers the queue with the firmware for operation
 * - Returns the allocated queue identifier to the caller
 *
 * The created queue can be used for submitting command buffers within
 * the specified context. The queue should be destroyed using
 * @ref pva_kmd_queue_destroy() when no longer needed.
 *
 * @param[in, out] ctx      Pointer to @ref pva_kmd_context structure
 *                          Valid value: non-null, must be initialized
 * @param[in] in_args       Pointer to queue creation parameters
 *                          Valid value: non-null
 * @param[out] queue_id     Pointer to store the allocated queue identifier
 *                          Valid value: non-null
 *
 * @retval PVA_SUCCESS                  Queue created successfully
 * @retval PVA_INVAL                    Invalid parameters or context
 * @retval PVA_NOMEM                    Failed to allocate queue resources
 * @retval PVA_NO_RESOURCE_ID           No available queue identifiers
 * @retval PVA_TIMEDOUT                 Failed to register queue with firmware
 */
enum pva_error pva_kmd_queue_create(struct pva_kmd_context *ctx,
				    const struct pva_ops_queue_create *in_args,
				    uint32_t *queue_id);

/**
 * @brief Destroy a user queue and free associated resources
 *
 * @details This function performs the following operations:
 * - Validates that the queue identifier is valid for the context
 * - Ensures no pending submissions remain in the queue
 * - Unregisters the queue from firmware operation
 * - Frees allocated memory and data structures for the queue
 * - Returns the queue identifier to the context's available pool
 * - Cleans up synchronization mechanisms and state
 *
 * All pending operations on the queue should be completed before
 * calling this function. After destruction, the queue identifier
 * becomes invalid and cannot be used for further operations.
 *
 * @param[in, out] ctx      Pointer to @ref pva_kmd_context structure
 *                          Valid value: non-null, must be initialized
 * @param[in] queue_id      Queue identifier to destroy
 *                          Valid range: [0 .. max_queues-1]
 *
 * @retval PVA_SUCCESS              Queue destroyed successfully
 * @retval PVA_INVAL                Invalid queue identifier or context
 * @retval PVA_AGAIN                Queue has pending operations
 * @retval PVA_TIMEDOUT             Failed to unregister queue from firmware
 */
enum pva_error pva_kmd_queue_destroy(struct pva_kmd_context *ctx,
				     uint32_t queue_id);

/**
 * @brief Submit a command buffer to the specified queue
 *
 * @details This function performs the following operations:
 * - Validates the submission information and queue state
 * - Acquires the queue's CCQ lock for thread-safe submission
 * - Adds the submission to the queue's submission ring buffer
 * - Updates queue pointers and submission tracking information
 * - Notifies firmware of the new submission via CCQ mechanism
 * - Releases the CCQ lock after successful submission
 * - Handles submission errors and queue overflow conditions
 *
 * The command buffer must be properly prepared and all referenced
 * resources must be registered before calling this function. The
 * submission is processed asynchronously by the firmware.
 *
 * @param[in, out] queue        Pointer to @ref pva_kmd_queue structure
 *                              Valid value: non-null, must be initialized
 * @param[in] submit_info       Pointer to command buffer submission information
 *                              Valid value: non-null
 *
 * @retval PVA_SUCCESS              Submission completed successfully
 * @retval PVA_INVAL                Invalid queue or submission parameters
 * @retval PVA_QUEUE_FULL           Queue has no space for new submissions
 * @retval PVA_TIMEDOUT             Failed to notify firmware of submission
 */
enum pva_error
pva_kmd_queue_submit(struct pva_kmd_queue *queue,
		     struct pva_fw_cmdbuf_submit_info const *submit_info);

/**
 * @brief Get available space in the submission queue
 *
 * @details This function performs the following operations:
 * - Reads the current queue head and tail pointers
 * - Calculates the number of available submission slots
 * - Accounts for queue wraparound and boundary conditions
 * - Returns the number of submissions that can be queued
 * - Provides thread-safe access to queue state information
 *
 * This function can be used to check if the queue has space for
 * additional submissions before attempting to submit command buffers.
 * The returned value represents an instantaneous snapshot and may
 * change if other threads are also submitting to the queue.
 *
 * @param[in] queue  Pointer to @ref pva_kmd_queue structure
 *                   Valid value: non-null, must be initialized
 *
 * @retval space_count  Number of available submission slots in the queue
 */
uint32_t pva_kmd_queue_space(struct pva_kmd_queue *queue);

/**
 * @brief Get read-write syncpoint information for a specific queue
 *
 * @details This function performs the following operations:
 * - Validates the provided CCQ and queue identifiers
 * - Looks up the syncpoint information for the specified queue
 * - Returns the syncpoint configuration including ID and IOVA address
 * - Provides access to queue-specific synchronization mechanisms
 * - Ensures proper mapping between queues and their assigned syncpoints
 *
 * The returned syncpoint information can be used for synchronization
 * operations and fence management specific to the queue. Each queue
 * is assigned dedicated syncpoint resources for independent operation.
 *
 * @param[in] pva       Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 * @param[in] ccq_id    CCQ identifier
 *                      Valid range: [0 .. PVA_MAX_NUM_CCQ-1]
 * @param[in] queue_id  Queue identifier within the CCQ
 *                      Valid range: [0 .. PVA_MAX_QUEUES_PER_CCQ-1]
 *
 * @retval non-null  Pointer to @ref pva_syncpt_rw_info structure
 * @retval NULL      Invalid CCQ/queue identifier or not configured
 */
const struct pva_syncpt_rw_info *
pva_kmd_queue_get_rw_syncpt_info(struct pva_kmd_device *pva, uint8_t ccq_id,
				 uint8_t queue_id);

#endif // PVA_KMD_QUEUE_H
