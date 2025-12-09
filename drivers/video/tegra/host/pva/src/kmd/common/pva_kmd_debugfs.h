/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_DEBUGFS_H
#define PVA_KMD_DEBUGFS_H
#include "pva_kmd.h"
#include "pva_kmd_shim_debugfs.h"
#include "pva_kmd_fw_profiler.h"

/**
 * @brief Number of VPU blocks supported for debugging
 *
 * @details Number of VPU blocks supported for debugging operations.
 * This constant defines the maximum number of VPU debugging interfaces
 * that can be created in the debug system.
 */
#define NUM_VPU_BLOCKS 2U

/**
 * @brief File operations structure for debug interfaces
 *
 * @details This structure defines the callback functions for debug file
 * operations including open, close, read, and write operations. It provides
 * a platform-agnostic interface for accessing debug information through
 * file-like operations, supporting both reading and writing of debug data.
 */
struct pva_kmd_file_ops {
	/**
	 * @brief Open callback for debug file
	 * Valid value: non-null function pointer or NULL if not supported
	 */
	int (*open)(struct pva_kmd_device *dev);

	/**
	 * @brief Release/close callback for debug file
	 * Valid value: non-null function pointer or NULL if not supported
	 */
	int (*release)(struct pva_kmd_device *dev);

	/**
	 * @brief Read callback for debug file
	 * Valid value: non-null function pointer or NULL if not supported
	 */
	int64_t (*read)(struct pva_kmd_device *dev, void *file_data,
			uint8_t *data, uint64_t offset, uint64_t size);

	/**
	 * @brief Write callback for debug file
	 * Valid value: non-null function pointer or NULL if not supported
	 */
	int64_t (*write)(struct pva_kmd_device *dev, void *file_data,
			 const uint8_t *data, uint64_t offset, uint64_t size);

	/**
	 * @brief Platform-specific device pointer
	 */
	void *pdev;

	/**
	 * @brief File-specific data pointer
	 */
	void *file_data;
};

/**
 * @brief Debug context for PVA device debugging
 *
 * @details This structure maintains the complete debug context
 * for a PVA device, including various debug interfaces, configuration
 * settings, and file operation handlers. It provides comprehensive debugging
 * capabilities including VPU debugging, profiling, allowlist management,
 * hardware performance monitoring, and firmware tracing.
 */
struct pva_kmd_debugfs_context {
	/**
	 * @brief Enable flag for statistics collection
	 * Valid values: true (enabled), false (disabled)
	 */
	bool stats_enable;

	/**
	 * @brief Enable flag for VPU debugging
	 * Valid values: true (enabled), false (disabled)
	 */
	bool vpu_debug;

	/**
	 * @brief Enable flag for VPU print output
	 * Valid values: true (enabled), false (disabled)
	 */
	bool vpu_print_enable;

	/**
	 * @brief Flag indicating if system entered SC7 state
	 * Valid values: true (entered), false (not entered)
	 */
	bool entered_sc7;

	/**
	 * @brief Path to allowlist configuration file
	 * Valid value: null-terminated string or NULL
	 */
	char *allowlist_path;

	/**
	 * @brief Current profiling level setting
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t profiling_level;

	/**
	 * @brief File operations for VPU debugging interface
	 */
	struct pva_kmd_file_ops vpu_fops;

	/**
	 * @brief File operations for allowlist enable interface
	 */
	struct pva_kmd_file_ops allowlist_ena_fops;

	/**
	 * @brief File operations for allowlist path interface
	 */
	struct pva_kmd_file_ops allowlist_path_fops;

	/**
	 * @brief File operations for hardware performance monitoring interface
	 */
	struct pva_kmd_file_ops hwpm_fops;

	/**
	 * @brief File operations for profiling level interface
	 */
	struct pva_kmd_file_ops profiling_level_fops;

	/**
	 * @brief Data pointer for hardware performance monitoring
	 */
	void *data_hwpm;

	/**
	 * @brief Array of file operations for VPU on-chip debugging interfaces
	 */
	struct pva_kmd_file_ops vpu_ocd_fops[NUM_VPU_BLOCKS];

