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
#ifndef PVA_CONFIG_H
#define PVA_CONFIG_H

#include <pva-types.h>
#include "pva_fw_constants.h"

/**
 * @defgroup PVA_CONFIG_PARAMS
 *
 * @brief PVA Configuration parameters.
 * @{
 */
/**
 * @brief Queue id for queue0.
 */
#define PVA_FW_QUEUE_0 (0U)

/**
 * @brief Total number of queues that are present
 *        for communication between KMD and FW.
 */
#define PVA_NUM_QUEUES (8U)

/**
 * @brief Maximum queue id value in PVA System.
 */
#define PVA_MAX_QUEUE_ID (PVA_NUM_QUEUES - 1U)

/**
 * @brief Maximum number of tasks that is supported by a queue.
 */
#define MAX_QUEUE_DEPTH (256U)

/**
 * @brief Number of Hardware Semaphore registers in PVA System.
 */
#define PVA_NUM_SEMA_REGS (4U)

/**
 * @brief Number of Hardware Mailbox registers in PVA System.
 */
#define PVA_NUM_MBOX_REGS (8U)

/**
 * @brief Maximum number of Pre-Actions for a task.
 */
#define PVA_MAX_PREACTIONS (26U)

/**
 * @brief Maximum number of Post-Actions for a task.
 */
#define PVA_MAX_POSTACTIONS (28U)

//! @cond DISABLE_DOCUMENTATION
/**
 * @brief Maximum number of DMA channels for T26x.
 */
#define PVA_NUM_DMA_CHANNELS_T26X (8U)

/**
 * @brief Total number of AXI data buffers for T26x.
 */
#define PVA_NUM_DMA_ADB_BUFFS_T26X (304U)

/**
 * @brief Number of reserved AXI data buffers for T26x.
 */
#define PVA_NUM_RESERVED_ADB_BUFFERS_T26X (16U)

/**
 * @brief Number of dynamic AXI data buffers for T26x.
 * These exclude the reserved AXI data buffers from total available ones.
 */
#define PVA_NUM_DYNAMIC_ADB_BUFFS_T26X                                         \
	(PVA_NUM_DMA_ADB_BUFFS_T26X - PVA_NUM_RESERVED_ADB_BUFFERS_T26X)

/**
 * @brief Maximum number of DMA channels for T23x.
 */
#define PVA_NUM_DMA_CHANNELS_T23X (16U)
//! @endcond

/**
 * @brief Number of DMA descriptors for T19x.
 */
#define PVA_NUM_DMA_DESCS_T19X (64U)
/**
 * @brief Number of DMA descriptors for T23x.
 */
#define PVA_NUM_DMA_DESCS_T23X (64U)
/**
 * @brief Number of DMA descriptors for T26x.
 */
#define PVA_NUM_DMA_DESCS_T26X (96U)

/**
 * @brief Number of reserved DMA channels. These channels
 * are reserved per DMA for R5 transfers. These channels
 * will be used by R5 to transfer data which it needs.
 */
#define PVA_NUM_RESERVED_CHANNELS (1U)

/**
 * @brief Number of dynamic DMA descriptors for T19x. These descriptors can be
 * used by the VPU application transfer data. These exclude
 * the reserved descriptors from total available ones.
 */
#define PVA_NUM_DYNAMIC_DESCS_T19X                                             \
	(PVA_NUM_DMA_DESCS_T19X - PVA_NUM_RESERVED_DESCRIPTORS)
/**
 * @brief Number of dynamic DMA descriptors for T23x. These descriptors can be
 * used by the VPU application transfer data. These exclude
 * the reserved descriptors from total available ones.
 */
#define PVA_NUM_DYNAMIC_DESCS_T23X                                             \
	(PVA_NUM_DMA_DESCS_T23X - PVA_NUM_RESERVED_DESCRIPTORS)
