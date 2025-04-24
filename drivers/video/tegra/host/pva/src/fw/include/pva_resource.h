/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_RESOURCE_H
#define PVA_RESOURCE_H
#include "pva_api.h"
#include "pva_api_dma.h"
#include "pva_bit.h"
#include "pva_constants.h"
#include "pva_utils.h"
#include "pva_math_utils.h"

/* The sizes of these structs must be explicitly padded to align to 4 bytes */

struct pva_fw_dma_descriptor {
	uint8_t transfer_control0;
	uint8_t link_did;
	uint8_t src_adr1;
	uint8_t dst_adr1;
	uint32_t src_adr0;
	uint32_t dst_adr0;
	uint16_t tx;
	uint16_t ty;
	uint16_t slp_adv;
	uint16_t dlp_adv;
	uint32_t srcpt1_cntl;
	uint32_t dstpt1_cntl;
	uint32_t srcpt2_cntl;
	uint32_t dstpt2_cntl;
	uint32_t srcpt3_cntl;
	uint32_t dstpt3_cntl;
	uint16_t sb_start;
	uint16_t db_start;
	uint16_t sb_size;
	uint16_t db_size;
	uint16_t trig_ch_events;
	uint16_t hw_sw_trig_events;
	uint8_t px;
	uint8_t py;
	uint8_t transfer_control1;
	uint8_t transfer_control2;
	uint8_t cb_ext;
	uint8_t rsvd;
	uint16_t frda;
};

/** Each slot is mapped to <reloc_count> number of pva_fw_dma_reloc. When
 * bind_dram/vmem_slot command is executed, the slot_id will be an index into
 * the slot array. The slot contains starting index and count of reloc structs.
 * All descriptor fields identified by the reloc structs will be patched.
 */
struct pva_fw_dma_slot {
/** This slot can be bound to a DRAM buffer */
#define PVA_FW_DMA_SLOT_FLAG_DRAM (1u << 0u)
/** This slot can be bound to a L2SRAM buffer */
#define PVA_FW_DMA_SLOT_FLAG_L2SRAM (1u << 1u)
/** This slot can be bound to a VMEM DATA buffer */
#define PVA_FW_DMA_SLOT_FLAG_VMEM_DATA (1u << 2u)
/** This slot can be bound to a VMEM VPU config table buffer */
#define PVA_FW_DMA_SLOT_FLAG_VMEM_VPUC_TABLE (1u << 3u)
/** This slot has enabled circular buffer. Slot with this flags cannot be bound
 * to block linear surface. */
#define PVA_FW_DMA_SLOT_FLAG_CB (1u << 4u)
#define PVA_FW_DMA_SLOT_FLAG_BOUND (1u << 5u)
#define PVA_FW_DMA_SLOT_FLAG_MASKED (1u << 6u)
#define PVA_FW_DMA_SLOT_FLAG_ACCESS_LSB 7u
#define PVA_FW_DMA_SLOT_FLAG_ACCESS_MSB 8u
	uint16_t flags;
	/** Bitmask of channels that use this slot */
	uint16_t ch_use_mask;

	/** The number of descriptor fields that share this slot. Each field
	 * will have a pva_fw_dma_reloc struct
	 */
	uint16_t reloc_count;
	/** Starting index in the pva_fw_dma_reloc array */
	uint16_t reloc_start_idx;

	int64_t start_addr;
	int64_t end_addr;
};

static inline uint32_t get_slot_size(struct pva_fw_dma_slot const *slot)
{
	uint32_t size = UINT32_MAX;
	int64_t tmp_size = 0;
	if (slot->end_addr < slot->start_addr) {
		return size;
	}
	tmp_size = slot->end_addr - slot->start_addr;
	if (tmp_size > (int64_t)UINT32_MAX) {
		return size;
	}
	size = (uint32_t)tmp_size;
	return size;
}

/**
 * A relocate struct identifies an address field (src, dst or dst2) in
 * the descriptor. The identified address field contains an offset instead of
 * absolute address. The base address will be added to the offset during
 * binding.
 *
 * This struct only has 2 bytes, so an array of this struct must have an even
 * number of elements to satisfy alignment requirement.
 */
