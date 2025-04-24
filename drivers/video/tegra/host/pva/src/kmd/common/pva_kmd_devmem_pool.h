/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_DEVMEM_POOL_H
#define PVA_KMD_DEVMEM_POOL_H
#include "pva_api.h"
#include "pva_kmd_block_allocator.h"
#include "pva_kmd_device_memory.h"

/** @brief A segment of a device memory pool.
 *
 * It holds a fixed size array of device memory blocks. A pool is a linked list
 * of segments.
 */
struct pva_kmd_devmem_pool_segment {
	/** The owner pool. */
	struct pva_kmd_devmem_pool *owner_pool;
	/** The next segment in the pool. */
	struct pva_kmd_devmem_pool_segment *next;
	/** The device memory for the segment. */
	struct pva_kmd_device_memory *mem;
	/** The allocator for the elements in the segment. */
	struct pva_kmd_block_allocator elem_allocator;
	/** The number of free elements in the segment. */
	uint32_t n_free_ele;
};

/** @brief A device memory pool that holds fixed size elements.
 *
 * It allocates memory in segments, each segment contains n_element_incr
 * elements.
 * - element_size will be rounded up to the nearest 8 bytes for alignment.
 * - The pool is initialized with element_size * n_element_incr capacity.
 * - Once exhausted, the pool will allocate a new segment of memory and increase
 *   the capacity by n_element_incr.
 * - When an element is freed, the pool does not immediately release the whole
 *   segment even if the whole segment is empty. However, if there are 2 *
 *   n_element_incr free elements, the pool will release a whole segment, so
 *   that there's still at least n_element_incr free elements.
 * - The pool is thread safe.
 */
struct pva_kmd_devmem_pool {
	/** The SMMU context index for the pool. */
	uint8_t smmu_ctx_idx;
	/** The size of each element in the pool. */
	uint32_t element_size;
	/** The number of elements to allocate in each segment. */
	uint32_t n_element_incr;
	/** The total number of free elements in the pool, across all segments. */
	uint32_t n_free_element;
	/** The head of the segment list. */
	struct pva_kmd_devmem_pool_segment *segment_list_head;
	/** The PVA device. */
	struct pva_kmd_device *pva;
	/** The mutex for the pool. */
	pva_kmd_mutex_t pool_lock;
};

/** @brief Device memory from a pool.
 *
 * It is an element in a segment of a pool.
 */
struct pva_kmd_devmem_element {
	/** The segment that contains the element. */
	struct pva_kmd_devmem_pool_segment *segment;
	/** The index of the element in the segment. */
	uint32_t ele_idx;
};

/** @brief Get the IOVA of a device memory element. */
uint64_t pva_kmd_get_devmem_iova(struct pva_kmd_devmem_element const *devmem);

/** @brief Get the virtual address of a device memory element. */
void *pva_kmd_get_devmem_va(struct pva_kmd_devmem_element const *devmem);

/** @brief Initialize a device memory pool.
 *
 * @param pool The device memory pool to initialize.
 * @param pva The PVA device.
 * @param smmu_ctx_idx The SMMU context index for the pool.
 * @param element_size The size of each element in the pool.
 * @param ele_incr_count The number of elements to allocate in each segment.
 */
enum pva_error pva_kmd_devmem_pool_init(struct pva_kmd_devmem_pool *pool,
					struct pva_kmd_device *pva,
					uint8_t smmu_ctx_idx,
					uint32_t element_size,
					uint32_t ele_incr_count);

/** @brief Allocate a device memory element from a pool and zero-initialize it. */
enum pva_error
pva_kmd_devmem_pool_zalloc(struct pva_kmd_devmem_pool *pool,
			   struct pva_kmd_devmem_element *devmem);

/** @brief Free a device memory element from a pool. */
void pva_kmd_devmem_pool_free(struct pva_kmd_devmem_element *devmem);

/** @brief Deinitialize a device memory pool. */
void pva_kmd_devmem_pool_deinit(struct pva_kmd_devmem_pool *pool);

#endif
