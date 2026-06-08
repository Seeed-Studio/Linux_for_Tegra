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

#ifndef __NVPVA_IOCTL_T264_H__
#define __NVPVA_IOCTL_T264_H__

/**
 * There are 96 DMA descriptors in T26x. But R5 FW reserves
 * 4 DMA descriptors for internal use.
 */
#define NVPVA_TASK_MAX_DMA_DESCRIPTORS_T26X		(92U)

/*
    Since the reserved descriptors in T26x are in the middle
    of the descriptor range, the last descriptor that can be
    used by a user task is the very last available descriptor.
*/
#define NVPVA_TASK_MAX_DMA_DESCRIPTOR_ID_T26X \
            ((NVPVA_TASK_MAX_DMA_DESCRIPTORS_T26X) \
             + (NVPVA_NUM_RESERVED_DESCRIPTORS))

/**
 * There are 16 DMA channels in T26x.
 * R5 FW reserves one DMA channel for internal use.
 */
#define NVPVA_TASK_MAX_DMA_CHANNELS_T26X (15U)

/**
 * Maximum number of frames allowed in hwseq mode
 * on T26x is 64.
 */
#define NVPVA_TASK_MAX_HWSEQ_FRAME_COUNT_T26X	(64U)

/* NOTE: This is a re-definition of nvpva_dma_channel that
 * contains T26x specific changes. Once T26x is public,
 * this definition may be merged nvpva_dma_channel.
 *
 * Also note that the flags1 field has the following flags:
 * - MSB for the HW Sequencer start index field in channel registers
 *  	DMA_CHANNEL_HWSEQCNTL[1].bit[0] = flags1[0].bit[0];
 * - MSB for the HW Sequencer end index field in channel registers
 *	DMA_CHANNEL_HWSEQCNTL[2].bit[4] = flags1[0].bit[2];
 */
struct nvpva_dma_channel_ex {
	uint8_t descIndex;
	uint8_t blockHeight;
	uint16_t adbSize;
	uint8_t vdbSize;
	uint16_t adbOffset;
	uint8_t vdbOffset;
	uint32_t outputEnableMask;
	uint32_t padValue;
	uint8_t reqPerGrant;
	uint8_t prefetchEnable;
	uint8_t chRepFactor;
	uint8_t hwseqStart;
	uint8_t hwseqEnd;
	uint8_t hwseqEnable;
	uint8_t hwseqTraversalOrder;
	uint8_t hwseqTxSelect;
	uint8_t hwseqTriggerDone;
	uint8_t hwseqFrameCount;
	uint8_t hwseqConFrameSeq;
	uint8_t flags1;
};

#endif /* __NVPVA_IOCTL_T264_H__ */
