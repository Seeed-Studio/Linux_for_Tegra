// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_hwseq_validate.h"
#include "pva_api_dma.h"
#include "pva_kmd_device.h"
//TODO: Use nv_speculate barrier
//#include "nv_speculation_barrier.h"

#define HWSEQ_MIN_WORDS 5U

static inline const void *read_hwseq_blob(struct pva_hwseq_buffer *buffer,
					  uint32_t num_bytes)
{
	const uint8_t *ret = NULL;
	if (num_bytes > buffer->bytes_left) {
		return NULL;
	}
	ret = buffer->data;
	buffer->data += num_bytes;
	buffer->bytes_left -= num_bytes;
	return (const void *)ret;
}

#if 0
// Debug code which can be removed later after HW Sequencer checks are done
static void print_hwseq_blob(const uint8_t *blob, uint16_t start_address,
			     uint16_t end_address)
{
	uint16_t i = 0;
	printf("  Start Address = %d	End Address = %d", start_address,
	       end_address);
	for (i = (start_address << 2U); i <= (end_address << 2U); i++) {
		printf("\nblob[%d] = %x", i, blob[i]);
	}
	printf("\n");
}
#endif
/**
 * \brief Validates the descriptor entry in HW Sequencer Blob
 *
 * This function ensures that the descriptor entry read from HW Sequencer Blob
 * passes the following checks:
 * - Non-NULL Descriptor Entry.
 * - Descriptor ID != 0 and Descriptor ID < num_descriptors.
 *
 * \param[in] desc_entry		A pointer to the desc_entry read from the HW Sequencer Blob.
 * \param[in] num_descriptors	Number of DMA Descriptors in the current DMA Config.
 *
 * \return
 * 	- PVA_SUCCESS if above checks pass.
 * 	- PVA_INVAL if any of the above checks fail.
 */
static enum pva_error
validate_desc_entry(struct pva_dma_hwseq_desc_entry const *desc_entry,
		    const uint8_t num_descriptors)
{
	if (desc_entry == NULL) {
		pva_kmd_log_err("Hwseq buffer too small");
		return PVA_INVAL;
	}

	if ((desc_entry->did == 0U) || (desc_entry->did > num_descriptors)) {
		pva_kmd_log_err("Invalid Descriptor ID found in HW Sequencer");
		return PVA_INVAL;
	}

	return PVA_SUCCESS;
}

static inline uint8_t get_head_desc_did(struct pva_hwseq_priv const *hwseq)
{
	return hwseq->dma_descs[0].did;
}

static inline enum pva_error check_adv_params(int32_t adv1, int32_t adv2,
					      int32_t adv3, uint8_t rpt1,
					      uint8_t rpt2, uint8_t rpt3,
					      bool has_dim3)
{
	if (!has_dim3 && ((adv1 != 0) || (adv2 != 0) || (adv3 != 0) ||
			  ((rpt1 + rpt2 + rpt3) != 0U))) {
		return PVA_INVAL;
	}
	return PVA_SUCCESS;
}

/**
 * \brief Validates advancement parameters of Head Descriptor of HW Sequencer
 *
 * This function validates the advancement paramters of the head descriptor of
 * HW Seqeuncer.
 * This checks that the advancement parameters for both source and destination of
 * DMA Transfer to be zero and the addition of all repetition parameters per transfer
 * mode add up to zero
 *
 * \param[in] attr		Pointer to a valid DMA Transfer Attributes
 * \param[in] has_dim3		Boolean to indicate if Tensor Data Flow is in use
 *							Range: False: TDF is not in use
 *									True: TDF is in use
 *
 * \return
 * 	- PVA_SUCCESS if above checks pass
 * 	- PVA_INVAL if above checks fail
 */
static inline enum pva_error
validate_adv_params(struct pva_dma_transfer_attr const *attr, bool has_dim3)
{
	enum pva_error err = PVA_SUCCESS;

	err = check_adv_params(attr->adv1, attr->adv2, attr->adv3, attr->rpt1,
			       attr->rpt2, attr->rpt3, has_dim3);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("descriptor source tile looping not allowed");
		return err;
	}
	if (attr->adv1 < 0) {
		pva_kmd_log_err(
			"source advance amount on dim1 can not be negative");
		return PVA_INVAL;
	}

	if ((attr->adv1 * ((int32_t)(attr->rpt1) + 1)) != attr->adv2) {
		pva_kmd_log_err(
			"Invalid source advance amount on dim1 or dim2");
		return PVA_INVAL;
	}
	return err;
}

static enum pva_error calculate_tx(uint32_t *tx,
				   const struct pva_hwseq_priv *hwseq)
{
	const struct pva_dma_descriptor *d0 =
		(hwseq->hdr->to >= 0) ? hwseq->head_desc : hwseq->tail_desc;
	const struct pva_dma_descriptor *d1 =
		(hwseq->hdr->to >= 0) ? hwseq->tail_desc : hwseq->head_desc;

	if (((d0->tx + hwseq->hdr->padl) > 0xFFFFU) ||
	    ((d1->tx + hwseq->hdr->padr) > 0xFFFFU)) {
		pva_kmd_log_err("Invalid Tx + Pad X in HW Sequencer");
		return PVA_INVAL;
	}
	*tx = maxu32(((uint32_t)(d0->tx) + hwseq->hdr->padl),
		     ((uint32_t)(d1->tx) + hwseq->hdr->padr));
	return PVA_SUCCESS;
}

static enum pva_error calculate_ty(uint32_t *ty,
				   const struct pva_hwseq_priv *hwseq)
{
	const struct pva_dma_descriptor *d0 =
		(hwseq->hdr->to >= 0) ? hwseq->head_desc : hwseq->tail_desc;
	const struct pva_dma_descriptor *d1 =
		(hwseq->hdr->to >= 0) ? hwseq->tail_desc : hwseq->head_desc;

	if (((d0->ty + hwseq->hdr->padt) > 0xFFFFU) ||
	    ((d1->ty + hwseq->hdr->padb) > 0xFFFFU)) {
		pva_kmd_log_err("Invalid Ty + Pad Y in HW Sequencer");
		return PVA_INVAL;
	}
	*ty = maxu32(((uint32_t)(d0->ty) + hwseq->hdr->padt),
		     ((uint32_t)(d1->ty) + hwseq->hdr->padb));
	return PVA_SUCCESS;
}

/**
 * \brief Validates the Tiles in Circular Buffer can be contained in the vmem
 *
 * This function returns error if  Circular Buffer size is more than the specified
 * vmem_size indicating that the Circular buffer cannot be contained in the VMEM
 * on DMA transfer to VMEM
 *
 * This function also calculates the tile size of the tile being addressed by the head
 * and tail descriptors of the HW Sequencer and ensures that the tile size lies within
 * the specified Circular Buffer Size
 *
 * \param[in] hwseq		A pointer to a populated struct \ref pva_hwseq_priv
 * \param[in] entry		Pointer to a valid DMA Access Entry
 * \param[in] has_dim3		Boolean to indicate if Tensor Data Flow is in use
 *							Range: False: TDF is not in use
 *									True: TDF is in use
 *
 * \return
 * 	- PVA_SUCCESS if above checks pass
 * 	- PVA_INVAL if any of the above checks fail
 *  - PVA_ERR_MATH_OP if any math operation fails
 */
