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
 * @details This structure serves as a base object for managing memory
 * allocations in the PVA KMD. It contains the essential information
 * needed to track memory mappings across different address spaces.
 *
 * More detailed information is required to manage memory allocations,
 * but this information is platform-dependent. Therefore, each platform
 * will have a derived implementation with this structure as part of it.
 * The structure provides abstraction for memory management across
 * different platforms while maintaining common interfaces.
 */
struct pva_kmd_device_memory {
	/**
	 * @brief IOVA address if mapped, otherwise 0
	 *
	 * @details Contains the I/O Virtual Address when the memory is
	 * mapped to IOVA space for device access. Set to 0 when not mapped.
	 * Valid range: [0 .. platform-specific max IOVA]
	 */
	uint64_t iova;

	/**
	 * @brief CPU virtual address if mapped, otherwise NULL
	 *
	 * @details Contains the CPU virtual address when the memory is
	 * mapped to CPU address space. Set to NULL when not mapped.
	 * Valid value: valid CPU virtual address or NULL
	 */
	void *va;

	/**
	 * @brief Size of the memory mapping in bytes
	 *
	 * @details Specifies the size of the allocated memory region.
	 * This size applies to both CPU and IOVA mappings when present.
	 * Valid range: [1 .. UINT64_MAX]
	 */
	uint64_t size;

	/**
	 * @brief Pointer to the PVA device this memory is mapped to
	 *
	 * @details References the PVA device that owns this memory mapping.
	 * Used for device-specific memory management operations.
	 * Valid value: non-null pointer to @ref pva_kmd_device
	 */
	struct pva_kmd_device *pva;

	/**
	 * @brief SMMU context index for memory mapping
	 *
	 * @details Identifies the SMMU context used for this memory mapping.
	 * Different contexts provide memory isolation and access control.
	 * Valid range: platform-specific SMMU context range
	 */
	uint8_t smmu_ctx_idx;

	/**
	 * @brief Access flags for IOVA memory mapping
	 *
	 * @details Specifies the access permissions for the IOVA mapping:
	 * - 1: Read-Only (RO)
	 * - 2: Write-Only (WO)
	 * - 3: Read-Write (RW)
	 * Valid values: 1, 2, 3
	 */
	uint32_t iova_access_flags;
};

/**
 * @brief Allocate device memory without mapping.
 *
 * @details This function performs the following operations:
 * - Allocates memory of the specified size for device use
 * - Does not perform CPU or IOVA mapping during allocation
 * - Uses platform-appropriate memory allocation mechanisms
 * - Returns a device memory object for subsequent mapping operations
 *
 * @note This API is not available on Linux platform and should not be
 * used by common code. Use @ref pva_kmd_device_memory_alloc_map() instead
 * for cross-platform compatibility.
 *
 * @param[in] size  Size of memory to allocate in bytes
 *                  Valid range: [1 .. UINT64_MAX]
 *
 * @retval non-null  Pointer to allocated device memory structure
 * @retval NULL      Memory allocation failed
 */
struct pva_kmd_device_memory *pva_kmd_device_memory_alloc(uint64_t size);

/**
 * @brief Allocate memory and map to both IOVA space and CPU space.
 *
 * @details This function performs the following operations:
 * - Allocates memory of the specified size
 * - Maps the memory to both IOVA and CPU virtual address spaces
 * - Configures SMMU context and access permissions
 * - Ensures memory coherency between CPU and device access
 * - Populates all fields of the device memory structure
 *
 * This function provides a combined allocation and mapping operation
 * required by platforms like Linux that use @ref dma_alloc_coherent(),
 * which allocates and maps simultaneously. The allocated memory is
 * immediately accessible from both CPU and device.
 *
 * @note The @p iova_access_flags parameter is only supported by QNX
 * implementation. Other platforms may ignore this parameter.
 *
 * @note CPU space mapping always provides read and write access
 * regardless of IOVA access flags.
 *
 * @param[in] size              Size of memory to allocate in bytes
 *                              Valid range: [1 .. UINT64_MAX]
 * @param[in, out] pva          Pointer to @ref pva_kmd_device structure
 *                              Valid value: non-null
 * @param[in] iova_access_flags Access flags for IOVA space mapping
 *                              Valid values: PVA_ACCESS_RO or PVA_ACCESS_RW
 * @param[in] smmu_ctx_idx      SMMU context index for mapping
 *                              Valid range: platform-specific
 *
 * @retval non-null  Pointer to allocated and mapped device memory
 * @retval NULL      Allocation or mapping failed
 */
