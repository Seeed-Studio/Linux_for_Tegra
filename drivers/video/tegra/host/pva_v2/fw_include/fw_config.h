/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2023, NVIDIA Corporation.  All rights reserved.
 */

#ifndef PVA_CONFIG_H
#define PVA_CONFIG_H

/**
 * @brief Number of DMA channels for T19x or Xavier.
 */
#define PVA_NUM_DMA_CHANNELS_T19X 14U

/**
 * @brief Number of DMA descriptors.
 */
#define PVA_NUM_DMA_DESCS 64U

/**
 * @brief Number of reserved DMA channels. These channels
 * are reserved per DMA for R5 transfers. These channels
 * will be used by R5 to transfer data which it needs.
 */
#define PVA_NUM_RESERVED_CHANNELS 1U

/**
 * @brief Number of reserved DMA descriptors. These descriptors
 * are reserved per DMA for R5 transfers. These descriptors along
 * with channels will be used by R5 to transfer data which it needs.
 */

#define PVA_NUM_RESERVED_DESCRIPTORS 4U
/**
 * @brief Number of dynamic DMA descriptors. These descriptors can be
 * used by the VPU application transfer data. These exclude
 * the reserved descriptors from total available ones.
 */
#define PVA_NUM_DYNAMIC_DESCS	(PVA_NUM_DMA_DESCS - \
				 PVA_NUM_RESERVED_DESCRIPTORS)

/**
 * @brief Number of reserved AXI data buffers for T19x.
 */
#define PVA_NUM_RESERVED_ADB_BUFFERS_T19X	8U

/**
 * @brief Number of reserved VMEM data buffers.
 */
#define PVA_NUM_RESERVED_VDB_BUFFERS	0U

/**
 * @brief Total number of VMEM data buffers.
 */
#define PVA_NUM_DMA_VDB_BUFFS	128U

/**
 * @brief Total number of AXI data buffers for T19x.
 */
#define PVA_NUM_DMA_ADB_BUFFS_T19X	256U

/**
 * @brief Number of dynamic AXI data buffers for T19x.
 * These exclude the reserved AXI data buffers from total available ones.
 */
#define PVA_NUM_DYNAMIC_ADB_BUFFS_T19X (PVA_NUM_DMA_ADB_BUFFS_T19X - \
					PVA_NUM_RESERVED_ADB_BUFFERS_T19X)

/**
 * @brief Number of dynamic VMEM data buffers for T19x.
 * These exclude the reserved VMEM data buffers from total available ones.
 */
#define PVA_NUM_DYNAMIC_VDB_BUFFS (PVA_NUM_DMA_VDB_BUFFS - \
				   PVA_NUM_RESERVED_VDB_BUFFERS)

/**
 * @brief The first Reserved DMA descriptor. This is used as a
 *        starting point to iterate over reserved DMA descriptors.
 */
#define PVA_RESERVED_DESC_START		(60U)

/**
 * @brief The first Reserved AXI data buffers. This is used as a
 *        starting point to iterate over reserved AXI data buffers.
 */
#define PVA_RESERVED_ADB_BUFF_START	PVA_NUM_DYNAMIC_ADB_BUFFS

/**
 * @brief The first Reserved VMEM data buffers. This is used as a
 *        starting point to iterate over reserved VMEM data buffers.
 */
#define PVA_RESERVED_VDB_BUFF_START	PVA_NUM_DYNAMIC_VDB_BUFFS
/**
 * @brief Maximum number of DMA channels for T23x.
 */

#define PVA_NUM_DMA_CHANNELS_T23X 16U

/**
 * @brief Number of reserved AXI data buffers for T23x.
 */
#define PVA_NUM_RESERVED_ADB_BUFFERS_T23X 16U

/**
 * @brief Total number of AXI data buffers for T23x.
 */
#define PVA_NUM_DMA_ADB_BUFFS_T23X 272U

/**
 * @brief Number of dynamic AXI data buffers for T23x.
 * These exclude the reserved AXI data buffers from total available ones.
 */

#define PVA_NUM_DYNAMIC_ADB_BUFFS_T23X (PVA_NUM_DMA_ADB_BUFFS_T23X - \
					PVA_NUM_RESERVED_ADB_BUFFERS_T23X)
/** @} */

#ifdef CONFIG_TEGRA_T26X_GRHOST_PVA
#include <fw_config_t264.h>
#else
#define PVA_NUM_DMA_ADB_BUFFS_T26X		PVA_NUM_DMA_ADB_BUFFS_T23X
#define PVA_NUM_RESERVED_ADB_BUFFERS_T26X	PVA_NUM_RESERVED_ADB_BUFFERS_T23X
#define PVA_NUM_DYNAMIC_ADB_BUFFS_T26X		PVA_NUM_DYNAMIC_ADB_BUFFS_T23X
#endif

#endif
