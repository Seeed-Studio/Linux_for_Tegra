/* SPDX-License-Identifier: MIT */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef DCE_HSP_H
#define DCE_HSP_H

#include <dce-os-types.h>

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