static enum pva_error validate_cb_tiles(struct pva_hwseq_priv *hwseq,
					struct pva_kmd_dma_access_entry *entry,
					bool has_dim3)
{
	const struct pva_dma_descriptor *head_desc = hwseq->head_desc;
	const struct pva_dma_descriptor *tail_desc = hwseq->tail_desc;
	uint32_t tx = 0;
	uint32_t ty = 0;
	int64_t end_addr = 0LL;
	pva_math_error math_err = MATH_OP_SUCCESS;

	if (hwseq->is_split_padding) {
		if (hwseq->is_raster_scan) {
			ty = head_desc->ty;
			if (calculate_tx(&tx, hwseq) != PVA_SUCCESS) {
				return PVA_INVAL;
			}
		} else {
			tx = head_desc->tx;
			if (calculate_ty(&ty, hwseq) != PVA_SUCCESS) {
				return PVA_INVAL;
			}
		}
	} else {
		tx = maxu32(head_desc->tx, tail_desc->tx);
		ty = maxu32(head_desc->ty, tail_desc->ty);
	}

	end_addr =
		adds64(muls64((int64_t)head_desc->dst.line_pitch,
			      subs64((int64_t)ty, 1LL, &math_err), &math_err),
		       (int64_t)tx, &math_err);

	end_addr = muls64(end_addr,
			  convert_to_signed_s64(1ULL
						<< (head_desc->log2_pixel_size &
						    MAX_BYTES_PER_PIXEL)),
			  &math_err);
	entry->start_addr = mins64(end_addr, 0LL);
	entry->end_addr = maxs64(end_addr, 0LL);

	if (math_err != MATH_OP_SUCCESS) {
		pva_kmd_log_err("Math error in tile size calculation");
		return PVA_ERR_MATH_OP;
	}

	return PVA_SUCCESS;
}

/**
 * \brief This function check if advancement parameters in DMA Descriptor
 * 	  are set correctly for handling valid VMEM transfer
 *
 * This function ensures that if vmem_tile_count is more than 1, then advancement paramters
 * for the DMA Descriptor are set to 0 else error is returned
 *
 * If vmem is dst, then dst.adv parameters are to be set to 0
 * If vmem is src, then src.adv parameters are to be set to 0
 *
 * \param[in] attr		Pointer to a valid DMA Transfer Attributes
 * \param[in] vmem_tile_count	Number of VMEM tile present
 * \param[in] has_dim3		Boolean to indicate if Tensor Data Flow is in use
 *							Range: False: TDF is not in use
 *									True: TDF is in use
 *
 * \return
 * 	- PVA_SUCCESS if above checks are valid
 * 	- PVA_INVAL if any of the above checks fail
 */
static inline enum pva_error
check_vmem_setup(struct pva_dma_transfer_attr const *attr,
		 uint32_t vmem_tile_count, bool has_dim3)
{
	if ((!has_dim3) && (vmem_tile_count > 1U) &&
	    ((attr->adv1 != 0) || (attr->adv2 != 0) || (attr->adv3 != 0))) {
		return PVA_INVAL;
	}
	return PVA_SUCCESS;
}

/**
 * \brief Validates Transfer Modes allowed with HW Sequener
 *
 * This function checks if the transfer modes allowed for HW Sequencer as per
 * DMA IAS are set correctly in the DMA Descriptor
 * Allowed Transfer Modes:
 * ----------------------------------------------
 * |		Source		|		Destination		|
 * ----------------------------------------------
 * |		VMEM		|		MC/L2SRAM		|
 * ----------------------------------------------
 * |		MC/L2SRAM	|		VMEM			|
 * ----------------------------------------------
 *
 * \param[in] dma_desc		DMA Descriptor of type \ref nvpva_dma_descriptor
 *
 * \return
 * 	- PVA_SUCCESS if valid source/destination pair is found
 * 	- PVA_INVAL if invalid source/destination pair is found
 */
static enum pva_error
validate_xfer_mode(const struct pva_dma_descriptor *dma_desc)
{
	enum pva_error err = PVA_SUCCESS;

	switch (dma_desc->src.transfer_mode) {
	case (uint8_t)PVA_DMA_TRANS_MODE_VMEM:
		if (!((dma_desc->dst.transfer_mode ==
		       (uint8_t)PVA_DMA_TRANS_MODE_DRAM) ||
		      (dma_desc->dst.transfer_mode ==
		       (uint8_t)PVA_DMA_TRANS_MODE_L2SRAM)) ||
		    (dma_desc->dst.cb_enable == 1U)) {
			pva_kmd_log_err(
				"HWSequncer: Invalid dst.transfer_mode");
			err = PVA_INVAL;
		}
		break;
	case (uint8_t)PVA_DMA_TRANS_MODE_L2SRAM:
	case (uint8_t)PVA_DMA_TRANS_MODE_DRAM:
		if ((dma_desc->dst.transfer_mode !=
		     (uint8_t)PVA_DMA_TRANS_MODE_VMEM) ||
		    (dma_desc->src.cb_enable == 1U)) {
			/* Source or destination Circular Buffer mode should not be used for MC or L2
		in frame addressing mode due to rtl bug 3136383 */
			pva_kmd_log_err(
				"HW Sequencer: Invalid src.transfer_mode");
			err = PVA_INVAL;
		}
		break;
	default:
		err = PVA_INVAL;
		pva_kmd_log_err("Unreachable branch");
		break;
	}

	return err;
}

static inline uint32_t
get_vmem_tile_count(struct pva_dma_transfer_attr const *attr, bool has_dim3)
{
	uint32_t rpt1_plus_1 = 0U;
	uint32_t rpt2_plus_1 = 0U;
	uint32_t rpt3_plus_1 = 0U;
	uint32_t temp = 0U;

	if (has_dim3) {
		return ((uint32_t)attr->rpt3 + 1U);
	}

	// Calculate intermediate results first to avoid multiple evaluations
	rpt1_plus_1 = ((uint32_t)attr->rpt1 + 1U);
	rpt2_plus_1 = ((uint32_t)attr->rpt2 + 1U);
	rpt3_plus_1 = ((uint32_t)attr->rpt3 + 1U);

	// Perform multiplications sequentially with error checking
	temp = safe_mulu32(rpt1_plus_1, rpt2_plus_1);
	return safe_mulu32(temp, rpt3_plus_1);
}

/**
 * \brief Validate HW Sequencer for VMEM at Destination of DMA Transfer
 *
 * This function validates the DMA Configuration in the HW Seqeuncer
 * where VMEM is the destination of the DMA Transfer.
 *
 * The function does the following checks:
 * - Validate Transfer Mode src/dst pair.
 * - Validate advancement paramters.
 * - Obtain VMEM Size by getting the buffer size associated with the HW Sequencer DMA Descriptor
 * - If Circular Buffer is present, validate Circular Buffer can handle the vmem tiles.
 *   This is done by calling function \ref validate_cb_tiles
 * - If Circular Buffer is not present do the following checks
 * 	- Reject Split Padding if no Circular Buffer is used for the destination
 * 	- Check VMEM Setup for the Destination Mode
 * 	  This is done by calling check_vmem_setup
 * 	- Calculate end address of the DMA Transfer
 *    Populate the start and end address of the DMA Transfer in the DMA Access Entry
 *
 * \param[in] hwseq		A const structure of type \ref pva_hwseq_priv
 * \param[in] vmem_tile_count	Number of VMEM tiles
 * \param[in] has_dim3		Boolean to indicate if Tensor Data Flow is in use
 *							Range: False: TDF is not in use
 *									True: TDF is in use
 *
 * \return
 * 	- PVA_SUCCESS if above checks pass
 * 	- PVA_INVAL if any of the above checks fail
 *  - PVA_ERR_MATH_OP if any math operation fails
 */
