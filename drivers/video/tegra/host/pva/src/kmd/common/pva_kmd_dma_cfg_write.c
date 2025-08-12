// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_resource_table.h"
#include "pva_kmd_device_memory.h"
#include "pva_api.h"
#include "pva_api_types.h"
#include "pva_kmd_dma_cfg.h"
#include "pva_resource.h"
#include "pva_kmd_hwseq_validate.h"

static void write_dma_channel(struct pva_dma_channel const *ch,
			      uint8_t base_desc_index,
			      struct pva_fw_dma_channel *fw_ch,
			      struct pva_dma_resource_map *dma_resource_map,
			      bool support_hwseq_frame_linking)
{
	/* DMA_CHANNEL_CNTL0_CHSDID: DMA_CHANNEL_CNTL0[0] = descIndex + 1;*/
	fw_ch->cntl0 =
		(((ch->desc_index + base_desc_index + 1U) & 0xFFU) << 0U);

	/* DMA_CHANNEL_CNTL0_CHVMEMOREQ */
	fw_ch->cntl0 |= ((ch->vdb_count & 0xFFU) << 8U);

	/* DMA_CHANNEL_CNTL0_CHBH */
	fw_ch->cntl0 |= ((ch->adb_count & 0x1FFU) << 16U);

	/* DMA_CHANNEL_CNTL0_CHPREF */
	fw_ch->cntl0 |= ((ch->prefetch_enable & 1U) << 30U);

	/* DMA_CHANNEL_CNTL1_CHPWT */
	fw_ch->cntl1 = ((ch->req_per_grant & 0x7U) << 2U);

	/* DMA_CHANNEL_CNTL1_CHVDBSTART */
	fw_ch->cntl1 |= ((ch->vdb_offset & 0x7FU) << 16U);

	/* DMA_CHANNEL_CNTL1_CHADBSTART */
	fw_ch->cntl1 |= ((ch->adb_offset & 0x1FFU) << 23U);

	fw_ch->boundary_pad = ch->pad_value;

	fw_ch->cntl1 |= ((ch->ch_rep_factor & 0x7U) << 8U);

	/* DMA_CHANNEL_HWSEQCNTL_CHHWSEQSTART */
	fw_ch->hwseqcntl = ((ch->hwseq_start & 0x1FFU) << 0U);

	/* DMA_CHANNEL_HWSEQCNTL_CHHWSEQEND */
	fw_ch->hwseqcntl |= ((ch->hwseq_end & 0x1FFU) << 12U);

	/* DMA_CHANNEL_HWSEQCNTL_CHHWSEQTD */
	fw_ch->hwseqcntl |= ((ch->hwseq_trigger_done & 0x3U) << 24U);

	/* DMA_CHANNEL_HWSEQCNTL_CHHWSEQTS */
	fw_ch->hwseqcntl |= ((ch->hwseq_tx_select & 0x1U) << 27U);

	/* DMA_CHANNEL_HWSEQCNTL_CHHWSEQTO */
	fw_ch->hwseqcntl |= ((ch->hwseq_traversal_order & 0x1U) << 30U);

	/* DMA_CHANNEL_HWSEQCNTL_CHHWSEQEN */
	fw_ch->hwseqcntl |= ((ch->hwseq_enable & 0x1U) << 31U);

	/* DMA_CHANNEL_HWSEQFSCNTL_CHHWSEQFCNT*/
	fw_ch->hwseqfscntl |=
		(((uint32_t)ch->hwseq_con_frame_seq & 0x1U) << 0U);

	/* DMA_CHANNEL_HWSEQFSCNTL_CHHWSEQCFS*/
	fw_ch->hwseqfscntl |=
		(((uint32_t)ch->hwseq_frame_count & 0x3FU) << 16U);

	pva_dma_resource_map_add_adbs(dma_resource_map, ch->adb_offset,
				      ch->adb_count);
}

static uint32_t assemble_rpt_cntl(uint8_t rpt, uint32_t adv)
{
	return PVA_INSERT(rpt, 31, 24) | PVA_INSERT(adv, 23, 0);
}

static void write_dma_descriptor(struct pva_dma_descriptor const *desc,
				 struct pva_fw_dma_descriptor *fw_desc)
{
	fw_desc->src_adr0 = iova_lo(desc->src.offset);
	fw_desc->src_adr1 = iova_hi(desc->src.offset);

	fw_desc->dst_adr0 = iova_lo(desc->dst.offset);
	fw_desc->dst_adr1 = iova_hi(desc->dst.offset);

