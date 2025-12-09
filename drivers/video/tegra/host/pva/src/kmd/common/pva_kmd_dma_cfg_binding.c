// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_resource_table.h"
#include "pva_kmd_device_memory.h"
#include "pva_api.h"
#include "pva_kmd_dma_cfg.h"
#include "pva_api_dma.h"
#include "pva_kmd_constants.h"
#include "pva_kmd_device.h"

/* DMA channels per engine (matches PVA_NUM_DMA_CHANNELS in pva_fw_constants.h) */
#define PVA_KMD_DMA_CHANNELS_PER_ENGINE 16U

static uint32_t get_slot_line_pitch(struct pva_fw_dma_descriptor *descs,
				    struct pva_fw_dma_reloc const *relocs,
				    struct pva_fw_dma_slot const *slot)
{
	struct pva_fw_dma_reloc const *reloc = &relocs[slot->reloc_start_idx];
	uint32_t first_desc_index = reloc->desc_index;
	struct pva_fw_dma_descriptor *first_desc = &descs[first_desc_index];
	uint8_t log2_bpp =
		PVA_EXTRACT(first_desc->transfer_control1, 1, 0, uint8_t);

	if (reloc->field == PVA_FW_DMA_RELOC_FIELD_SRC) {
		/* MISRA 10.8, INT31-C: Cast and shift in separate steps */
		uint16_t slp_adv_u16 = (uint16_t)first_desc->slp_adv;
		uint32_t result = (uint32_t)slp_adv_u16 << log2_bpp;
		return result;
	} else {
		/* MISRA 10.8, INT31-C: Cast and shift in separate steps */
		uint16_t dlp_adv_u16 = (uint16_t)first_desc->dlp_adv;
		uint32_t result = (uint32_t)dlp_adv_u16 << log2_bpp;
		return result;
	}
}

static enum pva_error
set_channel_block_height(struct pva_dma_config_resource *dma_config,
			 uint16_t ch_mask, uint8_t log2_block_height)
{
	struct pva_fw_dma_channel *channels =
		pva_dma_config_get_channels(dma_config);

	// max block height is 32 GOB
	if (log2_block_height > PVA_DMA_MAX_LOG2_BLOCK_HEIGHT) {
		pva_kmd_log_err("Invalid block height");
		return PVA_ERR_CMD_INVALID_BLOCK_HEIGHT;
	}

	while (ch_mask > 0U) {
		uint8_t ch_index = (uint8_t)__builtin_ctz(ch_mask);
		/* CERT INT34-C: ch_index from ctz(uint16_t) is guaranteed < 16 DMA channels */
		ASSERT(ch_index < PVA_KMD_DMA_CHANNELS_PER_ENGINE);
		if ((dma_config->ch_block_height_fixed_mask &
		     ((uint16_t)1U << ch_index)) != 0U) {
			/* If this bit is already set, it means block height cannot be changed.  */
			uint8_t set_bh = PVA_EXTRACT(channels[ch_index].cntl0,
						     27, 25, uint8_t);
			if (set_bh != log2_block_height) {
				pva_kmd_log_err("Conflicting block height");
				return PVA_INVAL;
			}
		} else {
			channels[ch_index].cntl0 &= ~PVA_MASK(27, 25);
			channels[ch_index].cntl0 |=
				PVA_INSERT(log2_block_height, 27, 25);

			dma_config->ch_block_height_fixed_mask |=
				((uint16_t)1U << ch_index);
		}

		ch_mask &= ~((uint16_t)1U << ch_index);
	}
	return PVA_SUCCESS;
}

static enum pva_error
validate_dram_slot_flags(struct pva_fw_dma_slot const *slot,
			 struct pva_kmd_dram_resource *dram_res)
{
	enum pva_error err = PVA_SUCCESS;
	uint8_t slot_access_flags =
		PVA_EXTRACT16(slot->flags, PVA_FW_DMA_SLOT_FLAG_ACCESS_MSB,
			      PVA_FW_DMA_SLOT_FLAG_ACCESS_LSB, uint8_t);

	if ((slot->flags & PVA_FW_DMA_SLOT_FLAG_DRAM) == 0U) {
		pva_kmd_log_err("Binding DRAM buffer to incompatible slot");
		err = PVA_INVALID_BINDING;
		goto out;
	}

