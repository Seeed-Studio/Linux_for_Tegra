/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_EXECUTABLE_H
#define PVA_KMD_EXECUTABLE_H
#include "pva_kmd.h"
#include "pva_resource.h"
#include "pva_kmd_utils.h"

struct pva_kmd_device;
struct pva_kmd_device_memory;

/**
 * @brief Symbol table for executable debugging and runtime information
 *
 * @details This structure maintains a symbol table for a loaded executable,
 * providing access to symbol information used for debugging, profiling, and
 * runtime symbol resolution. The symbol table contains metadata about functions,
 * variables, and other symbols within the executable that can be referenced
 * during execution or debugging operations.
 */
struct pva_kmd_exec_symbol_table {
	/**
	 * @brief Number of symbols in the symbol table
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t n_symbols;

	/**
	 * @brief Array of symbol information structures
	 * Valid value: non-null if n_symbols > 0, null if n_symbols == 0
	 */
	struct pva_symbol_info *symbols;
};

/**
 * @brief Get symbol information by symbol ID
 *
 * @details This function performs the following operations:
 * - Validates the symbol ID against the symbol table bounds
 * - Converts external symbol ID to internal array index
 * - Retrieves the symbol information structure for the specified symbol
 * - Provides access to symbol metadata for debugging and runtime operations
 * - Returns null for invalid or out-of-range symbol IDs
 *
 * The symbol ID is expected to be in the external format with PVA_SYMBOL_ID_BASE
 * offset. This function handles the conversion to internal array indexing
 * automatically.
 *
 * @param[in] symbol_table  Pointer to @ref pva_kmd_exec_symbol_table structure
 *                          Valid value: non-null, must be initialized
 * @param[in] symbol_id     External symbol ID to retrieve
 *                          Valid range: [PVA_SYMBOL_ID_BASE .. PVA_SYMBOL_ID_BASE+n_symbols-1]
 *
 * @retval non-null  Pointer to @ref pva_symbol_info if symbol exists
 * @retval NULL      Invalid symbol ID or symbol not found
 */
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

/**
 * @brief Get symbol information with type validation
 *
 * @details This function performs the following operations:
 * - Calls @ref pva_kmd_get_symbol() to retrieve symbol information
 * - Validates that the symbol has the expected type
 * - Returns the symbol information only if type matches
 * - Provides type-safe access to symbol information
 * - Logs error for type mismatches to aid debugging
 *
 * This function provides additional type safety when accessing symbols
 * by ensuring the symbol type matches expectations before returning
 * the symbol information.
 *
 * @param[in] symbol_table  Pointer to @ref pva_kmd_exec_symbol_table structure
 *                          Valid value: non-null, must be initialized
 * @param[in] symbol_id     External symbol ID to retrieve
 *                          Valid range: [PVA_SYMBOL_ID_BASE .. PVA_SYMBOL_ID_BASE+n_symbols-1]
 * @param[in] symbol_type   Expected symbol type for validation
 *                          Valid values: @ref pva_symbol_type enumeration values
 *
 * @retval non-null  Pointer to @ref pva_symbol_info if symbol exists and type matches
 * @retval NULL      Invalid symbol ID, symbol not found, or type mismatch
 */
static inline struct pva_symbol_info *
pva_kmd_get_symbol_with_type(struct pva_kmd_exec_symbol_table *symbol_table,
			     uint32_t symbol_id,
			     enum pva_symbol_type symbol_type)
{
	struct pva_symbol_info *symbol = NULL;

	symbol = pva_kmd_get_symbol(symbol_table, symbol_id);
	if (symbol == NULL) {
		return NULL;
	}

	if (symbol->symbol_type != symbol_type) {
		pva_kmd_log_err("Unexpected symbol type\n");
		return NULL;
	}

	return symbol;
}

/**
 * @brief Load and prepare VPU executable for execution
 *
 * @details This function performs the following operations:
 * - Validates the executable format and structure
 * - Parses executable sections including code, data, and metadata
 * - Allocates device memory for executable storage
 * - Maps executable sections to appropriate SMMU contexts
 * - Extracts and builds symbol table for debugging support
 * - Sets up memory permissions for code and data sections
 * - Prepares executable for hardware execution by the VPU
 * - Returns all necessary information for executable management
 *
 * The loaded executable is stored in device memory accessible by the VPU
 * hardware, with proper memory protection and SMMU mapping. The symbol
 * table provides debugging and profiling capabilities.
 *
 * @param[in] executable_data   Pointer to executable binary data
 *                              Valid value: non-null
 * @param[in] executable_size   Size of executable binary in bytes
 *                              Valid range: [1 .. UINT32_MAX]
 * @param[in] pva               Pointer to @ref pva_kmd_device structure
 *                              Valid value: non-null
 * @param[in] dma_smmu_id       SMMU context ID for DMA operations
 *                              Valid range: [0 .. PVA_MAX_NUM_SMMU_CONTEXTS-1]
 * @param[out] out_symbol_table Pointer to store symbol table information
 *                              Valid value: non-null
 * @param[out] out_metainfo     Pointer to store executable metadata memory
 *                              Valid value: non-null
 * @param[out] out_sections     Pointer to store executable sections memory
 *                              Valid value: non-null
 *
 * @retval PVA_SUCCESS                  Executable loaded successfully
 * @retval PVA_INVALID_SYMBOL           Executable format is invalid or corrupted
 * @retval PVA_NOMEM                    Failed to allocate device memory
 * @retval PVA_INVAL                    Failed to map executable to SMMU context
 * @retval PVA_NOT_IMPL                 Executable uses unsupported features
 */
enum pva_error
pva_kmd_load_executable(const void *executable_data, uint32_t executable_size,
			struct pva_kmd_device *pva, uint8_t dma_smmu_id,
			struct pva_kmd_exec_symbol_table *out_symbol_table,
			struct pva_kmd_device_memory **out_metainfo,
			struct pva_kmd_device_memory **out_sections);

/**
 * @brief Unload executable and free associated resources
 *
 * @details This function performs the following operations:
 * - Frees the symbol table and all symbol information
 * - Unmaps executable sections from SMMU contexts
 * - Releases device memory allocated for executable metadata
 * - Releases device memory allocated for executable code and data sections
 * - Cleans up all resources associated with the executable
 * - Invalidates all pointers and structures for the executable
 *
 * This function should be called when an executable is no longer needed
 * to ensure proper cleanup of all associated resources. After calling
 * this function, the executable cannot be used for further operations.
 *
 * @param[in, out] symbol_table  Pointer to symbol table to clean up
 *                               Valid value: non-null, must have been initialized
 * @param[in] metainfo           Pointer to executable metadata memory to free
 *                               Valid value: non-null, must have been allocated
 * @param[in] sections           Pointer to executable sections memory to free
 *                               Valid value: non-null, must have been allocated
 */
void pva_kmd_unload_executable(struct pva_kmd_exec_symbol_table *symbol_table,
			       struct pva_kmd_device_memory *metainfo,
			       struct pva_kmd_device_memory *sections);

#endif // PVA_KMD_EXECUTABLE_H
