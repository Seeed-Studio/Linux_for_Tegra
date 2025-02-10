/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2023 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

/*
 * Unit: Task Unit
 * SWUD Document:
 * p4sw-swarm.nvidia.com/view/sw/embedded/docs/projects/active/DRIVE_6.0/QNX/PLC_Work_Products/Element_WPs/Autonomous_Middleware/PVA/04_Unit_Design/PVA_FW/SWE-PVAFW-006-SWUD.pdf
 */
/**
 * @file pva-sys-params.h
 *
 * @brief Types and constants related to VPU application parameters.
 */

#ifndef PVA_SYS_PARAMS_H
#define PVA_SYS_PARAMS_H

#include <stdint.h>
#include <pva-packed.h>
#include <pva-types.h>

/** @brief VPU app parameters provided by kernel-user which is to be copied to
 * VMEM during runtime
 *
 * The VPU App parameters contains kernel-user-provided data to be
 * copied into the VMEM before executing the VPU app. The parameter
 * headers are stored in the IOVA address stored in the param_base
 * member of this structure.
 *
 * The FW can also initialize complex datatypes, which are marked by
 * special param_base outside the normal IOVA space. See the structure
 * pva_vpu_instance_data_t for an example.
 */
typedef struct PVA_PACKED {
	/** @brief IOVA address of the parameter data */
	pva_iova param_base;
	/** @brief VMEM offset where parameter data is to be copied */
	uint32_t addr;
	/** @brief Size of the parameter data in bytes */
	uint32_t size;
} pva_vpu_parameter_list_t;

/**
 * @brief The structure holds information of various
 *  VMEM parameters that is submitted in the task.
 */
typedef struct PVA_PACKED {
	/**
	 * @brief The IOVA address of the parameter data.
	 * This should point to an array of type @ref pva_vpu_parameter_list_t .
	 * If no parameters are present this should be set to 0
	 */
	pva_iova parameter_data_iova;

	/**
	 * @brief The starting IOVA address of the parameter data whose size
	 * is lower than @ref PVA_DMA_VMEM_COPY_THRESHOLD . This data is copied
	 * from DRAM to TCM using DMA, and then memcopied to VMEM.
	 * If no small parameters are present this should be set to 0.
	 */
	pva_iova small_vpu_param_data_iova;

	/**
	 * @brief The number of bytes of small VPU parameter data, i.e the
	 * data whose size is lower than @ref PVA_DMA_VMEM_COPY_THRESHOLD . If no small
	 * parameters are present, this should be set to 0
	 */
	uint32_t small_vpu_parameter_data_size;

	/**
	 * @brief The index of the array of type @ref pva_vpu_parameter_list_t from which
	 * the VPU large parameters are present, i.e the vpu parameters whose size is greater
	 * than @ref PVA_DMA_VMEM_COPY_THRESHOLD . This value will always point to the index
	 * immediately after the small parameters. If no large parameter is present, then
	 * this field value will be same as the value of
	 * @ref pva_vpu_parameter_info_t.vpu_instance_parameter_list_start_index field
	 */
	uint32_t large_vpu_parameter_list_start_index;

	/**
	 * @brief The index of the array of type @ref pva_vpu_parameter_list_t from which
	 * the VPU instance parameters are present. This value will always point to the index
	 * immediately after the large parameters if large parameters are present, else it
	 * will be the same value as @ref pva_vpu_parameter_info_t.large_vpu_parameter_list_start_index
	 * field.
	 */
	uint32_t vpu_instance_parameter_list_start_index;
} pva_vpu_parameter_info_t;

/** @brief Special marker for IOVA address of parameter data of a task to differentiate
 *  if the parameter data specified in task should be used or if FW should create a supported
 *  parameter data instance. If the IOVA address of parameter data is lesser than this
 *  special marker, then use the parameter data specified in the task, else FW
 *  creates the parameter data.
 */
#define PVA_COMPLEX_IOVA (0xDA7AULL << 48ULL)

/** @brief Macro used to create new parameter base markers
 *  from the special marker address @ref PVA_COMPLEX_IOVA
 */
#define PVA_COMPLEX_IOVA_V(v) (PVA_COMPLEX_IOVA | ((uint64_t)(v) << 32ULL))

/** @brief Special Marker for @ref pva_vpu_instance_data_t */
#define PVA_SYS_INSTANCE_DATA_V1_IOVA (PVA_COMPLEX_IOVA_V(1) | 0x00000001ULL)

/**
 * @brief The minimuim size of the VPU parameter for it to be considered
 * as a large parameter
 */
#define PVA_DMA_VMEM_COPY_THRESHOLD (uint32_t)(256U)

/**
 * @brief The maximum combined size of all VMEM parameters
 * that will be supported by PVA
 */
#define VMEM_PARAMETER_BUFFER_MAX_SIZE (uint32_t)(8192U)

/**
 * @brief The maximum number of symbols that will be supported
 * for one task
 */
#define TASK_VMEM_PARAMETER_MAX_SYMBOLS (uint32_t)(128U)

/**
 * @brief Information of the VPU instance data passed to VPU kernel.
 */
typedef struct PVA_PACKED {
	/** @brief ID of the VPU assigned to the task */
	uint16_t pve_id;
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
} pva_vpu_instance_data_t;

#endif /* PVA_SYS_PARAMS_H */