static enum pva_error validate_dst_vmem(struct pva_hwseq_priv *hwseq,
					uint32_t *vmem_tile_count,
					bool has_dim3)
{
	enum pva_error err = PVA_SUCCESS;
	uint32_t tx = 0U;
	uint32_t ty = 0U;
	int64_t end_addr = 0LL;
	int64_t num_bytes = 0LL;
	int64_t offset = 0LL;
	const struct pva_dma_descriptor *head_desc = hwseq->head_desc;
	const struct pva_dma_descriptor *tail_desc = hwseq->tail_desc;
	uint8_t head_desc_id = get_head_desc_did(hwseq);
	pva_math_error math_err = MATH_OP_SUCCESS;

	num_bytes = convert_to_signed_s64(
		1ULL << (head_desc->log2_pixel_size & MAX_BYTES_PER_PIXEL));
	offset = convert_to_signed_s64(head_desc->dst.offset);

	*vmem_tile_count = get_vmem_tile_count(&head_desc->dst, has_dim3);

	err = validate_adv_params(&head_desc->src, has_dim3);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Descriptor source tile looping not allowed");
		return PVA_INVAL;
	}

	if (head_desc->dst.cb_enable != 0U) {
		err = validate_cb_tiles(hwseq,
					&hwseq->access_sizes[head_desc_id].dst,
					has_dim3);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"VMEM address range validation failed for dst vmem with cb");
			return PVA_INVAL;
		}
	} else {
		if (hwseq->is_split_padding) {
			pva_kmd_log_err(
				"Split padding not supported without circular buffer");
			return PVA_INVAL;
		}

		err = check_vmem_setup(&head_desc->dst, *vmem_tile_count,
				       has_dim3);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Invalid VMEM destination setup");
			return PVA_INVAL;
		}

		if (head_desc->src.adv1 < 0) {
			pva_kmd_log_err("Source adv1 can not be negative");
			return PVA_INVAL;
		}

		tx = maxu32(head_desc->tx, tail_desc->tx);
		ty = maxu32(head_desc->ty, tail_desc->ty);
		end_addr =
			muls64((int64_t)head_desc->dst.line_pitch,
			       subs64((int64_t)ty, 1LL, &math_err), &math_err);

		end_addr = adds64(end_addr, (int64_t)tx, &math_err);
		end_addr = adds64(muls64((int64_t)head_desc->src.rpt1,
					 head_desc->dst.adv1, &math_err),
				  end_addr, &math_err);

		end_addr = adds64(muls64(end_addr, num_bytes, &math_err),
				  offset, &math_err);

		hwseq->access_sizes[head_desc_id].dst.start_addr =
			mins64(end_addr, 0LL);
		hwseq->access_sizes[head_desc_id].dst.end_addr =
			maxs64(end_addr, 0LL);
	}
	if (math_err != MATH_OP_SUCCESS) {
		pva_kmd_log_err("Invalid VMEM destination setup");
		return PVA_ERR_MATH_OP;
	}

	return err;
}

/**
 * \brief Ensures no padding is set in the Header of the HW Sequencer
 *
 * This function ensures that no padding is present in the HW Sequencer Header
 * i.e. all padding values are 0
 * If any of the padding values are found to be not 0, an error is returned
 *
 * \param[in] header		A struct of type \ref pva_dma_hwseq_hdr_t
 * 				which is the HW Sequencer Header
 *
 * \return
 * 	- PVA_SUCCESS if all padding values in the header are zero
 * 	- PVA_INVAL if any of the padding values in the header is non-zero
 */
static inline enum pva_error
check_no_padding(const struct pva_dma_hwseq_hdr *header)
{
	if ((header->padl != 0U) || (header->padr != 0U) ||
	    (header->padt != 0U) || (header->padb != 0U)) {
		return PVA_INVAL;
	}
	return PVA_SUCCESS;
}

/**
 * \brief Validate HW Sequencer for VMEM at Source of DMA Transfer
 *
 * This function validates the DMA Configuration in the HW Seqeuncer
 * where VMEM is the Source of the DMA Transfer.
 *
 * The function does the following checks:
 * - Validate Transfer Mode src/dst pair.
 * - Validate advancement paramters.
 * - Check no padding in HW Sequencer header.
 * - Obtain VMEM Size by getting the buffer size associated with the HW Sequencer DMA Descriptor,
 * - Calculate tile_size of the memory transfer using head and tail descriptors in the HW Sequencer.
 * - If Circular Buffer is present:
 * 	- Validate Circular Buffer size is less than VMEM size previously obtained
 * 	- Validate tile_size obtained is less than Circular Buffer Size
 * - If Circular Buffer is not present do the following checks
 * 	- Check VMEM Setup for Source Mode
 * 	  This is done by calling check_vmem_setup
 * 	  Calculate end address of the DMA Transfer
 *    Populate the start and end address of the DMA Transfer in the DMA Access Entry
 *
 * \param[in] hwseq		A const structure of type \ref pva_hwseq_priv
 * \param[in] vmem_tile_count	Number of VMEM tiles
 * \param[in] has_dim3		Boolean to indicate if Tensor Data Flow is in use
 *							Range: False: TDF is not in use
 *									True: TDF is in use
 *
 * \return
 * 	- PVA_SUCCESS if above checks pass
 * 	- PVA_INVAL if any of the above checks fail
 *  - PVA_ERR_MATH_OP if any math operation fails
 */
static enum pva_error validate_src_vmem(struct pva_hwseq_priv *hwseq,
					uint32_t *vmem_tile_count,
					bool has_dim3)
{
	const struct pva_dma_descriptor *head_desc = hwseq->head_desc;
	const struct pva_dma_descriptor *tail_desc = hwseq->tail_desc;
	uint8_t head_desc_id = get_head_desc_did(hwseq);
	uint32_t tx = 0U;
	uint32_t ty = 0U;
	int64_t end_addr = 0LL;
	int64_t num_bytes = 0LL;
	int64_t offset = 0LL;
	enum pva_error err = PVA_SUCCESS;
	pva_math_error math_err = MATH_OP_SUCCESS;

	num_bytes = convert_to_signed_s64(
		1ULL << (head_desc->log2_pixel_size & MAX_BYTES_PER_PIXEL));
	offset = convert_to_signed_s64(head_desc->src.offset);

	*vmem_tile_count = get_vmem_tile_count(&head_desc->src, has_dim3);

	// make sure last 3 loop dimensions are not used
	err = validate_adv_params(&head_desc->dst, has_dim3);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Descriptor destination tile looping not allowed");
		return PVA_INVAL;
	}

	// since we don't support output padding, make sure hwseq program header has none
	err = check_no_padding(hwseq->hdr);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("invalid padding value in hwseq program");
		return PVA_INVAL;
	}

	tx = maxu32(head_desc->tx, tail_desc->tx);
	ty = maxu32(head_desc->ty, tail_desc->ty);

	end_addr =
		adds64(muls64((int64_t)head_desc->src.line_pitch,
			      subs64((int64_t)ty, 1LL, &math_err), &math_err),
		       (int64_t)tx, &math_err);

	if (0U != head_desc->src.cb_enable) {
		end_addr = muls64(end_addr, num_bytes, &math_err);
		hwseq->access_sizes[head_desc_id].src.start_addr =
			mins64(end_addr, 0LL);
		hwseq->access_sizes[head_desc_id].src.end_addr =
			maxs64(end_addr, 0LL);
	} else {
		err = check_vmem_setup(&head_desc->src, *vmem_tile_count,
				       has_dim3);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"Invalid VMEM Source setup in hw sequencer");
			return PVA_INVAL;
		}

		end_addr = adds64(muls64((int64_t)head_desc->dst.rpt1,
					 head_desc->src.adv1, &math_err),
				  end_addr, &math_err);

		end_addr = adds64(muls64(end_addr, num_bytes, &math_err),
				  offset, &math_err);

		hwseq->access_sizes[head_desc_id].src.start_addr =
			mins64(end_addr, 0LL);
		hwseq->access_sizes[head_desc_id].src.end_addr =
			maxs64(end_addr, 0LL);
	}
	if (math_err != MATH_OP_SUCCESS) {
		pva_kmd_log_err(
			"Math error in VMEM Source setup in hw sequencer");
		return PVA_ERR_MATH_OP;
	}

	return PVA_SUCCESS;
}

/**
 * \brief Validates if grid is large enough to support defined padding
 *
 * This function validates the following
 * 	- Valid Horizontal padding/tile count
 * 	  This is done by ensuring if pad_x[0] and pad_x[1] are > 0 then
 * 	  grid_size_x should be atleast of size 2
 * 	- Valid Vertical Padding/tile count
 * 	  This is done by ensuring if pad_y[0] and pad_y[1] are > 0 then
 * 	  grid_size_y should be atlest of size 1
 * 	  Also checked is tile_y == max(pad_y[0], pad_y[1])
 * 	- Constant tile height in raster mode
 * 	  This is done by ensuring tile_y[0] == tile_y[1]
 *
 * \param[in] gi		Populated grid info of type \ref struct pva_hwseq_grid_info
 *
 * \return
 * 	- PVA_SUCCESS if all the above checks pass
 * 	- PVA_INVAL if any of the above checks fail
 */
