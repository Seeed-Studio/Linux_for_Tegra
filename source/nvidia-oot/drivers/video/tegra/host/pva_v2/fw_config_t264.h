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

#ifndef FW_CONFIG_T264_H
#define FW_CONFIG_T264_H
/**
 * @brief Total number of AXI data buffers for T26x.
 */
#define PVA_NUM_DMA_ADB_BUFFS_T26X		304U

/**
 * @brief Number of reserved AXI data buffers for T26x.
 */
#define PVA_NUM_RESERVED_ADB_BUFFERS_T26X	16U

/**
 * @brief Number of dynamic AXI data buffers for T26x.
 * These exclude the reserved AXI data buffers from total available ones.
 */
#define PVA_NUM_DYNAMIC_ADB_BUFFS_T26X \
	(PVA_NUM_DMA_ADB_BUFFS_T26X - PVA_NUM_RESERVED_ADB_BUFFERS_T26X)

#endif