/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_ERRORS_H
#define PVA_ERRORS_H

#include <stdint.h>
#include <pva-packed.h>

/**
 * @brief PVA Error codes
 */
typedef uint16_t pva_errors_t;

/**
 * @defgroup PVA_ERRORS
 *
 * @brief General and interface errors of PVA.
 * @{
 */
/**
 * @brief In case of no Error.
 */
#define PVA_ERR_NO_ERROR (0x0U)

/**
 * @brief Error in case of an illegal command
 *        PVA FW executes commands that are found
 *        in the command look up table. If a command
 *        is not part of supported commands, this
 *        error will be returned. Valid commands can be
 *        referred at @ref pva_cmd_lookup_t.
 *
 */
#define PVA_ERR_BAD_CMD (0x1U)

/**
 * @brief Error in case of bad queue id, ie
 * queue id that was requested is not available.
 */
#define PVA_ERR_BAD_QUEUE_ID (0x3U)

/**
 * @brief Error in case of invalid pve-id. This
 *        error is generated if PVE id is greater
 *        than @ref PVA_NUM_PVE.
 */
#define PVA_ERR_BAD_PVE_ID (0x4U)

/**
 * @brief Error in case when number of pre-actions
 * are more than what can be accommodated.
 */
#define PVA_ERR_BUFF_TOO_SMALL (0x5U)

/**
 * @brief Error in case when requested feature can not be satisfied.
 *        This error arises in scenarios where certain actions are
 *        not supported during execution of pre-actions or post-actions.
 *        For instance, @ref TASK_ACT_WRITE_STATUS is not supported in
 *        executing pre-actions of task.
 */
#define PVA_ERR_FEATURE_NOT_SUPPORTED (0x6U)

/**
 * @brief Error in case when the address generated or translated does not
 * meet the constraints like alignment or non-null.
 */
#define PVA_ERR_BAD_ADDRESS (0x9U)

/**
 * @brief Error in case when timestamp is requested on un-supported action.
 */
#define PVA_ERR_BAD_TIME_VALUE (0xdU)
#if PVA_SAFETY == 0
/**
 * @brief Error in case when the register provided to update
 *        the status is invalid.
 */
#define PVA_ERR_BAD_STATUS_REG (0x10U)
#endif
//! @endcond
/**
 * @brief Error in case of bad task.
 *        In scenarios where task does not meet the
 *        necessary criteria like non-zero or 64 byte alignment.
 *        This error will be returned.
 */
#define PVA_ERR_BAD_TASK (0x15U)

/**
 * @brief Error in case of invalid task action list. Invalid
 *        action list arises in scenarios like number of
 *        pre and post actions not being zero but actual
 *        pre or post action to be performed being NULL.
 */
#define PVA_ERR_BAD_TASK_ACTION_LIST (0x16U)

/**
 * @brief Error when internal state of task is not as expected.
 *        A task goes through transition of various state while
 *        executing. In case when a state is not coherent with
 *        action being performed this error is returned.
 *        For example, task can not be in a running state
 *        while tear-down is being performed.
 */
#define PVA_ERR_BAD_TASK_STATE (0x17U)

/**
 * @brief Error when there is a mis-match in input status and the actual status.
 *        This error occurs when there is a mis-match in status from @ref pva_gen_task_status_t
 *        and actual status that is populated by FW during task execution.
 */
#define PVA_ERR_TASK_INPUT_STATUS_MISMATCH (0x18U)

/**
 * @brief Error in case of invalid parameters. These errors occur when
 *        parameters passed are invalid and is applicable for task parameters
 *        and DMA parameters.
 */
#define PVA_ERR_BAD_PARAMETERS (0x1aU)

/**
 * @brief Error in case of when timed out occurred for batch of task.
 */
#define PVA_ERR_PVE_TIMEOUT (0x23U)

/**
 * @brief Error when VPU has halted or turned off.
 */
#define PVA_ERR_VPU_ERROR_HALT (0x25U)

