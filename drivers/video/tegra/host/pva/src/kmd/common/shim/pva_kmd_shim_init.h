/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SHIM_INIT_H
#define PVA_KMD_SHIM_INIT_H
#include "pva_api.h"
struct pva_kmd_device;
struct pva_kmd_file_ops;

/* TODO: remove plat_init APIs. We should just pass in plat_data directly to
 * pva_kmd_device_create. */
/**
 * @brief Platform-specific initialization for PVA KMD device.
 *
 * @details This function performs the following operations:
 * - Initializes platform-specific components of the PVA device
 * - Sets up platform-dependent data structures and resources
 * - Configures platform-specific settings for the PVA hardware
 * - Prepares the device for platform-specific operations
 *
 * @note This API is deprecated and will be removed. Platform data should
 * be passed directly to @ref pva_kmd_device_create() instead.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 */
void pva_kmd_device_plat_init(struct pva_kmd_device *pva);

/**
 * @brief Platform-specific deinitialization for PVA KMD device.
 *
 * @details This function performs the following operations:
 * - Cleans up platform-specific components of the PVA device
 * - Releases platform-dependent data structures and resources
 * - Reverses the initialization performed by @ref pva_kmd_device_plat_init()
 * - Ensures proper cleanup of platform-specific settings
 *
 * @note This API is deprecated and will be removed. Platform data should
 * be passed directly to @ref pva_kmd_device_create() instead.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null, previously initialized
 */
void pva_kmd_device_plat_deinit(struct pva_kmd_device *pva);

/**
 * @brief Read current synchronization point value.
 *
 * @details This function performs the following operations:
 * - Reads the current value of the specified synchronization point
 * - Provides access to hardware synchronization primitives
 * - Returns the current fence value for synchronization tracking
 * - Enables checking of synchronization point status
 *
 * Synchronization points are used for coordinating operations between
 * different hardware units and ensuring proper ordering of operations.
 *
 * @param[in] pva           Pointer to @ref pva_kmd_device structure
 *                          Valid value: non-null
 * @param[in] syncpt_id     Synchronization point identifier
 *                          Valid range: platform-specific
 * @param[out] syncpt_value Pointer to store the current syncpt value
 *                          Valid value: non-null
 */
void pva_kmd_read_syncpt_val(struct pva_kmd_device *pva, uint32_t syncpt_id,
			     uint32_t *syncpt_value);

/**
 * @brief Reset assert FW so it can be in recovery and user submission halted.
 *
 * @details This function performs the following operations:
 * - Places the PVA firmware into a reset state
 * - Halts all user command submissions to prevent new operations
 * - Prepares the device for recovery procedures
 * - Ensures the firmware is in a known stable state for debugging
 * - Disables firmware processing to prevent further state corruption
 *
 * This function is typically used in error recovery scenarios such as:
 * - Host1x watchdog timeout detection
 * - KMD submission timeout failures
 * - Fatal firmware errors requiring recovery
 *
 * After calling this function, the firmware must be reloaded and
 * reinitialized before normal operation can resume.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 */
void pva_kmd_freeze_fw(struct pva_kmd_device *pva);

/**
 * @brief Increase reference count on the PVA device.
 *
 * @details This function performs the following operations:
 * - Atomically increments the device reference counter
 * - Powers on the PVA device if it is currently powered off
 * - Ensures the device is ready for operation
 * - Manages power state transitions automatically
 * - Prevents the device from being powered down while in use
 *
 * This function implements reference counting for power management,
 * ensuring the device remains powered as long as there are active
 * users. Each call to this function must be balanced with a
 * corresponding call to @ref pva_kmd_device_idle().
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 *
 * @retval PVA_SUCCESS              Device reference acquired successfully
 *                                  and device is powered on
 * @retval PVA_INVAL                Device is in an invalid state for
 *                                  power-on operation
 * @retval PVA_TIMEDOUT             Power-on operation timed out
 * @retval PVA_INTERNAL             Hardware failure during power-on
 */
enum pva_error pva_kmd_device_busy(struct pva_kmd_device *pva);

/**
 * @brief Decrease reference count on the PVA device.
 *
 * @details This function performs the following operations:
 * - Atomically decrements the device reference counter
 * - Powers off the PVA device if reference count reaches zero
 * - Manages automatic power state transitions
 * - Ensures proper power management and energy efficiency
 * - Balances previous calls to @ref pva_kmd_device_busy()
 *
 * This function implements reference counting for power management.
 * When the reference count reaches zero, the device will be powered
 * down to save energy. The function is safe to call multiple times
 * but should balance calls to @ref pva_kmd_device_busy().
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 */
void pva_kmd_device_idle(struct pva_kmd_device *pva);

