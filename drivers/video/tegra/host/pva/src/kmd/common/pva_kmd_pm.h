/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_PM_H
#define PVA_KMD_PM_H

struct pva_kmd_device;

/**
 * @brief Prepare PVA device for system suspend operation
 *
 * @details This function performs the following operations:
 * - Ensures all pending commands are completed or properly handled
 * - Saves critical device state information for resume
 * - Gracefully halts firmware execution in preparation for suspend
 * - Configures hardware for low-power suspend state
 * - Validates that device is ready for power state transition
 * - Prepares synchronization mechanisms for suspend/resume cycle
 * - Ensures proper resource cleanup before suspend
 *
 * This function is called as part of the system suspend sequence to ensure
 * the PVA device can safely enter a suspended state without losing critical
 * information or leaving the hardware in an inconsistent state.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must be initialized
 *
 * @retval PVA_SUCCESS              Device prepared for suspend successfully
 * @retval PVA_AGAIN                Device has pending operations preventing suspend
 * @retval PVA_INTERNAL             Firmware in invalid state for suspend
 * @retval PVA_TIMEDOUT             Timeout waiting for operations to complete
 * @retval PVA_ERR_FW_ABORTED       Power management operation failed
 */
enum pva_error pva_kmd_prepare_suspend(struct pva_kmd_device *pva);

/**
 * @brief Complete PVA device resume operation after system wake
 *
 * @details This function performs the following operations:
 * - Restores hardware configuration and register state
 * - Reinitializes firmware and brings it to operational state
 * - Restores saved device context and operational parameters
 * - Re-establishes communication channels with firmware
 * - Validates device functionality after resume
 * - Restores resource tables and memory mappings
 * - Ensures device is fully operational for command processing
 *
 * This function is called as part of the system resume sequence to restore
 * the PVA device to full operational state after waking from suspend.
 * It ensures that all device functionality is restored and available.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must be initialized
 *
 * @retval PVA_SUCCESS              Device resumed successfully
 * @retval PVA_ERR_FW_ABORTED       Failed to boot or initialize firmware
 * @retval PVA_INTERNAL             Failed to initialize hardware after resume
 * @retval PVA_TIMEDOUT             Failed to establish firmware communication
 * @retval PVA_UNKNOWN_ERROR        Power management operation failed
 * @retval PVA_INVAL                Device in invalid state after resume
 */
enum pva_error pva_kmd_complete_resume(struct pva_kmd_device *pva);

#endif