	if ((slot_access_flags & dram_res->mem->iova_access_flags) !=
	    slot_access_flags) {
		pva_kmd_log_err(
			"DRAM buffer does not have the required access permissions");
		err = PVA_INVALID_BINDING;
	}

out:
	return err;
}

static enum pva_error
setup_block_linear_params(struct pva_dma_config_resource *dma_config,
			  struct pva_kmd_dma_resource_aux *dma_aux,
			  struct pva_fw_dma_slot const *slot,
			  struct pva_dma_dram_binding const *dram_bd,
			  uint64_t surface_base_addr, uint32_t line_pitch,
			  uint64_t *max_surface_size,
			  uint64_t *sector_pack_format)
{
	enum pva_error err = PVA_SUCCESS;
	pva_math_error math_error = MATH_OP_SUCCESS;

	if ((slot->flags & PVA_FW_DMA_SLOT_FLAG_CB) != 0U) {
		pva_kmd_log_err(
			"Block linear surface is not compatible with circular buffer");
		err = PVA_INVALID_BINDING;
		goto out;
	}

	*max_surface_size = pva_max_bl_surface_size(*max_surface_size,
						    dram_bd->log2_block_height,
						    line_pitch, &math_error);
	if (math_error != MATH_OP_SUCCESS) {
		pva_kmd_log_err(
			"bind_static_dram_slot pva_max_bl_surface_size triggered a math error");
		err = PVA_ERR_MATH_OP;
		goto out;
	}

	if (!pva_is_512B_aligned(surface_base_addr)) {
		pva_kmd_log_err("BL surface base address is not 512B aligned");
		err = PVA_BAD_SURFACE_BASE_ALIGNMENT;
		goto out;
	}

	err = set_channel_block_height(dma_config, slot->ch_use_mask,
				       dram_bd->log2_block_height);
	if (err != PVA_SUCCESS) {
		goto out;
	}
	*sector_pack_format = dma_aux->res_table->pva->bl_sector_pack_format;

out:
	return err;
}

static enum pva_error calculate_slot_offsets(
	struct pva_fw_dma_slot const *slot,
	struct pva_dma_dram_binding const *dram_bd, uint64_t max_surface_size,
	int64_t *slot_access_start_addr, int64_t *slot_access_end_addr,
	uint64_t *slot_surface_combined_offset,
	uint64_t *adjusted_max_surface_size, pva_math_error *math_error)
{
	enum pva_error err = PVA_SUCCESS;

	*slot_surface_combined_offset = addu64(
		dram_bd->slot_offset, dram_bd->surface_base_offset, math_error);

	if (*slot_surface_combined_offset >= (uint64_t)MAX_INT64) {
		pva_kmd_log_err("Slot surface offset too large");
		err = PVA_ERR_CMD_DRAM_BUF_OUT_OF_RANGE;
		goto out;
	}

	*slot_access_start_addr =
		adds64(slot->start_addr, (int64_t)*slot_surface_combined_offset,
		       math_error);

	*slot_access_end_addr =
		adds64(slot->end_addr, (int64_t)*slot_surface_combined_offset,
		       math_error);

	*adjusted_max_surface_size = addu64(
		max_surface_size, dram_bd->surface_base_offset, math_error);

out:
	return err;
}

static enum pva_error validate_slot_access_bounds(
	int64_t slot_access_start_addr, int64_t slot_access_end_addr,
	uint64_t adjusted_max_surface_size, pva_math_error math_error)
{
	enum pva_error err = PVA_SUCCESS;

	if (adjusted_max_surface_size >= (uint64_t)MAX_INT64) {
		pva_kmd_log_err(
			"bind_static_dram_slot: DRAM buffer too large for slot binding");
		err = PVA_ERR_CMD_DRAM_BUF_OUT_OF_RANGE;
		goto out;
	}

	if (math_error != MATH_OP_SUCCESS) {
		pva_kmd_log_err(
			"bind_static_dram_slot: Math error during slot binding");
		err = PVA_ERR_MATH_OP;
		goto out;
	}

	if (slot_access_start_addr < 0LL) {
		pva_kmd_log_err(
			"bind_static_dram_slot: DRAM buffer offset underflows for slot binding");
		err = PVA_ERR_CMD_DRAM_BUF_OUT_OF_RANGE;
		goto out;
	}

	if (slot_access_end_addr > (int64_t)adjusted_max_surface_size) {
		pva_kmd_log_err(
			"bind_static_dram_slot: DRAM buffer too small for slot binding");
		err = PVA_ERR_CMD_DRAM_BUF_OUT_OF_RANGE;
	}

out:
	return err;
}