struct pva_kmd_device_memory *
pva_kmd_device_memory_alloc_map(uint64_t size, struct pva_kmd_device *pva,
				uint32_t iova_access_flags,
				uint8_t smmu_ctx_idx);

/**
 * @brief Acquire memory shared from UMD.
 *
 * @details This function performs the following operations:
 * - Takes shared ownership of memory allocation from user space
 * - Imports the memory handle into kernel space management
 * - Maps the memory region with specified offset and size
 * - Ensures KMD can maintain allocation lifetime independently of UMD
 * - Enables memory sharing between user and kernel spaces
 * - Associates the memory with the specified user context
 *
 * This function allows KMD to keep memory allocations alive even after
 * UMD closes the memory handle, ensuring proper resource management
 * and preventing premature deallocation during ongoing operations.
 *
 * The memory handle format is platform-specific:
 * - Linux: file descriptor associated with dma_buf object
 * - QNX: NvRM import ID
 *
 * @param[in] memory_handle  Platform-specific memory handle from user space
 *                           Valid value: platform-specific handle format
 * @param[in] offset         Byte offset into the memory allocation
 *                           Valid range: [0 .. allocation_size - 1]
 * @param[in] size           Size of the mapping region in bytes
 *                           Valid range: [1 .. allocation_size - offset]
 * @param[in] ctx            User context importing the memory
 *                           Valid value: non-null pointer to @ref pva_kmd_context
 *
 * @retval non-null  Pointer to acquired device memory structure
 * @retval NULL      Memory acquisition or mapping failed
 */
struct pva_kmd_device_memory *
pva_kmd_device_memory_acquire(uint64_t memory_handle, uint64_t offset,
			      uint64_t size, struct pva_kmd_context *ctx);

/**
 * @brief Release the memory.
 *
 * @details This function performs the following operations:
 * - Frees memory allocated by @ref pva_kmd_device_memory_acquire() or
 *   @ref pva_kmd_device_memory_alloc_map()
 * - Unmaps active CPU virtual address mappings if present
 * - Unmaps active IOVA mappings if present
 * - Releases platform-specific memory resources
 * - Cleans up SMMU context associations
 * - Ensures complete resource cleanup and prevents memory leaks
 *
 * This function handles complete cleanup of device memory objects,
 * including any active mappings. It is safe to call this function
 * with memory that has partial mappings or mapping failures.
 *
 * @param[in] dev_memory  Pointer to device memory structure to release
 *                        Valid value: non-null pointer returned by allocation
 *                        or acquisition functions
 */
void pva_kmd_device_memory_free(struct pva_kmd_device_memory *dev_memory);

/**
 * @brief Map the memory to CPU space.
 *
 * @details This function performs the following operations:
 * - Creates a CPU virtual address mapping for the device memory
 * - Updates the @ref pva_kmd_device_memory::va field with mapped address
 * - Ensures memory coherency between CPU and device access
 * - Uses platform-appropriate CPU mapping mechanisms
 * - Enables CPU access to device memory content
 *
 * After successful mapping, the memory content can be accessed through
 * the CPU virtual address stored in the @ref pva_kmd_device_memory::va field.
 * The mapping provides coherent access between CPU and device.
 *
 * @param[in, out] dev_memory  Pointer to device memory structure to map
 *                             Valid value: non-null, not currently CPU-mapped
 *
 * @retval PVA_SUCCESS       CPU mapping created successfully
 * @retval PVA_NOMEM         Insufficient virtual address space
 * @retval PVA_INVAL         Invalid memory object or already mapped
 * @retval PVA_INTERNAL      Platform mapping operation failed
 */