struct pva_fw_dma_reloc {
	uint8_t desc_index;
/** This relocation is for source field */
#define PVA_FW_DMA_RELOC_FIELD_SRC 1u
/** This relocation is for destination field */
#define PVA_FW_DMA_RELOC_FIELD_DST 2u
/** This relocation is for destination 2 field */
#define PVA_FW_DMA_RELOC_FIELD_DST2 3u
	uint8_t field;
};

struct pva_fw_dma_channel {
	uint32_t cntl0;
	uint32_t cntl1;
	uint32_t boundary_pad;
	uint32_t hwseqcntl;
	uint32_t hwseqfscntl;
};

struct pva_fw_data_section_info {
	uint32_t data_buf_off; /*< offset in data section data byte array */
	uint32_t vmem_addr;
	uint32_t size;
};

struct pva_dma_resource_map {
// TODO: These macros should be derived using the maximum limits across platforms
//	 Today, they are being hardcoded. Make it automatic
#define PVA_DMA_NUM_CHANNEL_PARTITIONS                                         \
	((PVA_MAX_NUM_DMA_CHANNELS) / (PVA_DMA_CHANNEL_ALIGNMENT))
#define PVA_DMA_NUM_DESCRIPTOR_PARTITIONS                                      \
	((PVA_MAX_NUM_DMA_DESC) / (PVA_DMA_DESCRIPTOR_ALIGNMENT))
#define PVA_DMA_NUM_ADB_PARTITIONS                                             \
	((PVA_MAX_NUM_ADB_BUFFS) / (PVA_DMA_ADB_ALIGNMENT))
#define PVA_DMA_NUM_HWSEQ_WORD_PARTITIONS                                      \
	((PVA_MAX_NUM_HWSEQ_WORDS) / (PVA_DMA_HWSEQ_WORD_ALIGNMENT))

	uint64_t channels : PVA_DMA_NUM_CHANNEL_PARTITIONS;
	uint64_t descriptors : PVA_DMA_NUM_DESCRIPTOR_PARTITIONS;
	uint64_t adbs : PVA_DMA_NUM_ADB_PARTITIONS;
	uint64_t hwseq_words : PVA_DMA_NUM_HWSEQ_WORD_PARTITIONS;
	uint64_t triggers : 1;
};

static inline void
pva_dma_resource_map_reset(struct pva_dma_resource_map *resource_map)
{
	resource_map->channels = 0u;
	resource_map->descriptors = 0u;
	resource_map->adbs = 0u;
	resource_map->hwseq_words = 0u;
	resource_map->triggers = 0u;
}

// Note: the following pva_dma_resource_map_* APIs assume an alignment requirement
//	 on the 'start' index. We do not enforce it here though. If this requirement
//	 is not met, the FW may falsely predicted resource conflicts between commands.
//	 However, this will not impact functionality or correctness.
static inline void
pva_dma_resource_map_add_channels(struct pva_dma_resource_map *map,
				  uint16_t start, uint16_t count)
{
	map->channels |= pva_mask64(start, count, PVA_DMA_CHANNEL_ALIGNMENT);
}

static inline void
pva_dma_resource_map_add_descriptors(struct pva_dma_resource_map *map,
				     uint16_t start, uint16_t count)
{
	map->descriptors |=
		pva_mask64(start, count, PVA_DMA_DESCRIPTOR_ALIGNMENT);
}

static inline void
pva_dma_resource_map_add_adbs(struct pva_dma_resource_map *map, uint16_t start,
			      uint16_t count)
{
	map->adbs |= pva_mask64(start, count, PVA_DMA_ADB_ALIGNMENT);
}

static inline void
pva_dma_resource_map_add_hwseq_words(struct pva_dma_resource_map *map,
				     uint16_t start, uint16_t count)
{
	map->hwseq_words |=
		pva_mask64(start, count, PVA_DMA_HWSEQ_WORD_ALIGNMENT);
}

static inline void
pva_dma_resource_map_add_triggers(struct pva_dma_resource_map *map)
{
	// If an application is running on VPU, it has access to all the triggers
	// Only FW and DMA-only workloads can initiate transfers in parallel to
	// a running VPU application, but they do not require triggers.
	map->triggers |= 1;
}

static inline void
pva_dma_resource_map_copy_channels(struct pva_dma_resource_map *dst_map,
				   struct pva_dma_resource_map *src_map)
{
	dst_map->channels |= src_map->channels;
}

