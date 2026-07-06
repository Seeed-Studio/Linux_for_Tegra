/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_BLOCK_ALLOCATOR_H
#define PVA_KMD_BLOCK_ALLOCATOR_H

#include "pva_api.h"
#include "pva_kmd_mutex.h"

/**
 * @brief Block allocator for managing fixed-size memory blocks
 *
 * @details This structure provides efficient allocation and deallocation of
 * fixed-size memory blocks from a pre-allocated memory region. It maintains
 * a free list of available blocks and provides thread-safe access through
 * mutex protection. The allocator is particularly useful for managing pools
 * of objects such as command buffers, resource records, or other frequently
 * allocated data structures.
 */
struct pva_kmd_block_allocator {
	/**
	 * @brief Head of the free slot linked list
	 * Valid range: [0 .. max_num_blocks-1] or special sentinel values
	 */
	uint32_t free_slot_head;

	/**
	 * @brief Base ID for allocated blocks
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t base_id;

	/**
	 * @brief Maximum number of blocks that can be allocated
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t max_num_blocks;

	/**
	 * @brief Next free slot to be allocated
	 * Valid range: [0 .. max_num_blocks-1]
	 */
	uint32_t next_free_slot;

	/**
	 * @brief Size of each block in bytes
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t block_size;

	/**
	 * @brief Pointer to the base of the block memory region
	 */
	void *blocks;

	/**
	 * @brief Array tracking which slots are currently in use
	 */
	bool *slot_in_use;

	/**
	 * @brief Mutex protecting allocator operations for thread safety
	 */
	pva_kmd_mutex_t allocator_lock;
};

/**
 * @brief Initialize a block allocator with specified memory and parameters
 *
 * @details This function performs the following operations:
 * - Initializes the allocator structure with provided memory region
 * - Sets up the free list linking all available blocks
 * - Configures block size and maximum block count parameters
 * - Initializes the usage tracking array for allocated blocks
 * - Sets up mutex for thread-safe allocation operations
 * - Establishes base ID for block identification
 * - Prepares the allocator for block allocation and deallocation
 *
 * The provided memory region must be large enough to hold max_num_chunks
 * blocks of chunk_size bytes each. After successful initialization, blocks
 * can be allocated using @ref pva_kmd_alloc_block() and freed using
 * @ref pva_kmd_free_block().
 *
 * @param[out] allocator     Pointer to @ref pva_kmd_block_allocator structure to initialize
 *                           Valid value: non-null
 * @param[in] chunk_mem      Pointer to pre-allocated memory for block storage
 *                           Valid value: non-null, must be at least
 *                           (chunk_size * max_num_chunks) bytes
 * @param[in] base_id        Base identifier for allocated blocks
 *                           Valid range: [0 .. UINT32_MAX]
 * @param[in] chunk_size     Size of each block in bytes
 *                           Valid range: [1 .. UINT32_MAX]
 * @param[in] max_num_chunks Maximum number of blocks that can be allocated
 *                           Valid range: [1 .. UINT32_MAX]
 *
 * @retval PVA_SUCCESS       Allocator initialized successfully
 * @retval PVA_NOMEM         Failed to allocate memory for slot tracking array
 */
enum pva_error
pva_kmd_block_allocator_init(struct pva_kmd_block_allocator *allocator,
			     void *chunk_mem, uint32_t base_id,
			     uint32_t chunk_size, uint32_t max_num_chunks);

/**
 * @brief Allocate a block from the allocator (thread-safe version)
 *
 * @details This function performs the following operations:
 * - Acquires the allocator mutex for thread-safe operation
 * - Checks if any blocks are available for allocation
 * - Removes a block from the free list if available
 * - Marks the block as in use in the usage tracking array
 * - Assigns a unique block ID based on the base ID and slot index
 * - Returns pointer to the allocated block and its ID
 * - Releases the allocator mutex after completion
 *
 * The allocated block remains valid until it is freed using
 * @ref pva_kmd_free_block(). The block ID can be used to reference
 * the block in other operations or to free it later.
 *
 * @param[in, out] allocator  Pointer to @ref pva_kmd_block_allocator structure
 *                            Valid value: non-null, must be initialized
 * @param[out] out_id         Pointer to store the allocated block ID
 *                            Valid value: non-null
 *
 * @retval non-null  Pointer to allocated block if successful
 * @retval NULL      No blocks available for allocation
 */
void *pva_kmd_alloc_block(struct pva_kmd_block_allocator *allocator,
			  uint32_t *out_id);

/**
 * @brief Allocate a block from the allocator (unsafe version)
 *
 * @details This function performs the following operations:
 * - Checks if any blocks are available for allocation
 * - Removes a block from the free list if available
 * - Marks the block as in use in the usage tracking array
 * - Assigns a unique block ID based on the base ID and slot index
 * - Returns pointer to the allocated block and its ID
 *
 * This function is not thread-safe and requires external synchronization.
 * The caller must ensure proper locking around calls to this function and
 * the corresponding @ref pva_kmd_free_block_unsafe().
 *
 * @param[in, out] allocator  Pointer to @ref pva_kmd_block_allocator structure
 *                            Valid value: non-null, must be initialized
 * @param[out] out_id         Pointer to store the allocated block ID
 *                            Valid value: non-null
 *
 * @retval non-null  Pointer to allocated block if successful
 * @retval NULL      No blocks available for allocation
 */
void *pva_kmd_alloc_block_unsafe(struct pva_kmd_block_allocator *allocator,
				 uint32_t *out_id);