enum pva_error
pva_kmd_device_memory_cpu_map(struct pva_kmd_device_memory *dev_memory);

/**
 * @brief Unmap the memory from CPU space.
 *
 * @details This function performs the following operations:
 * - Removes the CPU virtual address mapping for the device memory
 * - Clears the @ref pva_kmd_device_memory::va field (sets to NULL)
 * - Releases CPU virtual address space resources
 * - Uses platform-appropriate CPU unmapping mechanisms
 * - Ensures proper cleanup of mapping resources
 *
 * After unmapping, the memory content is no longer accessible through
 * CPU virtual addresses. Attempting to access the memory through the
 * previous virtual address will result in undefined behavior.
 *
 * @note Attempting to unmap memory that is not currently mapped will
 * trigger an abort to indicate programming error.
 *
 * @param[in, out] dev_memory  Pointer to device memory structure to unmap
 *                             Valid value: non-null, currently CPU-mapped
 */
void pva_kmd_device_memory_cpu_unmap(struct pva_kmd_device_memory *dev_memory);

/**
 * @brief Map the memory to IOVA space.
 *
 * @details This function performs the following operations:
 * - Creates an IOVA (I/O Virtual Address) mapping for the device memory
 * - Updates the @ref pva_kmd_device_memory::iova field with mapped address
 * - Configures SMMU context and access permissions
 * - Sets up device-accessible memory mapping
 * - Enables hardware access to memory content
 * - Associates mapping with specified PVA device and SMMU context
 *
 * After successful mapping, the memory is accessible by PVA hardware
 * through the IOVA address stored in the @ref pva_kmd_device_memory::iova field.
 * The access permissions are controlled by the specified flags.
 *
 * @param[in, out] dev_memory  Pointer to device memory structure to map
 *                             Valid value: non-null, not currently IOVA-mapped
 * @param[in, out] pva         Pointer to @ref pva_kmd_device structure
 *                             Valid value: non-null
 * @param[in] access_flags     IOVA access permission flags
 *                             Valid values: PVA_ACCESS_RO, PVA_ACCESS_WO, PVA_ACCESS_RW
 * @param[in] smmu_ctx_idx     SMMU context index for mapping
 *                             Valid range: platform-specific
 *
 * @retval PVA_SUCCESS         IOVA mapping created successfully
 * @retval PVA_NOMEM           Insufficient IOVA address space
 * @retval PVA_INVAL           Invalid parameters or already mapped
 * @retval PVA_INTERNAL        SMMU configuration failed
 */
enum pva_error
pva_kmd_device_memory_iova_map(struct pva_kmd_device_memory *dev_memory,
			       struct pva_kmd_device *pva,
			       uint32_t access_flags, uint8_t smmu_ctx_idx);
/**
 * @brief Unmap the memory from IOVA space.
 *
 * @details This function performs the following operations:
 * - Removes the IOVA (I/O Virtual Address) mapping for the device memory
 * - Clears the @ref pva_kmd_device_memory::iova field (sets to 0)
 * - Releases IOVA address space resources
 * - Cleans up SMMU context associations
 * - Ensures proper cleanup of device mapping resources
 * - Prevents further device access to the memory region
 *
 * After unmapping, the memory is no longer accessible by PVA hardware.
 * Attempting device access to the previous IOVA address will result
 * in SMMU faults or undefined behavior.
 *
 * @note Attempting to unmap memory that is not currently mapped will
 * trigger an abort to indicate programming error.
 *
 * @param[in, out] dev_memory  Pointer to device memory structure to unmap
 *                             Valid value: non-null, currently IOVA-mapped
 */
void pva_kmd_device_memory_iova_unmap(struct pva_kmd_device_memory *dev_memory);

#endif // PVA_KMD_DEVICE_MEMORY_H