static enum pva_error
validate_grid_padding(struct pva_hwseq_grid_info const *gi)
{
	// make sure grid is large enough to support defined padding
	if ((gi->pad_x[0] > 0) && (gi->pad_x[1] > 0) &&
	    (gi->grid_size_x < 2U)) {
		pva_kmd_log_err("horizontal padding/tile count mismatch");
		return PVA_INVAL;
	}

	// validate vertical padding
	if (gi->tile_y[0] <= maxs32(gi->pad_y[0], gi->pad_y[1])) {
		pva_kmd_log_err("invalid vertical padding");
		return PVA_INVAL;
	}
	// make sure ty is fixed
	if (gi->tile_y[0] != gi->tile_y[1]) {
		pva_kmd_log_err(
			"tile height cannot change in raster-scan mode");
		return PVA_INVAL;
	}

	return PVA_SUCCESS;
}

/**
 * \brief Validate Horizontal padding
 *
 * Ensures that pad_start and pad_end are not larger than grid tile_x and grid tile_y
 * respectively
 *
 * \param[in] gi		A valid pointer to object of type \ref struct pva_hwseq_grid_info.
 * \param[in] pad_start		Start Horizontal padding value
 * \param[in] pad_end		End Horizontal Padding value
 *
 * \return
 * 	- PVA_SUCCESS if above checks pass
 * 	- PVA_INVAL if grid tile_x <= pad_start or grid tile_y <= pad_end
 */
static inline enum pva_error
validate_horizontal_padding(struct pva_hwseq_grid_info const *gi,
			    int32_t pad_start, int32_t pad_end)
{
	if ((gi->tile_x[0] <= pad_start) || (gi->tile_x[1] <= pad_end)) {
		return PVA_INVAL;
	}
	return PVA_SUCCESS;
}

/**
 * \brief This function generates frame_info from the grid info
 *
 * This function generates a frame_info of type \ref struct pva_hwseq_frame_info
 * from an input of type \ref struct pva_hwseq_grid_info.
 *
 * During conversion following checks are done:
 * - validate_grid_padding.
 * - get_grid_signed.
 * - If split_padding is specified:
 * 	- Reject overlapping tiles.
 * 	  This is done by ensuring left most tile x dimension
 * 	  is less than the grid step in x dimension.
 * - If split_padding is not specified:
 * 	- Ensure horizontal padding is valid.
 * 	  Function validate_horizontal_padding does this.
 *
 * \param[out] fi	A pointer to object of type \ref struct pva_hwseq_frame_info
 * 			which is populated with this function
 * \param[in] gi	A pointer to object of type \ref struct pva_hwseq_grid_info
 * 			whic is the input for the conversion
 *
 * \return
 * 	- PVA_SUCCESS if conversion is successfull and above checks pass
 * 	- PVA_INVAL if any of the above checks fail
 */
static enum pva_error compute_frame_info(struct pva_hwseq_frame_info *fi,
					 struct pva_hwseq_grid_info const *gi)
{
	int32_t dim_offset = 0;
	int32_t grid_size_x = 0;
	int32_t grid_size_y = 0;
	int32_t head_tile_count = 0;
	int32_t left_tile_x = 0;
	int32_t step_x = 0;
	int32_t pad_start = 0;
	int32_t pad_end = 0;
	int64_t alt_start_x = 0;
	int64_t alt_end_x = 0;
	pva_math_error math_err = MATH_OP_SUCCESS;

	if (validate_grid_padding(gi) != PVA_SUCCESS) {
		return PVA_INVAL;
	}

	grid_size_x = convert_to_signed_s32(gi->grid_size_x);
	grid_size_y = convert_to_signed_s32(gi->grid_size_y);
	head_tile_count = convert_to_signed_s32(gi->head_tile_count);

	if (gi->grid_step_x >= 0) {
		left_tile_x = gi->tile_x[0];
		pad_start = gi->pad_x[0];
		pad_end = gi->pad_x[1];
		step_x = gi->grid_step_x;
	} else {
		left_tile_x = gi->tile_x[1];
		pad_start = gi->pad_x[1];
		pad_end = gi->pad_x[0];
		step_x = ((-1) * gi->grid_step_x);
	}

	// update X span (partial)
	dim_offset = muls32(gi->grid_step_x, subs32(grid_size_x, 1, &math_err),
			    &math_err);

	fi->start_x = mins32(dim_offset, 0);
	fi->end_x = maxs32(dim_offset, 0);
	// update Y span (full)
	dim_offset = muls32(gi->grid_step_y, subs32(grid_size_y, 1, &math_err),
			    &math_err);

	fi->start_y = mins32(dim_offset, 0);

	fi->start_z = 0;

	if (gi->grid_step_y < 0) {
		// For reversed scans, when the padding is applied it will adjust the read offset
		fi->start_y += gi->pad_y[0];
	}
	fi->end_y = maxs32(dim_offset, 0);
	fi->end_y += ((int64_t)gi->tile_y[1] - gi->pad_y[0] - gi->pad_y[1]);

	fi->end_z = muls64(gi->tile_z, (int64_t)gi->grid_size_z, &math_err);

	if (gi->is_split_padding) {
		// update X span (final)
		fi->end_x += gi->tile_x[1];

		// disallow overlapping tiles
		if (left_tile_x > step_x) {
			pva_kmd_log_err(
				"sequencer horizontal jump offset smaller than tile width");
			return PVA_INVAL;
		}
	} else {
		// update X span (final)
		// remove padding since it's already included in tx in this mode
		fi->end_x +=
			((int64_t)gi->tile_x[1] - gi->pad_x[0] - gi->pad_x[1]);
		// validate horizontal padding
		// swap pad values if sequencing in reverse
		if (validate_horizontal_padding(gi, pad_start, pad_end) !=
		    PVA_SUCCESS) {
			pva_kmd_log_err("invalid horizontal padding");
			return PVA_INVAL;
		}
		// compute alternative span from 1st descriptor
		dim_offset = gi->grid_step_x * (head_tile_count - 1);
		alt_start_x = mins32(dim_offset, 0);
		if (gi->grid_step_x < 0) {
			// For reversed scans, when the padding is applied it will adjust the read offset
			fi->start_x += gi->pad_x[0];
			alt_start_x += gi->pad_x[0];
		}
		alt_end_x = maxs32(dim_offset, 0);
		alt_end_x += ((int64_t)gi->tile_x[0] - pad_start);
		if (gi->head_tile_count == gi->grid_size_x) {
			// if there is only a single tile configuration per grid row
			// then we should subtract padding at the end below since
			// repetitions of this single tile will include both pad at
			// start and end
			alt_end_x -= pad_end;
		}
		// pick the conservative span
		fi->start_x = mins64(alt_start_x, fi->start_x);
		fi->end_x = maxs64(alt_end_x, fi->end_x);
	}

	if (math_err != MATH_OP_SUCCESS) {
		pva_kmd_log_err("Math error in frame info calculation");
		return PVA_ERR_MATH_OP;
	}

	return PVA_SUCCESS;
}

/**
 * \brief Swaps Frame x and y co-ordinates of frame boundaries.
 *
 * This function is called to swap X and Y Co-ordinates of Frame info.
 * This is called to get correct frame info when using Vertical Mining Mode.
 *
 * \param[in, out] frame_info		A valid pointer to object of type \ref struct pva_hwseq_frame_info
 * 					X and Y Co-ordinates of frame_info are swapped
 *
 * \return void
 *
 */
static inline void
swap_frame_boundaries(struct pva_hwseq_frame_info *frame_info)
{
	int64_t tmp;
	tmp = frame_info->start_x;
	frame_info->start_x = frame_info->start_y;
	frame_info->start_y = tmp;
	tmp = frame_info->end_x;
	frame_info->end_x = frame_info->end_y;
	frame_info->end_y = tmp;
}