/**
 * @brief Check if PVA is already powered on.
 *
 * @details This function performs the following operations:
 * - Checks the current power state of the PVA device
 * - Provides a hint about device power status without side effects
 * - Does not modify the device power state or reference count
 * - Returns current power state information for optimization
 *
 * This function provides only a hint about the current power state.
 * The power state may change at any time due to other threads or
 * system events. Callers must still use @ref pva_kmd_device_busy()
 * to acquire a proper reference before attempting device communication.
 *
 * The function is useful for optimization decisions but should not
 * be relied upon for correctness of power management operations.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null
 *
 * @retval true   Device is likely powered on
 * @retval false  Device is likely powered off
 */
bool pva_kmd_device_maybe_on(struct pva_kmd_device *pva);

/**
 * @brief Load firmware.
 *
 * @details This function performs the following operations:
 * - Powers on the R5 processor core
 * - Loads the PVA firmware binary into R5 memory
 * - Configures firmware execution environment
 * - Binds interrupt handlers for firmware communication
 * - Waits for firmware boot sequence to complete successfully
 * - Establishes communication channels with the firmware
 * - Validates firmware initialization and readiness
 *
 * On silicon platforms, this involves hardware-specific operations
 * including memory mapping, interrupt setup, and processor control.
 * The function ensures the firmware is fully operational before
 * returning success.
 *
 * If any step fails, appropriate cleanup is performed to leave
 * the system in a consistent state.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 *
 * @retval PVA_SUCCESS              Firmware loaded and initialized
 *                                  successfully
 * @retval PVA_TIMEDOUT             Firmware boot timeout
 * @retval PVA_INTERNAL             Hardware failure during load
 * @retval PVA_INVAL                Device not ready for firmware load
 * @retval PVA_NOMEM                Insufficient memory for firmware
 */
enum pva_error pva_kmd_load_fw(struct pva_kmd_device *pva);

/**
 * @brief Unload firmware.
 *
 * @details This function performs the following operations:
 * - Stops firmware execution on the R5 processor
 * - Unbinds and frees interrupt handlers
 * - Powers off the R5 processor core
 * - Releases firmware memory allocations
 * - Cleans up communication channels
 * - Resets hardware state to pre-load conditions
 * - Ensures complete cleanup of firmware resources
 *
 * This function reverses all operations performed by @ref pva_kmd_load_fw()
 * and ensures the system is returned to a clean state. It is safe to
 * call this function even if firmware loading was not successful.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 */
void pva_kmd_unload_fw(struct pva_kmd_device *pva);

/**
 * @brief Disable all interrupts without waiting for running interrupt
 * handlers to complete.
 *
 * @details This function performs the following operations:
 * - Immediately disables all PVA interrupt sources at the hardware level
 * - Does not wait for currently executing interrupt handlers to finish
 * - Prevents new interrupts from being triggered by PVA hardware
 * - Provides emergency interrupt shutdown capability
 * - Protects the system from potential interrupt storms
 *
 * This function is designed for emergency situations where the PVA
 * has entered a bad state and may be generating excessive interrupts.
 * It can be safely called from within interrupt context since it
 * does not wait for other interrupt handlers to complete.
 *
 * Common use cases include:
 * - PVA hardware malfunction causing interrupt floods
 * - Watchdog timer repeatedly triggering
 * - Fatal firmware errors requiring immediate shutdown
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 */
void pva_kmd_disable_all_interrupts_nosync(struct pva_kmd_device *pva);

/**
 * @brief Report error to FSI.
 *
 * @details This function performs the following operations:
 * - Reports the specified error code to the Functional Safety Island (FSI)
 * - Enables safety-critical error reporting and monitoring
 * - Triggers appropriate safety response mechanisms
 * - Logs error information for safety compliance tracking
 * - Ensures proper error escalation to safety subsystems
 *
 * FSI (Functional Safety Island) is responsible for monitoring system
 * health and implementing safety responses when errors are detected.
 * This function enables the PVA KMD to participate in the overall
 * system safety architecture.
 *
 * @param[in, out] pva        Pointer to @ref pva_kmd_device structure
 *                            Valid value: non-null
 * @param[in] error_code      Error code to report to FSI
 *                            Valid range: platform-specific error codes
 */
/* Shim function with platform-specific implementations (QNX, Linux, Native) */
void pva_kmd_report_error_fsi(struct pva_kmd_device *pva, uint32_t error_code);

#endif // PVA_KMD_SHIM_INIT_H
