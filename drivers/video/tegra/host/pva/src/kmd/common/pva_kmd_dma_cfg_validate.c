// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_resource_table.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_hwseq_validate.h"
#include "pva_api.h"
#include "pva_kmd_dma_cfg.h"
#include "pva_api_dma.h"
#include "pva_kmd_device.h"
#include "pva_math_utils.h"
#include "pva_utils.h"
#include "pva_kmd_limits.h"

struct pva_fw_dma_reloc_slot_info {
	struct pva_fw_dma_slot *slots;
	struct pva_fw_dma_reloc *relocs;
	uint16_t num_slots;
	uint8_t *reloc_off;
};
struct pva_fw_dma_reloc_slots {
	struct pva_fw_dma_reloc_slot_info dyn_slot;
	struct pva_fw_dma_reloc_slot_info static_slot;
};

static enum pva_error check_replication(struct pva_dma_config const *out_cfg,
					struct pva_dma_channel const *channel)
{
	enum pva_error err = PVA_SUCCESS;
	switch (channel->ch_rep_factor) {
	case (uint8_t)REPLICATION_NONE:
	case (uint8_t)REPLICATION_FULL:
		break;
	default: {
		pva_kmd_log_err("Invalid Channel Replication Factor");
		err = PVA_INVAL;
	} break;
	}

	return err;
}

static enum pva_error
validate_channel_mapping(struct pva_dma_config const *out_cfg,
			 struct pva_kmd_hw_constants const *hw_consts)
{
	const struct pva_dma_channel *channel;
	struct pva_dma_config_header const *cfg_hdr = &out_cfg->header;
	pva_math_error math_err = MATH_OP_SUCCESS;
	enum pva_error err = PVA_SUCCESS;

	for (uint8_t i = 0U; i < cfg_hdr->num_channels; i++) {
		uint8_t desc_id;

		channel = &out_cfg->channels[i];
		desc_id = safe_addu8(channel->desc_index,
				     out_cfg->header.base_descriptor);
		if ((channel->desc_index >= out_cfg->header.num_descriptors) ||
		    (pva_is_reserved_desc(desc_id))) {
			pva_kmd_log_err(
				"ERR: Invalid Channel Descriptor Index");
			return PVA_INVAL;
		}
		if (addu8(channel->vdb_count, channel->vdb_offset, &math_err) >
		    PVA_NUM_DYNAMIC_VDB_BUFFS) {
			pva_kmd_log_err("ERR: Invalid Channel control data");
			return PVA_INVAL;
		}
		if (addu16(channel->adb_count, channel->adb_offset, &math_err) >
		    hw_consts->n_dynamic_adb_buffs) {
			pva_kmd_log_err("ERR: Invalid ADB Buff Size or Offset");
			return PVA_INVAL;
		}
		err = check_replication(out_cfg, channel);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Invalid Channel Replication Factor");
			return err;
		}
	}
	if (math_err != MATH_OP_SUCCESS) {
		pva_kmd_log_err("validate_channel_mapping math error");
		return PVA_ERR_MATH_OP;
	}

	return PVA_SUCCESS;
}

static enum pva_error validate_padding(const struct pva_dma_descriptor *desc)
{
	if ((desc->px != 0U) && (desc->px >= desc->tx)) {
		return PVA_INVAL;
	}

	if ((desc->py != 0U) && (desc->py >= desc->ty)) {
		return PVA_INVAL;
	}

	return PVA_SUCCESS;
}

static bool is_valid_vpu_trigger_mode(const struct pva_dma_descriptor *desc)
{
	bool valid = true;
	if (desc->trig_event_mode != 0U) {
		switch (desc->trig_vpu_events) {
		case (uint8_t)PVA_DMA_NO_TRIG:
			//HW Sequencer check
			break;
		case (uint8_t)PVA_DMA_TRIG_VPU_CFG:
			if (desc->src.transfer_mode !=
			    (uint8_t)PVA_DMA_TRANS_MODE_VPUCFG) {
				valid = false;
			}
			break;
		case (uint8_t)PVA_DMA_TRIG_READ0:
		case (uint8_t)PVA_DMA_TRIG_READ1:
		case (uint8_t)PVA_DMA_TRIG_READ2:
		case (uint8_t)PVA_DMA_TRIG_READ3:
		case (uint8_t)PVA_DMA_TRIG_READ4:
		case (uint8_t)PVA_DMA_TRIG_READ5:
		case (uint8_t)PVA_DMA_TRIG_READ6:
			if ((desc->src.transfer_mode !=
			     (uint8_t)PVA_DMA_TRANS_MODE_VPUCFG) &&
			    (desc->dst.transfer_mode !=
			     (uint8_t)PVA_DMA_TRANS_MODE_VMEM)) {
				valid = false;
			}
			break;
		case (uint8_t)PVA_DMA_TRIG_WRITE0:
		case (uint8_t)PVA_DMA_TRIG_WRITE1:
		case (uint8_t)PVA_DMA_TRIG_WRITE2:
		case (uint8_t)PVA_DMA_TRIG_WRITE3:
		case (uint8_t)PVA_DMA_TRIG_WRITE4:
		case (uint8_t)PVA_DMA_TRIG_WRITE5:
		case (uint8_t)PVA_DMA_TRIG_WRITE6:
			if ((desc->src.transfer_mode !=
			     (uint8_t)PVA_DMA_TRANS_MODE_VPUCFG) &&
			    (desc->src.transfer_mode !=
			     (uint8_t)PVA_DMA_TRANS_MODE_VMEM)) {
				valid = false;
			}
			break;
		default:
			valid = false;
			break;
		}
	}
	return valid;
}

