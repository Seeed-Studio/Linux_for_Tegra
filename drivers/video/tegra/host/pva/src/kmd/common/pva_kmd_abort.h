/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_ABORT_H
#define PVA_KMD_ABORT_H
#include "pva_kmd_device.h"
#include "pva_kmd_utils.h"

/**
 * @brief Abort firmware execution and initiate error recovery
 *
 * @details This function performs the following operations:
 * - Immediately signals the firmware to abort current operations
 * - Logs the provided error code using @ref pva_kmd_log_err_hex32()
 * - Initiates emergency shutdown procedures for the PVA device
 * - Marks the device as being in recovery mode for subsequent operations
 * - Notifies platform-specific error handling mechanisms
 * - Prepares the device for potential firmware restart or reset
 * - Ensures all pending operations are cancelled safely
 *
 * This function is used in critical error scenarios where the firmware
 * has encountered an unrecoverable error or the system has detected a
 * condition that requires immediate firmware termination. The error code
 * is preserved for debugging and diagnostic purposes.
 *
 * @param[in, out] pva        Pointer to @ref pva_kmd_device structure
 *                            Valid value: non-null
 * @param[in] error_code      Error code indicating the reason for abort
 *                            Valid range: [0 .. UINT32_MAX]
 */
void pva_kmd_abort_fw(struct pva_kmd_device *pva, uint32_t error_code);

#endif //PVA_KMD_ABORT_H
