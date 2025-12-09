/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_FW_PROFILER_H
#define PVA_KMD_FW_PROFILER_H
#include "pva_kmd_device.h"
#include "pva_kmd_shared_buffer.h"

/**
 * @brief Number of elements in firmware profiling buffer
 *
 * @details Number of elements allocated for the firmware profiling buffer.
 * This constant defines the total capacity of the profiling buffer used
 * to store firmware profiling events and tracepoints.
 * Value: 409,600 (4096 * 100)
 */
#define PVA_KMD_FW_PROFILING_BUF_NUM_ELEMENTS (4096 * 100)

/**
 * @brief Configuration structure for firmware profiling
 *
 * @details This structure contains the configuration parameters for firmware
 * profiling including filtering criteria, timestamp format, and enable state.
 * It controls how profiling data is collected and formatted by the firmware
 * for analysis and debugging purposes.
 */
struct pva_kmd_fw_profiling_config {
	/**
	 * @brief Filter mask for profiling events
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t filter;

	/**
	 * @brief Type of timestamp to use for profiling events
	 * Valid values: @ref pva_fw_timestamp_t enumeration values
	 */
	enum pva_fw_timestamp_t timestamp_type;

	/**
	 * @brief Size of timestamp data in bytes
	 * Valid range: [1 .. 16]
	 */
	uint8_t timestamp_size;

	/**
	 * @brief Enable flag for profiling
	 * Valid values: 0 (disabled), 1 (enabled)
	 */
	uint8_t enabled;
};

#if PVA_ENABLE_FW_PROFILING == 1
/**
 * @brief Initialize firmware profiler for a PVA device
 *
 * @details This function performs the following operations:
 * - Sets up profiling buffer infrastructure for firmware events
 * - Initializes shared buffer communication for profiling data
 * - Configures profiling event filtering and formatting parameters
 * - Prepares the profiler for collecting firmware execution data
 * - Establishes communication channels between firmware and KMD
 * - Allocates necessary memory resources for profiling operations
 *
 * The profiler enables collection of detailed firmware execution information
 * including timing data, event sequences, and performance metrics that are
 * essential for debugging and optimization.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null, must be initialized
 */
void pva_kmd_device_init_profiler(struct pva_kmd_device *pva);

/**
 * @brief Deinitialize firmware profiler and clean up resources
 *
 * @details This function performs the following operations:
 * - Disables firmware profiling if currently active
 * - Releases shared buffer resources used for profiling data
 * - Frees memory allocated for profiling buffer management
 * - Cleans up profiling event processing infrastructure
 * - Ensures proper cleanup of all profiler-related resources
 * - Invalidates profiler state for the device
 *
 * This function should be called during device shutdown to ensure
 * proper cleanup of all profiling resources and prevent memory leaks.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null, must have been initialized with profiler
 */
void pva_kmd_device_deinit_profiler(struct pva_kmd_device *pva);

/**
 * @brief Process firmware profiling event data
 *
 * @details This function performs the following operations:
 * - Validates incoming firmware event data format and size
 * - Parses event data to extract profiling information
 * - Processes different types of firmware events and tracepoints
 * - Updates profiling statistics and event counters
 * - Stores processed events in appropriate data structures
 * - Handles event filtering based on configuration settings
 * - Provides event data to profiling analysis tools
 *
 * This function is called when firmware sends profiling event data
 * through the shared buffer communication mechanism, enabling real-time
 * processing of firmware execution information.
 *
 * @param[in] pva        Pointer to @ref pva_kmd_device structure
 *                       Valid value: non-null, must be initialized
 * @param[in] data       Pointer to firmware event data buffer
 *                       Valid value: non-null
 * @param[in] data_size  Size of event data in bytes
 *                       Valid range: [1 .. UINT32_MAX]
 *
 * @retval PVA_SUCCESS              Event processed successfully
 * @retval PVA_INVAL                Invalid or corrupted event data
 * @retval PVA_ENOSPC               Profiling buffer is full
 * @retval PVA_BAD_PARAMETER_ERROR  Invalid parameters provided
 */
void pva_kmd_process_fw_event(struct pva_kmd_device *pva, uint8_t *data,
			      uint32_t data_size);

/**
 * @brief Notify firmware to enable profiling
 *
 * @details This function performs the following operations:
 * - Sends enable profiling command to firmware
 * - Configures firmware profiling parameters and filters
 * - Activates profiling data collection in firmware
 * - Sets up profiling buffer management in firmware
 * - Establishes profiling event reporting mechanisms
 * - Validates firmware acknowledgment of profiling enable
 *
 * After successful completion, firmware will begin collecting and
 * reporting profiling events according to the configured parameters.
 * The profiling data will be available through the shared buffer
 * communication interface.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must be initialized
 *
 * @retval PVA_SUCCESS              Profiling enabled successfully
 * @retval PVA_TIMEDOUT             Failed to communicate with firmware
 * @retval PVA_ERR_FW_ABORTED       Firmware did not respond within timeout
 * @retval PVA_INTERNAL             Firmware in invalid state for profiling
 */
enum pva_error pva_kmd_notify_fw_enable_profiling(struct pva_kmd_device *pva);

/**
 * @brief Notify firmware to disable profiling
 *
 * @details This function performs the following operations:
 * - Sends disable profiling command to firmware
 * - Stops firmware profiling data collection
 * - Flushes any remaining profiling events from firmware buffers
 * - Deactivates profiling event reporting mechanisms
 * - Cleans up firmware profiling state and resources
 * - Validates firmware acknowledgment of profiling disable
 *
 * After successful completion, firmware will stop collecting profiling
 * events and the profiling overhead will be eliminated. Any remaining
 * profiling data in buffers will be processed before disabling.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must be initialized
 *
 * @retval PVA_SUCCESS              Profiling disabled successfully
 * @retval PVA_TIMEDOUT             Failed to communicate with firmware
 * @retval PVA_ERR_FW_ABORTED       Firmware did not respond within timeout
 * @retval PVA_INTERNAL             Firmware in invalid state for profiling
 */
enum pva_error pva_kmd_notify_fw_disable_profiling(struct pva_kmd_device *pva);

#else
static inline void pva_kmd_device_init_profiler(struct pva_kmd_device *pva)
{
	(void)pva;
}

static inline void pva_kmd_device_deinit_profiler(struct pva_kmd_device *pva)
{
	(void)pva;
}

static inline void pva_kmd_process_fw_event(struct pva_kmd_device *pva,
					    uint8_t *data, uint32_t data_size)
{
	(void)pva;
	(void)data;
	(void)data_size;
}

static inline enum pva_error
pva_kmd_notify_fw_enable_profiling(struct pva_kmd_device *pva)
{
	(void)pva;
	return PVA_SUCCESS;
}

static inline enum pva_error
pva_kmd_notify_fw_disable_profiling(struct pva_kmd_device *pva)
{
	(void)pva;
	return PVA_SUCCESS;
}
#endif /* PVA_ENABLE_FW_PROFILING == 1 */

#if PVA_ENABLE_NSYS_PROFILING == 1
enum pva_error pva_kmd_notify_fw_set_profiling_level(struct pva_kmd_device *pva,
						     uint32_t level);
#else
static inline enum pva_error
pva_kmd_notify_fw_set_profiling_level(struct pva_kmd_device *pva,
				      uint32_t level)
{
	(void)pva;
	(void)level;
	return PVA_SUCCESS;
}
#endif /* PVA_ENABLE_NSYS_PROFILING == 1 */
#endif
