/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_RESOURCE_TABLE_H
#define PVA_KMD_RESOURCE_TABLE_H
#include "pva_api_ops.h"
#include "pva_fw.h"
#include "pva_bit.h"
#include "pva_resource.h"
#include "pva_kmd_block_allocator.h"
#include "pva_kmd.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_executable.h"
#include "pva_constants.h"
#include "pva_kmd_dma_cfg.h"
#include "pva_kmd_mutex.h"
#include "pva_kmd_thread_sema.h"
#include "pva_kmd_devmem_pool.h"

#define PVA_USER_RESOURCE_ID_BASE (PVA_MAX_PRIV_RES_ID + 1U)

struct pva_kmd_device;

/**
 * @brief Resource structure for DRAM memory buffers
 *
 * @details This structure contains information about a DRAM memory buffer
 * resource that has been registered with the resource table. It provides
 * access to the device memory allocation that backs the DRAM resource.
 */
struct pva_kmd_dram_resource {
	/**
	 * @brief Pointer to device memory allocation for this DRAM resource
	 */
	struct pva_kmd_device_memory *mem;
};

/**
 * @brief Resource structure for VPU binary executables
 *
 * @details This structure contains information about a VPU executable binary
 * that has been loaded and registered with the resource table. It includes
 * both metadata and code sections, along with symbol table information for
 * proper executable management and debugging.
 */
struct pva_kmd_vpu_bin_resource {
	/**
	 * @brief Device memory allocation for executable metadata
	 */
	struct pva_kmd_device_memory *metainfo_mem;

	/**
	 * @brief Device memory allocation for executable code sections
	 */
	struct pva_kmd_device_memory *sections_mem;

	/**
	 * @brief Symbol table for debugging and runtime symbol resolution
	 */
	struct pva_kmd_exec_symbol_table symbol_table;
};

/**
 * @brief Resource structure for DMA configuration objects
 *
 * @details This structure contains information about a DMA configuration
 * that has been validated and registered with the resource table. It includes
 * the device memory element, auxiliary memory, and addressing information
 * needed for DMA operations.
 */
struct pva_kmd_dma_config_resource {
	/**
	 * @brief Device memory element for DMA configuration storage
	 */
	struct pva_kmd_devmem_element devmem;

	/**
	 * @brief Auxiliary memory for additional DMA configuration data
	 */
	struct pva_kmd_dma_resource_aux *aux_mem;

	/**
	 * @brief Size of the DMA configuration in bytes
	 * Valid range: [1 .. UINT64_MAX]
	 */
	uint64_t size;

	/**
	 * @brief IOVA address for hardware access to DMA configuration
	 * Valid range: [0 .. UINT64_MAX]
	 */
	uint64_t iova_addr;
};

/**
 * @brief Resource record for tracking registered resources
 *
 * @details This structure represents a single entry in the resource table,
 * containing type information, reference counting, and type-specific data
 * for the resource. Resources can be DRAM buffers, VPU executables, or
 * DMA configurations, each with their own specific data requirements.
 */
struct pva_kmd_resource_record {
	/**
	 * @brief Type of resource stored in this record
	 *
	 * @details Possible types:
	 * - PVA_RESOURCE_TYPE_DRAM
	 * - PVA_RESOURCE_TYPE_EXEC_BIN
	 * - PVA_RESOURCE_TYPE_DMA_CONFIG
	 * Valid values: @ref pva_resource_type enumeration values
	 */
	uint8_t type;

	/**
	 * @brief Reference count for tracking resource usage
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t ref_count;

	/**
	 * @brief Union containing type-specific resource data
	 */
	union {
		/** @brief DRAM resource data */
		struct pva_kmd_dram_resource dram;
		/** @brief VPU binary resource data */
		struct pva_kmd_vpu_bin_resource vpu_bin;
		/** @brief DMA configuration resource data */
		struct pva_kmd_dma_config_resource dma_config;
	};
};

/**
 * @brief Resource table for managing hardware resources and firmware communication
 *
 * @details This structure manages the resource table used for communication
 * between KMD and firmware. It provides secure resource management through
 * indirection, where user space applications use opaque resource IDs instead
 * of direct hardware addresses. The table supports different resource types
 * including DRAM buffers, VPU executables, and DMA configurations.
 */
struct pva_kmd_resource_table {
	/**
	 * @brief User SMMU context ID for memory protection
	 *
	 * @details - DRAM memory, VPU data/text sections will be mapped to this space.
	 * - VPU metadata, DMA configurations will always be mapped to R5 SMMU context.
	 * Valid range: [0 .. PVA_MAX_NUM_SMMU_CONTEXTS-1]
	 */
	uint8_t user_smmu_ctx_id;

