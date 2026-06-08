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

#ifndef DCE_WAIT_COND_H
#define DCE_WAIT_COND_H

#include <dce-os-cond.h>
#include <dce-os-lock.h>
#include <dce-os-atomic.h>

struct tegra_dce;

struct dce_wait_cond {
	dce_os_atomic_t complete;
	struct dce_os_cond cond_wait;
};

int dce_wait_cond_init(struct tegra_dce *d, struct dce_wait_cond *wait);
void dce_wait_cond_deinit(struct tegra_dce *d, struct dce_wait_cond *wait);
int dce_wait_cond_wait_interruptible(struct tegra_dce *d, struct dce_wait_cond *wait,
			bool reset, u32 timeout_ms);
void dce_wait_cond_signal_interruptible(struct tegra_dce *d, struct dce_wait_cond *wait);
void dce_wait_cond_reset(struct tegra_dce *d, struct dce_wait_cond *wait);
void dce_wait_cond_broadcast_interruptible(struct tegra_dce *d, struct dce_wait_cond *wait);

#endif /* DCE_WAIT_COND_H */
