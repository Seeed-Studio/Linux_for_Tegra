/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_FW_HYP_H
#define PVA_FW_HYP_H

/**
 * @defgroup PVA_BOOT_TIME_MBOX
 *
 * @brief This group defines the mailboxes used by KMD to pass start iovas required for
 * user segment and priv2 segment configuration during boot.
 * @{
 */
/**
 * @brief Used to pass bits 31-0 of start iova of user segment.
 */
#define PVA_MBOXID_USERSEG_L (1U)
/**
 * @brief Used to pass bits 39-32 of start iova of user segment.
 */
#define PVA_MBOXID_USERSEG_H (2U)
/**
 * @brief Used to pass bits 31-0 of start iova of priv2 segment.
 */
#define PVA_MBOXID_PRIV2SEG_L (3U)
/**
 * @brief Used to pass bits 39-32 of start iova of priv2 segment.
 */
#define PVA_MBOXID_PRIV2SEG_H (4U)
/** @} */

/**
 * @defgroup PVA_SHARED_SEMAPHORE_STATUS_GROUP
 *
 * @brief The status bits for the shared semaphore which are mentioned in
 * the group are used to communicate various information between KMD and
 * PVA R5 FW. The highest 16 bits are used to send information from KMD to
 * R5 FW and the lower 16 bits are used to send information from R5 FW to KMD by
 * writing to the @ref PVA_BOOT_SEMA semaphore
 *
 * The bit-mapping of the semaphore is described below. The table below shows the mapping which
 * is sent by KMD to FW.
 *
 * | Bit Position |    Bit Field Name     |                                                 Description                                                                                               |
 * |:------------:|:---------------------:|:---------------------------------------------------------------------------------------------------------------------------------------------------------:|
 * |     31       |  BOOT INT             |  To indicate that KMD is expecting an interrupt from R5 once boot is complete                                                                             |
 * |     30       |  Reserved             |  Reserved for future use                                                                                                                                  |
 * |    27-25     |  Reserved             |  Reserved for future use                                                                                                                                  |
 * |    23-21     |  Reserved             |  Reserved for future use                                                                                                                                  |
 * |     20       |  CG DISABLE           |  To indicate the PVA R5 FW should disable the clock gating feature                                                                                        |
 * |     19       |  VMEM RD WAR DISABLE  |  To disable the VMEM Read fail workaround feature                                                                                                         |
 * |     18       |  TEST_MODE_ENABLE     |  To enter test mode. See Documentation.                                                                                                                   |
 * |     17       |  USE_XBAR_RAW         |  Reserved for future use                                                                                                                                  |
 * |     16       |  Reserved             |  Reserved for future use                                                                                                                                  |
 *
 * The table below shows the mapping which is sent by FW to KMD
 *
 * | Bit Position |    Bit Field Name     |                                      Description                                                            |
 * |:------------:|:---------------------:|:-----------------------------------------------------------------------------------------------------------:|
 * |   15-11      |    Reserved           |  Reserved for future use                                                                                    |
 * |   07-03      |    Reserved           |  Reserved for future use                                                                                    |
 * |     02       |    HALTED             |  To indicate to KMD that the PVA R5 FW has halted execution                                                 |
 * |     01       |   BOOT DONE           |  To indicate to KMD that the PVA R5 FW booting is complete                                                  |
 *
 * @{
 */

//! @endcond

/**
 * @brief This field is used to indicate that the R5 FW should
 * disable the clock gating feature
 */
#define PVA_BOOT_SEMA_CG_DISABLE PVA_BIT(20U)
//! @cond DISABLE_DOCUMENTATION

/** Tell firmware to enter test mode */
#define PVA_BOOT_SEMA_TEST_MODE_ENABLE PVA_BIT(18U)

/** Tell firmware that block linear surfaces are in XBAR_RAW format instead of
 * TEGRA_RAW format */
#define PVA_BOOT_SEMA_USE_XBAR_RAW PVA_BIT(17U)

/** Tell firmware to enable test mode */
#define PVA_BOOT_SEMA_TEST_MODE PVA_BIT(16U)

#define PVA_BOOT_SEMA 0U
#define PVA_RO_SYNC_BASE_SEMA 1U
#define PVA_RW_SYNC_BASE_SEMA 2U
#define PVA_RW_SYNC_SIZE_SEMA 3U
#define PVA_SEMA_MAX 4U

/**
 * @brief This macro has the value to be set by KMD in the shared semaphores
 * @ref PVA_PREFENCE_SYNCPT_REGION_IOVA_SEM or @ref PVA_POSTFENCE_SYNCPT_REGION_IOVA_SEM
 * if the syncpoint reserved region must not be configured as uncached
 * in R5 MPU.
 */
#define PVA_R5_SYNCPT_REGION_IOVA_OFFSET_NOT_SET (0xFFFFFFFFU)
/** @} */

/* Runtime mailbox messages between firmware and hypervisor */

/* When hypervisor send messages to R5 through mailboxes, we use mailbox 0 - 1
 * msg[0] = mailbox 1 -> generate interrupt to R5
 * msg[1] = mailbox 0
 */
#define PVA_FW_MBOX_TO_R5_BASE 0U
#define PVA_FW_MBOX_TO_R5_LAST 1U

/* When R5 send messages to hypervisor through mailboxes, we use mailbox 2 - 7
 * msg[0] = mailbox 7 -> generate interrupt to hypervisor
 * msg[1] = mailbox 2
 * msg[2] = mailbox 3
 * msg[3] = mailbox 4
 * msg[4] = mailbox 5
 * msg[5] = mailbox 6
 */
#define PVA_FW_MBOX_TO_HYP_BASE 2U
#define PVA_FW_MBOX_TO_HYP_LAST 7U

#define PVA_FW_MBOX_FULL_BIT PVA_BIT(31)

#endif // PVA_FW_HYP_H
