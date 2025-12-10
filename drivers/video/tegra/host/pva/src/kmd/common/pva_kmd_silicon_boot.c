// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_device.h"
#include "pva_fw_address_map.h"
#include "pva_fw_hyp.h"
#include "pva_kmd_shim_init.h"
#include "pva_kmd_thread_sema.h"
#include "pva_kmd_constants.h"
#include "pva_kmd_silicon_isr.h"
#include "pva_kmd_silicon_boot.h"
#include "pva_kmd_shim_silicon.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_limits.h"

static inline void pva_kmd_set_sema(struct pva_kmd_device *pva,
				    uint32_t sema_idx, uint32_t val)
{
	uint32_t gap = PVA_REG_HSP_SS1_SET_ADDR - PVA_REG_HSP_SS0_SET_ADDR;
	gap = safe_mulu32(gap, sema_idx);
	pva_kmd_write(pva, safe_addu32(PVA_REG_HSP_SS0_SET_ADDR, gap), val);
}

static void disable_sec_mission_error_reporting(struct pva_kmd_device *pva)
{
	pva_kmd_write(pva, PVA_REG_SEC_ERRSLICE0_MISSIONERR_ENABLE_ADDR, 0U);
	pva_kmd_write(pva, PVA_REG_SEC_ERRSLICE1_MISSIONERR_ENABLE_ADDR, 0U);
	pva_kmd_write(pva, PVA_REG_SEC_ERRSLICE2_MISSIONERR_ENABLE_ADDR, 0U);
	pva_kmd_write(pva, PVA_REG_SEC_ERRSLICE3_MISSIONERR_ENABLE_ADDR, 0U);
}

static void disable_sec_latent_error_reporting(struct pva_kmd_device *pva)
{
	pva_kmd_write(pva, PVA_REG_SEC_ERRSLICE0_LATENTERR_ENABLE_ADDR, 0U);
	pva_kmd_write(pva, PVA_REG_SEC_ERRSLICE1_LATENTERR_ENABLE_ADDR, 0U);
	pva_kmd_write(pva, PVA_REG_SEC_ERRSLICE2_LATENTERR_ENABLE_ADDR, 0U);
	pva_kmd_write(pva, PVA_REG_SEC_ERRSLICE3_LATENTERR_ENABLE_ADDR, 0U);
}

void pva_kmd_config_evp_seg_regs(struct pva_kmd_device *pva)
{
	uint64_t seg_reg_value;
	/* EVP */
	pva_kmd_write(pva, PVA_REG_EVP_RESET_ADDR, PVA_EVP_RESET_VECTOR);
	pva_kmd_write(pva, PVA_REG_EVP_UNDEF_ADDR,
		      PVA_EVP_UNDEFINED_INSTRUCTION_VECTOR);
	pva_kmd_write(pva, PVA_REG_EVP_SWI_ADDR, PVA_EVP_SVC_VECTOR);
	pva_kmd_write(pva, PVA_REG_EVP_PREFETCH_ABORT_ADDR,
		      PVA_EVP_PREFETCH_ABORT_VECTOR);
	pva_kmd_write(pva, PVA_REG_EVP_DATA_ABORT_ADDR,
		      PVA_EVP_DATA_ABORT_VECTOR);
	pva_kmd_write(pva, PVA_REG_EVP_RSVD_ADDR, PVA_EVP_RESERVED_VECTOR);
	pva_kmd_write(pva, PVA_REG_EVP_IRQ_ADDR, PVA_EVP_IRQ_VECTOR);
	pva_kmd_write(pva, PVA_REG_EVP_FIQ_ADDR, PVA_EVP_FIQ_VECTOR);
	/* R5 regions are defined as:
	 * - PRIV1 region for firmware code and data.
	 * - PRIV2 region for debug printf data.
	 * - Remaining region for resource table, queues, etc.
	 */
	pva_kmd_write(pva, pva->regspec.cfg_priv_ar1_start,
		      FW_CODE_DATA_START_ADDR);
	pva_kmd_write(pva, pva->regspec.cfg_priv_ar1_end,
		      FW_CODE_DATA_END_ADDR);
	pva_kmd_write(pva, pva->regspec.cfg_priv_ar2_start,
		      FW_DEBUG_DATA_START_ADDR);
	pva_kmd_write(pva, pva->regspec.cfg_priv_ar2_end,
		      FW_DEBUG_DATA_END_ADDR);
	/* Firmware expects R5 virtual address FW_CODE_DATA_START_ADDR to be
 	* mapped to the beginning of firmware binary. Therefore, we adjust
 	* segment registers accordingly
 	*
 	* */
	if (pva->load_from_gsc && pva->is_hv_mode) {
		/* Loading from GSC with HV (i.e AV+L or AV+Q case).
		 * This will be trapped by HV
		 */
		pva_kmd_write(pva, pva->regspec.cfg_priv_ar1_lsegreg,
			      0xFFFFFFFFU);
		pva_kmd_write(pva, pva->regspec.cfg_priv_ar1_usegreg,
			      0xFFFFFFFFU);
	} else {
		/* underflow is totally OK */
		seg_reg_value =
			pva->load_from_gsc ?
				      pva->fw_carveout.base_va -
					FW_CODE_DATA_START_ADDR : /* Load from GSC in L4T case */
				      pva->fw_bin_mem->iova -
					FW_CODE_DATA_START_ADDR; /* Boot from File case */

		pva_kmd_write(pva, pva->regspec.cfg_priv_ar1_lsegreg,
			      iova_lo(seg_reg_value));
		pva_kmd_write(pva, pva->regspec.cfg_priv_ar1_usegreg,
			      iova_hi(seg_reg_value));
	}
}

