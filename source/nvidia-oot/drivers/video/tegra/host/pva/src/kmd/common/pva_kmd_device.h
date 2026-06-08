/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_DEVICE_H
#define PVA_KMD_DEVICE_H
#include "pva_constants.h"
#include "pva_fw.h"
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_mutex.h"
#include "pva_kmd_block_allocator.h"
#include "pva_kmd_queue.h"
#include "pva_kmd_resource_table.h"
#include "pva_kmd_submitter.h"
#include "pva_kmd_regs.h"
#include "pva_kmd_thread_sema.h"
#include "pva_kmd_shim_init.h"
#include "pva_kmd_shim_ccq.h"
#include "pva_kmd_fw_profiler.h"
#include "pva_kmd_fw_debug_printf.h"
#include "pva_kmd_constants.h"
#include "pva_kmd_debugfs.h"
#include "pva_kmd_co.h"

/**
 * @brief Class ID for PVA device 0
 *
 * @details This macro defines the unique class identifier used to identify
 * the first PVA device (PVA0) in the system. The class ID is used by various
 * hardware and software components to distinguish between different PVA
 * instances and route operations to the correct device.
 */
#define NV_PVA0_CLASS_ID 0xF1

/**
 * @brief Class ID for PVA device 1
 *
 * @details This macro defines the unique class identifier used to identify
 * the second PVA device (PVA1) in the system. The class ID is used by various
 * hardware and software components to distinguish between different PVA
 * instances and route operations to the correct device.
 */
#define NV_PVA1_CLASS_ID 0xF2

/**
 * @brief Structure for syncpoint read/write information
 *
 * @details This structure contains information needed to access a specific
 * syncpoint for read and write operations. It includes both the syncpoint
 * identifier and its corresponding IOVA address for hardware access.
 */
struct pva_syncpt_rw_info {
	/**
	 * @brief Syncpoint identifier
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t syncpt_id;

	/**
	 * @brief IOVA address of the syncpoint for hardware access
	 * Valid range: [0 .. UINT64_MAX]
	 */
	uint64_t syncpt_iova;
};

/**
 * @brief Structure to maintain start and end address of vmem region
 *
 * @details This structure defines a contiguous region of vector memory (VMEM)
 * by specifying its start and end addresses. VMEM is the fast on-chip memory
 * used by the VPU for storing data being actively processed.
 */
struct vmem_region {
	/**
	 * @brief Start address of vmem region
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t start;

	/**
	 * @brief End address of vmem region
	 * Valid range: [start .. UINT32_MAX]
	 */
	uint32_t end;
};

/**
 * @brief Structure containing hardware-specific constants for PVA
 *
 * @details This structure holds various hardware configuration parameters
 * that are specific to different PVA hardware generations. These constants
 * are used throughout the KMD to adapt behavior based on the underlying
 * hardware capabilities and limitations.
 */
struct pva_kmd_hw_constants {
	/**
	 * @brief PVA hardware generation identifier
	 * Valid values: @ref pva_hw_gen enumeration values
	 */
	enum pva_hw_gen hw_gen;

	/**
	 * @brief Number of VMEM regions available
	 * Valid range: [0 .. UINT8_MAX]
	 */
	uint8_t n_vmem_regions;

	/**
	 * @brief Number of DMA descriptors available
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t n_dma_descriptors;

	/**
	 * @brief Number of user-accessible DMA channels
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t n_user_dma_channels;

	/**
	 * @brief Number of hardware sequencer words
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t n_hwseq_words;

	/**
	 * @brief Number of dynamic ADB buffers
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t n_dynamic_adb_buffs;

	/**
	 * @brief Number of SMMU contexts available
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t n_smmu_contexts;
};

struct pva_kmd_pfsd_info {
	const uint8_t *vpu_elf_data;
	uint32_t vpu_elf_size;
	const uint8_t *ppe_elf_data;
	uint32_t ppe_elf_size;
	struct pva_dma_config pfsd_dma_cfg;
	enum pva_error (*register_cmd_buffer)(struct pva_kmd_context *ctx);
};

/**
 * @brief This struct manages a single PVA cluster.
 *
 * @details Fields in this struct should be common across all platforms. Platform
 * specific data is stored in plat_data field. This structure represents the
 * complete state of a PVA device instance and contains all the resources,
 * configuration, and runtime state needed to manage the device.
 */
struct pva_kmd_device {
	/**
	 * @brief Device index identifying this PVA instance
	 * Valid range: [0 .. PVA_MAX_DEVICES-1]
	 */
	uint32_t device_index;

