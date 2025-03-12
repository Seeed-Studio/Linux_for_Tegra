/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_T23X_H
#define PVA_KMD_T23X_H
#include "pva_kmd_device.h"

/** Number of VMEM regions */
#define PVA_VMEM_REGION_COUNT_T23X 3U

/** Start Address of VMEM0 Bank in T23X */
#define T23x_VMEM0_START 0x40U
/** End Address of VMEM0 Bank in T23X */
#define T23x_VMEM0_END 0x20000U
/** Start Address of VMEM1 Bank in T23X */
#define T23x_VMEM1_START 0x40000U
/** End Address of VMEM1 Bank in T23X */
#define T23x_VMEM1_END 0x60000U
/** Start Address of VMEM2 Bank in T23X */
#define T23x_VMEM2_START 0x80000U
/** End Address of VMEM2 Bank in T23X */
#define T23x_VMEM2_END 0xA0000U

/** @brief Base address for PVA0 VPU Debug Register space (CSITE_PVA0VPU) */
#define TEGRA_PVA0_VPU_DBG_BASE 0x24740000U
/** @brief Size (in bytes) of the PVA0 VPU Debug Register space (CSITE_PVA0VPU) */
#define TEGRA_PVA0_VPU_DBG_SIZE 0x40000U

void pva_kmd_device_init_t23x(struct pva_kmd_device *pva);

#endif // PVA_KMD_T23X_H
