/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_CONSTANTS_H
#define PVA_KMD_CONSTANTS_H

#include "pva_kmd_limits.h"
#include "pva_constants.h"

/**
 * @brief Maximum number of resources that can be managed by KMD internally
 *
 * @details This constant defines the upper limit for the number of resources
 * that the KMD can manage for its own internal operations. These resources
 * include memory buffers, DMA configurations, and other hardware resources
 * needed for KMD's privileged operations and communication with firmware.
 */
#define PVA_KMD_MAX_NUM_KMD_RESOURCES 32

/**
 * @brief Maximum number of DMA configurations for KMD internal use
 *
 * @details This constant defines the maximum number of DMA configuration
 * objects that the KMD can allocate and manage for its own internal DMA
 * operations. These configurations are used for transferring data between
 * system memory and PVA internal memory during KMD operations.
 */
#define PVA_KMD_MAX_NUM_KMD_DMA_CONFIGS 1

/**
 * @brief Maximum number of command buffer chunks for KMD internal submissions
 *
 * @details This constant defines the upper limit for the number of command
 * buffer chunks that the KMD can use for its own internal command submissions.
 * These chunks are used to build command buffers for privileged operations
 * such as resource management and device configuration.
 */
#define PVA_KMD_MAX_NUM_KMD_CHUNKS 32

/**
 * @brief Maximum number of submissions that KMD can queue internally
 *
 * @details This constant defines the maximum number of command buffer
 * submissions that the KMD can have queued for its own internal operations.
 * This limits the depth of the internal submission queue to prevent
 * unbounded resource usage during high-throughput scenarios.
 */
#define PVA_KMD_MAX_NUM_KMD_SUBMITS 32

/**
 * @brief Maximum number of command buffer chunks for user privileged submissions
 *
 * @details This constant defines the upper limit for the number of command
 * buffer chunks that can be used for user-initiated privileged submissions.
 * Privileged submissions are those that require elevated permissions and
 * are processed through the KMD's privileged command path.
 */
#define PVA_KMD_MAX_NUM_PRIV_CHUNKS 256

/**
 * @brief Maximum number of privileged submissions that can be queued
 *
 * @details This constant defines the maximum number of privileged command
 * buffer submissions that can be queued at any given time. This limit
 * helps manage memory usage and ensures system stability under heavy
 * privileged operation loads.
 */
#define PVA_KMD_MAX_NUM_PRIV_SUBMITS 256

/**
 * @brief Base context ID for user contexts
 *
 * @details This constant defines the starting context ID value for user
 * contexts. Context ID 0 is typically reserved for privileged/system
 * operations, so user contexts start from this base value. This ensures
 * proper separation between system and user context namespaces.
 */
#define PVA_KMD_USER_CONTEXT_ID_BASE 1u

/**
 * @brief Physical register base address for PVA0 on T23x silicon
 *
 * @details This constant defines the physical memory address where the
 * PVA0 device registers are mapped on T23x (Tegra23x) silicon platforms.
 * This address is used for register access and memory mapping operations
 * specific to the first PVA instance on T23x hardware.
 */
#define PVA_KMD_PVA0_T23x_REG_BASE 0x16000000

/**
 * @brief Size of PVA0 register space on T23x silicon
 *
 * @details This constant defines the total size in bytes of the PVA0
 * register address space on T23x (Tegra23x) silicon platforms. This
 * size covers all register apertures and memory-mapped regions needed
 * for complete PVA0 device control and operation.
 */
#define PVA_KMD_PVA0_T23x_REG_SIZE 0x800000

/**
 * @brief Infinite timeout value for operations that should never timeout
 *
 * @details This constant represents an infinite timeout value, typically
 * used for operations that must complete regardless of how long they take.
 * Set to the maximum value of a 64-bit unsigned integer to effectively
 * disable timeout checking for critical operations.
 */
#define PVA_KMD_TIMEOUT_INF U64_MAX

// clang-format off
#if PVA_BUILD_MODE == PVA_BUILD_MODE_SIM
    /**
     * @brief Timeout scaling factor for simulation builds
     *
     * @details This constant provides a scaling factor applied to timeout
     * values when running in simulation mode. Simulation environments
     * typically run slower than real hardware, so timeouts are scaled up
     * by this factor to prevent spurious timeout failures during testing.
     */
    #define PVA_KMD_TIMEOUT_FACTOR 100
#elif (PVA_BUILD_MODE == PVA_BUILD_MODE_NATIVE)
    /**
     * @brief Timeout scaling factor for native builds
     *
     * @details This constant provides a scaling factor applied to timeout
     * values when running in native build mode. On native builds, the FW calls
     * the KMD's shared buffer handler in its own thread. In debug builds, if
     * there are a large number of messages (prints, unregister, etc.), this
     * handler might take a while to execute, making the FW delay the processing
     * of command buffers. This could lead to submission timeouts in KMD.
     */
    #define PVA_KMD_TIMEOUT_FACTOR 10
