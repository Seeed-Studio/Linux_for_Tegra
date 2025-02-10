/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#include "pva_kmd_dma_cfg.h"
#include "pva_utils.h"
#include "pva_kmd_resource_table.h"
#include "pva_kmd_device.h"

#define PVA_KMD_INVALID_CH_IDX 0xFF

void pva_kmd_unload_dma_config(struct pva_kmd_dma_resource_aux *dma_aux)
{
	uint32_t i;

	for (i = 0; i < dma_aux->dram_res_count; i++) {
		pva_kmd_drop_resource(dma_aux->res_table,
				      dma_aux->static_dram_res_ids[i]);
	}

	if (dma_aux->vpu_bin_res_id != PVA_RESOURCE_ID_INVALID) {
		pva_kmd_drop_resource(dma_aux->res_table,
				      dma_aux->vpu_bin_res_id);
	}
}

static void trace_dma_channels(struct pva_dma_config const *dma_config,
			       uint8_t *desc_to_ch)
{
	uint32_t ch_index;
	struct pva_dma_config_header const *cfg_hdr = &dma_config->header;
	struct pva_dma_channel *channel;
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
			void *dma_config_payload, uint32_t dma_config_size,
			struct pva_kmd_dma_resource_aux *dma_aux,
			void *fw_dma_cfg, uint32_t *out_fw_fetch_size)
{
	enum pva_error err = PVA_SUCCESS;
	uint32_t fw_fetch_size;
	struct pva_dma_config dma_config;
	struct pva_fw_dma_slot *dyn_slots;
	struct pva_fw_dma_reloc *dyn_relocs;
	struct pva_fw_dma_slot *static_slots = dma_aux->static_slots;
	struct pva_fw_dma_reloc *static_relocs = dma_aux->static_relocs;
	struct pva_kmd_dma_access *access_sizes = dma_aux->access_sizes;
	// Mapping descriptor index to channel index
	uint8_t desc_to_ch[PVA_MAX_NUM_DMA_DESC];

	for (uint32_t i = 0; i < PVA_MAX_NUM_DMA_DESC; i++) {
		desc_to_ch[i] = PVA_KMD_INVALID_CH_IDX;
	}

	//set access_sizes to 0 by default
	(void)memset(
		access_sizes, 0,
		(PVA_MAX_NUM_DMA_DESC * sizeof(struct pva_kmd_dma_access)));

	err = pva_kmd_parse_dma_config(dma_config_payload, dma_config_size,
				       &dma_config,
				       &resource_table->pva->hw_consts);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	err = pva_kmd_validate_dma_config(&dma_config,
					  &resource_table->pva->hw_consts,
					  access_sizes,
					  dma_aux->hw_dma_descs_mask);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	trace_dma_channels(&dma_config, desc_to_ch);

	err = pva_kmd_compute_dma_access(&dma_config, access_sizes,
					 dma_aux->hw_dma_descs_mask);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	dyn_slots = pva_offset_pointer(fw_dma_cfg,
				       sizeof(struct pva_dma_config_resource));

	dyn_relocs = pva_offset_pointer(dyn_slots,
					dma_config.header.num_dynamic_slots *
						sizeof(*dyn_slots));

	pva_kmd_collect_relocs(&dma_config, access_sizes, static_slots,
			       dma_config.header.num_static_slots,
			       static_relocs, dyn_slots,
			       dma_config.header.num_dynamic_slots, dyn_relocs,
			       desc_to_ch);

	pva_kmd_write_fw_dma_config(
		&dma_config, fw_dma_cfg, &fw_fetch_size,
		resource_table->pva->support_hwseq_frame_linking);

	dma_aux->res_table = resource_table;
	err = pva_kmd_dma_use_resources(&dma_config, dma_aux);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	err = pva_kmd_bind_static_buffers(fw_dma_cfg, dma_aux, static_slots,
					  dma_config.header.num_static_slots,
					  static_relocs,
					  dma_config.static_bindings,
					  dma_config.header.num_static_slots);
	if (err != PVA_SUCCESS) {
		goto drop_res;
	}

	*out_fw_fetch_size = fw_fetch_size;

	return PVA_SUCCESS;
drop_res:
	pva_kmd_unload_dma_config(dma_aux);
err_out:
	return err;
}
