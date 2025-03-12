/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
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

/**
 * @defgroup PVA_VPU_SYSCALL
 *
 * @brief PVA VPU SYS call IDs for each type of
 * SYS call.
 * @{
 */

//! @cond DISABLE_DOCUMENTATION

/**
 * @brief VPU Syscall id for vpu printf write.
 */
#define PVA_FW_PE_SYSCALL_ID_WRITE (1U)
//! @endcond
/**
 * @brief VPU Syscall id for Icache prefetch.
 */
#define PVA_FW_PE_SYSCALL_ID_ICACHE_PREFETCH (2U)

/**
 * @brief VPU Syscall id for masking exceptions.
 */
#define PVA_FW_PE_SYSCALL_ID_MASK_EXCEPTION (3U)

/**
 * @brief VPU Syscall id for unmasking exceptions.
 */
#define PVA_FW_PE_SYSCALL_ID_UNMASK_EXCEPTION (4U)
//! @cond DISABLE_DOCUMENTATION
/**
 * @brief VPU Syscall id for sampling VPU performance counters
 */
#define PVA_FW_PE_SYSCALL_ID_PERFMON_SAMPLE (5U)
//! @endcond
/** @} */

/**
 * @defgroup PVA_PPE_SYSCALL
 *
 * @brief PVA PPE SYS call IDs for each type of
 * SYS call.
 * @{
 */

//! @cond DISABLE_DOCUMENTATION

/**
 * @brief PPE Syscall id for ppe printf write.
 */
#define PVA_FW_PPE_SYSCALL_ID_WRITE (1U)

/**
 * @brief PPE Syscall id for masking exceptions.
 */
#define PVA_FW_PPE_SYSCALL_ID_MASK_EXCEPTION (2U)

/**
 * @brief PPE Syscall id for unmasking exceptions.
 */
#define PVA_FW_PPE_SYSCALL_ID_UNMASK_EXCEPTION (3U)

/**
 * @brief VPU Syscall id for sampling VPU performance counters
 */
#define PVA_FW_PPE_SYSCALL_ID_PERFMON_SAMPLE (4U)
/**
 * @brief PPE Syscall id for Icache prefetch.
 */
#define PVA_FW_PPE_SYSCALL_ID_ICACHE_PREFETCH (5U)

//! @endcond
/** @} */

/**
 * @brief Lookup table to convert PPE syscall IDs to VPU syscall IDs
 * Index is PPE syscall ID, value is corresponding VPU syscall ID
 */
#define PVA_FW_PPE_TO_VPU_SYSCALL_LUT                                                          \
	{                                                                                      \
		0U, /* Index 0: Invalid */                                                     \
			PVA_FW_PE_SYSCALL_ID_WRITE, /* Index 1: Write */                       \
			PVA_FW_PE_SYSCALL_ID_MASK_EXCEPTION, /* Index 2: Mask Exception */     \
			PVA_FW_PE_SYSCALL_ID_UNMASK_EXCEPTION, /* Index 3: Unmask Exception */ \
			PVA_FW_PE_SYSCALL_ID_PERFMON_SAMPLE, /* Index 4: Perfmon Sample */     \
			PVA_FW_PE_SYSCALL_ID_ICACHE_PREFETCH /* Index 5: ICache Prefetch */    \
	}

/**
 * @brief Maximum valid PPE syscall ID
 */
#define PVA_FW_PPE_SYSCALL_ID_MAX PVA_FW_PPE_SYSCALL_ID_ICACHE_PREFETCH

/**
 * @defgroup PVA_VPU_SYSCALL_WRITE_PARAM_GROUP
 *
 * @brief Parameter specification for syscall write
 */

/**
 * @defgroup PVA_VPU_SYSCALL_COMMAND_FIELDS_GROUP
 *
 * @brief The command format to be used while issuing vpu syscall command from VPU kernel to R5.
 * The fields mentioned in this group is used for submitting the command
 * through the Signal_R5 interface from VPU kernel.
 *
 * @{
 */

/**
 * @brief The most significant bit of the vpu syscall ID field in
 * the vpu syscall command interface
 */
#define PVA_FW_PE_SYSCALL_ID_MSB (31U)

/**
 * @brief The least significant bit of the vpu syscall ID field in
 * the vpu syscall command interface
 */
#define PVA_FW_PE_SYSCALL_ID_LSB (24U)

/**
 * @brief The most significant bit of the vpu syscall parameter field in
 * the vpu syscall command interface
 */
#define PVA_FW_PE_SYSCALL_PARAM_MSB (23U)

/**
 * @brief The least significant bit of the vpu syscall parameter field in
 * the vpu syscall command interface
 */
#define PVA_FW_PE_SYSCALL_PARAM_LSB (0U)
/** @} */

/**
 * @defgroup PVA_VPU_SYSCALL_ICACHE_PREFETCH_PARAM_FIELDS_GROUP
 *
 * @brief The parameter format to be used while issuing vpu syscall command from VPU kernel to R5 for syscall icache prefetch.
 * The fields mentioned in this group is used for submitting the icache prefetch command
 * through the Signal_R5 interface from VPU kernel.
 *
 * @{
 */

/**
 * @brief The most significant bit of the prefetch cache line count field in
 * the vpu syscall command interface
 */
#define PVA_FW_PE_SYSCALL_PREFETCH_CACHE_LINE_COUNT_MSB (23U)

/**
 * @brief The least significant bit of the prefetch cache line count field in
 * the vpu syscall command interface
 */
#define PVA_FW_PE_SYSCALL_PREFETCH_CACHE_LINE_COUNT_LSB (16U)

/**
 * @brief The most significant bit of the prefetch address field in
 * the vpu syscall command interface
 */
#define PVA_FW_PE_SYSCALL_PREFETCH_ADDR_MSB (15U)

/**
 * @brief The least significant bit of the prefetch address field in
 * the vpu syscall command interface
 */
#define PVA_FW_PE_SYSCALL_PREFETCH_ADDR_LSB (0U)
/** @} */

/**
 * @defgroup PVA_VPU_SYSCALL_MASK_UNMASK_PARAM_FIELDS_GROUP
 *
 * @brief The parameter format to be used while issuing vpu syscall command from VPU kernel
 * to R5 for masking or unmasking FP NaN Exception.
 * The fields mentioned in this group is used for submitting the mask and unmask FP NaN eception command
 * through the Signal_R5 interface from VPU kernel.
 *
 * @{
 */

/**
 * @brief Parameter specification for syscall mask/unmask exceptions
 */
#define PVA_FW_PE_MASK_FP_INV_NAN (1U << 2U)
/** @} */

#endif // PVA_API_VPU_H
