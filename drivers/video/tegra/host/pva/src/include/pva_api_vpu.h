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
 * @brief Used to store VPU Syscall IDs, that represent the
 *        vpu syscall id between FW and VPU kernel.
 */
typedef uint32_t pva_vpu_syscall_id_t;

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

/**
 * @brief VPU Syscall id for vpu printf write.
 */
#define PVA_FW_PE_SYSCALL_ID_WRITE (1U)

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

/**
 * @brief VPU Syscall id for sampling VPU performance counters
 */
#define PVA_FW_PE_SYSCALL_ID_PERFMON_SAMPLE (5U)

/**
 * @brief VPU Syscall id for checking DMA active after VPU exit
 */
#define PVA_FW_PE_SYSCALL_ID_ALLOW_DMA_ACTIVE_AFTER_VPU_EXIT (6U)

/**
 * @brief PPE Syscall id for ppe printf write.
 */
#define PVA_FW_PPE_SYSCALL_ID_WRITE (1U)

/**
 * @brief PPE Syscall id for Icache prefetch.
 */
#define PVA_FW_PPE_SYSCALL_ID_ICACHE_PREFETCH (2U)

/**
 * @brief PPE Syscall id for masking exceptions.
 */
#define PVA_FW_PPE_SYSCALL_ID_MASK_EXCEPTION (3U)

/**
 * @brief PPE Syscall id for unmasking exceptions.
 */
#define PVA_FW_PPE_SYSCALL_ID_UNMASK_EXCEPTION (4U)

/**
 * @brief PPE Syscall id for sampling PPE performance counters
 */
#define PVA_FW_PPE_SYSCALL_ID_PERFMON_SAMPLE (5U)

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

/**
 * @brief Parameter specification for syscall mask/unmask exceptions
 */
#define PVA_FW_PE_MASK_DIV_BY_0 (1U << 1U)
#define PVA_FW_PE_MASK_FP_INV_NAN (1U << 2U)

/**
 * @breif Write syscall parameter will be a pointer to this struct
 */
union pva_fw_pe_syscall_write {
	struct {
		uint32_t addr;
		uint32_t size;
	} in;
	struct {
		uint32_t written_size;
	} out;
};

/**
 * @brief Perfmon sample syscall parameter will be a pointer to this struct
 */
struct pva_fw_pe_syscall_perfmon_sample {
	/** counter_mask[0] is for ID: 0-31; counter_mask[1] is for ID: 32-63 */
	uint32_t counter_mask[2];
	uint32_t output_addr;
};

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

#endif // PVA_API_VPU_H
