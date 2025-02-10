/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2023, NVIDIA Corporation.  All rights reserved.
 */

#ifndef PVA_TYPES_H
#define PVA_TYPES_H
#if !defined(__KERNEL__)
#define __user
#include <stdint.h>
#include <inttypes.h>
#else
#include <linux/types.h>
#endif
#include <linux/stddef.h>

typedef uint64_t pva_iova;

/*
 * Queue IDs
 */
enum pva_queue_id_e {
	PVA_FW_QUEUE_0,
	PVA_FW_QUEUE_1,
	PVA_FW_QUEUE_2,
	PVA_FW_QUEUE_3,
	PVA_FW_QUEUE_4,
	PVA_FW_QUEUE_5,
	PVA_FW_QUEUE_6,
	PVA_FW_QUEUE_7,
	PVA_FW_QUEUE_8, /* PVA_SW_BIST_QUEUE_ID0 */
	PVA_FW_QUEUE_9, /* PVA_SW_BIST_QUEUE_ID1 */
	PVA_NUM_QUEUES
};

/*
 * Hardware FIFO IDs
 */
typedef uint8_t pva_ccq_fifo_id_t;

/*
 * PVE IDs
 */
typedef uint8_t pva_pve_id_t;
#define PVA_PVE_ID_NONE 0xffU

/*
 * VMEM IDs
 */
typedef uint8_t pva_vmem_id_t;

/*
 * DMA Descriptor IDs
 */
typedef uint8_t pva_dma_desc_t;

/*
 * DMA Channel IDs
 */
typedef uint8_t pva_dma_channel_id_t;

/*
 * DMA Channel Mask
 */
typedef uint16_t pva_dma_channel_mask_t;

/*
 * Address range
 */
struct pva_addr_range_s {
	uint32_t offset;
	uint32_t addr;
	uint32_t size;
};

/*
 * Macro to access size of a member of a struct
 */
#define PVA_MEMBER_SIZEOF(_struct_, _member_)                                  \
	(sizeof(((_struct_ *)0)->_member_))

/*
 * SID
 */
typedef uint8_t pva_sid_t;

#endif
