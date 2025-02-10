/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2024, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef _PVA_REGS_H_
#define _PVA_REGS_H_

#include "pva-bit.h"
#include "hw_cfg_pva_v1.h"
#include "hw_cfg_pva_v2.h"
#include "hw_dma_ch_pva.h"
#include "hw_dma_desc_pva.h"
#include "hw_proc_pva.h"
#include "hw_hsp_pva.h"
#include "hw_sec_pva_v1.h"
#include "hw_sec_pva_v2.h"
#include "hw_evp_pva.h"
#include "pva-interface.h"
#include "pva_mailbox.h"
#include "pva-ucode-header.h"

/**
 * @defgroup PVA_SCR_VALUES
 *
 * @brief Following macros specify SCR firewall values that are expected to be
 * programmed by PVA driver.
 * @{
 */
/**
 * @brief SEC EXT EVENT SCR firewall to enable only CCPLEX and R5 read/write access.
 */
#define PVA_SEC_SCR_SECEXT_INTR_EVENT_0_VAL (0x39008282U)

/**
 * @brief PROC SCR firewall to enable only CCPLEX read/write and R5 read access.
 */
#define PVA_PROC_SCR_PROC_0_VAL             (0x39000282U)
/** @} */

/**
 * @brief Macro to set lock bit of SCR firewall register.
 */
#define PVA_LOCK_SCR (0x20000000U)

/* Definition for LIC_INTR_ENABLE bits */
#define SEC_LIC_INTR_HSP1	0x1
#define SEC_LIC_INTR_HSP2	0x2
#define SEC_LIC_INTR_HSP3	0x4
#define SEC_LIC_INTR_HSP4	0x8
#define SEC_LIC_INTR_HSP_ALL	0xF
#define SEC_LIC_INTR_H1X_ALL_23	0x3
#define SEC_LIC_INTR_H1X_ALL_19	0x7

/* Watchdog support */
#define SEC_LIC_INTR_WDT	0x1

#define SEC_BASE_COMMON		0x20000U

/* unified register interface for both v1 and v2 */
static inline u32 sec_lic_intr_status_r(int version)
{
	if (version == 1)
		return v1_sec_lic_intr_status_r();
	else
		return v2_sec_lic_intr_status_r();
}

static inline u32 cfg_ccq_status_r(int version, u32 ccq_idx, u32 status_idx)
{
	if (version == 1)
		return v1_cfg_ccq_status_r(status_idx);
	else
		return v2_cfg_ccq_status_r(ccq_idx, status_idx);
}

static inline u32 cfg_ccq_r(int version, u32 ccq_idx)
{
	if (version == 1)
		return v1_cfg_ccq_r();
	else
		return v2_cfg_ccq_r(ccq_idx);
}

static inline u32 cfg_r5user_lsegreg_r(int version)
{
	if (version == 1)
		return v1_cfg_r5user_lsegreg_r();
	else
		return v2_cfg_r5user_lsegreg_r();
}

static inline u32 cfg_priv_ar1_lsegreg_r(int version)
{
	if (version == 1)
		return v1_cfg_priv_ar1_lsegreg_r();
	else
		return v2_cfg_priv_ar1_lsegreg_r();
}

static inline u32 cfg_priv_ar2_lsegreg_r(int version)
{
	if (version == 1)
		return v1_cfg_priv_ar2_lsegreg_r();
	else
		return v2_cfg_priv_ar2_lsegreg_r();
}

static inline u32 cfg_r5user_usegreg_r(int version)
{
	if (version == 1)
		return v1_cfg_r5user_usegreg_r();
	else
		return v2_cfg_r5user_usegreg_r();
}

static inline u32 cfg_priv_ar1_usegreg_r(int version)
{
	if (version == 1)
		return v1_cfg_priv_ar1_usegreg_r();
	else
		return v2_cfg_priv_ar1_usegreg_r();
}

static inline u32 cfg_priv_ar2_usegreg_r(int version)
{
	if (version == 1)
		return v1_cfg_priv_ar2_usegreg_r();
	else
		return v2_cfg_priv_ar2_usegreg_r();
}

static inline u32 cfg_priv_ar1_start_r(int version)
{
	if (version == 1)
		return v1_cfg_priv_ar1_start_r();
	else
		return v2_cfg_priv_ar1_start_r();
}

static inline u32 cfg_priv_ar1_end_r(int version)
{
	if (version == 1)
		return v1_cfg_priv_ar1_end_r();
	else
		return v2_cfg_priv_ar1_end_r();
}

static inline u32 cfg_priv_ar2_start_r(int version)
{
	if (version == 1)
		return v1_cfg_priv_ar2_start_r();
	else
		return v2_cfg_priv_ar2_start_r();
}

static inline u32 cfg_priv_ar2_end_r(int version)
{
	if (version == 1)
		return v1_cfg_priv_ar2_end_r();
	else
		return v2_cfg_priv_ar2_end_r();
}

static inline u32 sec_lic_intr_enable_r(int version)
{
	if (version == 1)
		return v1_sec_lic_intr_enable_r();
	else
		return v2_sec_lic_intr_enable_r();
}

static inline u32 hwpm_get_offset(void)
{
	return 0x200000;
}

static inline u32 sec_ec_errslice0_missionerr_enable_r(void)
{
	return (SEC_BASE_COMMON + 0x30U);
}

static inline u32 sec_ec_errslice1_missionerr_enable_r(void)
{
	return (SEC_BASE_COMMON + 0x60U);
}

static inline u32 sec_ec_errslice2_missionerr_enable_r(void)
{
	return (SEC_BASE_COMMON + 0x90U);
}

static inline u32 sec_ec_errslice3_missionerr_enable_r(void)
{
	return (SEC_BASE_COMMON + 0xC0U);
}

static inline u32 sec_ec_errslice0_latenterr_enable_r(void)
{
	return (SEC_BASE_COMMON + 0x40U);
}

static inline u32 sec_ec_errslice1_latenterr_enable_r(void)
{
	return (SEC_BASE_COMMON + 0x70U);
}

static inline u32 sec_ec_errslice2_latenterr_enable_r(void)
{
	return (SEC_BASE_COMMON + 0xA0U);
}

static inline u32 sec_ec_errslice3_latenterr_enable_r(void)
{
	return (SEC_BASE_COMMON + 0xD0U);
}

static inline u32 scr_secext_intr_event_0_r(void)
{
	return (SEC_BASE_COMMON + 0x8804U);
}

static inline u32 scr_proc_0_r(void)
{
	return (SEC_BASE_COMMON + 0x10800U);
}
#endif