/**
 * \brief Checks padding for the tiles
 *
 * This function ensures the following:
 * 	- px, py (Horizontal and Vertical Padding) are set to 0 for Head Descriptor.
 * 	- desc_reload_enable (Descriptor Release Enable) is set to 0 for Head Descriptor.
 * 	- tx, ty (Tile width and Tile Heiht) for both Head and Tail Descriptors are non-zero.
 *
 * \param[in] head_desc		Pointer to the head descriptor of HW Sequencer
 * 				of type \ref nvpva_dma_descriptor.
 * \param[in] tail_desc		Pointer to the tail descriptor of HW Sequencer
 * 				of type \ref nvpva_dma_descriptor.
 *
 * \return
 * 	- PVA_SUCCESS if above checks pass
 * 	- PVA_INVAL if any of the above checks fail
 */
static enum pva_error
check_padding_tiles(const struct pva_dma_descriptor *head_desc,
		    const struct pva_dma_descriptor *tail_desc)
{
	if ((head_desc->px != 0U) || (head_desc->py != 0U) ||
	    (head_desc->desc_reload_enable != 0U)) {
		pva_kmd_log_err("Invalid padding in descriptor");
		return PVA_INVAL;
	}

	if ((head_desc->tx == 0U) || (head_desc->ty == 0U) ||
	    (tail_desc->tx == 0U) || (tail_desc->ty == 0U)) {
		return PVA_INVAL;
	}
	return PVA_SUCCESS;
}

static enum pva_error validate_vmem(struct pva_hwseq_priv *hwseq,
				    bool is_dst_vmem,
				    struct pva_hwseq_per_frame_info *fr_info,
				    uint32_t cr_index, bool has_dim3)
{
	enum pva_error err = PVA_SUCCESS;
	uint32_t vmem_tile_count = 0U;
	pva_math_error math_err = MATH_OP_SUCCESS;

	err = validate_xfer_mode(hwseq->head_desc);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Invalid transfer mode");
		return PVA_INVAL;
	}

	if (is_dst_vmem) {
		err = validate_dst_vmem(hwseq, &vmem_tile_count, has_dim3);
	} else {
		err = validate_src_vmem(hwseq, &vmem_tile_count, has_dim3);
	}

	if (err != PVA_SUCCESS) {
		return PVA_INVAL;
	}

	if (cr_index == 0U) {
		fr_info->vmem_tiles_per_frame = vmem_tile_count;
	}

	// total count of tiles sequenced
	fr_info->seq_tile_count =
		addu32(fr_info->seq_tile_count,
		       mulu32(hwseq->tiles_per_packet,
			      ((uint32_t)hwseq->colrow->crr + 1U), &math_err),
		       &math_err);

	if (math_err != MATH_OP_SUCCESS) {
		pva_kmd_log_err("Math error in tile count calculation");
		return PVA_ERR_MATH_OP;
	}
	if ((fr_info->vmem_tiles_per_frame != fr_info->seq_tile_count) &&
	    (cr_index == hwseq->hdr->nocr)) {
		pva_kmd_log_err("hwseq/vmem tile count mismatch");
		err = PVA_INVAL;
	}
	return err;
}

static enum pva_error
prepare_frame_info(struct pva_hwseq_priv *hwseq,
		   struct pva_hwseq_frame_info *frame_info)
{
	struct pva_hwseq_grid_info grid_info = { 0 };
	if (hwseq->is_raster_scan) {
		grid_info.tile_x[0] = (int32_t)(hwseq->head_desc->tx);
		grid_info.tile_x[1] = (int32_t)(hwseq->tail_desc->tx);
		grid_info.tile_y[0] = (int32_t)(hwseq->head_desc->ty);
		grid_info.tile_y[1] = (int32_t)(hwseq->tail_desc->ty);
		grid_info.tile_z = convert_to_signed_s32(
			(uint32_t)hwseq->head_desc->src.rpt1 + 1U);
		grid_info.pad_x[0] = (int32_t)hwseq->hdr->padl;
		grid_info.pad_x[1] = (int32_t)hwseq->hdr->padr;
		grid_info.pad_y[0] = (int32_t)hwseq->hdr->padt;
		grid_info.pad_y[1] = (int32_t)hwseq->hdr->padb;
		grid_info.grid_size_x = hwseq->tiles_per_packet;
		grid_info.grid_size_y = (uint32_t)hwseq->colrow->crr + 1U;
		grid_info.grid_size_z =
			(uint32_t)hwseq->head_desc->src.rpt2 + 1U;
		grid_info.grid_step_x = hwseq->hdr->to;
		grid_info.grid_step_y = hwseq->colrow->cro;
		grid_info.head_tile_count =
			(uint32_t)hwseq->dma_descs[0].dr + 1U;
		grid_info.is_split_padding = hwseq->is_split_padding;

		if (compute_frame_info(frame_info, &grid_info) != PVA_SUCCESS) {
			pva_kmd_log_err("Error in converting grid to frame");
			return PVA_INVAL;
		}
	} else {
		// vertical-mining mode
		// this is just raster-scan transposed so let's transpose the tile and padding
		if (hwseq->is_split_padding) {
			pva_kmd_log_err(
				"vertical mining not supported with split padding");
			return PVA_INVAL;
		}
		grid_info.tile_x[0] = (int32_t)(hwseq->head_desc->ty);
		grid_info.tile_x[1] = (int32_t)(hwseq->tail_desc->ty);
		grid_info.tile_y[0] = (int32_t)(hwseq->head_desc->tx);
		grid_info.tile_y[1] = (int32_t)(hwseq->tail_desc->tx);
		grid_info.tile_z = convert_to_signed_s32(
			(uint32_t)hwseq->head_desc->src.rpt1 + 1U);
		grid_info.pad_x[0] = (int32_t)hwseq->hdr->padt;
		grid_info.pad_x[1] = (int32_t)hwseq->hdr->padb;
		grid_info.pad_y[0] = (int32_t)hwseq->hdr->padl;
		grid_info.pad_y[1] = (int32_t)hwseq->hdr->padr;
		grid_info.grid_size_x = hwseq->tiles_per_packet,
		grid_info.grid_size_y = (uint32_t)hwseq->colrow->crr + 1U;
		grid_info.grid_size_z =
			(uint32_t)hwseq->head_desc->src.rpt2 + 1U;
		grid_info.grid_step_x = hwseq->hdr->to;
		grid_info.grid_step_y = hwseq->colrow->cro;
		grid_info.head_tile_count =
			(uint32_t)hwseq->dma_descs[0].dr + 1U;
		grid_info.is_split_padding = false;

		if (compute_frame_info(frame_info, &grid_info) != PVA_SUCCESS) {
			pva_kmd_log_err("Error in converting grid to frame");
			return PVA_INVAL;
		}
		swap_frame_boundaries(frame_info);
	}

	return PVA_SUCCESS;
}