/**
 * @brief Error after FW sends an abort signal to KMD. KMD will write into status buffers for
 *        pending tasks after FW sends an abort signal to KMD.
 */
#define PVA_ERR_VPU_BAD_STATE (0x28U)

/**
 * @brief Error in case of exiting VPU.
 */
#define PVA_ERR_VPU_EXIT_ERROR (0x2aU)
//! @cond DISABLE_DOCUMENTATION
/**
 * @brief Error in case of exiting PPE.
 */
#define PVA_ERR_PPE_EXIT_ERROR (0x2bU)
//! @endcond
/**
 * @brief Error when a task running on PVE caused abort on PVE.
 */
#define PVA_ERR_PVE_ABORT (0x2dU)
/**
 * @brief Error in case of Floating point NAN.
 */

//! @cond DISABLE_DOCUMENTATION
#define PVA_ERR_PPE_ILLEGAL_INSTR_ALIGN (0x37U)

/**
 * @brief Error in case of Bad cached DRAM segment.
 */
#define PVA_ERR_BAD_CACHED_DRAM_SEG (0x3aU)

/**
 * @brief Error in case of Bad DRAM IOVA.
 */
#define PVA_ERR_BAD_DRAM_IOVA (0x3cU)
//! @endcond

/**
 * @brief Error in case of Register mis-match.
 */
#define PVA_ERR_REG_MISMATCH (0x3dU)

/**
 * @brief Error in case of AISR queue empty.
 */
#define PVA_ERR_AISR_INPUT_QUEUE_EMPTY (0x3fU)

/**
 * @brief Error in case of AISR queue full.
 */
#define PVA_ERR_AISR_OUTPUT_QUEUE_FULL (0x40U)
#if (PVA_HAS_L2SRAM == 1)
/**
 * @brief Error in case of L2SRAM allocation failed due to invalid parameters.
 */
#define PVA_ERR_BAD_L2SRAM_PARAMS (0x41U)
#endif
/**
 * @brief Error in case of bad or invalid task parameters.
 */
#define PVA_ERR_BAD_TASK_PARAMS (0x42U)
/**
 * @brief Error in case of invalid VPU system call.
 */
#define PVA_ERR_VPU_SYS_ERROR (0x43U)
/**
 * @brief Error in case of HW Watchdog timer timeout
 */
#define PVA_ERR_WDT_TIMEOUT_ERROR (0x44U)
/**
 * @brief Error in case Golden register check value mismatch.
 */
#define PVA_ERR_GR_REG_MISMATCH (0x45U)
/**
 * @brief Error in case Critical register check value mismatch.
 */
#define PVA_ERR_CRIT_REG_MISMATCH (0x46U)
/** @} */

/**
 * @defgroup PVA_DMA_ERRORS
 *
 * @brief DMA ERROR codes used across PVA.
 * @{
 */
/**
 * @brief Error when DMA transfer mode in DMA descriptor is invalid.
 */
#define PVA_ERR_DMA_TRANSFER_TYPE_INVALID (0x204U)

/**
 * @brief Error when DMA transfer was not successful.
 */
#define PVA_ERR_DMA_CHANNEL_TRANSFER (0x207U)

/**
 * @brief Error in case of BAD DMA descriptor.
 */
#define PVA_ERR_BAD_DMA_DESC_ID (0x208U)

/**
 * @brief Error in case of BAD DMA channel ID.
 */
#define PVA_ERR_BAD_DMA_CHANNEL_ID (0x209U)

/**
 * @brief Error in case of DMA timeout.
 */
#define PVA_ERR_DMA_TIMEOUT (0x20bU)

/**
 * @brief Error when program trying to use channel is already active.
 */
#define PVA_ERR_DMA_INVALID_CONFIG (0x220U)

/**
 * @brief Error in case DMA transfer was not successful.
 */
#define PVA_ERR_DMA_ERROR (0x221U)

/**
 * @brief Error when number of bytes of HW Seq data copy is
 * not a multiple of 4.
 */
#define PVA_ERR_DMA_HWSEQ_BAD_PROGRAM (0x216U)