void pva_kmd_config_scr_regs(struct pva_kmd_device *pva)
{
	uint32_t scr_lock_mask =
		pva->is_silicon ? 0xFFFFFFFFU : (~PVA_SCR_LOCK);

	pva_kmd_write(pva, PVA_REG_EVP_SCR_ADDR,
		      PVA_EVP_SCR_VAL & scr_lock_mask);
	if (pva->is_silicon) {
		pva_kmd_write(pva, PVA_CFG_SCR_STATUS_CNTL,
			      PVA_STATUS_CTL_SCR_VAL & scr_lock_mask);
	} else {
		pva_kmd_write(pva, PVA_CFG_SCR_STATUS_CNTL,
			      PVA_STATUS_CTL_SCR_VAL_SIM & scr_lock_mask);
	}

	pva_kmd_write(pva, PVA_CFG_SCR_PRIV, PVA_PRIV_SCR_VAL & scr_lock_mask);
	pva_kmd_write(pva, PVA_CFG_SCR_CCQ_CNTL,
		      PVA_CCQ_SCR_VAL & scr_lock_mask);
}

void pva_kmd_config_sid(struct pva_kmd_device *pva)
{
	uint32_t addr;
	uint32_t i;
	uint32_t offset;
	uint8_t priv1_sid;
	uint8_t priv_sid;
	priv_sid = pva->stream_ids[PVA_R5_SMMU_CONTEXT_ID] & (uint8_t)0xFFU;
	priv1_sid =
		pva->stream_ids[pva->r5_image_smmu_context_id] & (uint8_t)0xFFU;

	/* Priv SIDs */
	if (pva->load_from_gsc) {
		pva_kmd_write(pva, pva->regspec.cfg_priv_sid,
			      PVA_INSERT(priv_sid, 7, 0) |
				      PVA_INSERT(priv1_sid, 15, 8) |
				      PVA_INSERT(priv_sid, 23, 16));
	} else {
		pva_kmd_write(pva, pva->regspec.cfg_priv_sid,
			      PVA_INSERT(priv_sid, 7, 0) |
				      PVA_INSERT(priv_sid, 15, 8) |
				      PVA_INSERT(priv_sid, 23, 16));
	}
	/* VPS SIDs  */
	if ((pva->hw_consts.hw_gen == PVA_HW_GEN3) && pva->load_from_gsc) {
		pva_kmd_write(pva, pva->regspec.cfg_vps_sid,
			      PVA_INSERT(priv1_sid, 7, 0) |
				      PVA_INSERT(priv1_sid, 15, 8));
	} else {
		pva_kmd_write(pva, pva->regspec.cfg_vps_sid,
			      PVA_INSERT(priv_sid, 7, 0) |
				      PVA_INSERT(priv_sid, 15, 8));
	}
	/* User SIDs */
	offset = 0;
	for (i = 1U; i < (pva->hw_consts.n_smmu_contexts - 1U); i++) {
		addr = safe_addu32(pva->regspec.cfg_user_sid_base, offset);
		pva_kmd_write(pva, addr, pva->stream_ids[i]);
		offset = safe_addu32(offset, 4U);
	}
}