static enum pva_error
validate_frame_buffer_addr(struct pva_hwseq_priv *hwseq,
			   const struct pva_hwseq_frame_info *frame_info,
			   bool sequencing_to_vmem, int64_t frame_buffer_offset,
			   uint16_t frame_line_pitch, bool has_dim3)
{
	int64_t frame_buffer_start = 0;
	int64_t frame_buffer_end = 0;
	uint32_t num_bytes = 0;
	int64_t frame_plane_size = 0LL;
	uint8_t head_desc_id = get_head_desc_did(hwseq);
	const struct pva_dma_descriptor *head_desc = hwseq->head_desc;
	pva_math_error math_err = MATH_OP_SUCCESS;

	frame_plane_size =
		sequencing_to_vmem ? head_desc->src.adv1 : head_desc->dst.adv1;
	frame_buffer_start =
		adds64(muls64(frame_info->start_y, (int64_t)frame_line_pitch,
			      &math_err),
		       frame_info->start_x, &math_err);

	frame_buffer_end = muls64(subs64(frame_info->end_z, 1, &math_err),
				  frame_plane_size, &math_err);

	frame_buffer_end =
		adds64(muls64(subs64(frame_info->end_y, 1, &math_err),
			      (int64_t)frame_line_pitch, &math_err),
		       frame_buffer_end, &math_err);

	frame_buffer_end =
		adds64(frame_buffer_end, frame_info->end_x, &math_err);

	// convert to byte range
	num_bytes = (uint32_t)1U
		    << (head_desc->log2_pixel_size & MAX_BYTES_PER_PIXEL);

	frame_buffer_start =
		muls64(frame_buffer_start, (int64_t)num_bytes, &math_err);

	frame_buffer_end =
		muls64(frame_buffer_end, (int64_t)num_bytes, &math_err);

	if (!sequencing_to_vmem) {
		hwseq->access_sizes[head_desc_id].dst.start_addr =
			adds64(mins64(frame_buffer_start, frame_buffer_end),
			       frame_buffer_offset, &math_err);

		hwseq->access_sizes[head_desc_id].dst.end_addr =
			adds64(maxs64(frame_buffer_start, frame_buffer_end),
			       frame_buffer_offset, &math_err);
	} else {
		hwseq->access_sizes[head_desc_id].src.start_addr =
			adds64(mins64(frame_buffer_start, frame_buffer_end),
			       frame_buffer_offset, &math_err);
		hwseq->access_sizes[head_desc_id].src.end_addr =
			adds64(maxs64(frame_buffer_start, frame_buffer_end),
			       frame_buffer_offset, &math_err);
	}

	if (math_err != MATH_OP_SUCCESS) {
		pva_kmd_log_err(
			"Math error in frame buffer address calculation");
		return PVA_ERR_MATH_OP;
	}

	return PVA_SUCCESS;
}

static enum pva_error check_tile_offset(struct pva_hwseq_priv *hwseq)
{
	if ((hwseq->tiles_per_packet > 1U) && (hwseq->hdr->to == 0)) {
		pva_kmd_log_err(
			"unsupported hwseq program modality: Tile Offset = 0");
		return PVA_INVAL;
	}
	return PVA_SUCCESS;
}

static void get_sequencing_and_dim3(struct pva_hwseq_priv *hwseq,
				    bool *sequencing_to_vmem, bool *has_dim3)
{
	const struct pva_dma_descriptor *head_desc = hwseq->head_desc;
	*sequencing_to_vmem = (head_desc->dst.transfer_mode ==
			       (uint8_t)PVA_DMA_TRANS_MODE_VMEM);
	// Check if this a 3D tensor transfer.
	*has_dim3 = ((head_desc->src.rpt1 == head_desc->dst.rpt1) &&
		     (head_desc->src.rpt2 == head_desc->dst.rpt2));
	*has_dim3 = *has_dim3 &&
		    ((*sequencing_to_vmem) ? ((head_desc->src.adv1 > 0) &&
					      (head_desc->src.adv2 > 0) &&
					      (head_desc->dst.adv1 > 0)) :
						   ((head_desc->dst.adv1 > 0) &&
					      (head_desc->dst.adv2 > 0) &&
					      (head_desc->src.adv1 > 0)));
}

/**
 * \brief Checks if the DMA Transfers are valid and within memory bounds.
 *
 * This function does the following:
 * - Rejects unsupported HW Sequencer program modality i.e. tile_offset == 0 when
 *   more than one tile is present per packet.
 * - Check padding for the tiles by calling check_padding_tiles.
 * - Validate VMEM tiles at source/destination as per configuration.
 *   Calls validate_dst_vmem is sequencing to VMEM.
 *   Calls validate_src_vmem if sequencing from VMEM.
 * - Ensures number of tiles computed in validating VMEM tiles.
 *   is the same as sequencing tile count obtained from HW Sequencer Blob.
 * - Populate Grid Info and convert it to Frame Info to obtain
 *   start and end co-ordinates of the Frame in memory.
 * - Validate Frame Boundaries with the Frame Offset lies within
 *   the memory range of the DMA Transfer.
 *
 * \param[in] hwseq		A valid pointer to object of type \ref pva_hwseq_priv.
 *
 * \return
 * 	- PVA_SUCCESS if all the above checks pass
 * 	- PVA_INVAL if any of the above checks fail
 */
static enum pva_error
validate_dma_boundaries(struct pva_hwseq_priv *hwseq,
			struct pva_hwseq_per_frame_info *fr_info,
			uint32_t num_cr)
{
	enum pva_error err = PVA_SUCCESS;
	bool sequencing_to_vmem = false;
	bool has_dim3 = false;
	uint16_t frame_line_pitch = 0U;
	int64_t frame_buffer_offset = 0;
	struct pva_hwseq_frame_info frame_info = { 0 };
	const struct pva_dma_descriptor *head_desc = hwseq->head_desc;
	const struct pva_dma_descriptor *tail_desc = hwseq->tail_desc;

	err = check_tile_offset(hwseq);
	if (err != PVA_SUCCESS) {
		return err;
	}

	err = check_padding_tiles(head_desc, tail_desc);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("DMA Descriptors have empty tiles");
		return PVA_INVAL;
	}

	get_sequencing_and_dim3(hwseq, &sequencing_to_vmem, &has_dim3);

	err = validate_vmem(hwseq, sequencing_to_vmem, fr_info, num_cr,
			    has_dim3);
	if (err != PVA_SUCCESS) {
		return PVA_INVAL;
	}

	if (prepare_frame_info(hwseq, &frame_info) != PVA_SUCCESS) {
		pva_kmd_log_err("Error in preparing the frame");
		return PVA_INVAL;
	}

	frame_line_pitch = sequencing_to_vmem ? head_desc->src.line_pitch :
						      head_desc->dst.line_pitch;
	frame_buffer_offset =
		sequencing_to_vmem ?
			      convert_to_signed_s64(head_desc->src.offset) :
			      convert_to_signed_s64(head_desc->dst.offset);

	if (validate_frame_buffer_addr(hwseq, &frame_info, sequencing_to_vmem,
				       frame_buffer_offset, frame_line_pitch,
				       has_dim3) != PVA_SUCCESS) {
		pva_kmd_log_err("sequencer address validation failed");
		return PVA_INVAL;
	}

	return err;
}

/**
 * \brief Validates the HW Sequener when it is in Frame Addressing Mode
 *
 * This function performs the following initial checks on the HW Sequencer
 * having Frame Addressing Mode
 * - HW Sequencer has only 1 col/row header
 * - Col/Row has at most 2 descriptors
 * - Each descriptor has valid ID i.e. 0 < Desc ID < num_dma_descriptors
 * - tiles_per_packet is calculated as summation of all Descriptor repetition
 *   factors associated with each descriptor
 *
 * It populates the following fields in hwseq_info
 * 	- hwseq_info->desc_count
 * 	- hwseq_info->dma_descs
 * 	- hwseq_info->head_desc
 * 	- hwseq_info->tail_desc
 *
 * After the above checks and updates are done, validate_dma_boundaries is called
 * to validate the DMA Transfer setup by HW Sequencer does not go out of memory bounds
 *
 * \param[in, out] hwseq_info	A valid pointer to object of type \ref struct pva_hwseq_priv
 * \param[in] task 		A valid pointer to object of type \ref pva_submit_task
 * \param[in] dma_ch		A valid pointer to object of type \ref pva_dma_channel
 * \param[out] requires_block_height	A valid pointer to a boolean. This is updated
 * 				to indicate if current channel needs Block Height information.
 *
 * \return
 * 	- PVA_SUCCESS if all above checks pass
 * 	- PVA_INVAL in the following cases
 * 		- More than one Col/Row Header
 * 		- More than 2 Descriptors in Col/Row
 * 		- Invalid Descriptor Entry as highlighted above
 *		- validate_dma_boundaries returns an error
 */