	/**
	 * @brief Global firmware profiling configuration
	 */
	struct pva_kmd_fw_profiling_config g_fw_profiling_config;

	/**
	 * @brief File operations for firmware trace level interface
	 */
	struct pva_kmd_file_ops fw_trace_level_fops;

	/**
	 * @brief File operations for SC7 simulation interface
	 */
	struct pva_kmd_file_ops simulate_sc7_fops;

	/**
	 * @brief File operations for R5 on-chip debugging interface
	 */
	struct pva_kmd_file_ops r5_ocd_fops;

	/**
	 * @brief Buffer for R5 OCD staging operations
	 */
	void *r5_ocd_stage_buffer;
};

/**
 * @brief Create debug nodes for PVA device debugging
 *
 * @details This function performs the following operations:
 * - Creates debug nodes for various PVA debugging interfaces
 * - Sets up file operation callbacks for each debug interface
 * - Initializes VPU debugging interfaces for all VPU blocks
 * - Creates hardware performance monitoring debug nodes
 * - Sets up firmware profiling and tracing interfaces
 * - Configures allowlist management debug interfaces
 * - Establishes R5 and VPU on-chip debugging capabilities
 * - Provides comprehensive debugging infrastructure for the PVA device
 *
 * The created debug nodes enable access to various PVA debugging features
 * through the debug interface, supporting development, debugging,
 * and performance analysis workflows.
 *
 * @param[in] dev  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must be initialized
 *
 * @retval PVA_SUCCESS              Debug nodes created successfully
 * @retval PVA_NOMEM                Failed to allocate memory for debug infrastructure
 * @retval PVA_INVAL                Invalid parameters for debug node creation
 * @retval PVA_INTERNAL             Platform-specific debug creation failed
 */
enum pva_error pva_kmd_debugfs_create_nodes(struct pva_kmd_device *dev);

/**
 * @brief Destroy debug nodes and clean up resources
 *
 * @details This function performs the following operations:
 * - Removes all debug nodes created for the PVA device
 * - Cleans up file operation callbacks and associated data structures
 * - Frees memory allocated for debug interfaces and buffers
 * - Destroys VPU and R5 on-chip debugging interfaces
 * - Cleans up hardware performance monitoring debug nodes
 * - Removes firmware profiling and tracing interfaces
 * - Ensures proper cleanup of all debugging infrastructure
 *
 * This function should be called during device cleanup to ensure
 * proper removal of all debug interfaces and prevent resource leaks.
 *
 * @param[in] dev  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must have been initialized with debug nodes
 */
void pva_kmd_debugfs_destroy_nodes(struct pva_kmd_device *dev);

/**
 * @brief Copy data from buffer to user space with bounds checking
 *
 * @details This function performs the following operations:
 * - Validates the requested read parameters against buffer bounds
 * - Calculates the actual amount of data available for reading
 * - Handles offset positioning within the source buffer
 * - Copies data from the source buffer to the destination
 * - Ensures no buffer overruns or invalid memory access
 * - Returns the actual number of bytes copied to user space
 * - Validates result fits in int64_t for debugfs interface compatibility
 *
 * This utility function provides safe buffer-to-user copying for debug
 * file read operations, preventing buffer overruns and ensuring proper
 * bounds checking for all debug interface data transfers.
 *
 * @param[out] to         Destination buffer for copied data
 *                        Valid value: non-null, min size count bytes
 * @param[in] count       Maximum number of bytes to copy
 *                        Valid range: [0 .. UINT64_MAX]
 * @param[in] offset      Offset within source buffer to start reading
 *                        Valid range: [0 .. UINT64_MAX]
 * @param[in] from        Source buffer containing data to copy
 *                        Valid value: non-null
 * @param[in] available   Total size of available data in source buffer
 *                        Valid range: [0 .. UINT64_MAX]
 *
 * @retval bytes_copied  Number of bytes actually copied to destination
 *                       Range: [0 .. min(count, available-offset, INT64_MAX)]
 */
int64_t pva_kmd_read_from_buffer_to_user(void *to, uint64_t count,
					 uint64_t offset, const void *from,
					 uint64_t available);

#endif //PVA_KMD_DEBUGFS_H
