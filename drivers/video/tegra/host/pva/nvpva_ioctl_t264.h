/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
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

#endif /* __NVPVA_IOCTL_T264_H__ */