static enum pva_error validate_frame_mode(struct pva_hwseq_priv *hwseq_info,
					  uint64_t *hw_dma_descs_mask)
{
	struct pva_dma_hwseq_desc_entry *desc_entry = NULL;
	struct pva_dma_hwseq_desc_entry *desc_entries = hwseq_info->dma_descs;
	uint32_t num_descs = 0U;
	uint32_t i = 0U;
	uint32_t num_cr = 0U;
	enum pva_error err = PVA_SUCCESS;
	struct pva_hwseq_per_frame_info fr_info = { 0 };

	if ((hwseq_info->hw_gen < PVA_HW_GEN3) &&
	    (hwseq_info->hdr->nocr != 0U)) {
		pva_kmd_log_err_u64(
			"Cannot have more than 1 col/row header in GEN2",
			hwseq_info->hdr->nocr);
		return PVA_INVAL;
	}

	for (num_cr = 0; num_cr <= hwseq_info->hdr->nocr; num_cr++) {
		hwseq_info->tiles_per_packet = 0;
		hwseq_info->colrow =
			(struct pva_dma_hwseq_colrow_hdr *)(read_hwseq_blob(
				&hwseq_info->blob,
				(uint32_t)sizeof(
					struct pva_dma_hwseq_colrow_hdr)));

		if (hwseq_info->colrow == NULL) {
			pva_kmd_log_err(
				"Cannot read HW sequencer col/row header");
			return PVA_INVAL;
		}

		num_descs = hwseq_info->colrow->dec + (uint32_t)1U;
		//Check that the col/row has a max of 2 descriptors
		if (num_descs > 2U) {
			pva_kmd_log_err(
				"Cannot have more than 2 descriptors in HW Sequencer");
			return PVA_INVAL;
		}

		for (i = 0; i < num_descs; i++) {
			desc_entry = (struct pva_dma_hwseq_desc_entry
					      *)(read_hwseq_blob(
				&hwseq_info->blob,
				(uint32_t)sizeof(
					struct pva_dma_hwseq_desc_entry)));

			if (validate_desc_entry(
				    desc_entry,
				    safe_addu8(hwseq_info->dma_config->header
						       .base_descriptor,
					       hwseq_info->dma_config->header
						       .num_descriptors)) !=
			    PVA_SUCCESS) {
				pva_kmd_log_err("Invalid DMA Descriptor Entry");
				return PVA_INVAL;
			}
			desc_entries[i].did = desc_entry->did - 1U;
			//TODO enable nv_array_index_no_speculate later
			// desc_entries[i].did = (uint8_t) nv_array_index_no_speculate_u32(
			// 	desc_entries[i].did, max_num_descs);

			desc_entries[i].dr = desc_entry->dr;
			hw_dma_descs_mask[(desc_entries[i].did / 64ULL)] |=
				1ULL << (desc_entries[i].did & MAX_DESC_ID);

			hwseq_info->tiles_per_packet +=
				((uint32_t)desc_entry->dr + 1U);
		}
		if ((i == num_descs) && ((i % 2U) != 0U)) {
			(void)read_hwseq_blob(
				&hwseq_info->blob,
				(uint32_t)sizeof(
					struct pva_dma_hwseq_desc_entry));
		}

		hwseq_info->desc_count = num_descs;
		hwseq_info->head_desc =
			&hwseq_info->dma_config
				 ->descriptors[desc_entries[0].did -
					       hwseq_info->dma_config->header
						       .base_descriptor];
		hwseq_info->tail_desc =
			&hwseq_info->dma_config
				 ->descriptors[desc_entries[num_descs - 1U].did -
					       hwseq_info->dma_config->header
						       .base_descriptor];

		//TODO: User nv_array_index_no_speculate_u32
		// num_descs = nv_array_index_no_speculate_u32(num_descs, 3);

		err = validate_dma_boundaries(hwseq_info, &fr_info, num_cr);
		if (err != PVA_SUCCESS) {
			return err;
		}
	}

	return err;
}

static enum pva_error validate_rra_mode(struct pva_hwseq_priv *hwseq_info,
					uint64_t *hw_dma_descs_mask)
{
	const uint8_t *column = 0U;
	uint32_t i = 0U;
	uint32_t num_columns = 0U;
	uint32_t end = hwseq_info->entry.ch->hwseq_end;
	const uint8_t *blob_end = &(hwseq_info->blob.data[(end << 2) + 4U]);

	// In each NOCR entry, 4 bytes are used for CRO
	// and 4 bytes are used for Desc info
	const uint8_t column_entry_size = 8U;

	if (hwseq_info->entry.ch->hwseq_frame_count >
	    PVA_HWSEQ_RRA_MAX_FRAME_COUNT) {
		pva_kmd_log_err("Invalid HWSEQ frame count");
		return PVA_INVAL;
	}

	if (hwseq_info->hdr->nocr > PVA_HWSEQ_RRA_MAX_NOCR) {
		pva_kmd_log_err("Invalid HWSEQ column count");
		return PVA_INVAL;
	}

	if (hwseq_info->hdr->fr != 0) {
		pva_kmd_log_err("Invalid HWSEQ repetition factor");
		return PVA_INVAL;
	}

	num_columns = hwseq_info->hdr->nocr + 1U;
	column = hwseq_info->blob.data + sizeof(struct pva_dma_hwseq_hdr);

	// Ensure there are sufficient CRO and Desc ID entries in the HWSEQ blob
	if (((blob_end - column) / column_entry_size) < num_columns) {
		pva_kmd_log_err("HWSEQ Program does not have enough columns");
		return PVA_INVAL;
	}

	for (i = 0U; i < num_columns; i++) {
		struct pva_dma_hwseq_desc_entry desc_entry;
		uint32_t *desc_read_data = (uint32_t *)(read_hwseq_blob(
			&hwseq_info->blob, column_entry_size));
		if (desc_read_data == NULL) {
			pva_kmd_log_err(
				"Failed to read descriptor data from HWSEQ blob");
			return PVA_INVAL;
		}

		// Index 0 contains DEC and CRO, both of which are not used
		// Index 1 contains the DID and DR of 1st and second desc
		// In RRA mode, each HWSEQ column has only 1 descriptor
		// Hence, we validate the first descriptor and ignore the second
		// descriptor in each column
		desc_entry.did = (desc_read_data[1U] & 0x000000FFU);
		desc_entry.dr = ((desc_read_data[1U] & 0x0000FF00U) >> 8U);
		if (validate_desc_entry(&desc_entry, PVA_MAX_NUM_DMA_DESC) !=
		    PVA_SUCCESS) {
			pva_kmd_log_err(
				"Invalid Descriptor ID found in HW Sequencer");
			return PVA_INVAL;
		}
		desc_entry.did -= 1U;
		hw_dma_descs_mask[(desc_entry.did / 64ULL)] |=
			1ULL << (desc_entry.did & MAX_DESC_ID);
	}

	return 0;
}

/**
 * \brief Validates the HW Sequencer Blob if it has Descriptor Addressing Mode
 *
 * This function validates Descriptor Addressing Mode in HW Sequencer Blob
 *
 * The following checks are performed:
 * 	- Frame Repeat Count is checked to be 0
 * 	- Reading of HW Sequencer Blob does not lead to out of bounds read
 * 	- Loop through all the col/rows present in the HW Sequencer Blob.
 * 	  The count is obtained from HW Sequencer Blob Header
 * 	- Ensure that any descriptor present in the HW Sequencer Blob
 * 	  have valid IDs i.e. ID > 0 and ID < num_dma_descriptors
 * 	- Even number of descriptors are expected per col/row.
 * 	  If odd number of descriptors are found, increment the read pointer to the
 * 	  HW Sequencer Blob to ensure compliance with DMA IAS
 *
 * \param[in, out] hwseq_info	A valid pointer to object of type \ref struct pva_hwseq_priv
 *
 * \return
 * 	- PVA_SUCCESS if all of the above checks pass
 * 	- PVA_INVAL if any of the above checks fail
 */
static enum pva_error validate_desc_mode(struct pva_hwseq_priv *hwseq_info)
{
	enum pva_error err = PVA_SUCCESS;
	const struct pva_dma_hwseq_colrow_hdr *colrow = NULL;
	const struct pva_dma_hwseq_desc_entry *desc_entry = NULL;
	uint32_t num_colrows = 0U;
	uint32_t num_descs = 0U;
	uint32_t i = 0U;
	uint32_t j = 0U;