static enum pva_error
process_slot_relocations(struct pva_dma_config_resource *dma_config,
			 struct pva_fw_dma_slot const *slot,
			 struct pva_fw_dma_reloc const *static_relocs,
			 struct pva_dma_dram_binding const *dram_bd,
			 uint64_t surface_base_addr, uint64_t slot_offset_pl,
			 uint64_t sector_pack_format, uint32_t line_pitch,
			 bool is_block_linear)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_fw_dma_descriptor *descs =
		pva_dma_config_get_descriptors(dma_config);
	struct pva_fw_dma_reloc const *relocs;
	pva_math_error math_error = MATH_OP_SUCCESS;

	relocs = &static_relocs[slot->reloc_start_idx];
	for (uint32_t i = 0; i < slot->reloc_count; i++) {
		struct pva_fw_dma_reloc const *reloc = &relocs[i];
		struct pva_fw_dma_descriptor *desc = &descs[reloc->desc_index];
		uint8_t *addr_hi_ptr;
		uint32_t *addr_lo_ptr;
		uint8_t format_field_shift = 0;
		uint64_t addr;
		uint64_t desc_offset_pl;
		uint64_t offset;

		if (reloc->field == PVA_FW_DMA_RELOC_FIELD_SRC) {
			addr_hi_ptr = &desc->src_adr1;
			addr_lo_ptr = &desc->src_adr0;
			format_field_shift = 3U; //SRC_TF in TRANSFER_CONTROL0
		} else if (reloc->field == PVA_FW_DMA_RELOC_FIELD_DST) {
			addr_hi_ptr = &desc->dst_adr1;
			addr_lo_ptr = &desc->dst_adr0;
			format_field_shift = 7U; //DST_TF in TRANSFER_CONTROL0
		} else { /* PVA_FW_DMA_RELOC_FIELD_DST2 */
			pva_kmd_log_err("Binding DRAM buffer to DST2 slot");
			err = PVA_INVAL;
			goto out;
		}
		desc_offset_pl = assemble_addr(*addr_hi_ptr, *addr_lo_ptr);
		offset = sat_add64(slot_offset_pl, desc_offset_pl);
		/* CERT INT31-C: Bitwise NOT of shifted value, result masked by uint8_t cast,
		 * safe as only lower 8 bits are used in bitwise AND */
		desc->transfer_control0 &=
			(uint8_t) ~(1U << format_field_shift);
		if (is_block_linear) {
			/* We need to insert bits surface_base_addr[13, 9] to
			* transfer_control2[7:3] as specified by DMA IAS. This helps the
			* HW identify starting GOB index inside a block. */
			desc->transfer_control2 &= ~PVA_MASK8(7U, 3U);
			desc->transfer_control2 |=
				PVA_INSERT8(PVA_EXTRACT64(surface_base_addr, 13,
							  9, uint8_t),
					    7, 3);
			desc->transfer_control0 |=
				(uint8_t)(1U << format_field_shift);

			offset = pva_pl_to_bl_offset(offset, line_pitch,
						     dram_bd->log2_block_height,
						     &math_error);
			if (math_error != MATH_OP_SUCCESS) {
				pva_kmd_log_err(
					"pva_fw_do_cmd_bind_dram_slot pva_pl_to_bl_offset triggered a math error");
				err = PVA_ERR_MATH_OP;
				goto out;
			}
			if (!pva_is_64B_aligned(offset)) {
				pva_kmd_log_err(
					"Descriptor starting address is not aligned to 64 bytes");
				err = PVA_BAD_DESC_ADDR_ALIGNMENT;
				goto out;
			}
		}
		addr = sat_add64(surface_base_addr, offset);
		addr |= (sector_pack_format << PVA_BL_SECTOR_PACK_BIT_SHIFT);
		*addr_hi_ptr = iova_hi(addr);
		*addr_lo_ptr = iova_lo(addr);
	}

out:
	return err;
}