static bool validate_src_dst_adv_val(const struct pva_dma_descriptor *desc,
				     bool relax_dim3_check)
{
	uint8_t is_any_rpt_zero = 0U;

	is_any_rpt_zero = desc->src.rpt1 & desc->src.rpt2 & desc->dst.rpt1 &
			  desc->dst.rpt2;

	if ((desc->trig_event_mode == (uint8_t)PVA_DMA_TRIG_MODE_4TH_DIM) &&
	    (is_any_rpt_zero == 0U)) {
		return false;
	}

	if (desc->trig_event_mode == ((uint8_t)PVA_DMA_TRIG_MODE_3RD_DIM)) {
		if (false == relax_dim3_check) {
			if (((desc->src.rpt1 == 0U) &&
			     (desc->dst.rpt1 == 0U))) {
				return false;
			}
		} else {
			if (((desc->dst.rpt1 == 0U) ||
			     (desc->src.rpt1 > desc->dst.rpt1))) {
				return false;
			}
		}
	}

	return true;
}

/**
 * @brief Validate DMA descriptor transfer control modes
 *
 * @details This function validates the source and destination transfer modes
 * for a DMA descriptor to ensure they form a valid combination according to
 * hardware constraints. It performs the following operations:
 * - Validates source transfer mode (VMEM, DRAM, L2SRAM, VPUCFG)
 * - For VPUCFG sources, skips destination validation (VPUCFG is special case)
 * - For non-VPUCFG sources, validates destination transfer mode
 * - Enforces MMIO destination only with VPUCFG source
 * - Restricts TCM destination to test mode only
 * - Ensures transfer mode combinations are hardware-compatible
 *
 * Valid transfer mode combinations:
 * - VMEM/DRAM/L2SRAM -> VMEM/DRAM/L2SRAM (standard transfers)
 * - VPUCFG -> Any (VPU configuration transfers - validated separately)
 * - Non-VPUCFG -> MMIO (rejected as invalid)
 * - Any -> TCM (only in test mode)
 *
 * @param[in] desc       Pointer to DMA descriptor to validate
 *                       Valid value: non-null, properly initialized descriptor
 *
 * @retval PVA_SUCCESS  Transfer modes validated successfully
 * @retval PVA_INVAL    Invalid transfer mode or unsupported combination
 */
static enum pva_error
validate_dma_desc_trans_cntl0(const struct pva_dma_descriptor *desc)
{
	enum pva_error err = PVA_SUCCESS;
	bool valid_src = false;

	/* Validate source transfer mode */
	switch (desc->src.transfer_mode) {
	case (uint8_t)PVA_DMA_TRANS_MODE_VMEM:
		valid_src = true;
		break;
	case (uint8_t)PVA_DMA_TRANS_MODE_DRAM:
		valid_src = true;
		break;
	case (uint8_t)PVA_DMA_TRANS_MODE_L2SRAM:
		valid_src = true;
		break;
	case (uint8_t)PVA_DMA_TRANS_MODE_VPUCFG:
		valid_src = true;
		break;
	default:
		valid_src = false;
		break;
	}

	if (!valid_src) {
		err = PVA_INVAL;
	}

	/* For VPUCFG source, skip destination validation (early exit) */
	if ((err != PVA_SUCCESS) ||
	    (desc->src.transfer_mode == (uint8_t)PVA_DMA_TRANS_MODE_VPUCFG)) {
		/* Function exit point for VPUCFG or error cases */
	} else {
		/* Validate destination transfer mode for non-VPUCFG sources */
		switch (desc->dst.transfer_mode) {
		case (uint8_t)PVA_DMA_TRANS_MODE_L2SRAM:
			err = PVA_SUCCESS;
			break;
		case (uint8_t)PVA_DMA_TRANS_MODE_VMEM:
			err = PVA_SUCCESS;
			break;
		case (uint8_t)PVA_DMA_TRANS_MODE_DRAM:
			err = PVA_SUCCESS;
			break;
		case (uint8_t)PVA_DMA_TRANS_MODE_MMIO:
			/* MMIO only valid with VPUCFG source (already filtered out) */
			err = PVA_INVAL;
			break;
		default:
			err = PVA_INVAL;
			break;
		}
	}
	return err;
}

static enum pva_error
validate_dma_desc_trans_cntl2(const struct pva_dma_descriptor *desc)
{
	if ((desc->prefetch_enable != 0U) &&
	    ((desc->tx == 0U) || (desc->ty == 0U) ||
	     (desc->src.transfer_mode != (uint32_t)PVA_DMA_TRANS_MODE_DRAM) ||
	     (desc->dst.transfer_mode != (uint32_t)PVA_DMA_TRANS_MODE_VMEM))) {
		return PVA_INVAL;
	}
	return PVA_SUCCESS;
}