/**
 * @brief Number of dynamic DMA descriptors for T26x. These descriptors can be
 * used by the VPU application transfer data. These exclude
 * the reserved descriptors from total available ones.
 */
#define PVA_NUM_DYNAMIC_DESCS_T26X                                             \
	(PVA_NUM_DMA_DESCS_T26X - PVA_NUM_RESERVED_DESCRIPTORS)
/**
 * Note: T26x will be brought up first on Linux, and then on QNX. To support this,
 * the following macro is needed so that the QNX driver can build without requiring
 * any changes.
 */
#define PVA_NUM_DYNAMIC_DESCS (PVA_NUM_DYNAMIC_DESCS_T23X)

/**
 * @brief Number of reserved AXI data buffers for T23x.
 */
#define PVA_NUM_RESERVED_ADB_BUFFERS_T23X (16U)

/**
 * @brief Number of reserved VMEM data buffers.
 */
#define PVA_NUM_RESERVED_VDB_BUFFERS (0U)

/**
 * @brief Total number of VMEM data buffers.
 */
#define PVA_NUM_DMA_VDB_BUFFS (128U)

/**
 * @brief Total number of AXI data buffers for T23x.
 */
#define PVA_NUM_DMA_ADB_BUFFS_T23X (272U)

/**
 * @brief Number of dynamic AXI data buffers for T23x.
 * These exclude the reserved AXI data buffers from total available ones.
 */
#define PVA_NUM_DYNAMIC_ADB_BUFFS_T23X                                         \
	(PVA_NUM_DMA_ADB_BUFFS_T23X - PVA_NUM_RESERVED_ADB_BUFFERS_T23X)

/**
 * @brief Number of dynamic VMEM data buffers for T23x.
 * These exclude the reserved VMEM data buffers from total available ones.
 */
#define PVA_NUM_DYNAMIC_VDB_BUFFS                                              \
	(PVA_NUM_DMA_VDB_BUFFS - PVA_NUM_RESERVED_VDB_BUFFERS)

/**
 * @brief The first Reserved DMA descriptor. This is used as a
 *        starting point to iterate over reserved DMA descriptors.
 */
#define PVA_RESERVED_DESC_START (60U)

/**
 * @brief The first Reserved AXI data buffers. This is used as a
 *        starting point to iterate over reserved AXI data buffers.
 */
#define PVA_RESERVED_ADB_BUFF_START PVA_NUM_DYNAMIC_ADB_BUFFS

/**
 * @brief This macro has the value to be set by KMD in the shared semaphores
 * @ref PVA_PREFENCE_SYNCPT_REGION_IOVA_SEM or @ref PVA_POSTFENCE_SYNCPT_REGION_IOVA_SEM
 * if the syncpoint reserved region must not be configured as uncached
 * in R5 MPU.
 */
#define PVA_R5_SYNCPT_REGION_IOVA_OFFSET_NOT_SET (0xFFFFFFFFU)
/** @} */

/**
 * @defgroup PVA_CONFIG_PARAMS_T19X
 *
 * @brief PVA Configuration parameters exclusively for T19X.
 * @{
 */
/**
 * @brief Number of DMA channels for T19x or Xavier.
 */
#define PVA_NUM_DMA_CHANNELS_T19X (14U)

/**
 * @brief Number of reserved AXI data buffers for T19x.
 */
#define PVA_NUM_RESERVED_ADB_BUFFERS_T19X (8U)

/**
 * @brief Total number of AXI data buffers for T19x.
 */
#define PVA_NUM_DMA_ADB_BUFFS_T19X (256U)

/**
 * @brief Number of dynamic AXI data buffers for T19x.
 * These exclude the reserved AXI data buffers from total available ones.
 */
#define PVA_NUM_DYNAMIC_ADB_BUFFS_T19X                                         \
	(PVA_NUM_DMA_ADB_BUFFS_T19X - PVA_NUM_RESERVED_ADB_BUFFERS_T19X)

/** @} */
#endif
