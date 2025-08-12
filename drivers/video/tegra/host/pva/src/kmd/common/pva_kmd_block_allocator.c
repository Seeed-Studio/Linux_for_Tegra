// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
		pva_kmd_log_err(
			"pva_kmd_block_allocator_init slot_in_use NULL");
		goto err_out;
	}
	pva_kmd_mutex_init(&allocator->allocator_lock);
	return PVA_SUCCESS;
err_out:
	return err;
}

void pva_kmd_block_allocator_deinit(struct pva_kmd_block_allocator *allocator)
{
	pva_kmd_free(allocator->slot_in_use);
	pva_kmd_mutex_deinit(&allocator->allocator_lock);
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

void *pva_kmd_alloc_block_unsafe(struct pva_kmd_block_allocator *allocator,
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
			return NULL;
		}
	}
	allocator->slot_in_use[slot] = true;
	*out_id = slot + allocator->base_id;
	block = get_block(allocator, slot);
	return block;
}

void *pva_kmd_alloc_block(struct pva_kmd_block_allocator *allocator,
			  uint32_t *out_id)
{
	void *block = NULL;

	pva_kmd_mutex_lock(&allocator->allocator_lock);
	block = pva_kmd_alloc_block_unsafe(allocator, out_id);
	pva_kmd_mutex_unlock(&allocator->allocator_lock);
	return block;
}

static bool is_slot_valid(struct pva_kmd_block_allocator *allocator,
			  uint32_t slot)
{
	if (slot >= allocator->max_num_blocks) {
		return false;
	}

	return allocator->slot_in_use[slot];
}

void *pva_kmd_get_block_unsafe(struct pva_kmd_block_allocator *allocator,
			       uint32_t id)
{
	uint32_t slot = id - allocator->base_id;
	if (!is_slot_valid(allocator, slot)) {
		return NULL;
	}
	return get_block(allocator, slot);
}

enum pva_error
pva_kmd_free_block_unsafe(struct pva_kmd_block_allocator *allocator,
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

enum pva_error pva_kmd_free_block(struct pva_kmd_block_allocator *allocator,
				  uint32_t id)
{
	enum pva_error err = PVA_SUCCESS;

	pva_kmd_mutex_lock(&allocator->allocator_lock);
	err = pva_kmd_free_block_unsafe(allocator, id);
	pva_kmd_mutex_unlock(&allocator->allocator_lock);
	return err;
}
