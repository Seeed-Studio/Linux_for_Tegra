/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_EXECUTABLE_H
#define PVA_KMD_EXECUTABLE_H
#include "pva_kmd.h"
#include "pva_resource.h"
#include "pva_kmd_utils.h"

struct pva_kmd_device;
struct pva_kmd_device_memory;

struct pva_kmd_exec_symbol_table {
	uint32_t n_symbols;
	struct pva_symbol_info *symbols;
};

static inline struct pva_symbol_info *
pva_kmd_get_symbol(struct pva_kmd_exec_symbol_table *symbol_table,
		   uint32_t symbol_id)
{
	struct pva_symbol_info *symbol = NULL;
	uint32_t idx = symbol_id - PVA_SYMBOL_ID_BASE;

	if (idx >= symbol_table->n_symbols) {
		pva_kmd_log_err("Symbol ID out of range\n");
		return NULL;
	}

	symbol = &symbol_table->symbols[idx];
	return symbol;
}

static inline struct pva_symbol_info *
pva_kmd_get_symbol_with_type(struct pva_kmd_exec_symbol_table *symbol_table,
			     uint32_t symbol_id,
			     enum pva_symbol_type symbol_type)
{
	struct pva_symbol_info *symbol = NULL;

	symbol = pva_kmd_get_symbol(symbol_table, symbol_id);
	if (!symbol) {
		return NULL;
	}

#if !defined(PVA_SKIP_SYMBOL_TYPE_CHECK)
	if (symbol->symbol_type != symbol_type) {
		pva_kmd_log_err("Unexpected symbol type\n");
		return NULL;
	}
#endif

	return symbol;
}

enum pva_error
pva_kmd_load_executable(const void *executable_data, uint32_t executable_size,
			struct pva_kmd_device *pva, uint8_t dma_smmu_id,
			struct pva_kmd_exec_symbol_table *out_symbol_table,
			struct pva_kmd_device_memory **out_metainfo,
			struct pva_kmd_device_memory **out_sections);

void pva_kmd_unload_executable(struct pva_kmd_exec_symbol_table *symbol_table,
			       struct pva_kmd_device_memory *metainfo,
			       struct pva_kmd_device_memory *sections);

#endif // PVA_KMD_EXECUTABLE_H