	if (hwseq_info->hdr->fr != 0U) {
		pva_kmd_log_err(
			"invalid hwseq modality (frame repeat count>1)");
		return PVA_INVAL;
	}

	num_colrows = (uint32_t)hwseq_info->hdr->nocr + 1U;
	for (i = 0U; i < num_colrows; i++) {
		colrow = (const struct pva_dma_hwseq_colrow_hdr
				  *)(read_hwseq_blob(
			&hwseq_info->blob,
			(uint32_t)sizeof(struct pva_dma_hwseq_colrow_hdr)));
		if (colrow == NULL) {
			pva_kmd_log_err("Attempt to read out of bounds");
			return PVA_INVAL;
		}
		num_descs = (uint32_t)colrow->dec + 1U;
		for (j = 0U; j < num_descs; j++) {
			desc_entry = (const struct pva_dma_hwseq_desc_entry
					      *)(read_hwseq_blob(
				&hwseq_info->blob,
				(uint32_t)sizeof(
					struct pva_dma_hwseq_desc_entry)));

			if (validate_desc_entry(
				    desc_entry,
				    safe_addu8(hwseq_info->dma_config->header
						       .base_descriptor,
					       hwseq_info->dma_config->header
						       .num_descriptors)) !=
			    PVA_SUCCESS) {
				pva_kmd_log_err("Invalid DMA Descriptor Entry");
				return PVA_INVAL;
			}
		}
		if ((j == num_descs) && ((j % 2U) != 0U)) {
			(void)read_hwseq_blob(
				&hwseq_info->blob,
				(uint32_t)sizeof(
					struct pva_dma_hwseq_desc_entry));
		}
	}

	return err;
}

static enum pva_error
check_for_valid_hwseq_type(struct pva_hwseq_priv *hwseq_info,
			   struct hw_seq_blob_entry const *entry,
			   uint64_t *hw_dma_descs_mask)
{
	enum pva_error err = PVA_SUCCESS;
	//Populate hwseq_info header
	hwseq_info->hdr = (struct pva_dma_hwseq_hdr *)(read_hwseq_blob(
		&hwseq_info->blob, (uint32_t)sizeof(struct pva_dma_hwseq_hdr)));
	if (hwseq_info->hdr == NULL) {
		pva_kmd_log_err("HW sequencer buffer does not contain header");
		return PVA_INVAL;
	}

	if ((hwseq_info->hdr->fr != 0U) || (hwseq_info->hdr->fo != 0U)) {
		return PVA_INVAL;
	}

	if (hwseq_info->hdr->fid == (uint16_t)PVA_DMA_HWSEQ_DESC_MODE) {
		err = validate_desc_mode(hwseq_info);
	} else if (hwseq_info->hdr->fid == (uint16_t)PVA_DMA_HWSEQ_FRAME_MODE) {
		err = validate_frame_mode(hwseq_info, hw_dma_descs_mask);
	} else if (hwseq_info->hdr->fid == (uint16_t)PVA_DMA_HWSEQ_RRA_MODE) {
		if (hwseq_info->hw_gen < PVA_HW_GEN3) {
			pva_kmd_log_err(
				"RRA Mode not supported for current device");
			return PVA_INVAL;
		}
		err = validate_rra_mode(hwseq_info, hw_dma_descs_mask);
	} else {
		pva_kmd_log_err("Invalid Header in HW Sequencer Blob");
		return PVA_INVAL;
	}

	return err;
}

static enum pva_error validate_hwseq_blob(struct pva_hwseq_priv *hwseq_info,
					  struct hw_seq_blob_entry const *entry,
					  uint64_t *hw_dma_descs_mask)
{
	uint32_t i = 0U;
	enum pva_error err = PVA_SUCCESS;

	hwseq_info->entry.ch = entry->ch;
	hwseq_info->is_split_padding =
		(hwseq_info->entry.ch->hwseq_tx_select != 0U);
	hwseq_info->is_raster_scan =
		(hwseq_info->entry.ch->hwseq_traversal_order == 0U);
	hwseq_info->entry.hwseq_start = entry->hwseq_start;
	hwseq_info->entry.hwseq_end = entry->hwseq_end;
	hwseq_info->entry.num_frames = entry->num_frames;

	for (i = 0U; i < entry->num_frames; i++) {
		err = check_for_valid_hwseq_type(hwseq_info, entry,
						 hw_dma_descs_mask);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Invalid Header in HW Sequencer Blob");
			return err;
		}
	}
	return err;
}

static enum pva_error
validate_channel_accesses(const struct pva_dma_channel *ch,
			  const struct pva_dma_config_header *header,
			  enum pva_hw_gen hw_gen,
			  struct hw_seq_blob_entry *entry)
{
	if (ch->hwseq_end < ch->hwseq_start) {
		return PVA_ERR_HWSEQ_INVALID;
	}
	//TODO: Confirm below checks. Is header->base_hwseq_word needed?
	if (((ch->hwseq_end + 1U) + (header->base_hwseq_word)) >
	    (header->num_hwseq_words)) {
		pva_kmd_log_err(
			"Possible out of bounds read for HW Seqeuncer Blob");
		return PVA_ERR_HWSEQ_INVALID;
	}
	if ((uint16_t)(ch->hwseq_end - ch->hwseq_start) < 4U) {
		pva_kmd_log_err("HW Sequencer too small for channel");
		return PVA_ERR_HWSEQ_INVALID;
	}
	entry->hwseq_start = ch->hwseq_start;
	entry->hwseq_end = ch->hwseq_end;
	entry->num_frames = 1U;
	if (hw_gen == PVA_HW_GEN3) {
		entry->num_frames = ch->hwseq_frame_count + 1U;
	}
	entry->ch = ch;
	return PVA_SUCCESS;
}

enum pva_error validate_hwseq(struct pva_dma_config const *dma_config,
			      struct pva_kmd_hw_constants const *hw_consts,
			      struct pva_kmd_dma_access *access_sizes,
			      uint64_t *hw_dma_descs_mask)
{
	uint32_t i = 0U;
	struct pva_hwseq_priv hwseq_info = { 0 };
	enum pva_error err = PVA_SUCCESS;
	const struct pva_dma_channel *ch = NULL;
	struct hw_seq_blob_entry entries[PVA_MAX_NUM_DMA_CHANNELS] = { 0 };
	uint8_t num_hwseqs = 0U;
	uint8_t num_channels = dma_config->header.num_channels;

	hwseq_info.dma_config = (const struct pva_dma_config *)(dma_config);
	hwseq_info.hw_gen = hw_consts->hw_gen;
	hwseq_info.access_sizes = access_sizes;

	for (i = 0U; i < num_channels; i++) {
		ch = &dma_config->channels[i];
		if (ch->hwseq_enable == 1) {
			err = validate_channel_accesses(ch, &dma_config->header,
							hwseq_info.hw_gen,
							&entries[num_hwseqs]);
			if (err != PVA_SUCCESS) {
				return err;
			}
			num_hwseqs++;
		}
	}

	for (i = 0U; i < num_hwseqs; i++) {
		uint32_t start_index = entries[i].hwseq_start;
		uint32_t end_index = entries[i].hwseq_end + 1U;
		uint32_t curr_offset = start_index << 2U;
		uint32_t len = 0U;
		//Populate hwseq blob
		hwseq_info.blob.data =
			(uint8_t *)((uintptr_t)(dma_config->hwseq_words) +
				    (curr_offset));

		len = safe_subu32(end_index, start_index);
		hwseq_info.blob.bytes_left = (len << 2U);

		err = validate_hwseq_blob(&hwseq_info, &entries[i],
					  hw_dma_descs_mask);
		if (err != PVA_SUCCESS) {
			return PVA_ERR_HWSEQ_INVALID;
		}
	}
	return PVA_SUCCESS;
}
