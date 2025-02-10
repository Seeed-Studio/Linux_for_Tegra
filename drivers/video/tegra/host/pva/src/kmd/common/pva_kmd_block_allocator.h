/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#ifndef PVA_KMD_BLOCK_ALLOCATOR_H
#define PVA_KMD_BLOCK_ALLOCATOR_H

#include "pva_api.h"

struct pva_kmd_block_allocator {
	uint32_t free_slot_head;
	uint32_t base_id;
	uint32_t max_num_blocks;
	uint32_t next_free_slot;
	uint32_t block_size;
	void *blocks;
	bool *slot_in_use;
};

enum pva_error
pva_kmd_block_allocator_init(struct pva_kmd_block_allocator *allocator,
			     void *chunk_mem, uint32_t base_id,
			     uint32_t chunk_size, uint32_t max_num_chunks);

void *pva_kmd_alloc_block(struct pva_kmd_block_allocator *allocator,
			  uint32_t *out_id);
static inline void *
pva_kmd_zalloc_block(struct pva_kmd_block_allocator *allocator,
		     uint32_t *out_id)
{
	void *ptr = pva_kmd_alloc_block(allocator, out_id);
	if (ptr != NULL) {
		memset(ptr, 0, allocator->block_size);
	}
	return ptr;
}

void *pva_kmd_get_block(struct pva_kmd_block_allocator *allocator, uint32_t id);
enum pva_error pva_kmd_free_block(struct pva_kmd_block_allocator *allocator,
				  uint32_t id);

void pva_kmd_block_allocator_deinit(struct pva_kmd_block_allocator *allocator);

#endif // PVA_KMD_BLOCK_ALLOCATOR_H
