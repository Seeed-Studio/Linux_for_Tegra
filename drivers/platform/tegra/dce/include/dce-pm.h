/* SPDX-License-Identifier: MIT */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef DCE_PM_H
#define DCE_PM_H

struct tegra_dce;

struct dce_sc7_state {
	uint32_t hsp_ie;
};

int dce_pm_init(struct tegra_dce *d);
void dce_pm_deinit(struct tegra_dce *d);
int dce_pm_enter_sc7(struct tegra_dce *d);
int dce_pm_exit_sc7(struct tegra_dce *d);
int dce_pm_handle_sc7_enter_requested_event(struct tegra_dce *d, void *params);
int dce_pm_handle_sc7_enter_received_event(struct tegra_dce *d, void *params);
int dce_pm_handle_sc7_exit_received_event(struct tegra_dce *d, void *params);

#endif