	/**
	 * @brief Number of allocated entries in the resource table
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t n_entries;

	/**
	 * @brief Maximum resource ID allocated so far
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t curr_max_resource_id;

	/**
	 * @brief Semaphore to track resources currently in use
	 *
	 * @details Semaphore to keep track of resources in use
	 */
	pva_kmd_sema_t resource_semaphore;

	/**
	 * @brief Device memory for resource table entries in R5 segment
	 *
	 * @details Memory for resource table entries, in R5 segment
	 */
	struct pva_kmd_device_memory *table_mem;

	/**
	 * @brief Memory pool for firmware DMA configurations
	 *
	 * @details Pool for FW DMA configurations
	 */
	struct pva_kmd_devmem_pool dma_config_pool;

	/**
	 * @brief Memory allocation for resource record storage
	 *
	 * @details Memory for resource records
	 */
	void *records_mem;

	/**
	 * @brief Block allocator for managing resource record allocation
	 */
	struct pva_kmd_block_allocator resource_record_allocator;

	/**
	 * @brief Block allocator for managing privileged resource record allocation
	 */
	struct pva_kmd_block_allocator priv_resource_record_allocator;

	/**
	 * @brief Pointer to parent PVA device
	 */
	struct pva_kmd_device *pva;

	/**
	 * @brief Mutex protecting resource table operations
	 */
	pva_kmd_mutex_t resource_table_lock;
};

/**
 * @brief Initialize a resource table for KMD-firmware communication
 *
 * @details This function performs the following operations:
 * - Allocates device memory for the resource table entries
 * - Initializes the resource record allocator for managing entries
 * - Sets up the DMA configuration pool for firmware DMA operations
 * - Configures SMMU context for user memory protection
 * - Initializes synchronization primitives for thread-safe access
 * - Prepares the table for resource registration and management
 * - Establishes communication interface with firmware
 *
 * The initialized resource table provides secure resource management by
 * using opaque resource IDs instead of direct hardware addresses. After
 * initialization, resources can be registered using the various add
 * functions and accessed through resource IDs.
 *
 * @param[out] res_table         Pointer to @ref pva_kmd_resource_table structure to initialize
 *                               Valid value: non-null
 * @param[in] pva                Pointer to @ref pva_kmd_device structure
 *                               Valid value: non-null
 * @param[in] user_smmu_ctx_id   SMMU context ID for user memory protection
 *                               Valid range: [0 .. PVA_MAX_NUM_SMMU_CONTEXTS-1]
 * @param[in] n_entries          Maximum number of resource entries supported
 *                               Valid range: [1 .. UINT32_MAX]
 *
 * @retval PVA_SUCCESS                  Resource table initialized successfully
 * @retval PVA_NOMEM                    Failed to allocate table memory
 * @retval PVA_INTERNAL                 Failed to initialize DMA configuration pool
 * @retval PVA_ENOSPC                   Failed to initialize resource record allocator
 */
enum pva_error
pva_kmd_resource_table_init(struct pva_kmd_resource_table *res_table,
			    struct pva_kmd_device *pva,
			    uint8_t user_smmu_ctx_id, uint32_t n_entries);

/**
 * @brief Deinitialize a resource table and free associated resources
 *
 * @details This function performs the following operations:
 * - Ensures all resources are properly released and unreferenced
 * - Deinitializes the DMA configuration pool
 * - Cleans up the resource record allocator
 * - Frees allocated device memory for table entries
 * - Destroys synchronization primitives
 * - Invalidates the resource table for future use
 *
 * All resources must be properly dropped before calling this function.
 * After deinitialization, the resource table becomes invalid and cannot
 * be used for further operations.
 *
 * @param[in, out] res_table  Pointer to @ref pva_kmd_resource_table structure to deinitialize
 *                            Valid value: non-null, must be initialized
 */
void pva_kmd_resource_table_deinit(struct pva_kmd_resource_table *res_table);

/**
 * @brief Update firmware resource table with current resource information
 *
 * @details This function performs the following operations:
 * - Synchronizes the firmware resource table with KMD resource state
 * - Updates resource entries that firmware needs for operation
 * - Ensures firmware has current IOVA mappings and resource metadata
 * - Maintains consistency between KMD and firmware resource views
 *
 * KMD only writes to FW resource table during init time. Once the address of
 * the resource table is sent to FW, all updates should be done through commands.
 * This function is typically called during initialization or after significant
 * resource table changes that require firmware synchronization.
 *
 * @param[in, out] res_table  Pointer to @ref pva_kmd_resource_table structure
 *                            Valid value: non-null, must be initialized
 */
