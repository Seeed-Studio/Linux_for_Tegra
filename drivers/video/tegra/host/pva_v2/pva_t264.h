/*
 * Tegra Graphics Chip support for T264
 *
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0+
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef __PVA_T264_H__
#define __PVA_T264_H__

static char *aux_dev_name_t264 = "818c000000.pva0:pva0_niso1_ctx8";
static u32 aux_dev_name_len_t264 = 31;

struct nvhost_device_data t264_pva0_info = {
	.version = PVA_HW_GEN3,
	.num_channels		= 1,
	.clocks			= {
		{"axi", UINT_MAX,},
		{"vps0", UINT_MAX,},
		{"vps1", UINT_MAX,},
	},
	.ctrl_ops		= &tegra_pva_ctrl_ops,
	.devfs_name_family	= "pva",
	.class			= NV_PVA0_CLASS_ID,
	.autosuspend_delay	= 500,
	.finalize_poweron	= pva_finalize_poweron,
	.prepare_poweroff	= pva_prepare_poweroff,
	.firmware_name		= "nvhost_pva030.fw",
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.vm_regs		= {
		{0x240000, false, 0xFFFF0000},
		{0x240004, false, 0xFFFF0000},
		{0x240008, false, 0xFFFF0000},
		{0x24000c, false, 0xFFFF0000},
		{0x240010, false, 0xFFFF0000},
		{0x240014, false, 0xFFFF0000},
		{0x240018, false, 0xFFFF0000},
		{0x24001c, false, 0xFFFF0000},
		{0x240020, false, 0x00FF0000},
		{0x240020, false, 0x00FF0008},
		{0x240020, false, 0x00FF0010},
		{0x240024, false, 0x00FF0000},
		{0x240024, false, 0x00FF0008}
	},
	.poweron_reset		= true,
	.serialize		= true,
	.get_reloc_phys_addr	= nvhost_t23x_get_reloc_phys_addr,
	.can_powergate		= true,
};

static u32 vm_regs_sid_idx_t264_cod[] = {1, 2, 3, 4, 5, 6, 7, 8,
				     9, 9, 9, 9, 9, 0, 0, 0};
static u32 vm_regs_sid_idx_t264_co[] = {1, 2, 3, 4, 5, 6, 7, 8,
				     9, 0, 9, 0, 0, 0, 0, 0};
static u32 vm_regs_reg_idx_t264[] = {0, 1, 2, 3, 4, 5, 6, 7,
				     8, 8, 8, 9, 9, 0, 0, 0};
#endif