	/**
	 * @brief SMMU context ID for R5 firmware image
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t r5_image_smmu_context_id;

	/**
	 * @brief Array of stream IDs for SMMU contexts
	 * Valid range: Each element [0 .. UINT32_MAX]
	 */
	uint32_t stream_ids[PVA_MAX_NUM_SMMU_CONTEXTS];

	/**
	 * @brief Hardware constants specific to this PVA instance
	 */
	struct pva_kmd_hw_constants hw_consts;

	/**
	 * @brief Physical base addresses for device register apertures
	 * Valid range: Each element [0 .. UINT64_MAX]
	 */
	uint64_t reg_phy_base[PVA_KMD_APERTURE_COUNT];

	/**
	 * @brief Sizes of device register apertures
	 * Valid range: Each element [0 .. UINT64_MAX]
	 */
	uint64_t reg_size[PVA_KMD_APERTURE_COUNT];

	/**
	 * @brief Register specification structure for device access
	 */
	struct pva_kmd_regspec regspec;

	/**
	 * @brief Maximum number of contexts supported by this device
	 * Valid range: [0 .. PVA_MAX_NUM_USER_CONTEXTS]
	 */
	uint8_t max_n_contexts;

	/**
	 * @brief Pointer to allocated memory for context storage
	 * Valid value: non-null when contexts are allocated
	 */
	void *context_mem;

	/**
	 * @brief Block allocator for managing context allocation
	 */
	struct pva_kmd_block_allocator context_allocator;

	/**
	 * @brief Device-level resource table for privileged operations
	 */
	struct pva_kmd_resource_table dev_resource_table;

	/**
	 * @brief Command submission handler for device-level operations
	 */
	struct pva_kmd_submitter submitter;

	/**
	 * @brief Lock protecting submission operations and post fence increment
	 *
	 * @details The lock protects the submission to the queue, including
	 * incrementing the post fence
	 */
	pva_kmd_mutex_t submit_lock;

	/**
	 * @brief Device memory allocation for queue operations
	 */
	struct pva_kmd_device_memory *queue_memory;

	/**
	 * @brief Device-level command queue
	 */
	struct pva_kmd_queue dev_queue;

	/**
	 * @brief Memory allocation for submission operations
	 *
	 * @details Memory needed for submission: including command buffer chunks and fences
	 */
	struct pva_kmd_device_memory *submit_memory;

	/**
	 * @brief Resource ID for submission memory in resource table
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t submit_memory_resource_id;

	/**
	 * @brief Offset of fence within submission memory
	 * Valid range: [0 .. submit_memory_size-1]
	 */
	uint64_t fence_offset;

	/**
	 * @brief Lock protecting command buffer chunk pool operations
	 */
	pva_kmd_mutex_t chunk_pool_lock;

	/**
	 * @brief Pool of command buffer chunks for efficient allocation
	 */
	struct pva_kmd_cmdbuf_chunk_pool chunk_pool;

	/**
	 * @brief Lock protecting power cycle operations
	 */
	pva_kmd_mutex_t powercycle_lock;

	/**
	 * @brief Reference count for device usage tracking
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t refcount;

	/**
	 * @brief Semaphore posted by ISR when FW completes boot
	 *
	 * @details ISR post this semaphore when FW completes boot
	 */
	pva_kmd_sema_t fw_boot_sema;

	/**
	 * @brief Flag indicating if device is in recovery mode
	 * Valid values: true, false
	 */
	bool recovery;
	/** Firmware is aborted and won't respond */
	bool fw_aborted;

	/**
	 * @brief Device memory allocation for firmware debug information
	 */
	struct pva_kmd_device_memory *fw_debug_mem;

	/**
	 * @brief Device memory allocation for firmware binary
	 */
	struct pva_kmd_device_memory *fw_bin_mem;

