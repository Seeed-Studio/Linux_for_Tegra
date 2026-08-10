/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024, NVIDIA Corporation.  All rights reserved.
 */

#ifndef PVA_FW_ADDRESS_MAP_H
#define PVA_FW_ADDRESS_MAP_H

/**
 * @brief Starting R5 address where FW code and data is placed.
 * This address is expected to be programmed in PVA_CFG_AR1PRIV_START by KMD.
 * This address is also expected to be used as offset where
 * PVA_CFG_R5PRIV_LSEGREG1 and PVA_CFG_R5PRIV_USEGREG1 registers would point.
 */
#define FW_CODE_DATA_START_ADDR                     1610612736 //0x60000000

/**
 * @brief R5 address where FW code and data is expected to end.
 * This address is expected to be programmed in PVA_CFG_AR1PRIV_END by KMD.
 */
#define FW_CODE_DATA_END_ADDR                       1612840960 //0x60220000

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
#define EVP_RESET_VECTOR                       (1610877952) //0x60040C00
/**
 * @brief R5 address of undefined instruction exception vector
 */
#define EVP_UNDEFINED_INSTRUCTION_VECTOR       (1610878976) //0x60041000
/**
 * @brief R5 address of svc exception vector
 */
#define EVP_SVC_VECTOR                         (1610880000) //0x60041400
/**
 * @brief R5 address of prefetch abort exception vector
 */
#define EVP_PREFETCH_ABORT_VECTOR              (1610881024) //0x60041800
/**
 * @brief R5 address of data abort exception vector
 */
#define EVP_DATA_ABORT_VECTOR                  (1610882048) //0x60041C00
/**
 * @brief R5 address of reserved exception vector.
 * It points to a dummy handler.
 */
#define EVP_RESERVED_VECTOR                    (1610883072) //0x60042000
/**
 * @brief R5 address of IRQ exception vector
 */
#define EVP_IRQ_VECTOR                         (1610884096) //0x60042400
/**
 * @brief R5 address of FIQ exception vector
 */
#define EVP_FIQ_VECTOR                         (1610885120) //0x60042800
/** @} */

/**
 * @defgroup PVA_DEBUG_BUFFERS
 *
 * @brief These buffers are arranged in the following order:
 * DEBUG_LOG_BUFFER followed by TRACE_BUFFER followed by CODE_COVERAGE_BUFFER.
 * @{
 */
/**
 * @brief Maximum size of debug log buffer in bytes.
 */
#define FW_DEBUG_LOG_BUFFER_SIZE                    262144 //0x40000
/**
 * @brief Maximum size of trace buffer in bytes.
 */
#define FW_TRACE_BUFFER_SIZE                        262144 //0x40000
/**
 * @brief Maximum size of code coverage buffer in bytes.
 */
#define FW_CODE_COVERAGE_BUFFER_SIZE                524288 //0x80000
/** @} */

/**
 * @brief Total size of buffers used for FW debug in bytes.
 */
#define FW_DEBUG_DATA_TOTAL_SIZE  (FW_DEBUG_LOG_BUFFER_SIZE + \
				FW_TRACE_BUFFER_SIZE + \
				FW_CODE_COVERAGE_BUFFER_SIZE)

/**
 * @brief Starting R5 address where FW debug related data is placed.
 * This address is expected to be programmed in PVA_CFG_AR2PRIV_START by KMD.
 * This address is also expected to be used as offset where
 * PVA_CFG_R5PRIV_LSEGREG2 and PVA_CFG_R5PRIV_USEGREG2 registers would point.
 */
#define FW_DEBUG_DATA_START_ADDR                    1879048192 //0x70000000

/**
 * @brief R5 address where FW debug related data is expected to end.
 * This address is expected to be programmed in PVA_CFG_AR2PRIV_END by KMD.
 */
#define FW_DEBUG_DATA_END_ADDR	(FW_DEBUG_DATA_START_ADDR + \
				 FW_DEBUG_DATA_TOTAL_SIZE)

/**
 * @brief Starting R5 address where FW expects shared buffers between KMD and
 * FW to be placed. This is to be used as offset when programming
 * PVA_CFG_R5USER_LSEGREG and PVA_CFG_R5USER_USEGREG.
 */
#define FW_SHARED_MEMORY_START                 2147483648 //0x80000000

/**
 * @defgroup PVA_HYP_SCR_VALUES
 *
 * @brief Following macros specify SCR firewall values that are expected to be
 * programmed by Hypervisor.
 * @{
 */
/**
 * @brief EVP SCR firewall to enable only CCPLEX read/write access.
 */
#define PVA_EVP_SCR_VAL 0x19000202

/**
 * @brief PRIV SCR firewall to enable only CCPLEX and R5 read/write access.
 */
#define PVA_PRIV_SCR_VAL 0x1F008282

/**
 * @brief CCQ SCR firewall to enable only CCPLEX write access and R5 read access.
 */
#define PVA_CCQ_SCR_VAL 0x19000280

/**
 * @brief CCQ SCR firewall to enable only CCPLEX read access and R5 read/write access.
 */
#define PVA_STATUS_CTL_SCR_VAL 0x1F008082
/** @} */
#endif