	/* DMA_DESC_TRANS CNTL0 */
	fw_desc->transfer_control0 = PVA_INSERT(desc->src.transfer_mode, 2, 0) |
				     PVA_INSERT(desc->dst.transfer_mode, 6, 4);
	/* DMA_DESC_TRANS CNTL1 */
	fw_desc->transfer_control1 =
		PVA_INSERT(desc->log2_pixel_size, 1, 0) |
		PVA_INSERT(desc->px_direction, 2, 2) |
		PVA_INSERT(desc->py_direction, 3, 3) |
		PVA_INSERT(desc->boundary_pixel_extension, 4, 4) |
		PVA_INSERT(desc->tts, 5, 5) |
		PVA_INSERT(desc->trigger_completion, 7, 7);
	/* DMA_DESC_TRANS CNTL2 */
	fw_desc->transfer_control2 = PVA_INSERT(desc->prefetch_enable, 0, 0) |
				     PVA_INSERT(desc->dst.cb_enable, 1, 1) |
				     PVA_INSERT(desc->src.cb_enable, 2, 2);

	fw_desc->link_did = desc->link_desc_id;

	/* DMA_DESC_TX */
	fw_desc->tx = desc->tx;
	/* DMA_DESC_TY */
	fw_desc->ty = desc->ty;
	/* DMA_DESC_DLP_ADV */
	fw_desc->dlp_adv = desc->dst.line_pitch;
	/* DMA_DESC_SLP_ADV */
	fw_desc->slp_adv = desc->src.line_pitch;
	/* DMA_DESC_DB_START */
	fw_desc->db_start = desc->dst.cb_start;
	/* DMA_DESC_DB_SIZE */
	fw_desc->db_size = desc->dst.cb_size;
	/* DMA_DESC_SB_START */
	fw_desc->sb_start = desc->src.cb_start;
	/* DMA_DESC_SB_SIZE */
	fw_desc->sb_size = desc->src.cb_size;
	/* DMA_DESC_TRIG_CH */
	/* Channel events are not supported */
	fw_desc->trig_ch_events = 0U;
	/* DMA_DESC_HW_SW_TRIG */
	fw_desc->hw_sw_trig_events =
		PVA_INSERT(desc->trig_event_mode, 1, 0) |
		PVA_INSERT(desc->trig_vpu_events, 5, 2) |
		PVA_INSERT(desc->desc_reload_enable, 12, 12);
	/* DMA_DESC_PX */
	fw_desc->px = desc->px;
	/* DMA_DESC_PY */
	fw_desc->py = desc->py;
	/* DMA_DESC_FRDA */
	fw_desc->frda = ((desc->dst2_offset >> 6U) & 0x7FFF);

	/* DMA_DESC_NDTM_CNTL0 */
	fw_desc->cb_ext = (((desc->src.cb_start >> 16) & 0x1) << 0) |
			  (((desc->dst.cb_start >> 16) & 0x1) << 2) |
			  (((desc->src.cb_size >> 16) & 0x1) << 4) |
			  (((desc->dst.cb_size >> 16) & 0x1) << 6);

	/* DMA_DESC_NS1_ADV & DMA_DESC_ST1_ADV */
	fw_desc->srcpt1_cntl =
		assemble_rpt_cntl(desc->src.rpt1, desc->src.adv1);
	fw_desc->srcpt2_cntl =
		assemble_rpt_cntl(desc->src.rpt2, desc->src.adv2);
	fw_desc->srcpt3_cntl =
		assemble_rpt_cntl(desc->src.rpt3, desc->src.adv3);
	fw_desc->dstpt1_cntl =
		assemble_rpt_cntl(desc->dst.rpt1, desc->dst.adv1);
	fw_desc->dstpt2_cntl =
		assemble_rpt_cntl(desc->dst.rpt2, desc->dst.adv2);
	fw_desc->dstpt3_cntl =
		assemble_rpt_cntl(desc->dst.rpt3, desc->dst.adv3);
}

static void write_triggers(struct pva_dma_config const *dma_cfg,
			   struct pva_dma_config_resource *fw_cfg,
			   struct pva_dma_resource_map *dma_resource_map)
{
	uint32_t i, j;
	bool trigger_required = false;

	memset(fw_cfg->output_enable, 0, sizeof(fw_cfg->output_enable));

	for (i = 0; i < dma_cfg->header.num_channels; i++) {
		struct pva_dma_channel const *ch = &dma_cfg->channels[i];
		uint8_t ch_num = i + dma_cfg->header.base_channel;
		uint32_t mask;

		mask = ch->output_enable_mask;
		/* READ/STORE triggers */
		for (j = 0; j < 7; j++) {
			fw_cfg->output_enable[j] |=
				(((mask >> 2 * j) & 1U) << ch_num);
			fw_cfg->output_enable[j] |=
				(((mask >> (2 * j + 1)) & 1U)
				 << (ch_num + 16U));
		}

		/* VPU config trigger */
		fw_cfg->output_enable[7] |= (((mask >> 14) & 1U) << ch_num);
		/* HWSEQ tirgger */
		fw_cfg->output_enable[8] |= (((mask >> 15) & 1U) << ch_num);
		fw_cfg->output_enable[8] |=
			(((mask >> 16) & 1U) << (ch_num + 16U));

		if (mask != 0) {
			trigger_required = true;
		}
	}