	/**
	 * @brief DRAM buffers shared between KMD and FW
	 *
	 * @details 'kmd_fw_buffers' holds DRAM buffers shared between KMD and FW
	 * - Today, we have 1 buffer per CCQ. This may need to be extended in future
	 *   to support buffered communication through mailbox
	 * - Buffers will be used for the following purposes
	 *   - CCQ 0: Communications common to a VM
	 *		-- example, FW profiling data and NSIGHT data
	 *   - CCQ 1-8: Communications specific to each context
	 *		-- example, resource unregistration requests
	 * In the future, we may want to extend this to support communications between
	 * FW and Hypervisor
	 */
	struct pva_kmd_shared_buffer kmd_fw_buffers[PVA_MAX_NUM_CCQ];

	/**
	 * @brief Current firmware trace level setting
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t fw_trace_level;

	/**
	 * @brief Buffer for firmware print/debug output
	 */
	struct pva_kmd_fw_print_buffer fw_print_buffer;

	/**
	 * @brief Device memory allocation for Tegra statistics collection
	 */
	struct pva_kmd_device_memory *tegra_stats_memory;

	/**
	 * @brief Resource ID for Tegra statistics memory
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t tegra_stats_resource_id;

	/**
	 * @brief Size of Tegra statistics buffer
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t tegra_stats_buf_size;

	/**
	 * @brief Flag indicating if firmware should be loaded from GSC
	 * Valid values: true, false
	 */
	bool load_from_gsc;

	/**
	 * @brief Flag indicating if device is running in hypervisor mode
	 * Valid values: true, false
	 */
	bool is_hv_mode;

	/**
	 * @brief Flag indicating if device is running on silicon hardware
	 * Valid values: true, false
	 */
	bool is_silicon;

	/**
	 * @brief Debug filesystem context for development and debugging
	 */
	struct pva_kmd_debugfs_context debugfs_context;

	/**
	 * @brief Sector packing format for block linear surfaces
	 * Valid range: [0 .. UINT8_MAX]
	 */
	uint8_t bl_sector_pack_format;

	/**
	 * @brief Size offset between consecutive syncpoints
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t syncpt_page_size;

	/**
	 * @brief Base IOVA for read-only syncpoints
	 * Valid range: [0 .. UINT64_MAX]
	 */
	uint64_t ro_syncpt_base_iova;

	/**
	 * @brief Number of read-only syncpoints available
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t num_ro_syncpts;

	/**
	 * @brief Base IOVA for read-write syncpoints
	 * Valid range: [0 .. UINT64_MAX]
	 */
	uint64_t rw_syncpt_base_iova;

	/**
	 * @brief Size of read-write syncpoint region
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t rw_syncpt_region_size;

	/**
	 * @brief Array of read-write syncpoint information
	 */
	struct pva_syncpt_rw_info rw_syncpts[PVA_NUM_RW_SYNCPTS];

	/**
	 * @brief Pointer to table of VMEM region definitions
	 */
	struct vmem_region *vmem_regions_tab;

	/**
	 * @brief Flag indicating support for hardware sequencer frame linking
	 * Valid values: true, false
	 */
	bool support_hwseq_frame_linking;

	/**
	 * @brief Platform-specific private data pointer
	 */
	void *plat_data;

	/**
	 * @brief Pointer to VPU authentication context
	 */
	struct pva_vpu_auth *pva_auth;

	/**
	 * @brief Flag indicating if firmware has been initialized
	 * Valid values: true, false
	 */
	bool fw_inited;

	/**
	 * @brief Carveout memory information for firmware
	 */
	struct pva_co_info fw_carveout;

	/**
	 * @brief Flag indicating if device is in test mode
	 * Valid values: true, false
	 */
	bool test_mode;

	/**
	 * @brief Atomic counter for deferred context free operations
	 */
	pva_kmd_atomic_t n_deferred_context_free;

	/**
	 * @brief Array of context IDs pending deferred free
	 */
	uint8_t deferred_context_free_ids[PVA_MAX_NUM_USER_CONTEXTS];

	/**
	 * @brief Multiplier to convert TSC ticks to nanoseconds
	 * Valid range: [0 .. UINT64_MAX]
	 */
	uint64_t tsc_to_ns_multiplier;

