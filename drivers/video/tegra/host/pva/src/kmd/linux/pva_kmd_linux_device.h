/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_LINUX_DEVICE_H
#define PVA_KMD_LINUX_DEVICE_H

#include "pva_kmd_constants.h"
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/scatterlist.h>

#include "pva_api.h"
#include "pva_kmd_device.h"
#include "pva_kmd_linux_isr.h"

#define PVA_KMD_LINUX_MAX_IORESOURCE_MEM 5

extern const struct file_operations tegra_pva_ctrl_ops;

struct pva_kmd_linux_device_data {
	/*
	 * Always keep nvpva_device_data at the top of this struct
	 * APIs access this data using platform_get_drvdata
	 */
	struct nvpva_device_data *pva_device_properties;

	/* Global states required by a PVA device instance go here */
	struct platform_device *smmu_contexts[PVA_MAX_NUM_SMMU_CONTEXTS];
	struct pva_kmd_isr_data isr[PVA_KMD_INTR_LINE_COUNT];

	struct scatterlist syncpt_sg[PVA_NUM_RW_SYNCPTS];
};

struct pva_kmd_linux_device_data *
pva_kmd_linux_device_get_data(struct pva_kmd_device *device);

void pva_kmd_linux_device_set_data(struct pva_kmd_device *device,
				   struct pva_kmd_linux_device_data *data);

int pva_kmd_linux_host1x_init(struct pva_kmd_device *pva);
void pva_kmd_linux_host1x_deinit(struct pva_kmd_device *pva);

struct nvpva_device_data *
pva_kmd_linux_device_get_properties(struct platform_device *pdev);

void pva_kmd_linux_device_smmu_contexts_init(struct pva_kmd_device *pva_device);

bool pva_kmd_linux_smmu_contexts_initialized(enum pva_chip_id chip_id);

enum pva_error kernel_err2pva_err(int err);

#endif // PVA_KMD_LINUX_DEVICE_H
