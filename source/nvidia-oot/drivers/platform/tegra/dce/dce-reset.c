// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <dce.h>
#include <dce-os-log.h>
#include <dce-os-utils.h>
#include <dce-linux-device.h>
#include <linux/reset.h>

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

/**
 * dce_reset_dce_module - Reset DCE module using reset command
 *
 * @d : Pointer to tegra_dce struct.
 *
 * This function sends a reset command to BPMP to reset the DCE module.
 * which performs a module reset.
 *
 * Return : 0 if success, negative error code on failure
 *          -ENODEV if BPMP is not available
 *          -EINVAL if reset command failed
 */
int dce_reset_dce_module(struct tegra_dce *d)
{
	struct dce_linux_device *d_dev;
	int err;

	if (!d)
		return -EINVAL;

	d_dev = dce_linux_device_from_dce(d);
	if (!d_dev)
		return -ENODEV;

	/* Reset the DCE
	 * Current code support reset only if load_dce_fw_by_psc is enabled
	 * TODO: Enable this feature on all SOCs which bpmp allows the reset
	 */
	if (d_dev->rst && d_dev->dce_fw_load_by_psc) {
		err = reset_control_reset(d_dev->rst);
		if (err) {
			dce_os_err(d, "failed to reset DCE with err = %d\n", err);
			return err;
		}
	}

	return 0;
}
