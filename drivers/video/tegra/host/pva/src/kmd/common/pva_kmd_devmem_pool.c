// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_devmem_pool.h"
#include "pva_kmd_limits.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_limits.h"
#include "pva_api.h"
#include "pva_utils.h"

static uint64_t get_devmem_offset(struct pva_kmd_devmem_element const *devmem)
{
	return (uint64_t)safe_mulu32(devmem->ele_idx,
				     devmem->segment->owner_pool->element_size);
}

uint64_t pva_kmd_get_devmem_iova(struct pva_kmd_devmem_element const *devmem)
{
	return safe_addu64(devmem->segment->mem->iova,
			   get_devmem_offset(devmem));
}

void *pva_kmd_get_devmem_va(struct pva_kmd_devmem_element const *devmem)
{
	return pva_offset_pointer(devmem->segment->mem->va,
				  get_devmem_offset(devmem));
}

static struct pva_kmd_devmem_pool_segment *
allocate_segment(struct pva_kmd_devmem_pool *pool)
{
	struct pva_kmd_devmem_pool_segment *segment;
	struct pva_kmd_device_memory *mem = NULL;
	uint64_t segment_size = safe_mulu64((uint64_t)pool->element_size,
					    (uint64_t)pool->n_element_incr);
	void *va;
	enum pva_error err;

	/* Allocate the segment structure */
	segment = pva_kmd_zalloc(sizeof(*segment));
	if (segment == NULL) {
		goto err_out;
	}

	/* Allocate device memory */
	mem = pva_kmd_device_memory_alloc_map(
		segment_size, pool->pva, PVA_ACCESS_RW, pool->smmu_ctx_idx);
	if (mem == NULL) {
		goto free_segment;
	}

	segment->mem = mem;
	segment->owner_pool = pool;
	segment->n_free_ele =
		pool->n_element_incr; /* Initialize all elements as free */
	va = mem->va;

	/* Initialize the segment allocator */
	err = pva_kmd_block_allocator_init(&segment->elem_allocator, va, 0,
					   pool->element_size,
					   pool->n_element_incr);
	if (err != PVA_SUCCESS) {
		goto free_mem;
	}

	/* Add segment to the pool */
	segment->next = pool->segment_list_head;
	pool->segment_list_head = segment;
	pool->n_free_element =
		safe_addu32(pool->n_free_element, pool->n_element_incr);

	return segment;

free_mem:
	pva_kmd_device_memory_free(mem);
free_segment:
	pva_kmd_free(segment);
err_out:
	return NULL;
}

enum pva_error pva_kmd_devmem_pool_init(struct pva_kmd_devmem_pool *pool,
					struct pva_kmd_device *pva,
					uint8_t smmu_ctx_idx,
					uint32_t element_size,
					uint32_t ele_incr_count)
{
	struct pva_kmd_devmem_pool_segment *segment;
	enum pva_error err = PVA_SUCCESS;

	/* Initialize the pool structure */
	(void)memset(pool, 0, sizeof(*pool));
	pool->smmu_ctx_idx = smmu_ctx_idx;
	/* MISRA C-2023 Rule 10.3: Explicit cast for narrowing conversion */
	pool->element_size = (uint32_t)safe_pow2_roundup_u32(
		element_size, (uint32_t)sizeof(uint64_t));
	pool->n_element_incr = ele_incr_count;
	pool->n_free_element = 0;
	pool->segment_list_head = NULL;
	pool->pva = pva;

	err = pva_kmd_mutex_init(&pool->pool_lock);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	/* Allocate the first segment */
	segment = allocate_segment(pool);
	if (segment == NULL) {
		err = PVA_NOMEM;
		goto deinit_mutex;
	}

	return PVA_SUCCESS;

deinit_mutex:
	pva_kmd_mutex_deinit(&pool->pool_lock);
err_out:
	return err;
}

static enum pva_error
pva_kmd_devmem_pool_alloc(struct pva_kmd_devmem_pool *pool,
			  struct pva_kmd_devmem_element *devmem)
{
	struct pva_kmd_devmem_pool_segment *segment = NULL;
	struct pva_kmd_devmem_pool_segment *new_segment = NULL;
	/* Use U32_MAX instead of casting -1 */
	uint32_t ele_idx = U32_MAX;
	enum pva_error err = PVA_SUCCESS;

	pva_kmd_mutex_lock(&pool->pool_lock);