static enum pva_error
validate_descriptor(const struct pva_dma_descriptor *desc,
		    struct pva_dma_config_header const *cfg_hdr,
		    bool relax_dim3_check)
{
	enum pva_error err = PVA_SUCCESS;

	err = validate_padding(desc);
	if ((desc->dst.transfer_mode == (uint8_t)PVA_DMA_TRANS_MODE_VMEM) &&
	    (err != PVA_SUCCESS)) {
		return err;
	}

	if (!(is_valid_vpu_trigger_mode(desc))) {
		pva_kmd_log_err("Bad trigger");
		return PVA_INVAL;
	}

	/** Check src/dstADV values with respect to ECET bits */
	if (false == validate_src_dst_adv_val(desc, relax_dim3_check)) {
		pva_kmd_log_err(
			"Invalid src/dst ADV values with respect to ECET");
		return PVA_INVAL;
	}

	if (PVA_SUCCESS != validate_dma_desc_trans_cntl0(desc)) {
		pva_kmd_log_err("Bad trans cntl 0");
		return PVA_INVAL;
	}
	/* DMA_DESC_TRANS CNTL2 */
	if (PVA_SUCCESS != validate_dma_desc_trans_cntl2(desc)) {
		pva_kmd_log_err("Bad trans cntl 2");
		return PVA_INVAL;
	}

	/* DMA_DESC_LDID */
	if ((desc->link_desc_id - cfg_hdr->base_descriptor >
	     cfg_hdr->num_descriptors) ||
	    ((desc->link_desc_id != 0U) &&
	     pva_is_reserved_desc((uint32_t)desc->link_desc_id -
				  (uint32_t)PVA_DMA_DESC_ID_BASE))) {
		pva_kmd_log_err("ERR: Invalid linker Desc ID");
		return PVA_INVAL;
	}

	return PVA_SUCCESS;
}

struct pva_kmd_offset_pairs {
	uint32_t start;
	uint32_t end;
};

#define PVA_KMD_DMA_CONFIG_ARRAY_COUNT 4U

static bool
validate_dma_config_bounds(struct pva_dma_config_header const *cfg_hdr,
			   struct pva_kmd_hw_constants const *hw_consts)
{
	bool is_valid = true;

	if ((((uint32_t)cfg_hdr->base_descriptor +
	      (uint32_t)cfg_hdr->num_descriptors) >
	     hw_consts->n_dma_descriptors) ||
	    (((uint32_t)cfg_hdr->base_channel +
	      (uint32_t)cfg_hdr->num_channels) >
	     (hw_consts->n_user_dma_channels + 1U)) ||
	    (((uint32_t)cfg_hdr->base_hwseq_word +
	      (uint32_t)cfg_hdr->num_hwseq_words) > hw_consts->n_hwseq_words) ||
	    (cfg_hdr->num_static_slots > PVA_KMD_MAX_NUM_DMA_SLOTS) ||
	    (cfg_hdr->num_dynamic_slots > PVA_KMD_MAX_NUM_DMA_RELOCS) ||
	    (cfg_hdr->base_channel == 0U)) {
		is_valid = false;
	}

	return is_valid;
}

static bool validate_dma_offsets(struct pva_kmd_offset_pairs const *offsets,
				 uint32_t dma_config_size)
{
	bool is_valid = true;
	uint32_t i;

	//Validate:
	// 1. All start offsets are aligned to 8 bytes
	// 2. All end offsets are within the dma_config_size
	// Note: We do not check if the ranges overlap because we do not modify the buffer in place.
	for (i = 0; i < PVA_KMD_DMA_CONFIG_ARRAY_COUNT; i++) {
		if ((offsets[i].start % 8U) != 0U) {
			pva_kmd_log_err(
				"DMA config field offset is not aligned to 8 bytes");
			is_valid = false;
			goto out;
		}
		if (offsets[i].end > dma_config_size) {
			pva_kmd_log_err("DMA config field is out of bounds");
			is_valid = false;
			goto out;
		}
	}

out:
	return is_valid;
}

static bool
is_dma_config_header_valid(struct pva_ops_dma_config_register const *ops_hdr,
			   uint32_t dma_config_size,
			   struct pva_kmd_hw_constants const *hw_consts)
{
	struct pva_kmd_offset_pairs offsets[PVA_KMD_DMA_CONFIG_ARRAY_COUNT];
	struct pva_dma_config_header const *cfg_hdr;
	pva_math_error math_err = MATH_OP_SUCCESS;
	bool is_valid = true;

	if (dma_config_size < sizeof(*ops_hdr)) {
		pva_kmd_log_err("DMA configuration too small");
		is_valid = false;
		goto out;
	}

	cfg_hdr = &ops_hdr->dma_config_header;

	if (!validate_dma_config_bounds(cfg_hdr, hw_consts)) {
		is_valid = false;
		goto out;
	}

	offsets[0].start = ops_hdr->channels_offset;
	offsets[0].end = addu32(
		ops_hdr->channels_offset,
		align8_u32(mulu32(cfg_hdr->num_channels,
				  sizeof(struct pva_dma_channel), &math_err),
			   &math_err),
		&math_err);

	offsets[1].start = ops_hdr->descriptors_offset;
	offsets[1].end = addu32(
		ops_hdr->descriptors_offset,
		align8_u32(mulu32(cfg_hdr->num_descriptors,
				  sizeof(struct pva_dma_descriptor), &math_err),
			   &math_err),
		&math_err);

