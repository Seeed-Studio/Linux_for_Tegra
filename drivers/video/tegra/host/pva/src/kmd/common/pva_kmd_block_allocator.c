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
#include "pva_kmd_block_allocator.h"
#include "pva_kmd_utils.h"
#include "pva_api.h"

#define INVALID_ID 0xFFFFFFFF
enum pva_error
pva_kmd_block_allocator_init(struct pva_kmd_block_allocator *allocator,
			     void *block_mem, uint32_t base_id,
			     uint32_t block_size, uint32_t max_num_blocks)
{
	enum pva_error err = PVA_SUCCESS;

	allocator->free_slot_head = INVALID_ID;
	allocator->next_free_slot = 0;
	allocator->max_num_blocks = max_num_blocks;
	allocator->block_size = block_size;
	allocator->base_id = base_id;

	allocator->blocks = block_mem;

	allocator->slot_in_use = pva_kmd_zalloc(
		sizeof(*allocator->slot_in_use) * max_num_blocks);
	if (!allocator->slot_in_use) {
		err = PVA_NOMEM;
		goto err_out;
	}

	return PVA_SUCCESS;
err_out:
	return err;
}

void pva_kmd_block_allocator_deinit(struct pva_kmd_block_allocator *allocator)
{
	pva_kmd_free(allocator->slot_in_use);
}

static inline void *get_block(struct pva_kmd_block_allocator *allocator,
			      uint32_t slot)
{
	uintptr_t base = (uintptr_t)allocator->blocks;
	uintptr_t addr = base + (slot * allocator->block_size);
	return (void *)addr;
}

static inline uint32_t next_slot(struct pva_kmd_block_allocator *allocator,
				 uint32_t slot)
{
	uint32_t *next = (uint32_t *)get_block(allocator, slot);
	return *next;
}

void *pva_kmd_alloc_block(struct pva_kmd_block_allocator *allocator,
			  uint32_t *out_id)
{
	void *block = NULL;
	uint32_t slot = INVALID_ID;

	if (allocator->free_slot_head != INVALID_ID) {
		slot = allocator->free_slot_head;
		allocator->free_slot_head =
			next_slot(allocator, allocator->free_slot_head);
	} else {
		if (allocator->next_free_slot < allocator->max_num_blocks) {
			slot = allocator->next_free_slot;
			allocator->next_free_slot++;
		} else {
			goto err_out;
		}
	}
	allocator->slot_in_use[slot] = true;

	*out_id = slot + allocator->base_id;
	block = get_block(allocator, slot);
	return block;
err_out:
	return NULL;
}

static bool is_slot_valid(struct pva_kmd_block_allocator *allocator,
			  uint32_t slot)
{
	if (slot >= allocator->max_num_blocks) {
		return false;
	}

	return allocator->slot_in_use[slot];
}

void *pva_kmd_get_block(struct pva_kmd_block_allocator *allocator, uint32_t id)
{
	uint32_t slot = id - allocator->base_id;
	if (!is_slot_valid(allocator, slot)) {
		return NULL;
	}

	return get_block(allocator, slot);
}

enum pva_error pva_kmd_free_block(struct pva_kmd_block_allocator *allocator,
				  uint32_t id)
{
	uint32_t slot = id - allocator->base_id;
	uint32_t *next;
	if (!is_slot_valid(allocator, slot)) {
		return PVA_INVAL;
	}

	allocator->slot_in_use[slot] = false;
	next = (uint32_t *)get_block(allocator, slot);
	*next = allocator->free_slot_head;
	allocator->free_slot_head = slot;

	return PVA_SUCCESS;
}
