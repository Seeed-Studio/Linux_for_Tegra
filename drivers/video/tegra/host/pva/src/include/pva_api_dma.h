/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_API_DMA_H
#define PVA_API_DMA_H
#include "pva_api_types.h"

/** Bit indices for VPU GPIO triggers */
enum pva_gpio_bit {
	GPIO_VPU_CFG_BIT = 4U,
	GPIO_READ0_BIT = 16U,
	GPIO_READ1_BIT = 17U,
	GPIO_READ2_BIT = 18U,
	GPIO_READ3_BIT = 19U,
	GPIO_READ4_BIT = 20U,
	GPIO_READ5_BIT = 21U,
	GPIO_READ6_BIT = 22U,
	GPIO_WRITE0_BIT = 23U,
	GPIO_WRITE1_BIT = 24U,
	GPIO_WRITE2_BIT = 25U,
	GPIO_WRITE3_BIT = 26U,
	GPIO_WRITE4_BIT = 27U,
	GPIO_WRITE5_BIT = 28U,
	GPIO_WRITE6_BIT = 29U
};

#define PVA_DMA_DESC_ID_NULL 0
#define PVA_DMA_DESC_ID_BASE 1

/**
 * The values of the enum members conform to the definitions of DMA descriptors'
 * trig_vpu_events field. Therefore, they can be assigned to trig_vpu_events
 * directly.
 */
enum pva_dma_trigger {
	PVA_DMA_NO_TRIG = 0,
	PVA_DMA_TRIG_READ0,
	PVA_DMA_TRIG_WRITE0,
	PVA_DMA_TRIG_VPU_CFG,
	PVA_DMA_TRIG_READ1,
	PVA_DMA_TRIG_WRITE1,
	PVA_DMA_TRIG_READ2,
	PVA_DMA_TRIG_WRITE2,
	PVA_DMA_TRIG_READ3,
	PVA_DMA_TRIG_WRITE3,
	PVA_DMA_TRIG_READ4,
	PVA_DMA_TRIG_WRITE4,
	PVA_DMA_TRIG_READ5,
	PVA_DMA_TRIG_WRITE5,
	PVA_DMA_TRIG_READ6,
	PVA_DMA_TRIG_WRITE6,
	PVA_DMA_TRIG_HWSEQ_RD,
	PVA_DMA_TRIG_HWSEQ_WR,
};

enum pva_dma_trigger_mode {
	PVA_DMA_TRIG_MODE_DIS = 0,
	PVA_DMA_TRIG_MODE_4TH_DIM,
	PVA_DMA_TRIG_MODE_3RD_DIM,
	PVA_DMA_TRIG_MODE_TILE
};

enum pva_dma_transfer_mode {
	PVA_DMA_TRANS_MODE_INVALID = 0,
	PVA_DMA_TRANS_MODE_DRAM = 1,
	PVA_DMA_TRANS_MODE_VMEM = 2,
	PVA_DMA_TRANS_MODE_L2SRAM = 3,
	PVA_DMA_TRANS_MODE_TCM = 4,
	/** MMIO is valid as dst in VPU config mode only */
	PVA_DMA_TRANS_MODE_MMIO = 5,
	PVA_DMA_TRANS_MODE_RSVD = 5,
	/** VPU config mode, valid for src only */
	PVA_DMA_TRANS_MODE_VPUCFG = 7
};

struct pva_dma_transfer_attr {
	uint8_t rpt1;
	uint8_t rpt2;
	uint8_t rpt3;
	uint8_t cb_enable;
	uint8_t transfer_mode;
	/** When dynamic slot flag is set, it means the memory location will be
	* relocated by commands.
	*/
#define PVA_DMA_DYNAMIC_SLOT (1 << 15)
#define PVA_DMA_STATIC_SLOT (1 << 14)
#define PVA_DMA_SLOT_INVALID 0
#define PVA_DMA_SLOT_ID_MASK 0xFF
#define PVA_DMA_MAX_NUM_SLOTS 256
	uint16_t slot;
	/** Line pitch in pixels */
	uint16_t line_pitch;
	uint32_t cb_start;
	uint32_t cb_size;
	int32_t adv1;
	int32_t adv2;
	int32_t adv3;
	uint64_t offset;
};

struct pva_dma_descriptor {
	/**
	 * Linked descriptor ID
	 *
	 * - 0: No linked descriptor
	 * - N (> 0): Linking to descriptor N - 1 in the descriptor array
	 */
	uint8_t link_desc_id;
	uint8_t px;
	uint8_t py;
	/** enum pva_dma_trigger_mode */
	uint8_t trig_event_mode;
	/** Trigger from enum pva_dma_trigger */
	uint8_t trig_vpu_events;
	uint8_t desc_reload_enable;
	/**
	 * Log2(number bytes per pixel).
	 *
	 * - 0: 1 byte per pixel
	 * - 1: 2 bytes per pixel
	 * - 2: 4 bytes per pixel
	 * - others: invalid
	 */
	uint8_t log2_pixel_size;
	uint8_t px_direction;
	uint8_t py_direction;
	uint8_t boundary_pixel_extension;
	/** TCM transfer size */
	uint8_t tts;
	/**
	 * - 0: transfer true completion disabled
	 * - 1: transfer true completion enabled
	 */
	uint8_t trigger_completion;
	uint8_t prefetch_enable;

