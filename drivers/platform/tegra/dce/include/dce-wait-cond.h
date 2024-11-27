/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
