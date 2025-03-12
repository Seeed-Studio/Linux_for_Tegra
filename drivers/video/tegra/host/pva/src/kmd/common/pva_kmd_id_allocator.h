/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_ID_ALLOCATOR_H
#define PVA_KMD_ID_ALLOCATOR_H
#include "pva_api_types.h"

struct pva_kmd_id_allocator {
	uint32_t n_entries;
	uint32_t *free_ids;
	uint32_t *used_ids;
	uint32_t n_free_ids;
	uint32_t n_used_ids;
};

enum pva_error pva_kmd_id_allocator_init(struct pva_kmd_id_allocator *allocator,
					 uint32_t base_id, uint32_t n_entries);

enum pva_error
pva_kmd_id_allocator_deinit(struct pva_kmd_id_allocator *allocator);

enum pva_error pva_kmd_alloc_id(struct pva_kmd_id_allocator *allocator,
				uint32_t *id);

void pva_kmd_free_id(struct pva_kmd_id_allocator *allocator, uint32_t id);

#endif /* PVA_KMD_ID_ALLOCATOR_H */
