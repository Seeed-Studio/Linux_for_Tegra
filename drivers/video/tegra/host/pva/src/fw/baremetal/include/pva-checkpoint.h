/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

/*
 * Unit: Utility Unit
 * SWUD Document:
 * p4sw-swarm.nvidia.com/view/sw/embedded/docs/projects/active/DRIVE_6.0/QNX/PLC_Work_Products/Element_WPs/Autonomous_Middleware/PVA/04_Unit_Design/PVA_FW/SWE-PVAFW-006-SWUD.pdf
 */
#ifndef PVA_CHECKPOINT_H
#define PVA_CHECKPOINT_H

/**
 * @file pva-checkpoint.h
 * @brief Defines macros to create a checkpoint
 */

/**
 * @defgroup PVA_CHECKPOINT_MACROS Macros to define a checkpoint
 *
 * @brief Checkpoints are the 32-bit status values that can be written to status
 * register during R5's execution. The 32-bit value is divided into four 8-bit values.
 * These are:
 *  - major code: major aspect (usually a unit) of the uCode. Bit Position: [31:24]
 *                Valid values are defined at @ref PVA_CHECKPOINT_MAJOR_CODES.
 *  - minor code: minor aspect (usually a function) of the uCode.The interpretation of the
 *                minor value is determined by the major value. Bit Position: [23:16]
 *  - flags: flags indicating type of the checkpoint such as error checkpoint,
 *           performance checkpoint, checkpoint indicating start of an operation,
 *           checkpoint indicating end of an operation etc. Bit Position: [15:8]
 *           Valid values are defined at @ref PVA_CHECKPOINT_FLAGS.
 *  - sequence: disambiguate multiple checkpoints within a minor code or to convey additional
 *              information. The interpretation of the sequence value is determined by both the
 *              major and minor values. Bit Position: [7:0]
 *              Valid values are any values from 0 to UINT8_MAX
 * @{
 */

/**
 * @defgroup PVA_CHECKPOINT_MAJOR_CODES
 * @brief Macros to define the major code field of the checkpoint @ingroup PVA_CHECKPOINT_MACROS
 * @{
 */

/*
 * Operational major codes
 */

/**
 * @brief Major code for PVA during Boot.
 */
#define PVA_CHK_MAIN (0x01U)

//! @endcond

/**
 * @brief Error related major codes
 */
#define PVA_CHK_ABORT (0xFFU)

/** @} */

/**
 * @defgroup PVA_CHECKPOINT_HW_STATE_MINOR_CODES
 * @brief Macros to define the minor code field of the checkpoints with major code PVA_CHK_HW_STATE
 * @ingroup PVA_CHECKPOINT_MACROS
 *
 * @{
 */
/**
 * @brief Minor code while doing a MMIO HW state check.
 */
#define PVA_CHK_HW_STATE_MMIO (0x01U)

/**
 * @brief Minor code while doing a VIC HW state check.
 */
#define PVA_CHK_HW_STATE_VIC (0x02U)

/**
 * @brief Minor code while doing a ARM register HW state check.
 */
#define PVA_CHK_HW_STATE_ARM (0x03U)

/**
 * @brief Minor code while doing a MPU HW state check.
 */
#define PVA_CHK_HW_STATE_MPU (0x04U)

/**
 * @brief Minor code while doing a DMA HW state check.
 */
#define PVA_CHK_HW_STATE_DMA (0x05U)

/**
 * @brief Minor code while doing a VIC HW state check.
 */
#define PVA_CHK_HW_STATE_GOLDEN (0x06U)
/** @} */

/** @} */

/**
 * @defgroup PVA_ABORT_REASONS
 *
 * @brief Macros to define the abort reasons
 * @{
 */
/**
 * @brief Minor code for abort due to assert.
 */
#define PVA_ABORT_ASSERT (0x01U)

/**
 * @brief Minor code for abort in case pva main call fails.
 */
#define PVA_ABORT_FALLTHRU (0x02U)

/**
 * @brief Minor code for abort in case of fatal IRQ.
 */
#define PVA_ABORT_IRQ (0x05U)

/**
 * @brief Minor code for abort in case of MPU failure.
 */
#define PVA_ABORT_MPU (0x06U)

/**
 * @brief Minor code for abort in case of ARM exception.
 */
#define PVA_ABORT_EXCEPTION (0x07U)

/**
 * @brief Minor code for abort in case of un-supported SID read.
 */
#define PVA_ABORT_UNSUPPORTED (0x09U)

/**
 * @brief Minor code for abort in case of DMA failures.
 */
#define PVA_ABORT_DMA_TASK (0x0cU)

/**
 * @brief Minor code for abort in case of WDT failures.
 * Note: This code is not reported to HSM.
 */
#define PVA_ABORT_WATCHDOG (0x0eU)