	offsets[2].start = ops_hdr->hwseq_words_offset;
	offsets[2].end = addu32(ops_hdr->hwseq_words_offset,
				align8_u32(mulu32(cfg_hdr->num_hwseq_words,
						  sizeof(uint32_t), &math_err),
					   &math_err),
				&math_err);

	offsets[3].start = ops_hdr->static_bindings_offset;
	offsets[3].end =
		addu32(ops_hdr->static_bindings_offset,
		       align8_u32(mulu32(cfg_hdr->num_static_slots,
					 sizeof(struct pva_dma_static_binding),
					 &math_err),
				  &math_err),
		       &math_err);

	if (math_err != MATH_OP_SUCCESS) {
		pva_kmd_log_err("DMA config field offset math error");
		is_valid = false;
		goto out;
	}

	if (!validate_dma_offsets(offsets, dma_config_size)) {
		is_valid = false;
	}

out:
	return is_valid;
}

enum pva_error pva_kmd_parse_dma_config(
	const struct pva_ops_dma_config_register *ops_hdr,
	uint32_t dma_config_size, struct pva_dma_config *out_cfg,
	struct pva_kmd_hw_constants const *hw_consts, bool skip_validation)
{
	if (!skip_validation) { // Skip validation for PFSD and test mode
		if (!(is_dma_config_header_valid(ops_hdr, dma_config_size,
						 hw_consts))) {
			pva_kmd_log_err("Invalid PVA DMA Configuration Header");
			return PVA_INVAL;
		}
	}

	out_cfg->header = ops_hdr->dma_config_header;

	out_cfg->hwseq_words =
		pva_offset_const_ptr(ops_hdr, ops_hdr->hwseq_words_offset);

	out_cfg->channels =
		pva_offset_const_ptr(ops_hdr, ops_hdr->channels_offset);

	out_cfg->descriptors =
		pva_offset_const_ptr(ops_hdr, ops_hdr->descriptors_offset);

	out_cfg->static_bindings =
		pva_offset_const_ptr(ops_hdr, ops_hdr->static_bindings_offset);

	return PVA_SUCCESS;
}

static enum pva_error
validate_descriptors(const struct pva_dma_config *dma_config,
		     uint64_t const *hw_dma_descs_mask)
{
	uint8_t i = 0U;
	enum pva_error err = PVA_SUCCESS;
	const struct pva_dma_config_header *cfg_hdr = &dma_config->header;
	const struct pva_dma_descriptor *desc;
	bool relax_dim3_check = true;

	for (i = 0U; i < cfg_hdr->num_descriptors; i++) {
		uint8_t desc_id =
			safe_addu8((uint8_t)i, cfg_hdr->base_descriptor);

		if (pva_is_reserved_desc(desc_id)) {
			// skip over the reserved descriptor range
			i = safe_subu8(PVA_RESERVED_DESCRIPTORS_END,
				       dma_config->header.base_descriptor);
			continue;
		}

		relax_dim3_check =
			((hw_dma_descs_mask[desc_id / 64ULL] &
			  (1ULL << (desc_id & MAX_DESC_ID))) != 0ULL);

		desc = &dma_config->descriptors[i];
		err = validate_descriptor(desc, cfg_hdr, relax_dim3_check);
		if (err != PVA_SUCCESS) {
			return err;
		}
	}

	return err;
}

enum pva_error
pva_kmd_validate_dma_config(struct pva_dma_config const *dma_cfg,
			    struct pva_kmd_hw_constants const *hw_consts,
			    struct pva_kmd_dma_access *access_sizes,
			    uint64_t *hw_dma_descs_mask)
{
	enum pva_error err = PVA_SUCCESS;

	err = validate_channel_mapping(dma_cfg, hw_consts);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Bad Channels");
		return err;
	}

	if (dma_cfg->header.num_hwseq_words != 0U) {
		err = validate_hwseq(dma_cfg, hw_consts, access_sizes,
				     hw_dma_descs_mask);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Bad HW Sequencer Blob");
			return err;
		}
	}

	err = validate_descriptors(dma_cfg, hw_dma_descs_mask);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Bad Descriptors");
		return err;
	}

	return err;
}

static enum pva_error
use_vpu_bin_resource(struct pva_dma_config const *dma_cfg,
		     struct pva_kmd_dma_resource_aux *dma_aux,
		     struct pva_kmd_vpu_bin_resource **vpu_bin)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_resource_record *vpu_bin_rec;

	*vpu_bin = NULL;

	if (dma_cfg->header.vpu_exec_resource_id == PVA_RESOURCE_ID_INVALID) {
		goto out;
	}

	vpu_bin_rec = pva_kmd_use_resource_unsafe(
		dma_aux->res_table, dma_cfg->header.vpu_exec_resource_id);
	if (vpu_bin_rec == NULL) {
		pva_kmd_log_err(
			"VPU exec resource id used by DMA config does not exist");
		err = PVA_INVAL;
		goto out;
	}

	dma_aux->vpu_bin_res_id = dma_cfg->header.vpu_exec_resource_id;

	if (vpu_bin_rec->type != PVA_RESOURCE_TYPE_EXEC_BIN) {
		pva_kmd_log_err(
			"Invalid VPU exec resource id used by DMA config");
		err = PVA_INVAL;
		pva_kmd_drop_resource_unsafe(dma_aux->res_table,
					     dma_aux->vpu_bin_res_id);
		dma_aux->vpu_bin_res_id = PVA_RESOURCE_ID_INVALID;
		goto out;
	}

	*vpu_bin = &vpu_bin_rec->vpu_bin;

