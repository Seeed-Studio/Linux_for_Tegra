/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SILICON_ISR_H
#define PVA_KMD_SILICON_ISR_H
#include "pva_kmd_silicon_utils.h"
#include "pva_kmd_device.h"

/**
 * @brief Hypervisor interrupt service routine for PVA device
 *
 * @details This function handles hypervisor-level interrupts from the PVA
 * device. It processes interrupts that require hypervisor privileges and
 * performs necessary actions such as error handling, security validation,
 * and inter-VM communication. The function identifies the interrupt source
 * and dispatches appropriate handling routines based on the interrupt line.
 *
 * @param[in] data      Pointer to device-specific data context
 *                      Valid value: typically @ref pva_kmd_device pointer
 * @param[in] intr_line Interrupt line that triggered the handler
 *                      Valid values: @ref pva_kmd_intr_line enumeration values
 */
void pva_kmd_hyp_isr(void *data, enum pva_kmd_intr_line intr_line);

/**
 * @brief Main interrupt service routine for PVA device
 *
 * @details This function serves as the primary interrupt handler for PVA
 * device interrupts, particularly handling CCQ (Command and Control Queue)
 * interrupts and other device-level events. It processes hardware interrupts,
 * identifies the interrupt source, performs necessary acknowledgment, and
 * dispatches appropriate handling routines. The function ensures proper
 * interrupt handling and maintains device state consistency.
 *
 * @param[in] data      Pointer to device-specific data context
 *                      Valid value: typically @ref pva_kmd_device pointer
 * @param[in] intr_line Interrupt line that triggered the handler
 *                      Valid values: @ref pva_kmd_intr_line enumeration values
 */
void pva_kmd_isr(void *data, enum pva_kmd_intr_line intr_line);

#endif // PVA_KMD_SILICON_ISR_H
