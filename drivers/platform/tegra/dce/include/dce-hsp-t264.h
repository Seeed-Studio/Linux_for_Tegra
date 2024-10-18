/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef DCE_HSP_T264_H
#define DCE_HSP_T264_H

#include <linux/types.h> // TODO: use dce-types

struct tegra_dce;

#define DCE_MAX_HSP_T264	2
#define DCE_MAX_NO_SS_T264	4
#define DCE_MAX_NO_SMB_T264	8
#define DCE_MAX_HSP_IE_T264	8

/**
 * DCE HSP Shared Semaphore Utility functions. Description
 * can be found with function definitions.
 */
u32 dce_ss_get_state_t264(struct tegra_dce *d, u8 hsp_id, u8 id);
void dce_ss_set_t264(struct tegra_dce *d, u8 bpos, u8 hsp_id, u8 id);
void dce_ss_clear_t264(struct tegra_dce *d, u8 bpos, u8 hsp_id, u8 id);

/**
 * DCE HSP Shared Mailbox Utility functions.  Description
 * can be found with function definitions.
 */
void dce_smb_set_t264(struct tegra_dce *d, u32 val, u8 hsp_id, u8 id);
void dce_smb_set_full_ie_t264(struct tegra_dce *d, bool en, u8 hsp_id, u8 id);
u32 dce_smb_read_full_ie_t264(struct tegra_dce *d, u8 hsp_id, u8 id);
void dce_smb_set_empty_ie_t264(struct tegra_dce *d, bool en, u8 hsp_id, u8 id);
u32 dce_smb_read_t264(struct tegra_dce *d, u8 hsp_id, u8 id);
u32 dce_hsp_ie_read_t264(struct tegra_dce *d, u8 hsp_id, u8 id);
void dce_hsp_ie_write_t264(struct tegra_dce *d, u32 val, u8 hsp_id, u8 id);
u32 dce_hsp_ir_read_t264(struct tegra_dce *d, u8 hsp_id);

#define DCE_HSP_INIT_T264(hsp)					\
({								\
	hsp.ss_get_state	= dce_ss_get_state_t264;	\
	hsp.ss_set		= dce_ss_set_t264;		\
	hsp.ss_clear		= dce_ss_clear_t264;		\
	hsp.smb_set		= dce_smb_set_t264;		\
	hsp.smb_set_full_ie	= dce_smb_set_full_ie_t264;	\
	hsp.smb_read_full_ie	= dce_smb_read_full_ie_t264;	\
	hsp.smb_set_empty_ie	= dce_smb_set_empty_ie_t264;	\
	hsp.smb_read		= dce_smb_read_t264;		\
	hsp.hsp_ie_read		= dce_hsp_ie_read_t264;		\
	hsp.hsp_ie_write	= dce_hsp_ie_write_t264;	\
	hsp.hsp_ir_read		= dce_hsp_ir_read_t264;		\
})

#endif