static enum pva_error
bind_static_dram_slot(struct pva_dma_config_resource *dma_config,
		      struct pva_kmd_dma_resource_aux *dma_aux,
		      struct pva_fw_dma_slot const *slot,
		      struct pva_fw_dma_reloc const *static_relocs,
		      struct pva_dma_dram_binding const *dram_bd)
{
	struct pva_fw_dma_descriptor *descs =
		pva_dma_config_get_descriptors(dma_config);
	bool is_block_linear =
		(dram_bd->surface_format == (uint8_t)PVA_SURF_FMT_BLOCK_LINEAR);
	uint32_t line_pitch = get_slot_line_pitch(descs, static_relocs, slot);
	struct pva_kmd_resource_record *resource_record =
		pva_kmd_peek_resource(dma_aux->res_table, dram_bd->resource_id);
	struct pva_kmd_dram_resource *dram_res;
	uint64_t slot_offset_pl;
	uint64_t surface_base_addr;
	uint64_t max_surface_size;
	uint64_t sector_pack_format = 0;
	int64_t slot_access_start_addr = 0LL;
	int64_t slot_access_end_addr = 0LL;
	uint64_t slot_surface_combined_offset = 0ULL;
	uint64_t adjusted_max_surface_size = 0ULL;
	pva_math_error math_error = MATH_OP_SUCCESS;
	enum pva_error err = PVA_SUCCESS;

	if (resource_record == NULL) {
		return PVA_INVAL;
	}

	dram_res = &resource_record->dram;
	slot_offset_pl = dram_bd->slot_offset;
	surface_base_addr =
		sat_add64(dram_bd->surface_base_offset, dram_res->mem->iova);
	/* When binding a buffer, we add the binding->surface_base_offset to the
     * buffer base address. Therefore, the effective buffer size is
	 * reduced by the offset. */
	max_surface_size =
		sat_sub64(dram_res->mem->size, dram_bd->surface_base_offset);

	/* Validate slot flags and access permissions */
	err = validate_dram_slot_flags(slot, dram_res);
	if (err != PVA_SUCCESS) {
		goto out;
	}

	/* Handle block linear specific setup if needed */
	if (is_block_linear) {
		err = setup_block_linear_params(dma_config, dma_aux, slot,
						dram_bd, surface_base_addr,
						line_pitch, &max_surface_size,
						&sector_pack_format);
		if (err != PVA_SUCCESS) {
			goto out;
		}
	}

	/* Calculate and validate slot offsets and ranges */

	err = calculate_slot_offsets(slot, dram_bd, max_surface_size,
				     &slot_access_start_addr,
				     &slot_access_end_addr,
				     &slot_surface_combined_offset,
				     &adjusted_max_surface_size, &math_error);
	if (err != PVA_SUCCESS) {
		goto out;
	}

	err = validate_slot_access_bounds(slot_access_start_addr,
					  slot_access_end_addr,
					  adjusted_max_surface_size,
					  math_error);
	if (err != PVA_SUCCESS) {
		goto out;
	}

	err = process_slot_relocations(dma_config, slot, static_relocs, dram_bd,
				       surface_base_addr, slot_offset_pl,
				       sector_pack_format, line_pitch,
				       is_block_linear);

out:
	return err;
}

static enum pva_error
bind_static_vmem_slot(struct pva_dma_config_resource *dma_config,
		      struct pva_kmd_dma_resource_aux *dma_aux,
		      struct pva_fw_dma_slot const *slot,
		      struct pva_fw_dma_reloc const *static_relocs,
		      struct pva_dma_vmem_binding const *vmem_bd)
{
	struct pva_fw_dma_descriptor *descs =
		pva_dma_config_get_descriptors(dma_config);
	struct pva_kmd_vpu_bin_resource *vpu_bin;
	struct pva_symbol_info *sym;
	uint32_t buffer_size;
	uint32_t buffer_addr;
	struct pva_fw_dma_reloc const *relocs;
	enum pva_symbol_type needed_sym_type;
	struct pva_kmd_resource_record *vpu_res;
	uint32_t i;
	enum pva_error err = PVA_SUCCESS;

