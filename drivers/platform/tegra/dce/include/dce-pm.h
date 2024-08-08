/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_PM_H
#define DCE_PM_H

#include <dce.h>

struct dce_sc7_state {
	uint32_t hsp_ie;
};

int dce_pm_init(struct tegra_dce *d);
void dce_pm_deinit(struct tegra_dce *d);
int dce_pm_enter_sc7(struct tegra_dce *d);
int dce_pm_exit_sc7(struct tegra_dce *d);
void dce_resume_work_fn(struct tegra_dce *d);
int dce_pm_handle_sc7_enter_requested_event(struct tegra_dce *d, void *params);
int dce_pm_handle_sc7_enter_received_event(struct tegra_dce *d, void *params);
int dce_pm_handle_sc7_exit_received_event(struct tegra_dce *d, void *params);

#endif
