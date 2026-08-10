// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */


#include <dce.h>
#include <dce-mailbox.h>
#include <dce-os-utils.h>
#include <dce-client-ipc-internal.h>
#include <interface/dce-core-interface-errors.h>
#include <interface/dce-interface.h>
#include <interface/dce-admin-cmds.h>

/**
 * dce_admin_send_cmd_set_perf_stat - Start/stop DCE perf data collection.
 *
 * @d          - Pointer to tegra_dce struct.
 * @msg        - Pointer to dce_ipc_msg struct.
 * @start_perf - Bool to indicate start/stop of perf data collection
 *
 * Return - 0 if successful
 */
int dce_admin_send_cmd_set_perf_stat(struct tegra_dce *d,
				     struct dce_ipc_message *msg,
				     bool start_perf)
{
	int ret = -1;
	struct dce_admin_ipc_cmd *req_msg;
	struct dce_admin_ipc_resp *resp_msg;

	if (!msg || !msg->tx.data || !msg->rx.data)
		goto out;

	/* return if dce bootstrap not completed */
	if (!dce_is_bootstrap_done(d)) {
		dce_os_err(d, "Admin Bootstrap not yet done");
		goto out;
	}

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);

	if (start_perf == true)
		req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_PERF_START;
	else
		req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_PERF_STOP;

	ret = dce_admin_send_msg(d, msg);
	if (ret) {
		dce_os_err(d, "Error sending set perf msg : [%d]", ret);
		goto out;
	}

out:
	return ret;
}
/**
 * dce_admin_send_cmd_get_perf_stat - Get DCE perf data.
 *
 * @d   - Pointer to tegra_dce struct.
 * @msg - Pointer to dce_ipc_msg struct.
 *
 * Return - 0 if successful
 */
int dce_admin_send_cmd_get_perf_stat(struct tegra_dce *d,
				     struct dce_ipc_message *msg)
{
	int ret = -1;
	struct dce_admin_ipc_cmd *req_msg;
	struct dce_admin_ipc_resp *resp_msg;

	if (!msg || !msg->tx.data || !msg->rx.data)
		goto out;

	/* return if dce bootstrap not completed */
	if (!dce_is_bootstrap_done(d)) {
		dce_os_err(d, "Admin Bootstrap not yet done");
		goto out;
	}

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_PERF_RESULTS;

	ret = dce_admin_send_msg(d, msg);
	if (ret) {
		dce_os_err(d, "Error sending get perf msg : [%d]", ret);
		goto out;
	}

out:
	return ret;
}

int dce_admin_send_cmd_get_perf_events(struct tegra_dce *d,
				       struct dce_ipc_message *msg)
{
	int ret = -1;
	struct dce_admin_ipc_cmd *req_msg;
	struct dce_admin_ipc_resp *resp_msg;

	if (!msg || !msg->tx.data || !msg->rx.data)
		goto out;

	/* return if dce bootstrap not completed */
	if (!dce_is_bootstrap_done(d)) {
		dce_os_err(d, "Admin Bootstrap not yet done");
		goto out;
	}

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_PERF_GET_EVENTS;

	ret = dce_admin_send_msg(d, msg);
	if (ret) {
		dce_os_err(d, "Error sending get perf events msg : [%d]", ret);
		goto out;
	}

out:
	return ret;
}

int dce_admin_send_cmd_clear_perf_events(struct tegra_dce *d,
					 struct dce_ipc_message *msg)
{
	int ret = -1;
	struct dce_admin_ipc_cmd *req_msg;

	if (!msg || !msg->tx.data || !msg->rx.data)
		goto out;

	/* return if dce bootstrap not completed */
	if (!dce_is_bootstrap_done(d)) {
		dce_os_err(d, "Admin Bootstrap not yet done");
		goto out;
	}

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_PERF_CLEAR_EVENTS;

	ret = dce_admin_send_msg(d, msg);
	if (ret) {
		dce_os_err(d, "Error sending clear perf events msg : [%d]", ret);
		goto out;
	}

out:
	return ret;
}
