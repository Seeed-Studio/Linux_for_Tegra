// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <dce.h>
#include <dce-os-utils.h>

/**
 * dce_driver_start - Start executing DCE logic
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Returns void
 */
void dce_driver_start(struct tegra_dce *d)
{
	dce_fsm_start(d);
}

/**
 * dce_driver_stop - Stop executing DCE logic
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Returns void
 */
void dce_driver_stop(struct tegra_dce *d)
{
	dce_fsm_stop(d);
}

/**
 * dce_driver_init - Initializes the various sw components
 *					and few hw elements dce.
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : 0 if successful.
 */
int dce_driver_init(struct tegra_dce *d)
{
	int ret = 0;

	/**
	 * Set dce boot satus to false
	 */
	dce_set_boot_complete(d, false);

	ret = dce_boot_interface_init(d);
	if (ret) {
		dce_os_err(d, "dce boot interface init failed");
		goto err_boot_interface_init;
	}

	ret = dce_admin_init(d);
	if (ret) {
		dce_os_err(d, "dce admin interface init failed");
		goto err_admin_interface_init;
	}

	ret = dce_client_init(d);
	if (ret) {
		dce_os_err(d, "dce client workqueue init failed");
		goto err_client_init;
	}

	ret = dce_pm_init(d);
	if (ret) {
		dce_os_err(d, "Failed to init DCE Power management");
		goto err_pm_init;
	}

	ret = dce_waiters_init(d);
	if (ret) {
		dce_os_err(d, "dce sw resource init failed");
		goto err_sw_init;
	}

	ret = dce_fsm_init_unlocked(d);
	if (ret) {
		dce_os_err(d, "dce FSM init failed");
		goto err_fsm_init;
	}

	return ret;

err_fsm_init:
	dce_waiters_deinit(d);
err_sw_init:
	dce_pm_deinit(d);
err_pm_init:
	dce_client_deinit(d);
err_client_init:
	dce_admin_deinit(d);
err_admin_interface_init:
	dce_boot_interface_deinit(d);
err_boot_interface_init:
	d->boot_status |= DCE_STATUS_FAILED;
	return ret;
}

/**
 * dce_driver_deinit - Release various sw resources
 *					associated with dce.
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : Void
 */

void dce_driver_deinit(struct tegra_dce *d)
{
	/*  TODO : Reset DCE ? */

	dce_fsm_deinit_unlocked(d);

	dce_waiters_deinit(d);

	dce_pm_deinit(d);

	dce_client_deinit(d);

	dce_admin_deinit(d);

	dce_boot_interface_deinit(d);

	dce_os_release_fw(d, d->fw_data);
}