	if ((slot->flags & PVA_FW_DMA_SLOT_FLAG_VMEM_DATA) != 0U) {
		needed_sym_type = PVA_SYM_TYPE_DATA;
	} else if ((slot->flags & PVA_FW_DMA_SLOT_FLAG_VMEM_VPUC_TABLE) != 0U) {
		needed_sym_type = PVA_SYM_TYPE_VPUC_TABLE;
	} else {
		pva_kmd_log_err("Unexpected VMEM slot flags");
		err = PVA_INTERNAL;
		goto out;
	}

	vpu_res = pva_kmd_peek_resource(dma_aux->res_table,
					dma_aux->vpu_bin_res_id);

	if (vpu_res == NULL) {
		pva_kmd_log_err("Failed to get VPU resource");
		err = PVA_INVAL;
		goto out;
	}

	vpu_bin = &vpu_res->vpu_bin;
	sym = pva_kmd_get_symbol_with_type(&vpu_bin->symbol_table,
					   vmem_bd->addr.symbol_id,
					   needed_sym_type);
	if (sym == NULL) {
		err = PVA_INVALID_SYMBOL;
		goto out;
	}

	buffer_size = sat_sub32(sym->size, vmem_bd->addr.offset);
	buffer_addr = sat_add32(sym->vmem_addr, vmem_bd->addr.offset);

	if (buffer_size < get_slot_size(slot)) {
		pva_kmd_log_err("VMEM buffer too small for slot binding");
		err = PVA_RES_OUT_OF_RANGE;
		goto out;
	}

	relocs = &static_relocs[slot->reloc_start_idx];
	for (i = 0; i < slot->reloc_count; i++) {
		struct pva_fw_dma_reloc const *reloc = &relocs[i];
		struct pva_fw_dma_descriptor *desc = &descs[reloc->desc_index];

		if (reloc->field == PVA_FW_DMA_RELOC_FIELD_SRC) {
			desc->src_adr0 = sat_add32(buffer_addr, desc->src_adr0);
		} else if (reloc->field == PVA_FW_DMA_RELOC_FIELD_DST) {
			desc->dst_adr0 = sat_add32(buffer_addr, desc->dst_adr0);
		} else {
			if (!pva_is_64B_aligned(buffer_addr)) {
				pva_kmd_log_err(
					"VMEM replication address not aligned to 64 bytes");
				err = PVA_INVAL;
				goto out;
			}

			/* CERT INT31-C: buffer_addr >> 6 cast to uint16_t then masked with 0x7FFF.
		 * The uint16_t cast naturally truncates to 16 bits, then mask keeps 15 bits.
		 * This is intentional for VMEM address computation. */
			desc->frda =
				((uint16_t)(buffer_addr >> 6U) + desc->frda) &
				0x7FFFU;
		}
	}

out:
	return err;
}

enum pva_error pva_kmd_bind_static_buffers(
	struct pva_dma_config_resource *fw_dma_cfg_hdr,
	struct pva_kmd_dma_resource_aux *dma_aux,
	struct pva_fw_dma_slot const *static_slots, uint16_t num_static_slots,
	struct pva_fw_dma_reloc const *static_relocs,
	struct pva_dma_static_binding const *static_bindings,
	uint32_t num_static_bindings)
{
	uint32_t slot_id;
	enum pva_error err = PVA_SUCCESS;

	if (num_static_bindings != num_static_slots) {
		pva_kmd_log_err("Invalid number of static bindings");
		err = PVA_INVAL;
		goto out;
	}

	// Reset BL status for each channel
	fw_dma_cfg_hdr->ch_block_height_fixed_mask = 0U;

	for (slot_id = 0U; slot_id < num_static_slots; slot_id++) {
		struct pva_fw_dma_slot const *st_slot = &static_slots[slot_id];
		struct pva_dma_static_binding const *binding =
			&static_bindings[slot_id];

		if (binding->type == (uint8_t)PVA_DMA_STATIC_BINDING_DRAM) {
			err = bind_static_dram_slot(fw_dma_cfg_hdr, dma_aux,
						    st_slot, static_relocs,
						    &binding->dram);

		} else { // PVA_FW_DMA_SLOT_FLAG_VMEM
			err = bind_static_vmem_slot(fw_dma_cfg_hdr, dma_aux,
						    st_slot, static_relocs,
						    &binding->vmem);
		}

		if (err != PVA_SUCCESS) {
			goto out;
		}
	}

out:
	return err;
}