static uint32_t get_syncpt_offset(struct pva_kmd_device *pva,
				  uint64_t syncpt_iova)
{
	if (pva->num_ro_syncpts > 0U) {
		uint64_t offset;
		offset = safe_subu64(syncpt_iova, pva_kmd_get_r5_iova_start());

		ASSERT(offset <= U32_MAX);
		return (uint32_t)offset;
	} else {
		// This is only for SIM mode where syncpoints are not supported.
		return PVA_R5_SYNCPT_REGION_IOVA_OFFSET_NOT_SET;
	}
}

enum pva_error pva_kmd_load_fw(struct pva_kmd_device *pva)
{
	uint64_t seg_reg_value;
	uint32_t debug_data_size;
	uint32_t boot_sema = 0;
	enum pva_error err = PVA_SUCCESS;
	uint32_t checkpoint;
	uint32_t scr_lock_mask =
		pva->is_silicon ? 0xFFFFFFFFU : (~PVA_SCR_LOCK);

	/* Load firmware */
	if (!pva->load_from_gsc) {
		err = pva_kmd_read_fw_bin(pva);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"Failed to read firmware from filesystem");
			goto out;
		}
	}

	debug_data_size = (uint32_t)safe_pow2_roundup_u32(
		FW_DEBUG_DATA_TOTAL_SIZE, SIZE_4KB);
	pva->fw_debug_mem = pva_kmd_device_memory_alloc_map(
		debug_data_size, pva, PVA_ACCESS_RW, PVA_R5_SMMU_CONTEXT_ID);
	if (pva->fw_debug_mem == NULL) {
		err = PVA_NOMEM;
		pva_kmd_log_err(
			"pva_kmd_device_memory_alloc_map failed in pva_kmd_load_fw");
		goto free_fw_mem;
	}
	pva_kmd_init_fw_print_buffer(pva, pva->fw_debug_mem->va);
	pva->debugfs_context.r5_ocd_stage_buffer = pva->fw_debug_mem->va;

	/* Program SCRs */
	pva_kmd_write(pva, PVA_SEC_SCR_SECEXT_INTR_EVENT,
		      (PVA_SEC_SCR_SECEXT_INTR_EVENT_VAL & scr_lock_mask));
	pva_kmd_write(pva, PVA_PROC_SCR_PROC,
		      (PVA_PROC_SCR_PROC_VAL & scr_lock_mask));

	pva_kmd_config_evp_seg_scr_regs(pva);

	/* Write IOVA address of debug buffer to mailbox and FW will program
	 * PRIV2 segment register properly such that the debug buffer is located
	 * at R5 virtual address FW_DEBUG_DATA_START_ADDR */
	seg_reg_value = pva->fw_debug_mem->iova;

	/* When GSC is enabled, KMD cannot write directly to segment registers,
	 * therefore we write to mailbox registers and FW will program by
	 * itself.
	 * pva_kmd_writel(pva, pva->regspec.cfg_priv_ar2_lsegreg,
	 *	       iova_lo(seg_reg_value));
	 * pva_kmd_writel(pva, pva->regspec.cfg_priv_ar2_usegreg,
	 *             iova_hi(seg_reg_value));
	 */
	pva_kmd_write_mailbox(pva, PVA_MBOXID_PRIV2SEG_L,
			      iova_lo(seg_reg_value));
	pva_kmd_write_mailbox(pva, PVA_MBOXID_PRIV2SEG_H,
			      iova_hi(seg_reg_value));

	/* Write shared memory allocation start address to mailbox and FW will
	 * program user segment register accordingly so that virtual address
	 * PVA_SHARED_MEMORY_START will point to the allocation start address.
	 */
	seg_reg_value = pva_kmd_get_r5_iova_start();
	pva_kmd_write_mailbox(pva, PVA_MBOXID_USERSEG_L,
			      iova_lo(seg_reg_value));
	pva_kmd_write_mailbox(pva, PVA_MBOXID_USERSEG_H,
			      iova_hi(seg_reg_value));

	/* Boot parameters  */
	if (pva->bl_sector_pack_format == PVA_BL_XBAR_RAW) {
		boot_sema = PVA_BOOT_SEMA_USE_XBAR_RAW;
	}