out:
	return err;
}

static enum pva_error
process_static_dram_binding(struct pva_kmd_dma_resource_aux *dma_aux,
			    struct pva_dma_static_binding const *slot_buf)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_resource_record *rec;

	rec = pva_kmd_use_resource_unsafe(dma_aux->res_table,
					  slot_buf->dram.resource_id);
	if (rec == NULL) {
		pva_kmd_log_err("DRAM buffers used by DMA config do not exist");
		err = PVA_INVAL;
		goto out;
	}

	dma_aux->static_dram_res_ids[dma_aux->dram_res_count] =
		slot_buf->dram.resource_id;
	dma_aux->dram_res_count = safe_addu32(dma_aux->dram_res_count, 1U);

	if (rec->type != PVA_RESOURCE_TYPE_DRAM) {
		pva_kmd_log_err("Invalid DRAM resource id used DMA config");
		err = PVA_INVAL;
	}

out:
	return err;
}

static enum pva_error
process_static_vmem_binding(struct pva_kmd_vpu_bin_resource *vpu_bin,
			    struct pva_dma_static_binding const *slot_buf)
{
	enum pva_error err = PVA_SUCCESS;

	if (vpu_bin == NULL) {
		pva_kmd_log_err(
			"VPU bin resource not found for static VMEM buffer");
		err = PVA_INVAL;
		goto out;
	}

	if (pva_kmd_get_symbol(&vpu_bin->symbol_table,
			       slot_buf->vmem.addr.symbol_id) == NULL) {
		pva_kmd_log_err("Invalid VMEM symbol ID");
		err = PVA_INVAL;
	}

out:
	return err;
}

enum pva_error
pva_kmd_dma_use_resources(struct pva_dma_config const *dma_cfg,
			  struct pva_kmd_dma_resource_aux *dma_aux)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_vpu_bin_resource *vpu_bin = NULL;
	uint32_t i;

	/* Increment reference count for VPU bin */
	err = use_vpu_bin_resource(dma_cfg, dma_aux, &vpu_bin);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	/* Increment reference count for all static DRAM buffers; For static
	 * VMEM buffers, check that symbol ID is valid. */
	for (i = 0; i < dma_cfg->header.num_static_slots; i++) {
		struct pva_dma_static_binding const *slot_buf =
			&dma_cfg->static_bindings[i];

		if (slot_buf->type == (uint8_t)PVA_DMA_STATIC_BINDING_DRAM) {
			err = process_static_dram_binding(dma_aux, slot_buf);
			if (err != PVA_SUCCESS) {
				goto drop_resources;
			}
		} else if (slot_buf->type ==
			   (uint8_t)PVA_DMA_STATIC_BINDING_VMEM) {
			err = process_static_vmem_binding(vpu_bin, slot_buf);
			if (err != PVA_SUCCESS) {
				goto drop_resources;
			}
		} else {
			pva_kmd_log_err("Invalid slot buffer type");
			err = PVA_INVAL;
			goto drop_resources;
		}
	}

	return PVA_SUCCESS;
drop_resources:
	for (i = 0; i < dma_aux->dram_res_count; i++) {
		pva_kmd_drop_resource_unsafe(dma_aux->res_table,
					     dma_aux->static_dram_res_ids[i]);
	}
	/* Drop VPU bin if it was acquired */
	if (dma_aux->vpu_bin_res_id != PVA_RESOURCE_ID_INVALID) {
		pva_kmd_drop_resource_unsafe(dma_aux->res_table,
					     dma_aux->vpu_bin_res_id);
	}
err_out:
	return err;
}

static uint16_t get_slot_id(uint16_t slot)
{
	return slot & PVA_DMA_SLOT_ID_MASK;
}

static uint16_t get_slot_flag(uint8_t transfer_mode, bool cb_enable,
			      bool is_dst)
{
	uint16_t flags = 0;
	if (transfer_mode == (uint8_t)PVA_DMA_TRANS_MODE_VMEM) {
		flags |= PVA_FW_DMA_SLOT_FLAG_VMEM_DATA;
	} else if (transfer_mode == (uint8_t)PVA_DMA_TRANS_MODE_L2SRAM) {
		flags |= PVA_FW_DMA_SLOT_FLAG_L2SRAM;
	} else if (transfer_mode == (uint8_t)PVA_DMA_TRANS_MODE_DRAM) {
		flags |= PVA_FW_DMA_SLOT_FLAG_DRAM;
	} else if (transfer_mode == (uint8_t)PVA_DMA_TRANS_MODE_VPUCFG) {
		flags |= PVA_FW_DMA_SLOT_FLAG_VMEM_VPUC_TABLE;
	}

	if (cb_enable) {
		flags |= PVA_FW_DMA_SLOT_FLAG_CB;
	}
	if (is_dst) {
		flags |= PVA_INSERT((uint32_t)PVA_ACCESS_WO,
				    PVA_FW_DMA_SLOT_FLAG_ACCESS_MSB,
				    PVA_FW_DMA_SLOT_FLAG_ACCESS_LSB);
	} else {
		flags |= PVA_INSERT((uint32_t)PVA_ACCESS_RO,
				    PVA_FW_DMA_SLOT_FLAG_ACCESS_MSB,
				    PVA_FW_DMA_SLOT_FLAG_ACCESS_LSB);
	}
	return flags;
}

