/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_LINUX_DEVICE_H
#define PVA_KMD_LINUX_DEVICE_H

#include "pva_kmd_constants.h"
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>

#include "pva_api.h"
#include "pva_kmd_device.h"
#include "pva_kmd_linux_device_api.h"

#define PVA_KMD_LINUX_MAX_IORESOURCE_MEM 5

extern const struct file_operations tegra_pva_ctrl_ops;

/**
 * @brief Get unified device data from a common PVA KMD device
 *
 * @param device Pointer to common PVA KMD device
 * @return Pointer to unified nvpva_device_data structure
 */
static inline struct nvpva_device_data *
pva_kmd_linux_device_get_data(struct pva_kmd_device *device)
{
	return (struct nvpva_device_data *)device->plat_data;
}

/**
 * @brief Set unified device data for a common PVA KMD device
 *
 * @param device Pointer to common PVA KMD device
 * @param data Pointer to unified nvpva_device_data structure
 */
static inline void pva_kmd_linux_device_set_data(struct pva_kmd_device *device,
						 struct nvpva_device_data *data)
{
	device->plat_data = (void *)data;
}

/**
 * @brief Get unified device data from a platform device
 *
 * @param pdev Pointer to platform device
 * @return Pointer to unified nvpva_device_data structure
 */
static inline struct nvpva_device_data *
pva_kmd_linux_device_get_properties(struct platform_device *pdev)
{
	return platform_get_drvdata(pdev);
}

int pva_kmd_linux_host1x_init(struct pva_kmd_device *pva);
void pva_kmd_linux_host1x_deinit(struct pva_kmd_device *pva);

void pva_kmd_linux_device_smmu_contexts_init(struct pva_kmd_device *pva_device);

bool pva_kmd_linux_smmu_contexts_initialized(enum pva_chip_id chip_id);

enum pva_error kernel_err2pva_err(int err);

#endif // PVA_KMD_LINUX_DEVICE_H
