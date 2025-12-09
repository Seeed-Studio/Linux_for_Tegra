/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_DEVMEM_POOL_H
#define PVA_KMD_DEVMEM_POOL_H
#include "pva_api.h"
#include "pva_kmd_block_allocator.h"
#include "pva_kmd_device_memory.h"

/**
 * @brief A segment of a device memory pool
 *
 * @details A segment holds a fixed size array of device memory blocks within
 * a device memory pool. The pool is implemented as a linked list of segments,
 * allowing dynamic expansion of the pool capacity as needed. Each segment
 * manages its own allocation state and provides efficient block allocation
 * and deallocation within the segment.
 */
struct pva_kmd_devmem_pool_segment {
	/**
	 * @brief Pointer to the owner pool
	 */
	struct pva_kmd_devmem_pool *owner_pool;

	/**
	 * @brief Pointer to the next segment in the pool linked list
	 */
	struct pva_kmd_devmem_pool_segment *next;

	/**
	 * @brief Device memory allocation for this segment
	 */
	struct pva_kmd_device_memory *mem;

	/**
	 * @brief Block allocator for managing elements within this segment
	 */
	struct pva_kmd_block_allocator elem_allocator;

	/**
	 * @brief Number of free elements currently available in this segment
	 * Valid range: [0 .. n_element_incr]
	 */
	uint32_t n_free_ele;
};

/**
 * @brief A device memory pool that manages fixed-size elements
 *
 * @details A device memory pool that holds fixed size elements with automatic
 * capacity expansion. The pool allocates memory in segments, each segment
 * containing n_element_incr elements. Key features include:
 * - element_size is rounded up to nearest 8 bytes for alignment
 * - Pool initialized with element_size * n_element_incr capacity
 * - Automatic segment allocation when pool exhausted
 * - Intelligent segment deallocation to maintain efficiency
 * - Thread-safe operations with mutex protection
 * - SMMU context-aware memory allocation
 */
struct pva_kmd_devmem_pool {
	/**
	 * @brief SMMU context index for memory allocation
	 * Valid range: [0 .. PVA_MAX_NUM_SMMU_CONTEXTS-1]
	 */
	uint8_t smmu_ctx_idx;

	/**
	 * @brief Size of each element in the pool in bytes
	 * Valid range: [1 .. UINT32_MAX], rounded up to 8-byte alignment
	 */
	uint32_t element_size;

	/**
	 * @brief Number of elements to allocate in each segment
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t n_element_incr;

	/**
	 * @brief Total number of free elements across all segments
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t n_free_element;

	/**
	 * @brief Head of the segment linked list
	 */
	struct pva_kmd_devmem_pool_segment *segment_list_head;

	/**
	 * @brief Pointer to the PVA device
	 */
	struct pva_kmd_device *pva;

	/**
	 * @brief Mutex for thread-safe pool operations
	 */
	pva_kmd_mutex_t pool_lock;
};

/**
 * @brief Device memory element from a pool
 *
 * @details Represents a single element allocated from a device memory pool.
 * The element is contained within a specific segment of the pool and can
 * be used to access the allocated memory through IOVA addresses or virtual
 * addresses as needed by the application.
 */
struct pva_kmd_devmem_element {
	/**
	 * @brief Pointer to the segment containing this element
	 */
	struct pva_kmd_devmem_pool_segment *segment;

	/**
	 * @brief Index of this element within the segment
	 * Valid range: [0 .. n_element_incr-1]
	 */
	uint32_t ele_idx;
};

/**
 * @brief Get the IOVA address of a device memory element
 *
 * @details This function performs the following operations:
 * - Calculates the IOVA address for the specified device memory element
 * - Uses the segment's device memory base address and element index
 * - Accounts for element size and alignment requirements
 * - Returns hardware-accessible address for DMA operations
 * - Provides address suitable for programming hardware registers
 *
 * The returned IOVA can be used by hardware to access the element
 * memory directly through the SMMU context associated with the pool.
 *
 * @param[in] devmem  Pointer to @ref pva_kmd_devmem_element structure
 *                    Valid value: non-null, must be allocated from pool
 *
 * @retval iova_address  IOVA address of the device memory element
 */
uint64_t pva_kmd_get_devmem_iova(struct pva_kmd_devmem_element const *devmem);

/**
 * @brief Get the virtual address of a device memory element
 *
 * @details This function performs the following operations:
 * - Calculates the virtual address for the specified device memory element
 * - Uses the segment's device memory base virtual address and element index
 * - Accounts for element size and alignment requirements
 * - Returns CPU-accessible virtual address for software operations
 * - Provides address suitable for direct memory access by CPU
 *
 * The returned virtual address can be used by software to read and write
 * the element memory directly using normal memory operations.
 *
 * @param[in] devmem  Pointer to @ref pva_kmd_devmem_element structure
 *                    Valid value: non-null, must be allocated from pool
 *
 * @retval virtual_address  Virtual address pointer to the device memory element
 */