#else
    /**
     * @brief Default timeout scaling factor for silicon builds
     *
     * @details This constant provides the default scaling factor applied to
     * timeout values when running on actual silicon hardware. No scaling
     * is applied (factor of 1) as silicon hardware runs at expected speeds
     * and timeout values are calibrated for real hardware performance.
     */
    #define PVA_KMD_TIMEOUT_FACTOR 1
#endif
// clang-format on

/**
 * @brief Macro to apply platform-specific timeout scaling
 *
 * @details This macro applies the appropriate timeout scaling factor based
 * on the build mode. It multiplies the provided timeout value by the
 * platform-specific timeout factor to account for different execution
 * speeds in simulation, native, and silicon environments.
 *
 * @param val Base timeout value before scaling
 */
#define PVA_KMD_TIMEOUT(val) ((val)*PVA_KMD_TIMEOUT_FACTOR)

/**
 * @brief Timeout for resource semaphore operations in milliseconds
 *
 * @details This constant defines the timeout value for acquiring resource
 * semaphores in the KMD. Resource semaphores are used to coordinate access
 * to shared resources between different contexts and operations. The timeout
 * is scaled based on the build mode to account for platform differences.
 */
#define PVA_KMD_TIMEOUT_RESOURCE_SEMA_MS PVA_KMD_TIMEOUT(400)

/**
 * @brief Timeout for waiting on firmware responses in microseconds
 *
 * @details This constant defines the timeout value for operations that
 * wait for firmware responses or completion signals. This timeout ensures
 * that the KMD doesn't wait indefinitely for firmware operations that
 * may have failed or stalled. The timeout is scaled for platform differences.
 */
#define PVA_KMD_WAIT_FW_TIMEOUT_US PVA_KMD_TIMEOUT(100000)

/**
 * @brief Additional scaling factor for firmware timeouts in simulation
 *
 * @details This constant provides an additional scaling factor specifically
 * for firmware timeout operations when running in simulation mode. This
 * extra scaling accounts for the significantly slower execution speed
 * of firmware operations in simulation environments.
 */
#define PVA_KMD_WAIT_FW_TIMEOUT_SCALER_SIM 100

/**
 * @brief Polling interval for firmware status checks in microseconds
 *
 * @details This constant defines the interval between polling operations
 * when waiting for firmware status changes or responses. The polling
 * interval balances responsiveness with CPU usage - too frequent polling
 * wastes CPU cycles, while too infrequent polling increases response latency.
 */
#define PVA_KMD_WAIT_FW_POLL_INTERVAL_US PVA_KMD_TIMEOUT(100)

/**
 * @brief Timeout for firmware boot completion in milliseconds
 *
 * @details This constant defines the maximum time to wait for firmware
 * boot completion during device initialization. If the firmware doesn't
 * complete its boot sequence within this timeout, the initialization
 * is considered failed. The timeout is scaled for platform differences.
 */
#define PVA_KMD_FW_BOOT_TIMEOUT_MS PVA_KMD_TIMEOUT(1000)

/**
 * @brief Total number of read-write syncpoints available across all contexts
 *
 * @details This constant calculates the total number of read-write syncpoints
 * available in the system by multiplying the maximum number of CCQs by the
 * number of read-write syncpoints per context. This provides the total pool
 * of syncpoints that can be allocated for user operations requiring both
 * read and write access to syncpoint values.
 */
#define PVA_NUM_RW_SYNCPTS (PVA_MAX_NUM_CCQ * PVA_NUM_RW_SYNCPTS_PER_CONTEXT)

// clang-format off
#if PVA_DEV_MAIN_COMPATIBLE == 1
    /**
     * @brief Default setting for loading firmware from GSC in main-compatible builds
     *
     * @details This constant defines the default behavior for firmware loading
     * from GSC (Generic Security Controller) in builds that are compatible with
     * the main development branch. When true, firmware will be loaded through
     * the GSC secure path by default, providing enhanced security for production
     * environments.
     */
    #define PVA_KMD_LOAD_FROM_GSC_DEFAULT true
#else
    /**
     * @brief Default setting for loading firmware from GSC in non-main-compatible builds
     *
     * @details This constant defines the default behavior for firmware loading
     * from GSC (Generic Security Controller) in development or testing builds
     * that are not main-compatible. When false, firmware will be loaded through
     * conventional paths, which may be more suitable for development and debugging.
     */
    #define PVA_KMD_LOAD_FROM_GSC_DEFAULT false
#endif
// clang-format on

/**
 * @brief Increment size for DMA configuration pool expansion
 *
 * @details This constant defines the number of DMA configuration entries
 * to allocate when expanding the DMA configuration pool. When the pool
 * becomes full and needs to grow, it will be expanded by this many entries
 * to provide room for additional DMA configurations while minimizing
 * the frequency of pool expansion operations.
 */
#define PVA_KMD_DMA_CONFIG_POOL_INCR 256

#endif // PVA_KMD_CONSTANTS_H
