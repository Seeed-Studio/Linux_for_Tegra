/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_T23X_H
#define PVA_KMD_T23X_H
#include "pva_kmd_device.h"

/**
 * @brief Number of VMEM regions available in T23X platform
 *
 * @details Defines the total number of Vector Memory (VMEM) regions available
 * in the T23X Tegra platform. VMEM regions are fast on-chip memory banks used
 * by VPU for data processing operations.
 */
#define PVA_VMEM_REGION_COUNT_T23X 3U

/**
 * @brief Start address of VMEM0 bank in T23X platform
 *
 * @details Base address of the first Vector Memory bank (VMEM0) in T23X Tegra
 * platform. This region provides fast access memory for VPU operations.
 * Value: 0x40 (64 bytes offset)
 */
#define T23x_VMEM0_START 0x40U

/**
 * @brief End address of VMEM0 bank in T23X platform
 *
 * @details End address (exclusive) of the first Vector Memory bank (VMEM0) in
 * T23X Tegra platform. The usable range is from T23x_VMEM0_START to
 * (T23x_VMEM0_END - 1).
 * Value: 0x20000 (128KB)
 */
#define T23x_VMEM0_END 0x20000U

/**
 * @brief Start address of VMEM1 bank in T23X platform
 *
 * @details Base address of the second Vector Memory bank (VMEM1) in T23X Tegra
 * platform. This region provides additional fast access memory for VPU
 * operations.
 * Value: 0x40000 (256KB offset)
 */
#define T23x_VMEM1_START 0x40000U

/**
 * @brief End address of VMEM1 bank in T23X platform
 *
 * @details End address (exclusive) of the second Vector Memory bank (VMEM1) in
 * T23X Tegra platform. The usable range is from T23x_VMEM1_START to
 * (T23x_VMEM1_END - 1).
 * Value: 0x60000 (384KB)
 */
#define T23x_VMEM1_END 0x60000U

/**
 * @brief Start address of VMEM2 bank in T23X platform
 *
 * @details Base address of the third Vector Memory bank (VMEM2) in T23X Tegra
 * platform. This region provides additional fast access memory for VPU
 * operations.
 * Value: 0x80000 (512KB offset)
 */
#define T23x_VMEM2_START 0x80000U

/**
 * @brief End address of VMEM2 bank in T23X platform
 *
 * @details End address (exclusive) of the third Vector Memory bank (VMEM2) in
 * T23X Tegra platform. The usable range is from T23x_VMEM2_START to
 * (T23x_VMEM2_END - 1).
 * Value: 0xA0000 (640KB)
 */
#define T23x_VMEM2_END 0xA0000U

/**
 * @brief Base address for PVA0 VPU Debug Register space (CSITE_PVA0VPU)
 *
 * @details Physical base address of the PVA0 VPU debug register space in T23X
 * platform. This address space provides access to VPU debug and monitoring
 * registers through the CoreSight interface.
 * Value: 0x24740000
 */
#define TEGRA_PVA0_VPU_DBG_BASE_T23X 0x24740000U

/**
 * @brief Size (in bytes) of the PVA0 VPU Debug Register space (CSITE_PVA0VPU)
 *
 * @details Size of the PVA0 VPU debug register space in T23X platform. This
 * defines the extent of the debug register address space accessible through
 * the CoreSight interface.
 * Value: 0x40000 (256KB)
 */
#define TEGRA_PVA0_VPU_DBG_SIZE_T23X 0x40000U

/**
 * @brief Initialize PVA device for T23X platform
 *
 * @details This function performs T23X-specific initialization of the PVA
 * device. It configures platform-specific parameters, sets up memory regions,
 * initializes hardware-specific features, and prepares the device for operation
 * on the T23X Tegra platform. This includes setting up VMEM regions, debug
 * interfaces, and other platform-specific configurations.
 *
 * @param[in,out] pva Pointer to @ref pva_kmd_device structure to initialize
 *                    Valid value: non-null, must be pre-allocated
 */
void pva_kmd_device_init_t23x(struct pva_kmd_device *pva);

#endif // PVA_KMD_T23X_H