void *pva_kmd_get_devmem_va(struct pva_kmd_devmem_element const *devmem);

/**
 * @brief Initialize a device memory pool
 *
 * @details This function performs the following operations:
 * - Initializes the device memory pool structure with specified parameters
 * - Rounds element_size up to 8-byte alignment for optimal access
 * - Allocates initial segment with n_element_incr elements
 * - Sets up SMMU context for memory allocation and mapping
 * - Initializes mutex for thread-safe pool operations
 * - Configures automatic segment management policies
 * - Prepares pool for element allocation and deallocation
 *
 * The initialized pool is ready for element allocation using
 * @ref pva_kmd_devmem_pool_zalloc() and will automatically expand
 * capacity by adding new segments as needed.
 *
 * @param[out] pool          Pointer to @ref pva_kmd_devmem_pool structure to initialize
 *                           Valid value: non-null
 * @param[in] pva            Pointer to @ref pva_kmd_device structure
 *                           Valid value: non-null, must be initialized
 * @param[in] smmu_ctx_idx   SMMU context index for memory allocation
 *                           Valid range: [0 .. PVA_MAX_NUM_SMMU_CONTEXTS-1]
 * @param[in] element_size   Size of each element in bytes
 *                           Valid range: [1 .. UINT32_MAX]
 * @param[in] ele_incr_count Number of elements per segment
 *                           Valid range: [1 .. UINT32_MAX]
 *
 * @retval PVA_SUCCESS                Pool initialized successfully
 * @retval PVA_NOMEM                  Failed to allocate initial segment
 * @retval PVA_INTERNAL               Failed to initialize pool mutex
 * @retval PVA_INVAL                  Failed to map memory to SMMU context
 */
enum pva_error pva_kmd_devmem_pool_init(struct pva_kmd_devmem_pool *pool,
					struct pva_kmd_device *pva,
					uint8_t smmu_ctx_idx,
					uint32_t element_size,
					uint32_t ele_incr_count);

/**
 * @brief Allocate and zero-initialize a device memory element from pool
 *
 * @details This function performs the following operations:
 * - Searches for available element in existing segments
 * - Allocates new segment if all existing segments are full
 * - Assigns element from segment using block allocator
 * - Zero-initializes the allocated element memory
 * - Updates pool statistics and segment free counts
 * - Returns element information for subsequent use
 * - Ensures thread-safe allocation through mutex protection
 *
 * The allocated element is zero-initialized and ready for use. The
 * element remains allocated until freed using @ref pva_kmd_devmem_pool_free().
 *
 * @param[in] pool     Pointer to @ref pva_kmd_devmem_pool structure
 *                     Valid value: non-null, must be initialized
 * @param[out] devmem  Pointer to @ref pva_kmd_devmem_element to populate
 *                     Valid value: non-null
 *
 * @retval PVA_SUCCESS                Element allocated successfully
 * @retval PVA_NOMEM                  Failed to allocate new segment
 * @retval PVA_ENOSPC                 Pool limits exceeded
 * @retval PVA_INVAL                  Failed to map new segment memory
 */
enum pva_error
pva_kmd_devmem_pool_zalloc(struct pva_kmd_devmem_pool *pool,
			   struct pva_kmd_devmem_element *devmem);

/**
 * @brief Free a device memory element back to its pool
 *
 * @details This function performs the following operations:
 * - Validates the element belongs to a valid segment and pool
 * - Returns the element to its segment's block allocator
 * - Updates segment and pool free element counts
 * - Checks for segment deallocation opportunities
 * - Frees empty segments if pool has excess capacity
 * - Maintains optimal pool memory usage
 * - Ensures thread-safe deallocation through mutex protection
 *
 * The pool automatically manages segment deallocation to maintain
 * efficiency while avoiding unnecessary memory fragmentation.
 * Segments are freed when there are more than 2 * n_element_incr
 * free elements, ensuring at least n_element_incr remain available.
 *
 * @param[in] devmem  Pointer to @ref pva_kmd_devmem_element to free
 *                    Valid value: non-null, must be allocated from pool
 */
void pva_kmd_devmem_pool_free(struct pva_kmd_devmem_element *devmem);

/**
 * @brief Deinitialize a device memory pool and free all resources
 *
 * @details This function performs the following operations:
 * - Ensures all elements have been freed back to the pool
 * - Traverses segment linked list and frees all segments
 * - Unmaps device memory from SMMU contexts
 * - Releases all allocated device memory
 * - Destroys mutex and synchronization primitives
 * - Cleans up pool data structures and state
 * - Invalidates pool for future use
 *
 * All elements must be freed before calling this function. After
 * deinitialization, the pool cannot be used for further operations
 * and any remaining element references become invalid.
 *
 * @param[in, out] pool  Pointer to @ref pva_kmd_devmem_pool structure to deinitialize
 *                       Valid value: non-null, must be initialized, all elements freed
 */
void pva_kmd_devmem_pool_deinit(struct pva_kmd_devmem_pool *pool);

#endif
