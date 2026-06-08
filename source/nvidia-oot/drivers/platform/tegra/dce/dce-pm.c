// SPDX-License-Identifier: MIT
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

#include <dce.h>
#include <dce-os-utils.h>

#define CCPLEX_HSP_IE 1U /* TODO : Have an api to read from platform data */

static void dce_pm_save_state(struct tegra_dce *d)
{
	d->sc7_state.hsp_ie = d->hsp.hsp_ie_read(d, d->hsp_id, CCPLEX_HSP_IE);
}

static void dce_pm_restore_state(struct tegra_dce *d)
{
	uint32_t val = d->sc7_state.hsp_ie;

	d->hsp.hsp_ie_write(d, val, d->hsp_id, CCPLEX_HSP_IE);
}

/**
 * dce_resume_work_fn : execute resume and bootstrap flow
 *
 * @data : Pointer to callback data.
 *
 * Return : void
 */
static void dce_resume_work_fn(void *data)
{
	int ret = 0;
	struct tegra_dce *d = (struct tegra_dce *) data;

	if (d == NULL) {
		dce_os_err(d, "tegra_dce struct is NULL");
		return;
	}

	ret = dce_fsm_post_event(d, EVENT_ID_DCE_BOOT_COMPLETE_REQUESTED, NULL);
	if (ret) {
		dce_os_err(d, "Error while posting DCE_BOOT_COMPLETE_REQUESTED event");
		return;
	}

	ret = dce_start_boot_flow(d);
	if (ret) {
		dce_os_err(d, "DCE bootstrapping failed\n");
		return;
	}
}

/**
 * dce_handle_sc7_enter_requested_event - callback handler function for event
 *					 EVENT_ID_DCE_SC7_ENTER_REQUESTED
 *
 * @d : Pointer to tegra_dce struct.
 * @params : callback params
 *
 * Return : 0 if successful else error code
 */
int dce_pm_handle_sc7_enter_requested_event(struct tegra_dce *d, void *params)
{
	int ret = 0;
	struct dce_ipc_message *msg = NULL;

	DCE_WARN_ON_NOT_NULL(params);

	msg = dce_admin_channel_client_buffer_get(d, DCE_ADMIN_CH_CL_PM_BUFF,
		0 /* reserved flags */);
	if (!msg) {
		dce_os_err(d, "IPC msg allocation failed");
		goto out;
	}

	ret = dce_admin_send_enter_sc7(d, msg);
	if (ret) {
		dce_os_err(d, "Enter SC7 failed [%d]", ret);
		goto out;
	}

	dce_set_boot_complete(d, false);
	d->boot_status |= DCE_FW_SUSPENDED;

out:
	if (msg)
		dce_admin_channel_client_buffer_put(d, msg);
	return ret;
}

/**
 * dce_handle_sc7_enter_received_event - callback handler function for event
 *					 EVENT_ID_DCE_SC7_ENTER_RECEIVED
 *
 * @d : Pointer to tegra_dce struct.
 * @params : callback params
 *
 * Return : 0 if successful else error code
 */
int dce_pm_handle_sc7_enter_received_event(struct tegra_dce *d, void *params)
{
	DCE_WARN_ON_NOT_NULL(params);

	dce_wait_cond_signal_interruptible(d, &d->ipc_waits[DCE_WAIT_SC7_ENTER]);
	return 0;
}

/**
 * dce_handle_sc7_exit_received_event - callback handler function for event
 *					 EVENT_ID_DCE_SC7_EXIT_RECEIVED
 *
 * @d : Pointer to tegra_dce struct.
 * @params : callback params
 *
 * Return : 0 if successful else error code
 */
int dce_pm_handle_sc7_exit_received_event(struct tegra_dce *d, void *params)
{
	int ret = 0;

	DCE_WARN_ON_NOT_NULL(params);

	ret = dce_os_wq_work_schedule(d, NULL /* default WQ */,
			d->dce_resume_work);
	if (ret)
		dce_os_err(d, "Failed to schedule dce resume work");

	return ret;
}

int dce_pm_enter_sc7(struct tegra_dce *d)
{
	int ret = 0;
	struct dce_ipc_message *msg = NULL;

	/*
	 * If Bootstrap is not yet done. Nothing to do during SC7 Enter
	 * Return success immediately.
	 */
	if (!dce_is_bootstrap_done(d)) {
		dce_os_debug(d, "Bootstrap not done, Succeed SC7 enter\n");
		goto out;
	}

	msg = dce_admin_channel_client_buffer_get(d, DCE_ADMIN_CH_CL_ADMIN_BUFF,
		0 /* reserved flags */);
	if (!msg) {
		dce_os_err(d, "IPC msg allocation failed");
		ret = -1;
		goto out;
	}

	dce_pm_save_state(d);

	ret = dce_admin_send_prepare_sc7(d, msg);
	if (ret) {
		dce_os_err(d, "Prepare SC7 failed [%d]", ret);
		ret = -1;
		goto out;
	}

	ret = dce_fsm_post_event(d, EVENT_ID_DCE_SC7_ENTER_REQUESTED, NULL);
	if (ret) {
		dce_os_err(d, "Error while posting SC7_ENTER event [%d]", ret);
		ret = -1;
		goto out;
	}

out:
	if (msg)
		dce_admin_channel_client_buffer_put(d, msg);
	return ret;
}

int dce_pm_exit_sc7(struct tegra_dce *d)
{
	int ret = 0;

	dce_pm_restore_state(d);

	ret = dce_fsm_post_event(d, EVENT_ID_DCE_SC7_EXIT_RECEIVED, NULL);
	if (ret) {
		dce_os_err(d, "Error while posting SC7_EXIT event [%d]", ret);
		goto out;
	}
out:
	return ret;
}

int dce_pm_init(struct tegra_dce *d)
{
	int ret = 0;

	ret = dce_admin_channel_client_buffers_init(d, DCE_ADMIN_CH_CL_PM_BUFF);
	if (ret) {
		dce_os_err(d, "Admin channel client buffers init failed: PM");
		goto done;
	}

	ret = dce_os_wq_work_init(d, &d->dce_resume_work,
			dce_resume_work_fn, (void *)d);
	if (ret) {
		dce_os_err(d, "resume work init failed");
		goto done;
	}

done:
	return ret;
}

void dce_pm_deinit(struct tegra_dce *d)
{
	dce_os_wq_work_deinit(d, d->dce_resume_work);
	dce_admin_channel_client_buffers_deinit(d, DCE_ADMIN_CH_CL_PM_BUFF);
}