	uint16_t tx;
	uint16_t ty;
	uint16_t dst2_slot;
	uint32_t dst2_offset;
	struct pva_dma_transfer_attr src;
	struct pva_dma_transfer_attr dst;
};

struct pva_dma_channel {
	/**
	 *  Starting descriptor index in the descriptor array
	 *
	 *  Valid range is [0, max_num_descriptors - 1]. This is different from
	 *  link_desc_id field, where 0 means no linked descriptor.
	 */
	uint8_t desc_index;
	uint8_t vdb_count;
	uint8_t vdb_offset;
	uint8_t req_per_grant;
	uint8_t prefetch_enable;
	uint8_t ch_rep_factor;
	uint8_t hwseq_enable;
	uint8_t hwseq_traversal_order;
	uint8_t hwseq_tx_select;
	uint8_t hwseq_trigger_done;
	uint8_t hwseq_frame_count;
	uint8_t hwseq_con_frame_seq;
	uint16_t hwseq_start;
	uint16_t hwseq_end;
	uint16_t adb_count;
	uint16_t adb_offset;
	/*!
	* Holds the trigger signal this channel will react to.
	*
	* IAS:
	*     DMA_COMMON_DMA_OUTPUT_ENABLEn (4 Bytes)
	*
	* Mapping:
	*     chanId corresponding to this structure is allocated by KMD.
	*     DMA_COMMON_DMA_OUTPUT_ENABLE0.bit[chanId]      = outputEnableMask.bit[0];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE0.bit[16 + chanId] = outputEnableMask.bit[1];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE1.bit[chanId]      = outputEnableMask.bit[2];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE1.bit[16 + chanId] = outputEnableMask.bit[3];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE2.bit[chanId]      = outputEnableMask.bit[4];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE2.bit[16 + chanId] = outputEnableMask.bit[5];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE3.bit[chanId]      = outputEnableMask.bit[6];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE3.bit[16 + chanId] = outputEnableMask.bit[7];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE4.bit[chanId]      = outputEnableMask.bit[8];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE4.bit[16 + chanId] = outputEnableMask.bit[9];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE5.bit[chanId]      = outputEnableMask.bit[10];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE5.bit[16 + chanId] = outputEnableMask.bit[11];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE6.bit[chanId]      = outputEnableMask.bit[12];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE6.bit[16 + chanId] = outputEnableMask.bit[13];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE7.bit[chanId]      = outputEnableMask.bit[14];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE8.bit[chanId]      = outputEnableMask.bit[15];
	*     DMA_COMMON_DMA_OUTPUT_ENABLE8.bit[16 + chanId] = outputEnableMask.bit[16];
	*/
	uint32_t output_enable_mask;
	uint32_t pad_value;
};

struct pva_dma_config_header {
/* In order to make efficient the allocation and tracking of DMA resources, DMA resources
 * are allocated in groups. For example, descriptors may be allocated in groups of 4, which
 * means that every allocation of descriptors will start at an alignment of 4. The following
 * macros control the alignment/grouping requirement of DMA resources.
 */
#define PVA_DMA_CHANNEL_ALIGNMENT 1
#define PVA_DMA_DESCRIPTOR_ALIGNMENT 4
#define PVA_DMA_ADB_ALIGNMENT 16
#define PVA_DMA_HWSEQ_WORD_ALIGNMENT 128
	uint8_t base_channel;
	uint8_t base_descriptor;
	uint8_t num_channels;
	uint8_t num_descriptors;

	uint16_t num_static_slots;
	uint16_t num_dynamic_slots;

	uint16_t base_hwseq_word;
	uint16_t num_hwseq_words;
	uint32_t vpu_exec_resource_id;
};

enum pva_dma_static_binding_type {
	PVA_DMA_STATIC_BINDING_INVALID = 0,
	PVA_DMA_STATIC_BINDING_DRAM,
	PVA_DMA_STATIC_BINDING_VMEM,
};

/** Max block height is 32 GOB */
#define PVA_DMA_MAX_LOG2_BLOCK_HEIGHT 5

struct pva_dma_dram_binding {
	/** enum pva_surface_format */
	uint8_t surface_format;
	uint8_t log2_block_height;
	uint32_t resource_id;
	uint64_t surface_base_offset;
	uint64_t slot_offset;
};

struct pva_dma_vmem_binding {
	struct pva_vmem_addr addr;
};

struct pva_dma_static_binding {
	/** enum pva_dma_static_binding_type */
	uint8_t type;
	union {
		struct pva_dma_dram_binding dram;
		struct pva_dma_vmem_binding vmem;
	};
};

#endif // PVA_API_DMA_H