/**
 * @brief Error when number of bytes of HW Seq data copy is
 * more than HW Seq RAM size.
 */
#define PVA_ERR_DMA_HWSEQ_PROGRAM_TOO_LONG (0x217U)
/**
 * @defgroup PVA_VPU_ISR_ERRORS
 *
 * @brief VPU ISR error codes used across PVA.
 * @{
 */
/**
 * @defgroup PVA_FAST_RESET_ERRORS
 *
 * @brief Fast reset error codes used across PVA.
 * @{
 */
/**
 * @brief Error when VPU is not in idle state for a reset to be done.
 */
#define PVA_ERR_FAST_RESET_TIMEOUT_VPU (0x401U)
/**
 * @brief Error if VPU I-Cache is busy before checking DMA engine for idle state.
 */
#define PVA_ERR_FAST_RESET_TIMEOUT_ICACHE1 (0x402U)
/**
 * @brief Error if DMA channel is busy for a reset to be done.
 */
#define PVA_ERR_FAST_RESET_TIMEOUT_CH0 (0x403U)
/**
 * @brief Error if VPU I-Cache is busy after checking DMA engine for idle state.
 */
#define PVA_ERR_FAST_RESET_TIMEOUT_ICACHE2 (0x419U)

#if (PVA_CHIP_ID == CHIP_ID_T26X)
/**
 * @brief Error when PPE is not in idle state for a reset to be done.
 */
#define PVA_ERR_FAST_RESET_TIMEOUT_PPE (0x420U)
#endif
/** @} */

/**
 * @defgroup PVA_L2SRAM_ERRORS
 *
 * @brief L2SRAM memory error codes used across PVA.
 * @{
 */
/**
 * @brief Error if l2sram memory allocation failed because of insufficient l2sram memory or
 * if 2 chunks of memory are already allocated.
 */
#define PVA_ERR_ALLOC_FAILED (0x812U)
/**
 * @brief Error if If l2sram address given for clearing/freeing is not a valid L2SRAM address
 */
#define PVA_ERR_FREE_FAILED (0x813U)
/** @} */

/**
 * @defgroup PVA_INFO_ERRORS
 *
 * @brief Informational error codes.
 * @{
 */
/**
 * @brief Error when there is no task.
 */
#define PVA_ERR_NO_TASK (0x997U)
/**
 * @brief Error when CCQ IRQ line enable on VIC fails
 */
#define PVA_ERR_CCQ_IRQ_ENABLE_FAILED (0x998U)
/**
 * @brief Error when Mailbox IRQ line enable on VIC fails
 */
#define PVA_ERR_MBOX_IRQ_ENABLE_FAILED (0x999U)
/**
 * @brief Error when L2SRAM IRQ line enable on VIC fails
 */
#define PVA_ERR_L2SRAM_IRQ_ENABLE_FAILED (0x99AU)
/**
 * @brief Error when DMA0 IRQ line enable on VIC fails
 */
#define PVA_ERR_DMA0_IRQ_ENABLE_FAILED (0x99BU)
/**
 * @brief Error when DMA1 IRQ line enable on VIC fails
 */
#define PVA_ERR_DMA1_IRQ_ENABLE_FAILED (0x99CU)
/**
 * @brief Error when VPU IRQ line enable on VIC fails
 */
#define PVA_ERR_VPU_IRQ_ENABLE_FAILED (0x99DU)
/**
 * @brief Error when SEC IRQ line enable on VIC fails
 */
#define PVA_ERR_SEC_IRQ_ENABLE_FAILED (0x99EU)
/**
 * @brief Error when RAMIC IRQ line enable on VIC fails
 */
#define PVA_ERR_RAMIC_IRQ_ENABLE_FAILED (0x99FU)

/**
 * @brief Error in case to try again.
 * @note This error is internal to FW only.
 */
#define PVA_ERR_TRY_AGAIN (0x9A0U)
/** @} */

/* Never used */
#define PVA_ERR_MAX_ERR (0xFFFFU)

#endif /* _PVA_ERRORS_H_ */
