/*
 * SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program;  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PVA_HWSEQ_T264_H
#define PVA_HWSEQ_T264_H

#define PVA_HWSEQ_RAM_SIZE_T26X		2048U
#define PVA_HWSEQ_RAM_ID_MASK_T26X	0x1FFU
#define PVA_HWSEQ_RRA_ADDR		0xC0DAU
#define PVA_HWSEQ_MAX_CR_COUNT_T26X	32U

/** \brief Mask used to derive the MSB for HW sequencer
 *  buffer start index for a channel
 */
#define PVA_CH_FLAGS1_HWSEQ_START_IDX_MSB_MASK     (1U)

/** \brief Mask used to derive the MSB for HW sequencer
 *  buffer start index for a channel
 */
#define PVA_CH_FLAGS1_HWSEQ_START_IDX_MSB_SHIFT    (0U)

/** \brief Mask used to derive the MSB for HW sequencer
 *  buffer end index for a channel
 */
#define PVA_CH_FLAGS1_HWSEQ_END_IDX_MSB_MASK       (1U << 2U)

/** \brief Mask used to derive the MSB for HW sequencer
 *  buffer end index for a channel
 */
#define PVA_CH_FLAGS1_HWSEQ_END_IDX_MSB_SHIFT      (2U)

#define PVA_CH_FLAGS1_HWSEQ_START_IDX_MSB(flag) \
	((flag & PVA_CH_FLAGS1_HWSEQ_START_IDX_MSB_MASK) \
		>> PVA_CH_FLAGS1_HWSEQ_START_IDX_MSB_SHIFT)

#define PVA_CH_FLAGS1_HWSEQ_END_IDX_MSB(flag) \
	((flag & PVA_CH_FLAGS1_HWSEQ_END_IDX_MSB_MASK) \
		>> PVA_CH_FLAGS1_HWSEQ_END_IDX_MSB_SHIFT)

static inline bool is_rra_mode(u16 id)
{
	return (id == PVA_HWSEQ_RRA_ADDR);
}

static inline void set_hwseq_mode_rra(struct pva_submit_task *task, u8 desc_id)
{
	u8 idx = desc_id / 64U;
	u8 shift = desc_id % 64U;

	task->desc_hwseq_t26x[idx] |= (1ULL << shift);
}

static inline u32 nvpva_get_hwseq_start_idx_t26x(
			struct nvpva_dma_channel *user_ch)
{
	u32 idx = ((user_ch->hwseqStart & 0xFF)
		| (PVA_CH_FLAGS1_HWSEQ_START_IDX_MSB(user_ch->flags1) << 8U));

	return (idx & (u32)PVA_HWSEQ_RAM_ID_MASK_T26X);
}

static inline u32 nvpva_get_hwseq_end_idx_t26x(
			struct nvpva_dma_channel *user_ch)
{
	u32 idx = ((user_ch->hwseqEnd & 0xFF)
		| (PVA_CH_FLAGS1_HWSEQ_END_IDX_MSB(user_ch->flags1) << 8U));

	return (idx & (u32)PVA_HWSEQ_RAM_ID_MASK_T26X);
}

static int validate_rra_mode(struct pva_hw_sweq_blob_s *blob,
			     struct pva_submit_task *task,
			     struct nvpva_dma_channel *dma_ch)
{
	const u8 *desc_entry = NULL;
	const u8 *column = 0U;
	uint32_t i = 0U;
	uint32_t num_columns = 0U;
	u32 end = nvpva_get_hwseq_end_idx_t26x(dma_ch) * 4U;
	u8 *blob_end = &((uint8_t *)blob)[end + 4];
	// In each NOCR entry, 4 bytes are used for CRO
	// and 4 bytes are used for Desc info
	const u8 column_entry_size = 8U;

	if (task->pva->version < PVA_HW_GEN3) {
		pr_err("Selected HWSEQ mode is not supported");
		return -EINVAL;
	}

	if (blob->f_header.fr != 0) {
		pr_err("Invalid HWSEQ repetition factor");
		return -EINVAL;
	}

	num_columns = blob->f_header.no_cr + 1U;
	column = (u8 *)&blob->cr_header;
	desc_entry = (u8 *)&blob->desc_header;

	// Ensure there are sufficient CRO and Desc ID entries
	// in the HWSEQ blob
	if (((blob_end - column) / column_entry_size) < num_columns) {
		pr_err("HWSEQ Program does not have enough columns.");
		return -EINVAL;
	}

	for (i = 0U; i < num_columns; i++) {
		// In RRA mode, each HWSEQ column has only 1 descriptor
		// Hence, we validate the first descriptor and ignore
		// the second descriptor in each column
		if ((*desc_entry == 0U) || (*desc_entry >
			(NVPVA_TASK_MAX_DMA_DESCRIPTOR_ID_T26X))) {
			return -EINVAL;
		}
		set_hwseq_mode_rra(task, *desc_entry -1U);
		desc_entry += column_entry_size;
	}

	return 0;
}

static inline bool hwseq_blob_validate_t26x(struct pva_hw_sweq_blob_s *blob,
					    struct pva_submit_task *task,
					    struct nvpva_dma_channel *dma_ch,
					    bool *validation_done)
{
	if (is_rra_mode(blob->f_header.fid)) {
		*validation_done = true;
		if (validate_rra_mode(blob, task, dma_ch) != 0) {
			return false;
		}
	} else {
		*validation_done = false;
	}
	return true;
}

static inline void nvpva_task_dma_channel_mapping_t26x(
			struct pva_dma_ch_config_s *ch,
			struct nvpva_dma_channel *user_ch)
{
	/* DMA_CHANNEL_HWSEQCNTL_CHHWSEQSTART */
	/* Note: the MSB for HWSEQ start idx comes from bit 0 of flags1 field*/
	ch->hwseqcntl &= ~((u32)PVA_HWSEQ_RAM_ID_MASK_T26X);
	ch->hwseqcntl |= nvpva_get_hwseq_start_idx_t26x(user_ch);

	/* DMA_CHANNEL_HWSEQCNTL_CHHWSEQEND */
	/* Note: the MSB for HWSEQ end idx comes from bit 2 of flags1 field*/
	ch->hwseqcntl &= (~((u32)PVA_HWSEQ_RAM_ID_MASK_T26X << 12U));
	ch->hwseqcntl |= (nvpva_get_hwseq_end_idx_t26x(user_ch) << 12U);

	/* DMA_CHANNEL_HWSEQFSCNTL_CHHWSEQFCNT*/
	ch->hwseqfscntl |= (((uint32_t)user_ch->hwseqConFrameSeq & 0x1U) << 0U);

	/* DMA_CHANNEL_HWSEQFSCNTL_CHHWSEQCFS*/
	ch->hwseqfscntl |= (((uint32_t)user_ch->hwseqFrameCount & 0x3FU) << 16U);
}

#endif
