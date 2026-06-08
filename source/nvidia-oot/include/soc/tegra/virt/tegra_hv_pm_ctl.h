/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef TEGRA_HV_PM_CTL_H
#define TEGRA_HV_PM_CTL_H

extern int (*tegra_hv_pm_ctl_prepare_shutdown)(void);

int tegra_hv_pm_ctl_trigger_sys_suspend(void);
int tegra_hv_pm_ctl_trigger_sys_shutdown(void);
int tegra_hv_pm_ctl_trigger_sys_reboot(void);

#endif /* TEGRA_HV_PM_CTL_H */
