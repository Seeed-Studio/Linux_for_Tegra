// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_dma_cfg.h"
#include "pva_utils.h"
#include "pva_kmd_resource_table.h"
#include "pva_kmd_device.h"

#define PVA_KMD_INVALID_CH_IDX 0xFF

void pva_kmd_unload_dma_config_unsafe(struct pva_kmd_dma_resource_aux *dma_aux)
{
	uint32_t i;

	for (i = 0; i < dma_aux->dram_res_count; i++) {
		pva_kmd_drop_resource_unsafe(dma_aux->res_table,
					     dma_aux->static_dram_res_ids[i]);
	}

	if (dma_aux->vpu_bin_res_id != PVA_RESOURCE_ID_INVALID) {
		pva_kmd_drop_resource_unsafe(dma_aux->res_table,
					     dma_aux->vpu_bin_res_id);
	}
}

static void trace_dma_channels(struct pva_dma_config const *dma_config,
			       uint8_t *desc_to_ch)
{
	uint32_t ch_index;
	const struct pva_dma_config_header *cfg_hdr = &dma_config->header;
	const struct pva_dma_channel *channel;
	uint32_t num_descs = dma_config->header.num_descriptors;

	for (ch_index = 0; ch_index < cfg_hdr->num_channels; ch_index++) {
		uint8_t desc_index;

		channel = &dma_config->channels[ch_index];
		desc_index = channel->desc_index;
		for (uint32_t i = 0; i < PVA_MAX_NUM_DMA_DESC; i++) {
			desc_index = array_index_nospec(desc_index, num_descs);
			if (desc_to_ch[desc_index] != PVA_KMD_INVALID_CH_IDX) {
				//Already traced this descriptor
				break;
			}
			desc_to_ch[desc_index] = ch_index;
			desc_index = sat_sub8(
				dma_config->descriptors[desc_index].link_desc_id,
				1);
		}
	}
}

enum pva_error
pva_kmd_load_dma_config(struct pva_kmd_resource_table *resource_table,
			const struct pva_ops_dma_config_register *dma_cfg_hdr,
			uint32_t dma_config_size,
			struct pva_kmd_dma_resource_aux *dma_aux,
			void *fw_dma_cfg, uint32_t *out_fw_fetch_size)
{
	enum pva_error err = PVA_SUCCESS;
	uint32_t fw_fetch_size;
	struct pva_dma_config dma_config;
	struct pva_fw_dma_slot *dyn_slots;
	struct pva_fw_dma_reloc *dyn_relocs;
	struct pva_kmd_dma_scratch_buffer *scratch_buf;
	// Mapping descriptor index to channel index
	uint8_t desc_to_ch[PVA_MAX_NUM_DMA_DESC];

	scratch_buf = pva_kmd_zalloc(sizeof(*scratch_buf));
	if (scratch_buf == NULL) {
		err = PVA_NOMEM;
		goto err_out;
	}

	for (uint32_t i = 0; i < PVA_MAX_NUM_DMA_DESC; i++) {
		desc_to_ch[i] = PVA_KMD_INVALID_CH_IDX;
	}

	err = pva_kmd_parse_dma_config(dma_cfg_hdr, dma_config_size,
				       &dma_config,
				       &resource_table->pva->hw_consts);
	if (err != PVA_SUCCESS) {
		goto free_scratch_buf;
	}

	err = pva_kmd_validate_dma_config(&dma_config,
					  &resource_table->pva->hw_consts,
					  scratch_buf->access_sizes,
					  scratch_buf->hw_dma_descs_mask);
	if (err != PVA_SUCCESS) {
		goto free_scratch_buf;
	}

	trace_dma_channels(&dma_config, desc_to_ch);

	err = pva_kmd_compute_dma_access(&dma_config, scratch_buf->access_sizes,
					 scratch_buf->hw_dma_descs_mask);
	if (err != PVA_SUCCESS) {
		goto free_scratch_buf;
	}

	dyn_slots = pva_offset_pointer(fw_dma_cfg,
				       sizeof(struct pva_dma_config_resource));

	dyn_relocs = pva_offset_pointer(dyn_slots,
					dma_config.header.num_dynamic_slots *
						sizeof(*dyn_slots));

	pva_kmd_collect_relocs(&dma_config, scratch_buf->access_sizes,
			       scratch_buf->static_slots,
			       dma_config.header.num_static_slots,
			       scratch_buf->static_relocs, dyn_slots,
			       dma_config.header.num_dynamic_slots, dyn_relocs,
			       desc_to_ch);

	pva_kmd_write_fw_dma_config(
		&dma_config, fw_dma_cfg, &fw_fetch_size,
		resource_table->pva->support_hwseq_frame_linking);

	err = pva_kmd_dma_use_resources(&dma_config, dma_aux);
	if (err != PVA_SUCCESS) {
		goto free_scratch_buf;
	}

	err = pva_kmd_bind_static_buffers(
		fw_dma_cfg, dma_aux, scratch_buf->static_slots,
		dma_config.header.num_static_slots, scratch_buf->static_relocs,
		dma_config.static_bindings, dma_config.header.num_static_slots);
	if (err != PVA_SUCCESS) {
		goto drop_res;
	}

	*out_fw_fetch_size = fw_fetch_size;

	pva_kmd_free(scratch_buf);
	return PVA_SUCCESS;
drop_res:
	pva_kmd_unload_dma_config_unsafe(dma_aux);
free_scratch_buf:
	pva_kmd_free(scratch_buf);
err_out:
	return err;
}
