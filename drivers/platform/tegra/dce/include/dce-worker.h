/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_WORKER_H
#define DCE_WORKER_H

#include <os-cond.h>
#include <os-lock.h>
#include <dce-thread.h>
#include <atomic.h>

struct tegra_dce;

#define DCE_WAIT_BOOT_COMPLETE		0
#define DCE_WAIT_BOOT_CMD		1
#define DCE_WAIT_ADMIN_IPC		2
#define DCE_WAIT_SC7_ENTER		3
#define DCE_WAIT_LOG			4
#define DCE_MAX_WAIT			5

struct dce_wait_cond {
	os_atomic_t complete;
	struct dce_cond cond_wait;
};

int dce_work_cond_sw_resource_init(struct tegra_dce *d);
void dce_work_cond_sw_resource_deinit(struct tegra_dce *d);
void dce_schedule_boot_complete_wait_worker(struct tegra_dce *d);
int dce_wait_interruptible(struct tegra_dce *d, u32 msg_id);
void dce_wakeup_interruptible(struct tegra_dce *d, u32 msg_id);
void dce_cond_wait_reset(struct tegra_dce *d, u32 msg_id);

#endif
