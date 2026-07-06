/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SILICON_BOOT_H
#define PVA_KMD_SILICON_BOOT_H

#include "pva_kmd_device.h"

/**
 * @brief Configure EVP and Segment config registers
 *
 * @details This function configures the Exception Vector Prefix (EVP) and
 * Segment configuration registers for the PVA device. The EVP registers
 * control the location of exception vectors for the embedded processor,
 * while segment configuration registers define memory mapping and access
 * permissions. This configuration is essential for proper firmware loading
 * and execution on the PVA hardware.
 *
 * @param[in] pva Pointer to @ref pva_kmd_device structure
 *                Valid value: non-null, must be initialized
 */
void pva_kmd_config_evp_seg_regs(struct pva_kmd_device *pva);

/**
 * @brief Configure SCR (System Control Register) registers
 *
 * @details This function configures the System Control Registers (SCR) for
 * the PVA device. SCR registers control various system-level settings including
 * cache policies, memory protection attributes, and processor modes. Proper
 * SCR configuration is critical for secure and efficient operation of the
 * embedded processor within the PVA hardware.
 *
 * @param[in] pva Pointer to @ref pva_kmd_device structure
 *                Valid value: non-null, must be initialized
 */
void pva_kmd_config_scr_regs(struct pva_kmd_device *pva);

/**
 * @brief Configure SID (Stream ID) registers
 *
 * @details This function configures the Stream ID (SID) registers for the PVA
 * device. SID registers define the stream identifiers used by the SMMU (System
 * Memory Management Unit) for memory access control and virtualization. Proper
 * SID configuration ensures that PVA memory accesses are correctly identified
 * and routed through the appropriate SMMU translation contexts for security
 * and isolation.
 *
 * @param[in] pva Pointer to @ref pva_kmd_device structure
 *                Valid value: non-null, must be initialized
 */
void pva_kmd_config_sid(struct pva_kmd_device *pva);

#endif /* PVA_KMD_SILICON_BOOT_H */
