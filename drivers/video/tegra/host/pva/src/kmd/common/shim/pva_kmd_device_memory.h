/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_DEVICE_MEMORY_H
#define PVA_KMD_DEVICE_MEMORY_H
#include "pva_kmd.h"
#include "pva_api.h"
struct pva_kmd_context;

/**
 * @brief KMD device memory structure.
 *
 * This structure is essentially a base object. More information is needed to
 * manage memory allocations but the required information is platform dependent.
 * Therefore, each platform will have a derived implementation and this
 * structure is just part of it.
 */
struct pva_kmd_device_memory {
	uint64_t iova; /**< IOVA address if mapped. Otherwise 0 */
	void *va; /**< CPU address if mapped. Otherwise 0. */
	uint64_t size; /**< Size of the mapping. */
	struct pva_kmd_device *pva; /**< The PVA this memory is mapped to. */
	uint32_t smmu_ctx_idx; /**< The SMMU context this memory is mapped to. */
	uint32_t iova_access_flags; /**< Access flags for the memory. RO - 1/WO - 2/RW - 3 */
};

/**
 * This API is not available in Linux and should not be used by the common code.
 */
struct pva_kmd_device_memory *pva_kmd_device_memory_alloc(uint64_t size);

/**
 * Allocate memory and map to both IOVA space and CPU space.
 *
 * @note We cannot just allocate without mapping or just mapping to one
 * space. This restriction comes from the Linux dma_alloc_coherent API, which
 * allocates and maps at the same time.
 *
 * @note iova_access_flag is only supported by QNX implementation.
 *
 * @param size Size of the allocation
 * @param pva The PVA device to map to
 * @param iova_access_flags Access flags for IOVA space. PVA_ACCESS_RO or
 *                          PVA_ACCESS_RW. For CPU space, it's always
 *                          read and write.
 * @param smmu_ctx_idx The SMMU context to map to
 */
struct pva_kmd_device_memory *
pva_kmd_device_memory_alloc_map(uint64_t size, struct pva_kmd_device *pva,
				uint32_t iova_access_flags,
				uint32_t smmu_ctx_idx);
/** @brief Acquire memory shared from UMD.
 *
 * This function takes a shared ownership of the memory allocation so that KMD
 * can keep the allocation alive even after UMD closed the memory handle.
 *
 * @param memory_handle Memory handle passed from user space. On Linux, this is
 *                      a file descriptor associated with dma_buf object. On
 *                      QNX, this is NvRM import ID.
 * @param offset Offset into the allocation. This affects the mapped address.
 * @param size Size of the mapping, which can be smaller than the size of the
 *             allocation.
 * @param ctx The user from whom we are importing the memory.
 */
struct pva_kmd_device_memory *
pva_kmd_device_memory_acquire(uint64_t memory_handle, uint64_t offset,
			      uint64_t size, struct pva_kmd_context *ctx);
/**
 * @brief Release the memory.
 *
 * This function frees memory allocated from acquire or alloc_map. If there are
 * active CPU mapping or IOVA mapping, this function will unmap them.
 *
 * @param memory Pointer to the memory to release.
 */
void pva_kmd_device_memory_free(struct pva_kmd_device_memory *memory);

/**
 * @brief Map the memory to CPU space.
 */
enum pva_error
pva_kmd_device_memory_cpu_map(struct pva_kmd_device_memory *memory);

/**
 * @brief Unmap the memory from CPU space.
 *
 * Unmap a not mapped memory will trigger abort.
 */
void pva_kmd_device_memory_cpu_unmap(struct pva_kmd_device_memory *memory);

/**
 * @brief Map the memory to IOVA space.
 */
enum pva_error
pva_kmd_device_memory_iova_map(struct pva_kmd_device_memory *memory,
			       struct pva_kmd_device *pva,
			       uint32_t access_flags, uint32_t smmu_ctx_idx);
/**
 * @brief Unmap the memory from IOVA space.
 *
 * Unmap a not mapped memory will trigger abort.
 */
void pva_kmd_device_memory_iova_unmap(struct pva_kmd_device_memory *memory);

#endif // PVA_KMD_DEVICE_MEMORY_H
