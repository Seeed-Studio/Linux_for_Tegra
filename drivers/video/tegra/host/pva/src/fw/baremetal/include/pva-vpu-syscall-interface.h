/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*
 * Unit: VPU Unit
 * SWUD Document:
 * p4sw-swarm.nvidia.com/view/sw/embedded/docs/projects/active/DRIVE_6.0/QNX/PLC_Work_Products/Element_WPs/Autonomous_Middleware/PVA/04_Unit_Design/PVA_FW/SWE-PVAFW-006-SWUD.pdf
 */
/**
 * @file pva-vpu-syscall-interface.h
 *
 * @brief Syscall command specification
 *
 * VPU uses syscall commands to request services from R5. A syscall command is a
 * 32bit value that consists of a 8 bit syscall ID and 24 bit parameter. If more
 * information needs to be passed to R5, the parameter field will be a pointer
 * to a VMEM location.
 */

#ifndef PVA_VPU_SYSCALL_INTERFACE_H
#define PVA_VPU_SYSCALL_INTERFACE_H

#include <stdint.h>

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

/**
 * @breif Write syscall parameter will be a pointer to this struct
 * @{
 */
typedef union {
	struct {
		uint32_t addr;
		uint32_t size;
	} in;
	struct {
		uint32_t written_size;
	} out;
} pva_fw_pe_syscall_write;
/** @} */

/**
 * @defgroup PVA_VPU_SYSCALL_PERFMON_SAMPLE_PARAM_GROUP
 *
 * @brief Parameter specification for syscall perfmon_sample
 *
 * @{
 */

/**
 * @brief Perfmon sample syscall parameter will be a pointer to this struct
 */
typedef struct {
	/** counter_mask[0] is for ID: 0-31; counter_mask[1] is for ID: 32-63 */
	uint32_t counter_mask[2];
	uint32_t output_addr;
} pva_fw_pe_syscall_perfmon_sample;

/**
 * @brief Index for t26x performance counters for VPU
 */
#define PERFMON_COUNTER_ID_VPS_STALL_ID_NO_VAL_INSTR_T26X (0U)
#define PERFMON_COUNTER_ID_VPS_ID_VALID_T26X (1U)
#define PERFMON_COUNTER_ID_VPS_STALL_ID_REG_DEPEND_T26X (2U)
#define PERFMON_COUNTER_ID_VPS_STALL_ID_ONLY_T26X (3U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX1_ONLY_T26X (4U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX4_RSC_HZRD_T26X (5U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX4_DATA_HZRD_T26X (6U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX4_RAMIC_HI_PRI_T26X (7U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX5_APB_T26X (8U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX8_RSC_HZRD_T26X (9U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX8_RAMIC_HI_PRI_T26X (10U)
#define PERFMON_COUNTER_ID_VPS_WFE_GPI_EX_STATE_T26X (11U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_RD_REQ_L01_T26X (12U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_RD_REQ_ACT_L01_T26X (13U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_RD_REQ_L23_T26X (14U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_RD_REQ_ACT_L23_T26X (15U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_WR_REQ_L01_T26X (16U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_WR_REQ_ACT_L01_T26X (17U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_WR_REQ_L23_T26X (18U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_WR_REQ_ACT_L23_T26X (19U)
#define PERFMON_COUNTER_ID_VPS_ICACHE_FETCH_REQ_T26X (20U)
#define PERFMON_COUNTER_ID_VPS_ICACHE_MISS_T26X (21U)
#define PERFMON_COUNTER_ID_VPS_ICACHE_PREEMPT_T26X (22U)
#define PERFMON_COUNTER_ID_VPS_ICACHE_PREFETCH_LINES_T26X (23U)
#define PERFMON_COUNTER_ID_VPS_ICACHE_MISS_DUR_T26X (24U)
#define PERFMON_COUNTER_ID_VPS_ICACHE_PREFETCH_DUR_T26X (25U)
#define PERFMON_COUNTER_ID_DLUT_BUSY_T26X (26U)
#define PERFMON_COUNTER_ID_DLUT_VPU_BOTH_BUSY_T26X (27U)
#define PERFMON_COUNTER_ID_VPU_WAIT_FOR_DLUT_T26X (28U)
#define PERFMON_COUNTER_ID_DLUT_WAIT_FOR_VPU_T26X (29U)
#define PERFMON_COUNTER_ID_DLUT_IDX_TRANS_T26X (30U)
#define PERFMON_COUNTER_ID_DLUT_LUT_TRANS_T26X (31U)
#define PERFMON_COUNTER_ID_DLUT_OUT_TRANS_T26X (32U)
#define PERFMON_COUNTER_ID_DLUT_IDX_REQ_ACT_T26X (33U)
#define PERFMON_COUNTER_ID_DLUT_LUT_REQ_ACT_T26X (34U)
#define PERFMON_COUNTER_ID_DLUT_OUT_REQ_ACT_T26X (35U)
#define PERFMON_COUNTER_ID_DLUT_NULL_GROUPS_T26X (36U)

/**
 * @brief Index for t23x performance counters
 */