//! @endcond

/**
 * @brief Minor code for abort in case of VPU init failures.
 */
#define PVA_ABORT_VPU (0x0fU)

/**
 * @brief Minor code for abort in case of DMA MISR setup failures.
 */
#define PVA_ABORT_DMA (0x10U)

//! @cond DISABLE_DOCUMENTATION
/**
 * @brief Minor code for abort in case of Mbox errors.
 * Note: This is used only in T19x
 */
#define PVA_ABORT_MBOX_WAR (0x12U)
//! @endcond

/**
 * @brief Minor code for abort in case of AISR errors.
 */
#define PVA_ABORT_AISR_QUEUE (0x14U)

/**
 * @brief Minor code for abort in case of bad task.
 */
#define PVA_ABORT_BAD_TASK (0x15U)

//! @cond DISABLE_DOCUMENTATION
/**
 * @brief Minor code for abort in case of PPE init failures.
 * Note: This is only used in T26x
 */
#define PVA_ABORT_PPE (0x16U)
//! @endcond

/**
 * @brief Minor code for abort in case of RAMIC failures.
 */
#define PVA_ABORT_RAMIC (0x20U)

/**
 * @brief Minor Code for SEC for safety errors.
 * Note: This code is not reported to HSM.
 */
#define PVA_ABORT_SEC_SERR (0x21U)

/**
 * @brief Minor Code for SEC for functional errors.
 * Note: This code is not reported to HSM.
 */
#define PVA_ABORT_SEC_FERR (0x22U)

/**
 * @brief Minor code for abort in case of firewall decode error.
 */
#define PVA_ABORT_L2SRAM_FWDEC (0x23U)

/**
 * @brief Minor code for abort in case of kernel panic.
 */
#define PVA_ABORT_KERNEL_PANIC (0x30U)

/**
 * @brief Minor code for abort in case of Batch Timeout.
 */
#define PVA_ABORT_BATCH_TIMEOUT (0x40U)

/**
 * @brief Minor code for abort in case of DMA Transfer Timeout.
 * while in launch phase for the VPU)
 */
#define PVA_ABORT_DMA_SETUP_TIMEOUT (0x41U)

//! @cond DISABLE_DOCUMENTATION
/**
 * @brief Minor code used when NOC BIST is run.
 * Note: This is only used in T19x
 */
#define PVA_ABORT_NOC_BIST (0xfcU)
//! @endcond

/** @} */

/**
 * @defgroup PVA_ABORT_ARGUMENTS Macros to define the argument for pva_abort operation
 *
 * @brief Argument of pva_abort operation is updated in status register
 *
 */

/**
 * @defgroup PVA_ABORT_ARGUMENTS_MPU
 * @brief Argument to pva_abort() from MPU operations
 * @ingroup PVA_ABORT_ARGUMENTS
 * @{
 */
/**
 * @brief Minor code when there is an error while configuring MPU.
 */
#define PVA_ABORT_MPU_CONFIG (0xE001U)

/**
 * @brief Minor code when there is an error while initializing MPU.
 */
#define PVA_ABORT_MPU_INIT (0xE002U)
/** @} */

/**
 * @defgroup PVA_ABORT_ARGUMENTS_VPU
 * @brief Argument to pva_abort() from VPU operations
 * @ingroup PVA_ABORT_ARGUMENTS
 * @{
 */
/**
 * @brief Minor code when VPU is in debug state.
 */
#define PVA_ABORT_VPU_DEBUG (0xE001U)
/** @} */

/**
 * @defgroup PVA_ABORT_ARGUMENTS_PPE
 * @brief Argument to pva_abort() from PPE operations
 * @ingroup PVA_ABORT_ARGUMENTS
 * @{
 */
/**
 * @brief Minor code when PPE is in debug state.
 */
#define PVA_ABORT_PPE_DEBUG (0xE002U)
/** @} */

/**
 * @brief Minor Code when DMA state is not idle to perform
 * DMA MISR setup.
 */
#define PVA_ABORT_DMA_MISR_BUSY (0xE001U)
/**
 * @brief Minor Code in DMA when MISR has timed out
 */
#define PVA_ABORT_DMA_MISR_TIMEOUT (0xE002U)

/**
 * @defgroup PVA_ABORT_ARGUMENTS_IRQ Argument to pva_abort() from IRQs
 * @ingroup PVA_ABORT_ARGUMENTS
 * @{
 */

/**
 * @brief Minor Code for Command FIFO used by Interrupt Handler.
 */
#define PVA_ABORT_IRQ_CMD_FIFO (0xE001U)

#if (0 == DOXYGEN_DOCUMENTATION)
#define PVA_ABORT_IRQ_TEST_HOST (0xE002U)
#endif
/** @} */
#endif