#if SYSTEM_TESTS_ENABLED == 1
	if (pva->test_mode) {
		boot_sema |= PVA_BOOT_SEMA_TEST_MODE;
	}
#endif
	pva_kmd_set_sema(pva, PVA_BOOT_SEMA, boot_sema);

	pva_kmd_set_sema(pva, PVA_RO_SYNC_BASE_SEMA,
			 get_syncpt_offset(pva, pva->ro_syncpt_base_iova));
	pva_kmd_set_sema(pva, PVA_RW_SYNC_BASE_SEMA,
			 get_syncpt_offset(pva, pva->rw_syncpt_base_iova));
	pva_kmd_set_sema(pva, PVA_RW_SYNC_SIZE_SEMA,
			 pva->rw_syncpt_region_size);

	pva_kmd_config_sid_regs(pva);

	/* Enable LIC INTR line for HSP1 and WDT */
	pva_kmd_write(pva, pva->regspec.sec_lic_intr_enable,
		      PVA_BIT(0) /*Watchdog*/
			      | PVA_INSERT(0x1, 4, 1) /* HSP1 */
			      | PVA_INSERT(0x3, 7, 5) /* All H1X errors */);

	/* Bind interrupts */
	err = pva_kmd_bind_intr_handler(pva, PVA_KMD_INTR_LINE_SEC_LIC,
					pva_kmd_hyp_isr, pva);
	if (err != PVA_SUCCESS) {
		goto free_fw_debug_mem;
	}

	/* Take R5 out of reset */
	pva_kmd_write(pva, PVA_REG_PROC_CPUHALT_ADDR, 0x1);

	/* Wait until fw boots */
	err = pva_kmd_sema_wait_timeout(&pva->fw_boot_sema,
					PVA_KMD_FW_BOOT_TIMEOUT_MS);

	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Waiting for FW boot timed out.");
		/* show checkpoint value here*/
		checkpoint = pva_kmd_read(
			pva, pva->regspec.ccq_regs[PVA_PRIV_CCQ_ID]
				     .status[PVA_REG_CCQ_STATUS6_IDX]);
		pva_kmd_log_err_hex32("Checkpoint value:", checkpoint);
		pva_kmd_report_error_fsi(pva, (uint32_t)err);
		goto free_sec_lic;
	}

	return err;

free_sec_lic:
	pva_kmd_free_intr(pva, PVA_KMD_INTR_LINE_SEC_LIC);
free_fw_debug_mem:
	pva_kmd_drain_fw_print(pva);
	pva_kmd_freeze_fw(pva);
	pva_kmd_device_memory_free(pva->fw_debug_mem);
free_fw_mem:
	if (!pva->load_from_gsc) {
		pva_kmd_device_memory_free(pva->fw_bin_mem);
	}
out:
	return err;
}

void pva_kmd_freeze_fw(struct pva_kmd_device *pva)
{
	/*
	 * Before freezing PVA, disable SEC error reporting.
	 * While setting the reset line, PVA might generate (unexplained) error
	 * interrupts This causes HSM to read some PVA SEC registers. However,
	 * since PVA might already be powergated by this time, access to PVA SEC
	 * registers from HSM fails. This was discussed in Bug 3785498.
	 *
	 * Note: we do not explicity enable these errors during power on since
	 *	 'enable' is their reset value
	 */
	disable_sec_mission_error_reporting(pva);
	disable_sec_latent_error_reporting(pva);

	pva_kmd_set_reset_line(pva);
}

void pva_kmd_unload_fw(struct pva_kmd_device *pva)
{
	pva_kmd_free_intr(pva, PVA_KMD_INTR_LINE_SEC_LIC);
	pva_kmd_drain_fw_print(pva);

	// FW so that we can free memory
	pva_kmd_freeze_fw(pva);

	pva_kmd_device_memory_free(pva->fw_debug_mem);
	if (!pva->load_from_gsc) {
		pva_kmd_device_memory_free(pva->fw_bin_mem);
	}
}
