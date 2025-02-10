/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#ifndef PVA_API_VPU_H
#define PVA_API_VPU_H
#include "pva_api_types.h"

/**
 * @brief Information of the VPU instance data passed to VPU kernel.
 */
struct pva_vpu_instance_data {
	/** @brief ID of the VPU assigned to the task */
	uint16_t engine_id;
	/** @brief Variable to indicate that ppe task was launched or not */
	uint16_t ppe_task_launched;
	/** @brief Base of the VMEM memory */
	uint32_t vmem_base;
	/** @brief Base of the DMA descriptor SRAM memory */
	uint32_t dma_descriptor_base;
	/** @brief Base of L2SRAM allocated for the task executed */
	uint32_t l2ram_base;
	/** @brief Size of L2SRAM allocated for the task executed */
	uint32_t l2ram_size;
};

#endif // PVA_API_VPU_H