	/**
	 * @brief PFSD information
	 */
	struct pva_kmd_pfsd_info pfsd_info;

#if PVA_ENABLE_R5_OCD == 1
	/**
	 * @brief Flag indicating if R5 on-chip debugging is enabled
	 * Valid values: true, false
	 */
	bool r5_ocd_on;
#endif
};

/**
 * @brief Create and initialize a new PVA KMD device instance
 *
 * @details This function performs the following operations:
 * - Allocates memory for a new @ref pva_kmd_device structure
 * - Initializes the device based on the specified chip ID and configuration
 * - Sets up hardware constants and register mappings for the device
 * - Configures authentication settings based on the app_authenticate parameter
 * - Initializes internal data structures including resource tables, queues,
 *   and memory allocators
 * - Sets up platform-specific components using @ref pva_kmd_device_plat_init()
 * - Configures test mode settings if requested
 * - Prepares the device for firmware loading and initialization
 *
 * The created device must be destroyed using @ref pva_kmd_device_destroy()
 * when no longer needed to prevent resource leaks. The device will be in
 * an uninitialized state until @ref pva_kmd_init_fw() is called.
 *
 * @param[in] chip_id          Chip identifier specifying the target hardware
 *                             Valid values: @ref pva_chip_id enumeration values
 * @param[in] device_index     Index of the PVA device instance to create
 *                             Valid range: [0 .. PVA_MAX_DEVICES-1]
 * @param[in] app_authenticate Flag to enable application authentication
 *                             Valid values: true, false
 * @param[in] test_mode        Flag to enable test mode configuration
 *                             Valid values: true, false
 *
 * @retval non-null  Pointer to successfully created @ref pva_kmd_device
 * @retval NULL      Device creation failed due to memory allocation failure,
 *                   invalid parameters, or platform initialization failure
 */
struct pva_kmd_device *pva_kmd_device_create(enum pva_chip_id chip_id,
					     uint32_t device_index,
					     bool app_authenticate,
					     bool test_mode, void *plat_data);

/**
 * @brief Destroy a PVA KMD device instance and free all resources
 *
 * @details This function performs the following operations:
 * - Deinitializes the firmware using @ref pva_kmd_deinit_fw() if initialized
 * - Stops and cleans up all active contexts and their associated resources
 * - Frees all allocated device memory including command buffers, queues,
 *   and shared buffers
 * - Releases hardware resources and register mappings
 * - Cleans up platform-specific resources through platform callbacks
 * - Destroys synchronization primitives including mutexes and semaphores
 * - Frees the device structure itself
 *
 * After calling this function, the device pointer becomes invalid and must
 * not be used. Any ongoing operations on the device should be completed
 * or cancelled before calling this function.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure to destroy
 *                 Valid value: non-null, must be a valid device created by
 *                 @ref pva_kmd_device_create()
 */
void pva_kmd_device_destroy(struct pva_kmd_device *pva);

/**
 * @brief Add a context ID to the deferred free list
 *
 * @details This function performs the following operations:
 * - Atomically increments the count of deferred context free operations
 * - Adds the specified CCQ ID to the deferred context free array
 * - Ensures thread-safe access to the deferred free list
 * - Marks the context for cleanup during the next power cycle
 *
 * This function is used when a context cannot be freed immediately,
 * typically because the firmware is still processing commands for that
 * context. The context will be freed when the device is next powered off
 * and reinitialized.
 *
 * @param[in, out] pva     Pointer to @ref pva_kmd_device structure
 *                         Valid value: non-null
 * @param[in] ccq_id       CCQ ID of the context to defer free
 *                         Valid range: [0 .. PVA_MAX_NUM_CCQ-1]
 */
void pva_kmd_add_deferred_context_free(struct pva_kmd_device *pva,
				       uint8_t ccq_id);

/**
 * @brief Initialize firmware on the PVA device
 *
 * @details This function performs the following operations:
 * - Loads the firmware binary into device memory using platform-specific loaders
 * - Sets up firmware execution environment including memory mappings
 * - Initializes communication channels between KMD and firmware
 * - Configures hardware resources needed by the firmware
 * - Starts the firmware execution and waits for boot completion
 * - Sets up shared buffers for KMD-FW communication
 * - Initializes firmware debug and profiling capabilities
 * - Establishes resource tables for hardware resource management
 *
 * The device must be created using @ref pva_kmd_device_create() before
 * calling this function. After successful initialization, the device is
 * ready to accept and process user commands.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 *
 * @retval PVA_SUCCESS                    Firmware initialized successfully
 * @retval PVA_TIMEDOUT                  Firmware boot timeout occurred
 * @retval PVA_NOMEM                     Memory allocation for FW resources failed
 * @retval PVA_ERR_FW_ABORTED            Firmware operation aborted during recovery
 */
enum pva_error pva_kmd_init_fw(struct pva_kmd_device *pva);

/**
 * @brief Deinitialize firmware and clean up resources
 *
 * @details This function performs the following operations:
 * - Stops firmware execution and waits for graceful shutdown
 * - Frees all firmware-related memory allocations
 * - Cleans up communication channels and shared buffers
 * - Releases hardware resources allocated to firmware
 * - Deinitializes debug and profiling subsystems
 * - Resets device hardware to a known state
 * - Marks the device as uninitialized for future use
 *
 * After calling this function, the device can be reinitialized using
 * @ref pva_kmd_init_fw() or destroyed using @ref pva_kmd_device_destroy().
 * Any ongoing operations should be completed before calling this function.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 *
 * @retval PVA_SUCCESS                 Firmware deinitialized successfully
 * @retval PVA_INTERNAL               Failed to properly deinitialize firmware
 * @retval PVA_TIMEDOUT               Firmware shutdown timeout occurred
 */
enum pva_error pva_kmd_deinit_fw(struct pva_kmd_device *pva);

/**
 * @brief Check if the PVA device might be powered on
 *
 * @details This function performs the following operations:
 * - Checks device power state indicators to determine if device is active
 * - Reads hardware registers to verify device accessibility
 * - Uses platform-specific power management information if available
 * - Returns a best-effort assessment of device power state
 * - Does not guarantee definitive power state due to timing considerations
 *
 * This function provides a quick check for device availability without
 * performing operations that might cause side effects. The result should
 * be treated as a hint rather than a definitive power state.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null
 *
 * @retval true   Device appears to be powered on and accessible
 * @retval false  Device appears to be powered off or inaccessible
 */
bool pva_kmd_device_maybe_on(struct pva_kmd_device *pva);

/**
 * @brief Query the firmware version string from the device
 *
 * @details This function performs the following operations:
 * - Sends a version query command to the firmware
 * - Waits for the firmware response containing version information
 * - Copies the version string to the provided buffer
 * - Ensures null-termination of the version string
 * - Handles buffer size limitations gracefully
 * - Returns error if firmware is not responsive or accessible
 *
 * The version information includes firmware build details, version numbers,
 * and other identification data useful for debugging and compatibility
 * checking. The device must be initialized before calling this function.
 *
 * @param[in] pva            Pointer to @ref pva_kmd_device structure
 *                           Valid value: non-null
 * @param[out] version_buffer  Buffer to store the firmware version string
 *                             Valid value: non-null
 * @param[in] buffer_size    Size of the version buffer in bytes
 *                           Valid range: [1 .. UINT32_MAX]
 *
 * @retval PVA_SUCCESS              Version retrieved successfully
 * @retval PVA_INVAL                Invalid buffer pointer or buffer size <= 1
 * @retval PVA_NOMEM                Failed to allocate device memory for version query
 * @retval PVA_TIMEDOUT             Firmware did not respond within timeout
 * @retval PVA_ERR_FW_ABORTED       Firmware operation aborted during recovery
 */
enum pva_error pva_kmd_query_fw_version(struct pva_kmd_device *pva,
					char *version_buffer,
					uint32_t buffer_size);

/**
 * @brief Get the device class ID for the specified PVA device
 *
 * @details This function performs the following operations:
 * - Examines the device index to determine which PVA instance this represents
 * - Returns the appropriate class ID for device identification
 * - Maps device index 0 to @ref NV_PVA0_CLASS_ID
 * - Maps device index 1 to @ref NV_PVA1_CLASS_ID
 * - Provides consistent class ID mapping for hardware identification
 *
 * The class ID is used by various hardware and software components to
 * route operations to the correct PVA device instance in multi-device
 * systems.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null
 *
 * @retval NV_PVA0_CLASS_ID  If device index is 0
 * @retval NV_PVA1_CLASS_ID  If device index is 1 or any other value
 */
static inline uint32_t pva_kmd_get_device_class_id(struct pva_kmd_device *pva)
{
	if (pva->device_index == 0U) {
		return NV_PVA0_CLASS_ID;
	} else {
		return NV_PVA1_CLASS_ID;
	}
}

/**
 * @brief Get the maximum command buffer chunk size for the device
 *
 * @details This function performs the following operations:
 * - Checks if the device is configured for test mode operation
 * - Returns @ref PVA_TEST_MODE_MAX_CMDBUF_CHUNK_SIZE for test mode
 * - Returns @ref PVA_MAX_CMDBUF_CHUNK_SIZE for normal operation
 * - Provides appropriate chunk size limits based on device configuration
 *
 * The chunk size determines the maximum size of individual command buffer
 * segments that can be processed by the firmware. Test mode may use smaller
 * chunks for validation and debugging purposes.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null
 *
 * @retval PVA_TEST_MODE_MAX_CMDBUF_CHUNK_SIZE  If device is in test mode
 * @retval PVA_MAX_CMDBUF_CHUNK_SIZE            If device is in normal mode
 */
static inline uint16_t
pva_kmd_get_max_cmdbuf_chunk_size(struct pva_kmd_device *pva)
{
	/* MISRA C-2023 Rule 10.3: Explicit cast for narrowing conversion */
	uint16_t max_chunk_size = (uint16_t)PVA_MAX_CMDBUF_CHUNK_SIZE;
#if SYSTEM_TESTS_ENABLED == 1
	if (pva->test_mode) {
		max_chunk_size = (uint16_t)PVA_TEST_MODE_MAX_CMDBUF_CHUNK_SIZE;
	}
#endif
	return max_chunk_size;
}

/**
 * @brief Convert TSC (Time Stamp Counter) value to nanoseconds
 *
 * @details This function performs the following operations:
 * - Multiplies the TSC value by the device-specific conversion multiplier
 * - Uses @ref safe_mulu64() to prevent overflow during multiplication
 * - Returns the equivalent time value in nanoseconds
 * - Provides high-precision timing conversion for performance measurement
 *
 * The TSC is a hardware counter that increments at a fixed frequency.
 * The conversion multiplier is calibrated based on the device's clock
 * configuration to provide accurate nanosecond timing.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null
 * @param[in] tsc  TSC value to convert
 *                 Valid range: [0 .. UINT64_MAX]
 *
 * @retval converted_time  TSC value converted to nanoseconds
 */
static inline uint64_t pva_kmd_tsc_to_ns(struct pva_kmd_device *pva,
					 uint64_t tsc)
{
	// Convert TSC to nanoseconds using the multiplier
	return safe_mulu64(tsc, pva->tsc_to_ns_multiplier);
}

/**
 * @brief Convert TSC (Time Stamp Counter) value to microseconds
 *
 * @details This function performs the following operations:
 * - Converts the TSC value to nanoseconds using @ref safe_mulu64()
 * - Divides the nanosecond result by 1000 to get microseconds
 * - Returns the equivalent time value in microseconds
 * - Provides medium-precision timing conversion for performance measurement
 *
 * The TSC is a hardware counter that increments at a fixed frequency.
 * This function combines nanosecond conversion with division to provide
 * microsecond precision timing suitable for most timing measurements.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null
 * @param[in] tsc  TSC value to convert
 *                 Valid range: [0 .. UINT64_MAX]
 *
 * @retval converted_time  TSC value converted to microseconds
 */
static inline uint64_t pva_kmd_tsc_to_us(struct pva_kmd_device *pva,
					 uint64_t tsc)
{
	// Convert TSC to microseconds using the multiplier
	return safe_mulu64(tsc, pva->tsc_to_ns_multiplier) / 1000U;
}
#endif // PVA_KMD_DEVICE_H
