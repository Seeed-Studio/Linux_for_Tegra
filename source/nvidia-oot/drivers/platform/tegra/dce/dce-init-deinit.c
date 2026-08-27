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
	if (ret != 0) {
		dce_os_err(d, "dce boot interface init failed");
		goto err_boot_interface_init;
	}

	ret = dce_admin_init(d);
	if (ret != 0) {
		dce_os_err(d, "dce admin interface init failed");
		goto err_admin_interface_init;
	}

	ret = dce_client_init(d);
	if (ret != 0) {
		dce_os_err(d, "dce client workqueue init failed");
		goto err_client_init;
	}

	ret = dce_pm_init(d);
	if (ret != 0) {
		dce_os_err(d, "Failed to init DCE Power management");
		goto err_pm_init;
	}

	ret = dce_waiters_init(d);
	if (ret != 0) {
		dce_os_err(d, "dce sw resource init failed");
		goto err_sw_init;
	}

	ret = dce_fsm_init_unlocked(d);
	if (ret != 0) {
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
 * dce_handle_deinit_requested_event - callback handler function for event
 *					 EVENT_ID_DCE_DEINIT_REQUESTED
 *
 * @d : Pointer to tegra_dce struct.
 * @params : callback params
 *
 * Return : 0 if successful else error code
 */
int dce_handle_deinit_requested_event(struct tegra_dce *d, void *params)
{
	int ret = 0;
	struct dce_ipc_message *msg = NULL;

	DCE_WARN_ON_NOT_NULL(params);

	dce_os_err(d, "Deinit called\n");
	msg = dce_admin_channel_client_buffer_get(d, DCE_ADMIN_CH_CL_PM_BUFF,
		0 /* reserved flags */);
	if (!msg) {
		dce_os_err(d, "IPC msg allocation failed");
		ret = -1;
		goto out;
	}

	ret = dce_admin_send_cmd_deinit(d, msg);
	if (ret) {
		dce_os_err(d, "Deinit failed [%d]", ret);
		goto out;
	}

out:
	if (msg)
		dce_admin_channel_client_buffer_put(d, msg);
	return ret;
}

/**
 * dce_handle_deinit_done_received_event - callback handler function for event
 *					 EVENT_ID_DCE_DEINIT_DONE_RECEIVED
 *
 * @d : Pointer to tegra_dce struct.
 * @params : callback params
 *
 * Return : 0 if successful else error code
 */
int dce_handle_deinit_done_received_event(struct tegra_dce *d, void *params)
{
	DCE_WARN_ON_NOT_NULL(params);

	dce_wait_cond_signal_interruptible(d, &d->ipc_waits[DCE_WAIT_DEINIT]);
	return 0;
}

/**
 * dce_deinit - Deinitialize DCE by sending deinit command to firmware
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : 0 if successful, error code otherwise
 */
int dce_deinit(struct tegra_dce *d)
{
	int ret = 0;

	/* Check if bootstrap is done, if not, nothing to deinit */
	if (!dce_is_bootstrap_done(d)) {
		dce_os_debug(d, "Bootstrap not done, nothing to deinit");
		return 0;
	}

	/* Post DEINIT_REQUESTED event to FSM */
	ret = dce_fsm_post_event(d, EVENT_ID_DCE_DEINIT_REQUESTED, NULL);
	if (ret) {
		dce_os_err(d, "Error while posting DEINIT_REQUESTED event [%d]", ret);
		return ret;
	}

	return 0;
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
