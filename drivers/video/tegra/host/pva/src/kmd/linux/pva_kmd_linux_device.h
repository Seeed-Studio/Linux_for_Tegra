// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>

#include "pva_api.h"
#include "pva_kmd_device.h"
#include "pva_kmd_linux_isr.h"

#define NV_PVA0_CLASS_ID 0xF1
#define PVA_KMD_LINUX_MAX_IORESOURCE_MEM 5

extern const struct file_operations tegra_pva_ctrl_ops;

struct pva_kmd_linux_device_data {
	/*
	 * Always keep nvhost_device_data at the top of this struct
	 * APIs access this data using platform_get_drvdata
	 */
	struct nvhost_device_data *pva_device_properties;

	/* Global states required by a PVA device instance go here */
	struct platform_device *smmu_contexts[PVA_MAX_NUM_SMMU_CONTEXTS];
	struct pva_kmd_isr_data isr[PVA_KMD_INTR_LINE_COUNT];
};

struct pva_kmd_linux_device_data *
pva_kmd_linux_device_get_data(struct pva_kmd_device *device);

void pva_kmd_linux_device_set_data(struct pva_kmd_device *device,
				   struct pva_kmd_linux_device_data *data);

void pva_kmd_linux_host1x_init(struct pva_kmd_device *pva);
void pva_kmd_linux_host1x_deinit(struct pva_kmd_device *pva);

struct nvhost_device_data *
pva_kmd_linux_device_get_properties(struct platform_device *pdev);

void pva_kmd_linux_device_smmu_contexts_init(struct pva_kmd_device *pva_device);

bool pva_kmd_linux_smmu_contexts_initialized(enum pva_chip_id chip_id);

enum pva_error kernel_err2pva_err(int err);
