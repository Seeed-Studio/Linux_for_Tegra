// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_t23x.h"
#include "pva_kmd_constants.h"

struct vmem_region vmem_regions_tab_t23x[PVA_VMEM_REGION_COUNT_T23X] = {
	{ .start = T23x_VMEM0_START, .end = T23x_VMEM0_END },
	{ .start = T23x_VMEM1_START, .end = T23x_VMEM1_END },
	{ .start = T23x_VMEM2_START, .end = T23x_VMEM2_END },
};

void pva_kmd_device_init_t23x(struct pva_kmd_device *pva)
{
	uint32_t ccq;
	uint32_t st_idx;

	pva->hw_consts.hw_gen = PVA_HW_GEN2;
	pva->hw_consts.n_smmu_contexts = PVA_NUM_SMMU_CONTEXTS_T23X;
	pva->r5_image_smmu_context_id = PVA_NUM_SMMU_CONTEXTS_T23X - 1;
	pva->hw_consts.n_dma_descriptors = PVA_NUM_DMA_DESC_T23X;
	pva->hw_consts.n_user_dma_channels = PVA_DMA_NUM_CHANNELS_T23X - 1U;
	pva->hw_consts.n_hwseq_words = PVA_NUM_HWSEQ_WORDS_T23X;
	pva->hw_consts.n_dynamic_adb_buffs = PVA_NUM_DYNAMIC_ADB_BUFFS_T23X;
	pva->hw_consts.n_vmem_regions = PVA_VMEM_REGION_COUNT_T23X;
	pva->support_hwseq_frame_linking = false;
	pva->vmem_regions_tab = vmem_regions_tab_t23x;

	pva->reg_phy_base[PVA_KMD_APERTURE_PVA_CLUSTER] =
		PVA_KMD_PVA0_T23x_REG_BASE;
	pva->reg_size[PVA_KMD_APERTURE_PVA_CLUSTER] =
		PVA_KMD_PVA0_T23x_REG_SIZE;
	pva->reg_phy_base[PVA_KMD_APERTURE_VPU_DEBUG] = TEGRA_PVA0_VPU_DBG_BASE;
	pva->reg_size[PVA_KMD_APERTURE_VPU_DEBUG] = TEGRA_PVA0_VPU_DBG_SIZE;

	pva->regspec.sec_lic_intr_enable = 0x28064;
	pva->regspec.sec_lic_intr_status = 0x2806C;

	pva->regspec.cfg_user_sid_base = 0x240000;
	pva->regspec.cfg_priv_sid = 0x240020;
	pva->regspec.cfg_vps_sid = 0x240024;
	pva->regspec.cfg_r5user_lsegreg = 0x250008;
	pva->regspec.cfg_r5user_usegreg = 0x25001c;
	pva->regspec.cfg_priv_ar1_lsegreg = 0x25000c;
	pva->regspec.cfg_priv_ar1_usegreg = 0x250020;
	pva->regspec.cfg_priv_ar2_lsegreg = 0x250010;
	pva->regspec.cfg_priv_ar2_usegreg = 0x250024;
	pva->regspec.cfg_priv_ar1_start = 0x250028;
	pva->regspec.cfg_priv_ar1_end = 0x25002c;
	pva->regspec.cfg_priv_ar2_start = 0x250030;
	pva->regspec.cfg_priv_ar2_end = 0x250034;

	pva->regspec.cfg_scr_priv_0 = 0x18004;
	pva->regspec.cfg_perf_mon = 0x200000;

	pva->regspec.ccq_count = 8U;
	/* For VPU 0*/
	pva->regspec.vpu_dbg_instr_reg_offset[0] = 0x50000U;
	/* For VPU 1*/
	pva->regspec.vpu_dbg_instr_reg_offset[1] = 0x70000U;
	for (ccq = 0; ccq < pva->regspec.ccq_count; ccq++) {
		uint32_t n_st = PVA_CFG_CCQ_STATUS_COUNT;
		uint32_t ccq_base = safe_addu32(
			(uint32_t)0x260000,
			safe_mulu32((uint32_t)PVA_CFG_CCQ_BLOCK_SIZE, ccq));
		pva->regspec.ccq_regs[ccq].status_count = n_st;
		pva->regspec.ccq_regs[ccq].fifo = ccq_base;
		for (st_idx = 0; st_idx < n_st; st_idx++) {
			pva->regspec.ccq_regs[ccq].status[st_idx] = safe_addu32(
				ccq_base,
				safe_addu32((uint32_t)0x4U,
					    safe_mulu32((uint32_t)0x4U,
							st_idx)));
		}
	}

#if PVA_SUPPORT_XBAR_RAW == 1
	pva->bl_sector_pack_format = PVA_BL_XBAR_RAW;
#else
	pva->bl_sector_pack_format = PVA_BL_TEGRA_RAW;
#endif

	pva->tsc_to_ns_multiplier = PVA_NS_PER_TSC_TICK_T23X;
}