static void update_reloc_count(uint16_t slot, uint8_t transfer_mode,
			       bool cb_enable,
			       struct pva_fw_dma_slot *out_static_slots,
			       uint16_t num_static_slots,
			       struct pva_fw_dma_slot *out_dyn_slots,
			       uint16_t num_dyn_slots, bool is_dst)
{
	uint8_t slot_id = get_slot_id(slot);

	if ((slot & PVA_DMA_DYNAMIC_SLOT) != 0U) {
		out_dyn_slots[slot_id].reloc_count =
			safe_addu16(out_dyn_slots[slot_id].reloc_count, 1U);
		out_dyn_slots[slot_id].flags |=
			get_slot_flag(transfer_mode, cb_enable, is_dst);
	} else if ((slot & PVA_DMA_STATIC_SLOT) != 0U) {
		out_static_slots[slot_id].reloc_count =
			safe_addu16(out_static_slots[slot_id].reloc_count, 1U);
		out_static_slots[slot_id].flags |=
			get_slot_flag(transfer_mode, cb_enable, is_dst);
	}
}

static void count_relocs(struct pva_dma_config const *dma_cfg,
			 struct pva_fw_dma_slot *out_static_slots,
			 uint16_t num_static_slots,
			 struct pva_fw_dma_slot *out_dyn_slots,
			 uint16_t num_dyn_slots)
{
	uint8_t i;
	const struct pva_dma_descriptor *desc;

	for (i = 0U; i < dma_cfg->header.num_descriptors; i++) {
		if (pva_is_reserved_desc(i + dma_cfg->header.base_descriptor)) {
			// skip over the reserved descriptor range
			i = PVA_RESERVED_DESCRIPTORS_END -
			    dma_cfg->header.base_descriptor;
			continue;
		}
		desc = &dma_cfg->descriptors[i];

		update_reloc_count(desc->src.slot, desc->src.transfer_mode,
				   desc->src.cb_enable, out_static_slots,
				   num_static_slots, out_dyn_slots,
				   num_dyn_slots, false);

		update_reloc_count(desc->dst.slot, desc->dst.transfer_mode,
				   desc->dst.cb_enable, out_static_slots,
				   num_static_slots, out_dyn_slots,
				   num_dyn_slots, true);

		update_reloc_count(desc->dst2_slot, desc->dst.transfer_mode,
				   desc->dst.cb_enable, out_static_slots,
				   num_static_slots, out_dyn_slots,
				   num_dyn_slots, true);
	}
}

static void write_one_reloc(uint8_t ch_index, uint32_t desc_index,
			    uint16_t slot, uint8_t transfer_mode,
			    uint8_t reloc_field,
			    struct pva_fw_dma_reloc_slot_info *info,
			    struct pva_kmd_dma_access_entry const *access_entry)
{
	uint16_t slot_id = get_slot_id(slot);
	uint16_t reloc_id = safe_addu16(info->slots[slot_id].reloc_start_idx,
					info->reloc_off[slot_id]);
	int64_t old_start_addr = info->slots[slot_id].start_addr;
	int64_t old_end_addr = info->slots[slot_id].end_addr;
	uint32_t shift_amount = (uint32_t)ch_index & 0x0FU;
	uint32_t shift_result = 1U << shift_amount;
	uint16_t ch_mask_u16;
	uint16_t new_mask;

	info->slots[slot_id].start_addr =
		mins64(access_entry->start_addr, old_start_addr);
	info->slots[slot_id].end_addr =
		maxs64(access_entry->end_addr, old_end_addr);

	ASSERT(shift_result <= U16_MAX);
	ch_mask_u16 = (uint16_t)shift_result;
	new_mask = info->slots[slot_id].ch_use_mask | ch_mask_u16;
	info->slots[slot_id].ch_use_mask = new_mask;

	/* desc_index field is uint8_t - validated by DMA config validation */
	ASSERT(desc_index <= U8_MAX);
	info->relocs[reloc_id].desc_index = (uint8_t)desc_index;
	info->relocs[reloc_id].field = reloc_field;
	info->reloc_off[slot_id] = safe_addu8(info->reloc_off[slot_id], 1U);
}

static void handle_reloc(uint16_t slot, uint8_t transfer_mode,
			 struct pva_kmd_dma_access_entry const *access_entry,
			 struct pva_fw_dma_reloc_slots *rel_info,
			 uint8_t reloc_field, uint8_t ch_index,
			 uint8_t desc_index)
{
	if ((slot & PVA_DMA_DYNAMIC_SLOT) != 0U) {
		write_one_reloc(ch_index, desc_index, slot, transfer_mode,
				reloc_field, &rel_info->dyn_slot, access_entry);
	} else if ((slot & PVA_DMA_STATIC_SLOT) != 0U) {
		write_one_reloc(ch_index, desc_index, slot, transfer_mode,
				reloc_field, &rel_info->static_slot,
				access_entry);
	}
}