	if (trigger_required) {
		pva_dma_resource_map_add_triggers(dma_resource_map);
	}
}

void pva_kmd_write_fw_dma_config(struct pva_dma_config const *dma_cfg,
				 void *fw_dma_config,
				 uint32_t *out_fw_fetch_size,
				 bool support_hwseq_frame_linking)
{
	struct pva_dma_config_resource *hdr;
	struct pva_fw_dma_channel *fw_channels;
	struct pva_fw_dma_descriptor *fw_descs;
	struct pva_fw_dma_slot *fw_slots, *last_slot;
	struct pva_dma_resource_map *dma_resource_map;
	uint32_t *hwseq_words;
	uintptr_t offset;
	uint32_t i;

	hdr = fw_dma_config;
	hdr->base_channel = dma_cfg->header.base_channel;
	hdr->base_descriptor = dma_cfg->header.base_descriptor;
	hdr->base_hwseq_word = dma_cfg->header.base_hwseq_word;
	hdr->num_channels = dma_cfg->header.num_channels;
	hdr->num_descriptors = dma_cfg->header.num_descriptors;
	hdr->num_hwseq_words = dma_cfg->header.num_hwseq_words;
	hdr->vpu_exec_resource_id = dma_cfg->header.vpu_exec_resource_id;
	hdr->num_dynamic_slots = dma_cfg->header.num_dynamic_slots;

	dma_resource_map = &hdr->dma_resource_map;
	pva_dma_resource_map_reset(dma_resource_map);
	pva_dma_resource_map_add_channels(dma_resource_map,
					  dma_cfg->header.base_channel,
					  dma_cfg->header.num_channels);
	pva_dma_resource_map_add_descriptors(dma_resource_map,
					     dma_cfg->header.base_descriptor,
					     dma_cfg->header.num_descriptors);
	pva_dma_resource_map_add_hwseq_words(dma_resource_map,
					     dma_cfg->header.base_hwseq_word,
					     dma_cfg->header.num_hwseq_words);

	offset = sizeof(*hdr);
	fw_slots = pva_offset_pointer(fw_dma_config, offset);

	if (hdr->num_dynamic_slots > 0) {
		last_slot = &fw_slots[hdr->num_dynamic_slots - 1];

		hdr->num_relocs = safe_addu16(last_slot->reloc_start_idx,
					      last_slot->reloc_count);
		/* Round of the number of relocs to satisfy alignment requirement */
		hdr->num_relocs = safe_pow2_roundup_u16(hdr->num_relocs, 2U);

		offset += sizeof(struct pva_fw_dma_slot) *
				  hdr->num_dynamic_slots +
			  sizeof(struct pva_fw_dma_reloc) * hdr->num_relocs;
	} else {
		hdr->num_relocs = 0;
	}

	fw_channels = pva_offset_pointer(fw_dma_config, offset);
	offset += sizeof(*fw_channels) * hdr->num_channels;

	fw_descs = pva_offset_pointer(fw_dma_config, offset);
	offset += sizeof(*fw_descs) * hdr->num_descriptors;

	/* Do not include fields beyond descriptors as they are not fetched to
	 * TCM */
	*out_fw_fetch_size = offset;

	for (i = 0; i < hdr->num_channels; i++) {
		write_dma_channel(&dma_cfg->channels[i],
				  dma_cfg->header.base_descriptor,
				  &fw_channels[i], dma_resource_map,
				  support_hwseq_frame_linking);
	}

	for (i = 0; i < dma_cfg->header.num_descriptors; i++) {
		if (pva_is_reserved_desc(i)) {
			// skip over the reserved descriptor range
			i = PVA_RESERVED_DESCRIPTORS_END;
			continue;
		}
		write_dma_descriptor(&dma_cfg->descriptors[i], &fw_descs[i]);
	}

	write_triggers(dma_cfg, fw_dma_config, dma_resource_map);

	hwseq_words = pva_offset_pointer(fw_dma_config, offset);

	memcpy(hwseq_words, dma_cfg->hwseq_words,
	       sizeof(*hwseq_words) * hdr->num_hwseq_words);

	/*TODO: write hdr->common_config for hwseq and MISR*/
}