static inline void
pva_dma_resource_map_copy_descriptors(struct pva_dma_resource_map *dst_map,
				      struct pva_dma_resource_map *src_map)
{
	dst_map->descriptors |= src_map->descriptors;
}

static inline void
pva_dma_resource_map_copy_adbs(struct pva_dma_resource_map *dst_map,
			       struct pva_dma_resource_map *src_map)
{
	dst_map->adbs |= src_map->adbs;
}

static inline void
pva_dma_resource_map_copy_triggers(struct pva_dma_resource_map *dst_map,
				   struct pva_dma_resource_map *src_map)
{
	dst_map->triggers |= src_map->triggers;
}

static inline void
pva_dma_resource_map_copy_hwseq_words(struct pva_dma_resource_map *dst_map,
				      struct pva_dma_resource_map *src_map)
{
	dst_map->hwseq_words |= src_map->hwseq_words;
}

struct pva_dma_config_resource {
	uint8_t base_descriptor;
	uint8_t base_channel;
	uint8_t num_descriptors;
	uint8_t num_channels;

	uint16_t num_dynamic_slots;
	/** Must be an even number to satisfy padding requirement. */
	uint16_t num_relocs;
	/** Indices of channels. Once the corresponding bit is set, the block height of
	 * this channel should not be changed. */
	uint16_t ch_block_height_fixed_mask;

	uint16_t base_hwseq_word;
	uint16_t num_hwseq_words;
	uint16_t pad;

	uint32_t vpu_exec_resource_id;
	uint32_t common_config;
	uint32_t output_enable[PVA_NUM_DMA_TRIGGERS];

	struct pva_dma_resource_map dma_resource_map;
	/* Followed by <num_dynamic_slots> of pva_fw_dma_slot */
	/* Followed by <num_reloc_infos> of pva_fw_dma_reloc */
	/* Followed by an array of pva_fw_dma_channel */
	/* Followed by an array of pva_fw_dma_descriptor */

	/* =====================================================================
	 * The following fields do not need to be fetched into TCM. The DMA config
	 * resource size (as noted in the resource table) does not include these
	 * fields */

	/* Followed by an array of hwseq words */
};

struct pva_fw_vmem_buffer {
#define PVA_FW_SYM_TYPE_MSB 31
#define PVA_FW_SYM_TYPE_LSB 29
#define PVA_FW_VMEM_ADDR_MSB 28
#define PVA_FW_VMEM_ADDR_LSB 0
	uint32_t addr;
	uint32_t size;
};

struct pva_exec_bin_resource {
	uint8_t code_addr_hi;
	uint8_t data_section_addr_hi;
	uint8_t num_data_sections;
	uint8_t pad;

	uint32_t code_addr_lo;
	uint32_t data_section_addr_lo;
	uint32_t code_size;
	uint32_t num_vmem_buffers;

	/* Followed by <num_data_sections> number of pva_fw_data_section_info  */
	/* Followed by <num_vmem_buffers> number of pva_fw_vmem_buffer */
};

static inline struct pva_fw_dma_slot *
pva_dma_config_get_slots(struct pva_dma_config_resource *dma_config)
{
	return (struct pva_fw_dma_slot
			*)((uint8_t *)dma_config +
			   sizeof(struct pva_dma_config_resource));
}

static inline struct pva_fw_dma_reloc *
pva_dma_config_get_relocs(struct pva_dma_config_resource *dma_config)
{
	return (struct pva_fw_dma_reloc
			*)((uint8_t *)pva_dma_config_get_slots(dma_config) +
			   sizeof(struct pva_fw_dma_slot) *
				   dma_config->num_dynamic_slots);
}

static inline struct pva_fw_dma_channel *
pva_dma_config_get_channels(struct pva_dma_config_resource *dma_config)
{
	return (struct pva_fw_dma_channel *)((uint8_t *)
						     pva_dma_config_get_relocs(
							     dma_config) +
					     sizeof(struct pva_fw_dma_reloc) *
						     dma_config->num_relocs);
}

static inline struct pva_fw_dma_descriptor *
pva_dma_config_get_descriptors(struct pva_dma_config_resource *dma_config)
{
	return (struct pva_fw_dma_descriptor
			*)((uint8_t *)pva_dma_config_get_channels(dma_config) +
			   sizeof(struct pva_fw_dma_channel) *
				   dma_config->num_channels);
}

#endif // PVA_RESOURCE_H