void pva_kmd_update_fw_resource_table(struct pva_kmd_resource_table *res_table);

/**
 * @brief Add a DRAM buffer resource to the resource table
 *
 * @details This function performs the following operations:
 * - Validates the provided device memory allocation
 * - Allocates a new resource record for the DRAM buffer
 * - Maps the memory to the appropriate SMMU context
 * - Assigns a unique resource ID for the buffer
 * - Initializes reference counting for the resource
 * - Updates the resource table with the new entry
 * - Returns the assigned resource ID to the caller
 *
 * The DRAM buffer becomes accessible to firmware through the returned
 * resource ID. The memory must remain valid for the lifetime of the
 * resource registration.
 *
 * @param[in, out] resource_table  Pointer to @ref pva_kmd_resource_table structure
 *                                 Valid value: non-null, must be initialized
 * @param[in] memory               Pointer to device memory allocation
 *                                 Valid value: non-null, must be allocated
 * @param[out] out_resource_id     Pointer to store the assigned resource ID
 *                                 Valid value: non-null
 *
 * @retval PVA_SUCCESS                  Resource added successfully
 * @retval PVA_NOMEM                    Failed to allocate resource record
 * @retval PVA_NO_RESOURCE_ID           No available resource IDs
 * @retval PVA_INVAL                    Failed to map memory to SMMU context
 */
enum pva_error
pva_kmd_add_dram_buffer_resource(struct pva_kmd_resource_table *resource_table,
				 struct pva_kmd_device_memory *memory,
				 uint32_t *out_resource_id, bool priv);

/**
 * @brief Add a VPU binary executable resource to the resource table
 *
 * @details This function performs the following operations:
 * - Validates and parses the provided executable binary
 * - Allocates device memory for executable metadata and code sections
 * - Loads the executable into appropriate memory segments
 * - Maps executable sections to SMMU contexts with proper permissions
 * - Extracts and stores symbol table information for debugging
 * - Assigns a unique resource ID for the executable
 * - Updates the resource table with the new executable entry
 *
 * The VPU executable becomes available for execution through the returned
 * resource ID. The executable data must remain valid for the lifetime of
 * the resource registration.
 *
 * @param[in, out] resource_table  Pointer to @ref pva_kmd_resource_table structure
 *                                 Valid value: non-null, must be initialized
 * @param[in] executable           Pointer to executable binary data
 *                                 Valid value: non-null
 * @param[in] executable_size      Size of executable binary in bytes
 *                                 Valid range: [1 .. UINT32_MAX]
 * @param[out] out_resource_id     Pointer to store the assigned resource ID
 *                                 Valid value: non-null
 *
 * @retval PVA_SUCCESS                  Executable resource added successfully
 * @retval PVA_INVALID_SYMBOL           Executable format is invalid or corrupted
 * @retval PVA_NOMEM                    Failed to allocate memory for executable
 * @retval PVA_NO_RESOURCE_ID           No available resource IDs
 * @retval PVA_INVAL                    Failed to map executable to SMMU context
 */
enum pva_error
pva_kmd_add_vpu_bin_resource(struct pva_kmd_resource_table *resource_table,
			     const void *executable, uint32_t executable_size,
			     uint32_t *out_resource_id, bool priv);

/**
 * @brief Add a DMA configuration resource to the resource table
 *
 * @details This function performs the following operations:
 * - Validates the provided DMA configuration parameters
 * - Allocates device memory from the DMA configuration pool
 * - Stores the DMA configuration in firmware-accessible memory
 * - Maps the configuration to appropriate SMMU contexts
 * - Validates DMA access patterns and constraints
 * - Assigns a unique resource ID for the DMA configuration
 * - Updates the resource table with the new configuration entry
 *
 * The DMA configuration becomes available for DMA operations through the
 * returned resource ID. The configuration includes transfer parameters,
 * access patterns, and memory slot definitions.
 *
 * @param[in, out] resource_table  Pointer to @ref pva_kmd_resource_table structure
 *                                 Valid value: non-null, must be initialized
 * @param[in] dma_cfg_hdr          Pointer to DMA configuration header
 *                                 Valid value: non-null
 * @param[in] dma_config_size      Size of DMA configuration in bytes
 *                                 Valid range: [1 .. UINT32_MAX]
 * @param[out] out_resource_id     Pointer to store the assigned resource ID
 *                                 Valid value: non-null
 *
 * @retval PVA_SUCCESS                  DMA configuration added successfully
 * @retval PVA_INVALID_DMA_CONFIG       DMA configuration is invalid
 * @retval PVA_ENOSPC                   DMA configuration pool is full
 * @retval PVA_NO_RESOURCE_ID           No available resource IDs
 * @retval PVA_INVAL                    Failed to map configuration to SMMU context
 */
