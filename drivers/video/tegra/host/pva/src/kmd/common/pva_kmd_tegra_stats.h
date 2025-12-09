/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_TEGRA_STATS_H
#define PVA_KMD_TEGRA_STATS_H
#include "pva_kmd_device.h"

#if PVA_ENABLE_TEGRASTATS == 1

/**
 * @brief Structure containing VPU utilization statistics for Tegra monitoring
 *
 * @details This structure holds performance statistics for VPU engines within
 * the PVA device, providing utilization metrics and timing information for
 * system monitoring and performance analysis. The statistics are collected
 * over defined time windows and provide insights into VPU workload distribution
 * and efficiency.
 */
struct pva_kmd_tegrastats {
	/**
	 * @brief Array of average VPU utilization percentages for each VPU engine
	 *
	 * @details Holds VPU utilization as a percentage for each VPU in the PVA cluster.
	 * Each entry represents the average utilization of a specific VPU engine
	 * during the measurement window.
	 * Valid range for each element: [0 .. 100] (percentage)
	 */
	uint64_t average_vpu_utilization[PVA_NUM_PVE];

	/**
	 * @brief Start timestamp of the current statistics measurement window
	 *
	 * @details Timestamp marking the beginning of the current measurement window
	 * for statistics collection. Used to calculate measurement duration and
	 * normalize utilization metrics.
	 * Valid range: [0 .. UINT64_MAX]
	 */
	uint64_t window_start_time;

	/**
	 * @brief End timestamp of the current statistics measurement window
	 *
	 * @details Timestamp marking the end of the current measurement window
	 * for statistics collection. Used in conjunction with window_start_time
	 * to determine the measurement period.
	 * Valid range: [window_start_time .. UINT64_MAX]
	 */
	uint64_t window_end_time;
};

/**
 * @brief Initialize Tegra statistics collection for PVA device
 *
 * @details This function performs the following operations:
 * - Initializes the Tegra statistics collection infrastructure
 * - Sets up performance monitoring counters for VPU engines
 * - Configures timing mechanisms for utilization measurement
 * - Prepares data structures for statistics accumulation
 * - Establishes baseline metrics for utilization calculation
 * - Enables performance monitoring in firmware if needed
 *
 * The statistics collection provides valuable information for system
 * monitoring tools like tegrastats to display PVA utilization metrics.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must be initialized
 */
void pva_kmd_device_init_tegra_stats(struct pva_kmd_device *pva);

/**
 * @brief Deinitialize Tegra statistics collection and clean up resources
 *
 * @details This function performs the following operations:
 * - Stops statistics collection and performance monitoring
 * - Disables performance monitoring counters
 * - Cleans up statistics data structures and allocated memory
 * - Frees resources associated with utilization tracking
 * - Ensures proper cleanup of monitoring infrastructure
 * - Resets statistics state for the device
 *
 * This function should be called during device shutdown to ensure
 * proper cleanup of all statistics-related resources.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must have been initialized with stats
 */
void pva_kmd_device_deinit_tegra_stats(struct pva_kmd_device *pva);

/* Initialize Tegrastats debugfs nodes */
enum pva_error pva_kmd_tegrastats_init_debugfs(struct pva_kmd_device *pva);

#else /* PVA_ENABLE_TEGRASTATS */

/* Dummy inline functions when Tegrastats is disabled */
static inline void pva_kmd_device_init_tegra_stats(struct pva_kmd_device *pva)
{
	(void)pva;
}

static inline void pva_kmd_device_deinit_tegra_stats(struct pva_kmd_device *pva)
{
	(void)pva;
}

/* Dummy initialization function when Tegrastats is disabled */
static inline enum pva_error
pva_kmd_tegrastats_init_debugfs(struct pva_kmd_device *pva)
{
	(void)pva;
	return PVA_SUCCESS;
}

#endif /* PVA_ENABLE_TEGRASTATS */

#endif