static void write_relocs(const struct pva_dma_config *dma_cfg,
			 struct pva_kmd_dma_access const *access_sizes,
			 struct pva_fw_dma_reloc_slots *rel_info,
			 uint8_t const *desc_to_ch)
{
	uint8_t i;
	uint16_t start_idx = 0U;
	const struct pva_dma_descriptor *desc = NULL;
	uint8_t ch_index = 0U;
	for (i = 0U; i < rel_info->dyn_slot.num_slots; i++) {
		rel_info->dyn_slot.slots[i].reloc_start_idx = start_idx;
		start_idx = safe_addu16(
			start_idx, rel_info->dyn_slot.slots[i].reloc_count);
	}

	for (i = 0U; i < rel_info->static_slot.num_slots; i++) {
		rel_info->static_slot.slots[i].reloc_start_idx = start_idx;
		start_idx = safe_addu16(
			start_idx, rel_info->static_slot.slots[i].reloc_count);
	}

	for (i = 0U; i < dma_cfg->header.num_descriptors; i++) {
		if (pva_is_reserved_desc(
			    safe_addu8(i, dma_cfg->header.base_descriptor))) {
			// skip over the reserved descriptor range
			i = safe_subu8(PVA_RESERVED_DESCRIPTORS_END,
				       dma_cfg->header.base_descriptor);
			continue;
		}
		desc = &dma_cfg->descriptors[i];
		ch_index = desc_to_ch[i];

		handle_reloc(desc->src.slot, desc->src.transfer_mode,
			     &access_sizes[i].src, rel_info,
			     PVA_FW_DMA_RELOC_FIELD_SRC, ch_index, i);
		handle_reloc(desc->dst.slot, desc->dst.transfer_mode,
			     &access_sizes[i].dst, rel_info,
			     PVA_FW_DMA_RELOC_FIELD_DST, ch_index, i);
		handle_reloc(desc->dst2_slot, desc->dst.transfer_mode,
			     &access_sizes[i].dst2, rel_info,
			     PVA_FW_DMA_RELOC_FIELD_DST2, ch_index, i);
	}
}

static enum pva_error
validate_descriptor_tile_and_padding(const struct pva_dma_descriptor *desc,
				     bool is_dst)
{
	enum pva_error err = PVA_SUCCESS;

	if (desc->ty == 0U) {
		err = PVA_INVALID_DMA_CONFIG;
		return err;
	}

	if (!is_dst) {
		if ((desc->tx <= desc->px) || (desc->ty <= desc->py)) {
			// invalid tile size/padding config
			err = PVA_INVALID_DMA_CONFIG;
			return err;
		}
	}

	return PVA_SUCCESS;
}

static enum pva_error get_access_size(const struct pva_dma_descriptor *desc,
				      struct pva_kmd_dma_access_entry *entry,
				      bool is_dst,
				      struct pva_kmd_dma_access_entry *dst2)

{
	const struct pva_dma_transfer_attr *attr = NULL;
	uint32_t tx = 0U;
	uint32_t ty = 0U;
	uint64_t tile_size = 0U;
	int64_t start = 0;
	int64_t end = 0;
	int32_t dim_offset = 0;
	uint32_t dim_offset_U = 0U;
	uint32_t num_bytes = 0U;
	int64_t offset_to_add = 0;
	enum pva_error err = PVA_SUCCESS;
	pva_math_error math_err = MATH_OP_SUCCESS;

	// early out for empty tiles
	if (desc->tx == 0U) {
		return err;
	}

	err = validate_descriptor_tile_and_padding(desc, is_dst);
	if (err != PVA_SUCCESS) {
		return err;
	}

	if (is_dst) {
		attr = &desc->dst;
		tx = desc->tx;
		ty = desc->ty;
	} else {
		attr = &desc->src;
		tx = subu32((uint32_t)desc->tx, (uint32_t)desc->px, &math_err);
		ty = subu32((uint32_t)desc->ty, (uint32_t)desc->py, &math_err);
	}

	if (attr->offset > (uint64_t)(MAX_INT64)) {
		err = PVA_INVALID_DMA_CONFIG;
		pva_kmd_log_err("Offset is too large");
		goto err_out;
	}
	offset_to_add = convert_to_signed_s64(attr->offset);

	dim_offset_U = mulu32((uint32_t)(attr->line_pitch),
			      subu32(ty, 1U, &math_err), &math_err);

	if (attr->cb_enable != 0U) {
		tile_size = addu32(dim_offset_U, tx, &math_err);
		tile_size = tile_size
			    << (desc->log2_pixel_size & MAX_BYTES_PER_PIXEL);

		if (tile_size > attr->cb_size) {
			pva_kmd_log_err(
				"Tile size is bigger than circular buffer size");
			err = PVA_INVALID_DMA_CONFIG;
		}
		start = 0LL;
		end = (int64_t)attr->cb_size;
		offset_to_add = 0;
		goto end;
	}

	end += adds64((int64_t)dim_offset_U, (int64_t)tx, &math_err);

