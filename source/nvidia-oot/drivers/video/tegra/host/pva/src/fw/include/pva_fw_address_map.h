/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_FW_ADDRESS_MAP_H
#define PVA_FW_ADDRESS_MAP_H

/**
 * @brief Starting R5 address where FW code and data is placed.
 * This address is expected to be programmed in PVA_CFG_AR1PRIV_START by KMD.
 * This address is also expected to be used as offset where PVA_CFG_R5PRIV_LSEGREG1
 * and PVA_CFG_R5PRIV_USEGREG1 registers would point.
 */
#define FW_CODE_DATA_START_ADDR 0x60000000

/**
 * @brief R5 address where FW code and data is expected to end.
 * This address is expected to be programmed in PVA_CFG_AR1PRIV_END by KMD.
 */
#if PVA_DEV_MAIN_COMPATIBLE == 1
#define FW_CODE_DATA_END_ADDR 0x60220000
#else
#define FW_CODE_DATA_END_ADDR 0x62000000
#endif
/**
 * @defgroup PVA_EXCEPTION_VECTORS
 *
 * @brief Following macros define R5 addresses that are expected to be
 * programmed by KMD in EVP registers as is.
 * @{
 */
/**
 * @brief R5 address of reset exception vector
 */
#define PVA_EVP_RESET_VECTOR 0x60040C00
/**
 * @brief R5 address of undefined instruction exception vector
 */
#define PVA_EVP_UNDEFINED_INSTRUCTION_VECTOR (PVA_EVP_RESET_VECTOR + 0x400 * 1)
/**
 * @brief R5 address of svc exception vector
 */
#define PVA_EVP_SVC_VECTOR (PVA_EVP_RESET_VECTOR + 0x400 * 2)
/**
 * @brief R5 address of prefetch abort exception vector
 */
#define PVA_EVP_PREFETCH_ABORT_VECTOR (PVA_EVP_RESET_VECTOR + 0x400 * 3)
/**
 * @brief R5 address of data abort exception vector
 */
#define PVA_EVP_DATA_ABORT_VECTOR (PVA_EVP_RESET_VECTOR + 0x400 * 4)
/**
 * @brief R5 address of reserved exception vector.
 * It points to a dummy handler.
 */
#define PVA_EVP_RESERVED_VECTOR (PVA_EVP_RESET_VECTOR + 0x400 * 5)
/**
 * @brief R5 address of IRQ exception vector
 */
#define PVA_EVP_IRQ_VECTOR (PVA_EVP_RESET_VECTOR + 0x400 * 6)
/**
 * @brief R5 address of FIQ exception vector
 */
#define PVA_EVP_FIQ_VECTOR (PVA_EVP_RESET_VECTOR + 0x400 * 7)
/** @} */

/**
 * @defgroup PVA_DEBUG_BUFFERS
 *
 * @brief These buffers are arranged in the following order:
 * TRACE_BUFFER followed by CODE_COVERAGE_BUFFER followed by DEBUG_LOG_BUFFER.
 * @{
 */
/**
 * @brief Maximum size of trace buffer in bytes.
 */
#define FW_TRACE_BUFFER_SIZE 0x40000
/**
 * @brief Maximum size of code coverage buffer in bytes.
 */
#define FW_CODE_COVERAGE_BUFFER_SIZE 0x80000
/**
 * @brief Maximum size of debug log buffer in bytes.
 */
#if PVA_DEV_MAIN_COMPATIBLE == 1
#define FW_DEBUG_LOG_BUFFER_SIZE 0x40000
#else
#define FW_DEBUG_LOG_BUFFER_SIZE 0x400000
#endif
/** @} */

/**
 * @brief Total size of buffers used for FW debug in bytes.
 * TBD: Update this address based on build configuration once KMD changes are merged.
 */
#define FW_DEBUG_DATA_TOTAL_SIZE                                               \
	(FW_TRACE_BUFFER_SIZE + FW_DEBUG_LOG_BUFFER_SIZE +                     \
	 FW_CODE_COVERAGE_BUFFER_SIZE)

/**
 * @brief Starting R5 address where FW debug related data is placed.
 * This address is expected to be programmed in PVA_CFG_AR2PRIV_START by KMD.
 * This address is also expected to be used as offset where PVA_CFG_R5PRIV_LSEGREG2
 * and PVA_CFG_R5PRIV_USEGREG2 registers would point.
 */
#define FW_DEBUG_DATA_START_ADDR (0x70000000) //1879048192 0x70000000

/**
 * @brief R5 address where FW debug related data is expected to end.
 * This address is expected to be programmed in PVA_CFG_AR2PRIV_END by KMD.
 */
#define FW_DEBUG_DATA_END_ADDR                                                 \
	(FW_DEBUG_DATA_START_ADDR + FW_DEBUG_DATA_TOTAL_SIZE)

/**
 * @brief Starting R5 address where FW expects shared buffers between KMD and FW to be placed.
 * This is to be used as offset when programming PVA_CFG_R5USER_LSEGREG and PVA_CFG_R5USER_USEGREG.
 */
#define FW_SHARED_MEMORY_START (0x80000000U) //2147483648 0x80000000

/**
 * @defgroup PVA_HYP_SCR_VALUES
 *
 * @brief Following macros specify SCR firewall values that are expected to be
 * programmed by Hypervisor.
 * @{
 */

#define PVA_SCR_LOCK PVA_BIT(29)

/**
 * @brief EVP SCR firewall to enable only CCPLEX read/write access.
 */
#define PVA_EVP_SCR_VAL 0x19000202U

/**
 * @brief PRIV SCR firewall to enable only CCPLEX and R5 read/write access.
 */
#define PVA_PRIV_SCR_VAL 0x1F008282U

/**
 * @brief CCQ SCR firewall to enable only CCPLEX write access and R5 read access.
 */
#define PVA_CCQ_SCR_VAL 0x19000280U

/**
 * @brief Status Ctl SCR firewall to enable only CCPLEX read access and R5 read/write access.
 */
#define PVA_STATUS_CTL_SCR_VAL 0x1f008082U
#define PVA_STATUS_CTL_SCR_VAL_SIM 0x1f008282U
/** @} */

/**
 * @defgroup PVA_KMD_SCR_VALUES
 *
 * @brief Following macros specify SCR firewall values that are expected to be
 * programmed by KMD.
 * @{
 */
/**
 * @brief SECEXT_INTR SCR firewall to enable only CCPLEX and R5 read/write access.
 */
#define PVA_SEC_SCR_SECEXT_INTR_EVENT_VAL 0x39008282U
/**
 * @brief PROC SCR firewall to enable only CCPLEX read/write access and R5 read only access.
 */
#define PVA_PROC_SCR_PROC_VAL 0x39000282U
/** @} */

#endif
