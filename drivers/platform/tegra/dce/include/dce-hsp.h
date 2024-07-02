/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_HSP_H
#define DCE_HSP_H

#include <types.h>

struct tegra_dce;

struct dce_hsp_fn {
	u32  (*ss_get_state)(struct tegra_dce *d, u8 hsp_id, u8 id);
	void (*ss_set)(struct tegra_dce *d, u8 bpos, u8 hsp_id, u8 id);
	void (*ss_clear)(struct tegra_dce *d, u8 bpos, u8 hsp_id, u8 id);

	void (*smb_set)(struct tegra_dce *d, u32 val, u8 hsp_id, u8 id);
	void (*smb_set_full_ie)(struct tegra_dce *d, bool en, u8 hsp_id, u8 id);
	u32  (*smb_read_full_ie)(struct tegra_dce *d, u8 hsp_id, u8 id);
	void (*smb_set_empty_ie)(struct tegra_dce *d, bool en, u8 hsp_id, u8 id);
	u32  (*smb_read)(struct tegra_dce *d, u8 hsp_id, u8 id);

	u32  (*hsp_ie_read)(struct tegra_dce *d, u8 hsp_id, u8 id);
	void (*hsp_ie_write)(struct tegra_dce *d, u32 val, u8 hsp_id, u8 id);
	u32  (*hsp_ir_read)(struct tegra_dce *d, u8 hsp_id);
};

#endif
