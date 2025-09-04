/*
 * Copyright (c) 2017-2022, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CORTEX_A76_H
#define CORTEX_A76_H

#include <lib/utils_def.h>

/* Cortex-A76 MIDR for revision 0 */
#define CORTEX_A76_MIDR						U(0x410fd0b0)

/* Cortex-A76 loop count for CVE-2022-23960 mitigation */
#define CORTEX_A76_BHB_LOOP_COUNT				U(24)

/*******************************************************************************
 * CPU Extended Control register specific definitions.
 ******************************************************************************/
#define CORTEX_A76_CPUPWRCTLR_EL1				S3_0_C15_C2_7
#define CORTEX_A76_CPUECTLR_EL1					S3_0_C15_C1_4

#define CORTEX_A76_CPUECTLR_EL1_WS_THR_L2			(ULL(3) << 24)
#define CORTEX_A76_CPUECTLR_EL1_BIT_51				(ULL(1) << 51)

/*******************************************************************************
 * Definitions for CORTEX_A76_CPUECTLR_EL1 register related to Write-streaming.
 ******************************************************************************/
#define WS_THR_L2_256B		(U(0) << 24)
#define WS_THR_L2_4KB		(U(1) << 24)
#define WS_THR_L2_8KB		(U(2) << 24)
#define WS_THR_L2_DISABLE	(U(3) << 24)

#define WS_THR_L3_768B		(U(0) << 22)
#define WS_THR_L3_16KB		(U(1) << 22)
#define WS_THR_L3_32KB		(U(2) << 22)
#define WS_THR_L3_DISABLE	(U(3) << 22)

#define WS_THR_L4_16KB		(U(0) << 20)
#define WS_THR_L4_64KB		(U(1) << 20)
#define WS_THR_L4_128KB		(U(2) << 20)
#define WS_THR_L4_DISABLE	(U(3) << 20)

#define WS_THR_DRAM_64KB	(U(0) << 18)
/* 1MB, for memory designated as outer-allocate.*/
#define WS_THR_DRAM_ALLOC_1MB	(U(1) << 18)
/* 1MB, allocating irrespective of outer-allocation designation. */
#define WS_THR_DRAM_1MB		(U(2) << 18)
#define WS_THR_DRAM_DISABLE	(U(3) << 18)

#define WS_THR_DISABLE_ALL	(WS_THR_L2_DISABLE | WS_THR_L3_DISABLE | WS_THR_L4_DISABLE | \
				WS_THR_DRAM_DISABLE)

/*******************************************************************************
 * CPU Auxiliary Control register specific definitions.
 ******************************************************************************/
#define CORTEX_A76_CPUACTLR_EL1					S3_0_C15_C1_0

#define CORTEX_A76_CPUACTLR_EL1_DISABLE_STATIC_PREDICTION	(ULL(1) << 6)

#define CORTEX_A76_CPUACTLR_EL1_BIT_13				(ULL(1) << 13)

#define CORTEX_A76_CPUACTLR2_EL1				S3_0_C15_C1_1

#define CORTEX_A76_CPUACTLR2_EL1_BIT_2				(ULL(1) << 2)

#define CORTEX_A76_CPUACTLR2_EL1_DISABLE_LOAD_PASS_STORE	(ULL(1) << 16)

#define CORTEX_A76_CPUACTLR3_EL1				S3_0_C15_C1_2

#define CORTEX_A76_CPUACTLR3_EL1_BIT_10				(ULL(1) << 10)


/* Definitions of register field mask in CORTEX_A76_CPUPWRCTLR_EL1 */
#define CORTEX_A76_CORE_PWRDN_EN_MASK				U(0x1)

#endif /* CORTEX_A76_H */