	/* Check if we have any free elements */
	if (pool->n_free_element == 0U) {
		/* Need to allocate a new segment */
		new_segment = allocate_segment(pool);
		if (new_segment == NULL) {
			err = PVA_NOMEM;
			goto unlock;
		}
	}

	/* Try to find a free element in the pool */
	segment = pool->segment_list_head;
	while (segment != NULL) {
		void *va = NULL;
		va = pva_kmd_alloc_block_unsafe(&segment->elem_allocator,
						&ele_idx);
		if (va != NULL) {
			/* Found a free element */
			break;
		}
		segment = segment->next;
	}

	ASSERT(segment != NULL);

	devmem->segment = segment;
	devmem->ele_idx = ele_idx;
	pool->n_free_element = safe_subu32(pool->n_free_element, 1);
	segment->n_free_ele = safe_subu32(segment->n_free_ele, 1);

unlock:
	pva_kmd_mutex_unlock(&pool->pool_lock);
	return err;
}

enum pva_error pva_kmd_devmem_pool_zalloc(struct pva_kmd_devmem_pool *pool,
					  struct pva_kmd_devmem_element *devmem)
{
	enum pva_error err = pva_kmd_devmem_pool_alloc(pool, devmem);
	if (err != PVA_SUCCESS) {
		return err;
	}

	(void)memset(pva_kmd_get_devmem_va(devmem), 0, pool->element_size);
	return PVA_SUCCESS;
}

static void free_segment(struct pva_kmd_devmem_pool *pool,
			 struct pva_kmd_devmem_pool_segment *target_segment)
{
	struct pva_kmd_devmem_pool_segment *segment;
	struct pva_kmd_devmem_pool_segment *prev_segment = NULL;

	/* Find previous segment to update the linked list */
	segment = pool->segment_list_head;
	while (segment != NULL && segment != target_segment) {
		prev_segment = segment;
		segment = segment->next;
	}

	/* Segment not found in the list */
	ASSERT(segment != NULL);

	/* Remove this segment from the list */
	if (prev_segment == NULL) {
		/* This is the head segment */
		pool->segment_list_head = target_segment->next;
	} else {
		prev_segment->next = target_segment->next;
	}

	/* Free the segment allocator */
	pva_kmd_block_allocator_deinit(&target_segment->elem_allocator);

	/* Free the device memory */
	pva_kmd_device_memory_free(target_segment->mem);

	/* Free the segment structure */
	pva_kmd_free(target_segment);

	/* Update the free element count */
	pool->n_free_element =
		safe_subu32(pool->n_free_element, pool->n_element_incr);
}

void pva_kmd_devmem_pool_free(struct pva_kmd_devmem_element *devmem)
{
	struct pva_kmd_devmem_pool *pool = devmem->segment->owner_pool;
	struct pva_kmd_devmem_pool_segment *current_segment = devmem->segment;
	uint32_t threshold;
	enum pva_error tmp_err;

	pva_kmd_mutex_lock(&pool->pool_lock);

	/* Free the element */
	tmp_err = pva_kmd_free_block_unsafe(&current_segment->elem_allocator,
					    devmem->ele_idx);
	ASSERT(tmp_err == PVA_SUCCESS);
	pool->n_free_element = safe_addu32(pool->n_free_element, 1);
	current_segment->n_free_ele =
		safe_addu32(current_segment->n_free_ele, 1);

	/* Check if the current segment is now empty using n_free_ele counter */
	if (current_segment->n_free_ele ==
	    current_segment->elem_allocator.max_num_blocks) {
		/* We only free the segment if we still have n_ele_incr free elements
		after the free */
		threshold = safe_mulu32(pool->n_element_incr, 2);
		if (pool->n_free_element >= threshold) {
			free_segment(pool, current_segment);
		}
	}

	pva_kmd_mutex_unlock(&pool->pool_lock);
}

void pva_kmd_devmem_pool_deinit(struct pva_kmd_devmem_pool *pool)
{
	struct pva_kmd_devmem_pool_segment *segment = pool->segment_list_head;
	struct pva_kmd_devmem_pool_segment *next;

	/* Free all segments */
	while (segment != NULL) {
		next = segment->next;

		/* Free the segment allocator */
		pva_kmd_block_allocator_deinit(&segment->elem_allocator);

		/* Free the device memory */
		pva_kmd_device_memory_free(segment->mem);

		/* Free the segment structure */
		pva_kmd_free(segment);

		segment = next;
	}

	pool->segment_list_head = NULL;
	pva_kmd_mutex_deinit(&pool->pool_lock);
}