#define PERFMON_COUNTER_ID_VPS_STALL_ID_NO_VAL_INSTR_T23X (0U)
#define PERFMON_COUNTER_ID_VPS_ID_VALID_T23X (1U)
#define PERFMON_COUNTER_ID_VPS_STALL_ID_REG_DEPEND_T23X (2U)
#define PERFMON_COUNTER_ID_VPS_STALL_ID_ONLY_T23X (3U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX1_ONLY_T23X (4U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX4_RSC_HZRD_T23X (5U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX4_DATA_HZRD_T23X (6U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX4_RAMIC_HI_PRI_T23X (7U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX5_APB_T23X (8U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX8_RSC_HZRD_T23X (9U)
#define PERFMON_COUNTER_ID_VPS_STALL_EX8_RAMIC_HI_PRI_T23X (10U)
#define PERFMON_COUNTER_ID_VPS_WFE_GPI_EX_STATE_T23X (11U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_RD_REQ_L01_T23X (12U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_RD_REQ_ACT_L01_T23X (13U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_RD_REQ_L23_T23X (14U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_RD_REQ_ACT_L23_T23X (15U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_WR_REQ_L01_T23X (16U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_WR_REQ_ACT_L01_T23X (17U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_WR_REQ_L23_T23X (18U)
#define PERFMON_COUNTER_ID_VMEMIF_RAMIC_WR_REQ_ACT_L23_T23X (19U)
#define PERFMON_COUNTER_ID_ICACHE_FETCH_REQ_T23X (20U)
#define PERFMON_COUNTER_ID_ICACHE_MISS_T23X (21U)
#define PERFMON_COUNTER_ID_ICACHE_PREEMP_T23X (22U)
#define PERFMON_COUNTER_ID_ICACHE_PREFETCH_LINES_T23X (23U)
#define PERFMON_COUNTER_ID_ICACHE_MISS_DUR_T23X (24U)
#define PERFMON_COUNTER_ID_ICACHE_PREFETCH_DUR_T23X (25U)
#define PERFMON_COUNTER_ID_DLUT_BUSY_T23X (26U)
#define PERFMON_COUNTER_ID_DLUT_VPU_BOTH_BUSY_T23X (27U)
#define PERFMON_COUNTER_ID_VPU_WAIT_FOR_DLUT_T23X (28U)
#define PERFMON_COUNTER_ID_DLUT_WAIT_FOR_VPU_T23X (29U)
#define PERFMON_COUNTER_ID_DLUT_IDX_TRANS_T23X (30U)
#define PERFMON_COUNTER_ID_DLUT_LUT_TRANS_T23X (31U)
#define PERFMON_COUNTER_ID_DLUT_OUT_TRANS_T23X (32U)
#define PERFMON_COUNTER_ID_DLUT_IDX_REQ_ACT_T23X (33U)
#define PERFMON_COUNTER_ID_DLUT_LUT_REQ_ACT_T23X (34U)
#define PERFMON_COUNTER_ID_DLUT_OUT_REQ_ACT_T23X (35U)
#define PERFMON_COUNTER_ID_DLUT_NULL_GROUPS_T23X (36U)

/**
 * @brief Index for t26x performance counters for PPE
 */
#define PERFMON_COUNTER_ID_PPS_STALL_ID_NO_VAL_INSTR_T26X (0U)
#define PERFMON_COUNTER_ID_PPS_ID_VALID_T26X (1U)
#define PERFMON_COUNTER_ID_PPS_STALL_ID_REG_DEPEND_T26X (2U)
#define PERFMON_COUNTER_ID_PPS_STALL_ID_ONLY_T26X (3U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX1_ONLY_T26X (4U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_IORF_LD_DEPENDENCY_T26X (5U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_IORF_ST_DEPENDENCY_T26X (6U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_IORF_DEPENDENCY_T26X (7U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_STRM_STORE_FLUSH_T26X (8U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_SCALAR_STORE_FLUSH_T26X (9U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_STORE_FLUSH_T26X (10U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_STREAM_START_LD_T26X (11U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_STREAM_START_ST_T26X (12U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_STREAM_START_T26X (13U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_SCALAR_LD_T26X (14U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_SCALAR_ST_T26X (15U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_SCALAR_LDST_T26X (16U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_LDQ_PUSHBACK_T26X (17U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_STQ_PUSHBACK_T26X (18U)
#define PERFMON_COUNTER_ID_PPS_STALL_EX3_LDQ_FLUSH_T26X (19U)
#define PERFMON_COUNTER_ID_PPS_WFE_GPI_EX_STATE_T26X (20U)
#define PERFMON_COUNTER_ID_PPS_ICACHE_FETCH_REQ_T26X (21U)
#define PERFMON_COUNTER_ID_PPS_ICACHE_MISS_T26X (22U)
#define PERFMON_COUNTER_ID_PPS_ICACHE_PREEMPT_T26X (23U)
#define PERFMON_COUNTER_ID_PPS_ICACHE_PREFETCH_LINES_T26X (24U)
#define PERFMON_COUNTER_ID_PPS_ICACHE_MISS_DUR_T26X (25U)
#define PERFMON_COUNTER_ID_PPS_ICACHE_PREFETCH_DUR_T26X (26U)
/** @} */

#endif /*PVA_VPU_SYSCALL_INTERFACE_H*/
