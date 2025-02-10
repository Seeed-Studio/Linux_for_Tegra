/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#ifndef PVA_KMD_SILICON_BOOT_H
#define PVA_KMD_SILICON_BOOT_H

#include "pva_kmd_device.h"

/**
 * @brief Configure EVP and Segment config registers
 *
 * This function configures the EVP and Segment config registers.
 *
 * @param pva Pointer to the PVA device.
 */
void pva_kmd_config_evp_seg_regs(struct pva_kmd_device *pva);

/**
 * @brief Configure SCR registers.
 *
 * This function configures the SCR registers.
 *
 * @param pva Pointer to the PVA device.
 */
void pva_kmd_config_scr_regs(struct pva_kmd_device *pva);

/**
 * @brief Configure SID registers.
 *
 * This function configures the SID registers.
 *
 * @param pva Pointer to the PVA device.
 */
void pva_kmd_config_sid(struct pva_kmd_device *pva);

#endif /* PVA_KMD_SILICON_BOOT_H */