enum pva_error pva_kmd_add_dma_config_resource(
	struct pva_kmd_resource_table *resource_table,
	const struct pva_ops_dma_config_register *dma_cfg_hdr,
	uint32_t dma_config_size, uint32_t *out_resource_id, bool priv);

/**
 * @brief Increment reference count and get resource record (unsafe version)
 *
 * @details This function performs the following operations:
 * - Validates the resource ID against the resource table bounds
 * - Retrieves the resource record for the specified ID
 * - Increments the reference count for usage tracking
 * - Returns pointer to the resource record for access
 *
 * This function is not thread-safe and requires external synchronization.
 * The caller must ensure proper locking around calls to this function and
 * the corresponding @ref pva_kmd_drop_resource_unsafe().
 *
 * @param[in, out] resource_table  Pointer to @ref pva_kmd_resource_table structure
 *                                 Valid value: non-null, must be initialized
 * @param[in] resource_id          Resource ID to access
 *                                 Valid range: [0 .. curr_max_resource_id]
 *
 * @retval non-null  Pointer to @ref pva_kmd_resource_record if resource exists
 * @retval NULL      Invalid resource ID or resource not found
 *
 * @note TODO: make use and drop thread safe.
 */
struct pva_kmd_resource_record *
pva_kmd_use_resource_unsafe(struct pva_kmd_resource_table *resource_table,
			    uint32_t resource_id);

/**
 * @brief Increment reference count and get resource record (thread-safe version)
 *
 * @details This function performs the following operations:
 * - Acquires the resource table lock for thread-safe operation
 * - Validates the resource ID against the resource table bounds
 * - Retrieves the resource record for the specified ID
 * - Increments the reference count for usage tracking
 * - Releases the resource table lock
 * - Returns pointer to the resource record for access
 *
 * This function is thread-safe and can be called concurrently from multiple
 * threads. The returned resource record remains valid until the corresponding
 * @ref pva_kmd_drop_resource() is called.
 *
 * @param[in, out] resource_table  Pointer to @ref pva_kmd_resource_table structure
 *                                 Valid value: non-null, must be initialized
 * @param[in] resource_id          Resource ID to access
 *                                 Valid range: [0 .. curr_max_resource_id]
 *
 * @retval non-null  Pointer to @ref pva_kmd_resource_record if resource exists
 * @retval NULL      Invalid resource ID or resource not found
 */
struct pva_kmd_resource_record *
pva_kmd_use_resource(struct pva_kmd_resource_table *resource_table,
		     uint32_t resource_id);

/**
 * @brief Get resource record without incrementing reference count
 *
 * @details This function performs the following operations:
 * - Acquires the resource table lock for thread-safe operation
 * - Validates the resource ID against the resource table bounds
 * - Retrieves the resource record for the specified ID
 * - Returns pointer to the resource record without changing reference count
 * - Releases the resource table lock
 *
 * This function allows inspection of resource records without affecting
 * their reference counting. The returned pointer is only valid while
 * the caller maintains appropriate synchronization with the resource table.
 *
 * @param[in] resource_table  Pointer to @ref pva_kmd_resource_table structure
 *                            Valid value: non-null, must be initialized
 * @param[in] resource_id     Resource ID to inspect
 *                            Valid range: [0 .. curr_max_resource_id]
 *
 * @retval non-null  Pointer to @ref pva_kmd_resource_record if resource exists
 * @retval NULL      Invalid resource ID or resource not found
 */
struct pva_kmd_resource_record *
pva_kmd_peek_resource(struct pva_kmd_resource_table *resource_table,
		      uint32_t resource_id);

/**
 * @brief Decrement reference count for a resource (thread-safe version)
 *
 * @details This function performs the following operations:
 * - Acquires the resource table lock for thread-safe operation
 * - Validates the resource ID against the resource table bounds
 * - Decrements the reference count for the specified resource
 * - Checks if the reference count reaches zero for cleanup
 * - Frees the resource record if no longer referenced
 * - Releases the resource table lock
 *
 * This function must be called for every successful @ref pva_kmd_use_resource()
 * to maintain proper reference counting. When the reference count reaches
 * zero, the resource is automatically cleaned up and its ID becomes available
 * for reuse.
 *
 * @param[in, out] resource_table  Pointer to @ref pva_kmd_resource_table structure
 *                                 Valid value: non-null, must be initialized
 * @param[in] resource_id          Resource ID to release
 *                                 Valid range: [0 .. curr_max_resource_id]
 */