	// 3rd dim
	dim_offset = muls32((attr->adv1), (int32_t)(attr->rpt1), &math_err);
	start += mins32(dim_offset, 0);
	end += maxs32(dim_offset, 0);

	// 4th dim
	dim_offset = muls32((attr->adv2), (int32_t)(attr->rpt2), &math_err);
	start += mins32(dim_offset, 0);
	end += maxs32(dim_offset, 0);

	// 5th dim
	dim_offset = muls32((attr->adv3), (int32_t)(attr->rpt3), &math_err);
	start += mins32(dim_offset, 0);
	end += maxs32(dim_offset, 0);
	// convert to byte range
	num_bytes =
		((uint32_t)1U << (desc->log2_pixel_size & MAX_BYTES_PER_PIXEL));
	start *= (int64_t)num_bytes;
	end *= (int64_t)num_bytes;

	if (math_err != MATH_OP_SUCCESS) {
		err = PVA_ERR_MATH_OP;
		pva_kmd_log_err("get_access_size math error");
		goto err_out;
	}

end:
	entry->start_addr =
		adds64(mins64(start, end), offset_to_add, &math_err);
	entry->end_addr = adds64(maxs64(start, end), offset_to_add, &math_err);

	if (is_dst) {
		dst2->start_addr =
			adds64(mins64(start, end), (int64_t)desc->dst2_offset,
			       &math_err);

		dst2->end_addr = adds64(maxs64(start, end),
					(int64_t)desc->dst2_offset, &math_err);
	}
	if (math_err != MATH_OP_SUCCESS) {
		err = PVA_ERR_MATH_OP;
		pva_kmd_log_err("get_access_size math error");
	}
err_out:
	return err;
}

enum pva_error
pva_kmd_compute_dma_access(struct pva_dma_config const *dma_cfg,
			   struct pva_kmd_dma_access *access_sizes,
			   uint64_t *hw_dma_descs_mask)
{
	uint8_t i;
	const struct pva_dma_descriptor *desc = NULL;
	enum pva_error err = PVA_SUCCESS;
	bool skip_swseq_size_compute = false;

	for (i = 0U; i < dma_cfg->header.num_descriptors; i++) {
		/**
		 * Check if DMA descriptor has been used in HW Sequencer.
		 * If used, skip_swseq_size_compute = true
		 * else skip_swseq_size_compute = false
		 *
		 * If skip_swseq_size_compute == true then set access_sizes to 0
		 * else go ahead with access_sizes calculation.access_sizes
		 */
		skip_swseq_size_compute = ((hw_dma_descs_mask[i / 64ULL] &
					    (1ULL << (i & 0x3FU))) == 1U);
		if (pva_is_reserved_desc(
			    safe_addu8(i, dma_cfg->header.base_descriptor))) {
			// skip over the reserved descriptor range
			i = safe_subu8(PVA_RESERVED_DESCRIPTORS_END,
				       dma_cfg->header.base_descriptor);
			continue;
		}

		if (skip_swseq_size_compute == true) {
			continue;
		}

		desc = &dma_cfg->descriptors[i];

		//Calculate src_size
		err = get_access_size(desc, &access_sizes[i].src, false,
				      &access_sizes[i].dst2);
		if (err != PVA_SUCCESS) {
			goto out;
		}

		//Calculate dst_size
		err = get_access_size(desc, &access_sizes[i].dst, true,
				      &access_sizes[i].dst2);
		if (err != PVA_SUCCESS) {
			goto out;
		}
	}

out:
	return err;
}

void pva_kmd_collect_relocs(struct pva_dma_config const *dma_cfg,
			    struct pva_kmd_dma_access const *access_sizes,
			    struct pva_fw_dma_slot *out_static_slots,
			    uint16_t num_static_slots,
			    struct pva_fw_dma_reloc *out_static_relocs,
			    struct pva_fw_dma_slot *out_dyn_slots,
			    uint16_t num_dyn_slots,
			    struct pva_fw_dma_reloc *out_dyn_relocs,
			    uint8_t const *desc_to_ch)
{
	struct pva_fw_dma_reloc_slots rel_info = { 0 };
	uint8_t static_reloc_off[PVA_MAX_NUM_DMA_DESC * 3];
	uint8_t dyn_reloc_off[PVA_MAX_NUM_DMA_DESC * 3];

	/* First pass: count the number of relocates for each slot */
	count_relocs(dma_cfg, out_static_slots, num_static_slots, out_dyn_slots,
		     num_dyn_slots);

	(void)memset(static_reloc_off, 0U, sizeof(static_reloc_off));
	(void)memset(dyn_reloc_off, 0U, sizeof(dyn_reloc_off));

	rel_info.dyn_slot.slots = out_dyn_slots;
	rel_info.dyn_slot.relocs = out_dyn_relocs;
	rel_info.dyn_slot.num_slots = num_dyn_slots;
	rel_info.dyn_slot.reloc_off = dyn_reloc_off;

	rel_info.static_slot.slots = out_static_slots;
	rel_info.static_slot.relocs = out_static_relocs;
	rel_info.static_slot.num_slots = num_static_slots;
	rel_info.static_slot.reloc_off = static_reloc_off;

	/* Second pass: write reloc info */
	write_relocs(dma_cfg, access_sizes, &rel_info, desc_to_ch);
}