/**
 * @brief Allocate and zero-initialize a block from the allocator
 *
 * @details This function performs the following operations:
 * - Calls @ref pva_kmd_alloc_block() to allocate a block
 * - Zero-initializes the entire block using @ref memset()
 * - Returns pointer to the zero-initialized block
 * - Provides the allocated block ID through the output parameter
 *
 * This inline function provides a convenient way to allocate blocks that
 * need to be initialized to zero. The zero initialization covers the entire
 * block size as specified during allocator initialization.
 *
 * @param[in, out] allocator  Pointer to @ref pva_kmd_block_allocator structure
 *                            Valid value: non-null, must be initialized
 * @param[out] out_id         Pointer to store the allocated block ID
 *                            Valid value: non-null
 *
 * @retval non-null  Pointer to zero-initialized allocated block if successful
 * @retval NULL      No blocks available for allocation
 */
static inline void *
pva_kmd_zalloc_block(struct pva_kmd_block_allocator *allocator,
		     uint32_t *out_id)
{
	void *ptr = pva_kmd_alloc_block(allocator, out_id);
	if (ptr != NULL) {
		(void)memset(ptr, 0, allocator->block_size);
	}
	return ptr;
}

/**
 * @brief Get pointer to a block by ID without allocation (unsafe version)
 *
 * @details This function performs the following operations:
 * - Validates the provided block ID against the allocator's range
 * - Calculates the memory address of the block from the ID
 * - Returns pointer to the block without changing its allocation state
 * - Does not perform any allocation or reference counting
 *
 * This API is not thread safe and has to be explicitly locked during use of the obtained block.
 * This is to ensure that a parallel free operation does not result in dangling pointer to obtained block.
 * Correct usage:
 *    lock(allocator)
 *    block - @ref pva_kmd_get_block_unsafe();
 *    use block
 *    unlock(allocator)
 *
 * @param[in] allocator  Pointer to @ref pva_kmd_block_allocator structure
 *                       Valid value: non-null, must be initialized
 * @param[in] id         Block ID to retrieve
 *                       Valid range: [base_id .. base_id+max_num_blocks-1]
 *
 * @retval non-null  Pointer to the block if ID is valid
 * @retval NULL      Invalid block ID or allocator not properly initialized
 */
void *pva_kmd_get_block_unsafe(struct pva_kmd_block_allocator *allocator,
			       uint32_t id);

/**
 * @brief Free a previously allocated block (thread-safe version)
 *
 * @details This function performs the following operations:
 * - Acquires the allocator mutex for thread-safe operation
 * - Validates the provided block ID against allocated blocks
 * - Marks the block as no longer in use in the usage tracking array
 * - Adds the block back to the free list for future allocation
 * - Updates allocator state to reflect the freed block
 * - Releases the allocator mutex after completion
 *
 * The block ID must correspond to a previously allocated block. After
 * freeing, the block becomes available for future allocation and the
 * ID should not be used to access the block.
 *
 * @param[in, out] allocator  Pointer to @ref pva_kmd_block_allocator structure
 *                            Valid value: non-null, must be initialized
 * @param[in] id              Block ID to free
 *                            Valid range: [base_id .. base_id+max_num_blocks-1]
 *
 * @retval PVA_SUCCESS        Block freed successfully
 * @retval PVA_INVAL          Invalid block ID or block not allocated
 */
enum pva_error pva_kmd_free_block(struct pva_kmd_block_allocator *allocator,
				  uint32_t id);

/**
 * @brief Free a previously allocated block (unsafe version)
 *
 * @details This function performs the following operations:
 * - Validates the provided block ID against allocated blocks
 * - Marks the block as no longer in use in the usage tracking array
 * - Adds the block back to the free list for future allocation
 * - Updates allocator state to reflect the freed block
 *
 * This function is not thread-safe and requires external synchronization.
 * The caller must ensure proper locking around calls to this function and
 * the corresponding @ref pva_kmd_alloc_block_unsafe().
 *
 * @param[in, out] allocator  Pointer to @ref pva_kmd_block_allocator structure
 *                            Valid value: non-null, must be initialized
 * @param[in] id              Block ID to free
 *                            Valid range: [base_id .. base_id+max_num_blocks-1]
 *
 * @retval PVA_SUCCESS        Block freed successfully
 * @retval PVA_INVAL          Invalid block ID or block not allocated
 */
enum pva_error
pva_kmd_free_block_unsafe(struct pva_kmd_block_allocator *allocator,
			  uint32_t id);

/**
 * @brief Deinitialize a block allocator and clean up resources
 *
 * @details This function performs the following operations:
 * - Ensures no blocks are currently allocated (debugging builds)
 * - Deinitializes the allocator mutex using @ref pva_kmd_mutex_deinit()
 * - Cleans up internal allocator state and data structures
 * - Invalidates the allocator for future use
 * - Does not free the underlying memory region (caller's responsibility)
 *
 * All allocated blocks should be freed before calling this function.
 * After deinitialization, the allocator cannot be used for block allocation
 * until it is reinitialized. The underlying memory region provided during
 * initialization is not freed and remains the caller's responsibility.
 *
 * @param[in, out] allocator  Pointer to @ref pva_kmd_block_allocator structure to deinitialize
 *                            Valid value: non-null, must be initialized
 */
void pva_kmd_block_allocator_deinit(struct pva_kmd_block_allocator *allocator);

#endif // PVA_KMD_BLOCK_ALLOCATOR_H
