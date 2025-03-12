// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_id_allocator.h"
#include "pva_api_types.h"
#include "pva_kmd_utils.h"

enum pva_error pva_kmd_id_allocator_init(struct pva_kmd_id_allocator *allocator,
					 uint32_t base_id, uint32_t n_entries)
{
	enum pva_error err = PVA_SUCCESS;

	allocator->n_entries = n_entries;
	allocator->n_free_ids = n_entries;
	allocator->n_used_ids = 0;

	// Allocate space for both free and used IDs
	allocator->free_ids = pva_kmd_zalloc(sizeof(uint32_t) * n_entries * 2);
	if (allocator->free_ids == NULL) {
		err = PVA_NOMEM;
		goto err_out;
	}

	allocator->used_ids = allocator->free_ids + n_entries;

	// Put free IDs in reverse order so that we allocate in ascending order
	for (uint32_t i = 0; i < n_entries; i++) {
		allocator->free_ids[i] = base_id + n_entries - i - 1;
	}

	return PVA_SUCCESS;

err_out:
	return err;
}

enum pva_error
pva_kmd_id_allocator_deinit(struct pva_kmd_id_allocator *allocator)
{
	pva_kmd_free(allocator->free_ids);
	return PVA_SUCCESS;
}

enum pva_error pva_kmd_alloc_id(struct pva_kmd_id_allocator *allocator,
				uint32_t *id)
{
	if (allocator->n_free_ids == 0) {
		return PVA_NOENT;
	}

	allocator->n_free_ids--;
	*id = allocator->free_ids[allocator->n_free_ids];

	allocator->used_ids[allocator->n_used_ids] = *id;
	allocator->n_used_ids++;

	return PVA_SUCCESS;
}

void pva_kmd_free_id(struct pva_kmd_id_allocator *allocator, uint32_t id)
{
	ASSERT(allocator->n_used_ids > 0);
	ASSERT(allocator->n_free_ids < allocator->n_entries);

	allocator->free_ids[allocator->n_free_ids] = id;
	allocator->n_free_ids++;

	allocator->n_used_ids--;
}