void pva_kmd_drop_resource(struct pva_kmd_resource_table *resource_table,
			   uint32_t resource_id);

/**
 * @brief Decrement reference count for a resource (unsafe version)
 *
 * @details This function performs the following operations:
 * - Validates the resource ID against the resource table bounds
 * - Decrements the reference count for the specified resource
 * - Checks if the reference count reaches zero for cleanup
 * - Frees the resource record if no longer referenced
 *
 * This function is not thread-safe and requires external synchronization.
 * The caller must ensure proper locking around calls to this function and
 * the corresponding @ref pva_kmd_use_resource_unsafe().
 *
 * @param[in, out] resource_table  Pointer to @ref pva_kmd_resource_table structure
 *                                 Valid value: non-null, must be initialized
 * @param[in] resource_id          Resource ID to release
 *                                 Valid range: [0 .. curr_max_resource_id]
 */
void pva_kmd_drop_resource_unsafe(struct pva_kmd_resource_table *resource_table,
				  uint32_t resource_id);

/**
 * @brief Create a firmware-compatible resource entry from a resource record
 *
 * @details This function performs the following operations:
 * - Retrieves the resource record for the specified resource ID
 * - Extracts type-specific information from the resource record
 * - Converts the resource data to firmware-compatible format
 * - Populates the provided resource entry structure
 * - Includes IOVA addresses and metadata needed by firmware
 * - Validates resource consistency before entry creation
 *
 * The created resource entry can be used by firmware to access the
 * resource through hardware operations. The entry contains all necessary
 * information for firmware to perform DMA operations or executable loading.
 *
 * @param[in] resource_table  Pointer to @ref pva_kmd_resource_table structure
 *                            Valid value: non-null, must be initialized
 * @param[in] resource_id     Resource ID to create entry for
 *                            Valid range: [0 .. curr_max_resource_id]
 * @param[out] entry          Pointer to @ref pva_resource_entry to populate
 *                            Valid value: non-null
 *
 * @retval PVA_SUCCESS              Resource entry created successfully
 * @retval PVA_INVAL                Invalid resource ID or entry pointer
 * @retval PVA_INVALID_RESOURCE     Resource in invalid state for entry creation
 */
enum pva_error
pva_kmd_make_resource_entry(struct pva_kmd_resource_table *resource_table,
			    uint32_t resource_id,
			    struct pva_resource_entry *entry);

/**
 * @brief Acquire lock for a specific resource table
 *
 * @details This function performs the following operations:
 * - Identifies the resource table associated with the specified ID
 * - Acquires the mutex lock for thread-safe resource table access
 * - Provides exclusive access to resource table operations
 * - Ensures atomic operations on resource table state
 *
 * This function should be used when multiple resource table operations
 * need to be performed atomically. The lock must be released using
 * @ref pva_kmd_resource_table_unlock() after the operations complete.
 *
 * @param[in] pva           Pointer to @ref pva_kmd_device structure
 *                          Valid value: non-null
 * @param[in] res_table_id  Resource table identifier to lock
 *                          Valid range: [0 .. PVA_KMD_MAX_NUM_KMD_RESOURCES-1]
 */
void pva_kmd_resource_table_lock(struct pva_kmd_device *pva,
				 uint8_t res_table_id);

/**
 * @brief Release lock for a specific resource table
 *
 * @details This function performs the following operations:
 * - Identifies the resource table associated with the specified ID
 * - Releases the mutex lock to allow other threads access
 * - Completes the atomic operation sequence started with lock
 * - Enables other threads to perform resource table operations
 *
 * This function must be called after @ref pva_kmd_resource_table_lock()
 * to release the exclusive access to the resource table. Failure to
 * unlock will result in deadlock for other threads.
 *
 * @param[in] pva           Pointer to @ref pva_kmd_device structure
 *                          Valid value: non-null
 * @param[in] res_table_id  Resource table identifier to unlock
 *                          Valid range: [0 .. PVA_KMD_MAX_NUM_KMD_RESOURCES-1]
 */
void pva_kmd_resource_table_unlock(struct pva_kmd_device *pva,
				   uint8_t res_table_id);
#endif // PVA_KMD_RESOURCE_TABLE_H
