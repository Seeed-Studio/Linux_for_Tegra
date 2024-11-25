// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <dce.h>
#include <dce-os-log.h>
#include <dce-os-utils.h>
#include <dce-linux-device.h>

enum pm_controls {
	FW_LOAD_HALTED,
	FW_LOAD_DONE
};

/**
 * dce_evp_set_reset_addr - Writes to the evp reset addr register.
 *
 * @d : Pointer to struct tegra_dce
 * @addr : 32bit address
 *
 * Return : Void
 */
static inline void dce_evp_set_reset_addr(struct tegra_dce *d, u32 addr)
{
	dce_os_writel(d, evp_reset_addr_r(), addr);
}

/**
 * dce_pm_set_pm_ctrl - Writes to the reset control register.
 *
 * @d : Pointer to struct tegra_dce
 * @val : Value to programmed to the register
 *
 * Return : Void
 */
static void dce_pm_set_pm_ctrl(struct tegra_dce *d, enum pm_controls val)
{
	switch (val) {
	case FW_LOAD_DONE:
		dce_os_writel(d, pm_r5_ctrl_r(), pm_r5_ctrl_fwloaddone_done_f());
		break;
	case FW_LOAD_HALTED:
		dce_os_writel(d, pm_r5_ctrl_r(), pm_r5_ctrl_fwloaddone_halted_f());
		break;
	default:
		break;
	}
}

/**
 * dce_reset_dce - Configures the pertinent registers in
 *					DCE cluser to reset DCE.
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : 0 if success
 */
int dce_reset_dce(struct tegra_dce *d)
{
	u32 fw_dce_addr;

	if (!d->fw_data) {
		dce_os_err(d, "No fw_data present");
		return -1;
	}

	fw_dce_addr = pdata_from_dce_linux_device(d)->fw_dce_addr;
	dce_evp_set_reset_addr(d, fw_dce_addr);

	dce_pm_set_pm_ctrl(d, FW_LOAD_DONE);

	return 0;
}
