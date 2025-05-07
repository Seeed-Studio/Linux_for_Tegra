// SPDX-License-Identifier: MIT
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include <dce-mailbox.h>
#include <dce-os-utils.h>
#include <dce-logging.h>
#include <dce-client-ipc-internal.h>
#include <interface/dce-core-interface-errors.h>
#include <interface/dce-interface.h>
#include <interface/dce-admin-cmds.h>

/**
 * dce_admin_ipc_wait - Waits for message from DCE.
 *
 * @d :  Pointer to tegra_dce struct.
 *
 * Return : 0 if successful, -ETIMEOUT if timeout, -ERESTARTSYS if interrupted by signal
 */
int dce_admin_ipc_wait(struct tegra_dce *d)
{
	int ret = 0;

	ret = dce_wait_cond_wait_interruptible(d, &d->ipc_waits[DCE_WAIT_ADMIN_IPC], true,
						DCE_IPC_TIMEOUT_MS_MAX);
	if (ret) {
		dce_os_err(d, "Admin IPC wait, interrupted or timedout:%d", ret);
	}

	return ret;
}

/**
 * dce_admin_wakeup_ipc - wakeup process, waiting for Admin RPC
 *
 * @d :  Pointer to tegra_de struct.
 *
 * Return : Void
 */
static void dce_admin_wakeup_ipc(struct tegra_dce *d)
{
	int ret;

	ret = dce_fsm_post_event(d, EVENT_ID_DCE_ADMIN_IPC_MSG_RECEIVED, NULL);
	if (ret)
		dce_os_err(d, "Error while posting ADMIN_IPC_MSG_RECEIVED event");
}

/**
 * dce_admin_ipc_handle_signal - Isr for the CCPLEX<->DCE admin interface
 *
 * @d :  Pointer to tegra_de struct.
 *
 * Return : Void
 */
void dce_admin_ipc_handle_signal(struct tegra_dce *d, u32 ch_type)
{
	bool wakeup_needed = false;

	if (!dce_ipc_channel_is_synced(d, ch_type)) {
		/**
		 * The ivc channel is not ready yet. Exit
		 * and wait for another signal from target.
		 */
		return;
	}

	/**
	 * Channel already in sync with remote. Check if data
	 * is available to read.
	 */
	wakeup_needed = dce_ipc_is_data_available(d, ch_type);

	if (!wakeup_needed) {
		dce_os_debug(d, "Spurious signal on channel: [%d]. Ignored...",
			 ch_type);
		return;
	}

	if (ch_type == DCE_IPC_CH_KMD_TYPE_ADMIN) {
		dce_admin_wakeup_ipc(d);
	} else {
		dce_client_ipc_wakeup(d, ch_type);
	}
}

/**
 * dce_admin_ivc_channel_reset - Resets the admin ivc channel
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : Void.
 */
void dce_admin_ivc_channel_reset(struct tegra_dce *d)
{
	dce_ipc_channel_reset(d, DCE_IPC_CH_KMD_TYPE_ADMIN);
}

/**
 * dce_admin_channel_client_get_buff_count - Get admin channel client buffer count.
 *
 * @d : Pointer to tegra_dce struct.
 * @cl_id : Admin channel client ID.
 *
 * Return : Admin channel client buffer count if client is valid,
 *		0 otherwise.
 */
static u32 dce_admin_channel_client_get_buff_count(struct tegra_dce *d, u32 cl_id)
{
	u32 count = 0;

	if (d == NULL)
		dce_os_warn(d, "Invalid tegra DCE struct");

	switch (cl_id) {
		case DCE_ADMIN_CH_CL_ADMIN_BUFF: {
			count = DCE_ADMIN_CH_CL_ADMIN_BUFF_COUNT;
			break;
		}
		case DCE_ADMIN_CH_CL_PM_BUFF: {
			count = DCE_ADMIN_CH_CL_PM_BUFF_COUNT;
			break;
		}
		case DCE_ADMIN_CH_CL_DBG_BUFF: {
			count = DCE_ADMIN_CH_CL_DBG_BUFF_COUNT;
			break;
		}
		case DCE_ADMIN_CH_CL_DBG_PERF_BUFF: {
			count = DCE_ADMIN_CH_CL_DBG_PERF_BUFF_COUNT;
			break;
		}
		default: {
			count = 0;
			break;
		}
	}

	return count;
}

/**
 * dce_admin_channel_client_buffers_deinit - De-init admin channel client buffers.
 *
 * @d : Pointer to tegra_dce struct.
 * @ch_id : Admin channel client buffer ID.
 *
 * Return : Void.
 *
 * Note: This function shall not be called concurrently with any of the following
 *	functions for the same client:
 *	- dce_admin_channel_client_buffers_init()
 *	- dce_{get/put}_admin_channel_client_buffer()
 */
void dce_admin_channel_client_buffers_deinit(struct tegra_dce *d, u32 cl_id)
{
	struct dce_admin_ch_cl_buff **cl_buff_arr = NULL;
	u32 cl_buff_count = 0;
	u32 buff_idx = 0;

	if (cl_id >= DCE_ADMIN_CH_CL_MAX) {
		dce_os_err(d, "Invalid client ID [%u]", cl_id);
		goto fail;
	}

	cl_buff_arr = d->admin_ch_cl_buff[cl_id];

	// Nothing to do for this client if the array is not allocated.
	if (!cl_buff_arr) {
		dce_os_err(d, "Trying to free unallocated buffers for client ID [%u]", cl_id);
		goto fail;
	}

	// Get number of buffers for this client.
	cl_buff_count = dce_admin_channel_client_get_buff_count(d, cl_id);

	// De-init and free actual buffer structs
	for (buff_idx = 0; buff_idx < cl_buff_count; buff_idx++) {
		struct dce_admin_ch_cl_buff *buff = cl_buff_arr[buff_idx];
		struct dce_ipc_message *pmsg = NULL;

		// Nothing to do if this buffer is not allocated.
		if (!buff)
			continue;

		// Free DCE IPC message tx and rx buffers.
		pmsg = &buff->msg;

		if (!pmsg->tx.data)
			dce_os_kfree(d, pmsg->tx.data);

		if (!pmsg->rx.data)
			dce_os_kfree(d, pmsg->rx.data);

		pmsg->tx.data = NULL;
		pmsg->rx.data = NULL;
		pmsg->tx.size = 0;
		pmsg->rx.size = 0;

		// Free the buffer.
		dce_os_kfree(d, buff);
		cl_buff_arr[buff_idx] = NULL;
	}


	// Finally, free admin channel client buffer array for this client.
	dce_os_kfree(d, cl_buff_arr);
	d->admin_ch_cl_buff[cl_id] = NULL;

fail:
	return;
}

/**
 * dce_admin_channel_client_buffers_init - Init admin channel client buffers.
 *
 * @d : Pointer tegra_dce struct.
 * @ch_id : Admin channel client buffer ID.
 *
 * Return : 0 on success, non-zero error otherwise.
 *
 * Notes:
 * 1) Since we call dce_admin_channel_client_buffers_deinit() if anything fails
 *	in this function, we need to be careful with the order of how we
 *	set allocated pointers in this function.
 *	- dce_admin_channel_client_buffers_deinit() will free any data that's set.
 *	- If this not done carefully then we will have memory leaks.
 *
 * 2) This function shall not be called concurrently with any of the following
 *	functions for the same client:
 *	- dce_admin_channel_client_buffers_deinit()
 *	- dce_{get/put}_admin_channel_client_buffer()
 */
int dce_admin_channel_client_buffers_init(struct tegra_dce *d, u32 cl_id)
{
	int ret = -1;
	struct dce_admin_ch_cl_buff **cl_buff_arr = NULL;
	u32 cl_buff_count = 0;
	u32 buff_idx = 0;

	if (cl_id >= DCE_ADMIN_CH_CL_MAX) {
		dce_os_err(d, "Invalid client ID [%u]", cl_id);
		goto fail;
	}

	// Get number of buffers for this client.
	cl_buff_count = dce_admin_channel_client_get_buff_count(d, cl_id);

	// Set to NULL so deinit works as intended.
	d->admin_ch_cl_buff[cl_id] = NULL;

	// Allocate admin channel client buffer array for this client.
	cl_buff_arr = dce_os_kzalloc(d, sizeof(*cl_buff_arr) * cl_buff_count, false);
	if (!cl_buff_arr) {
		dce_os_err(d, "Insufficient memory for admin client [%u] buff array",
				cl_id);
		goto fail;
	}

	// Set the allocated buffer array for this client.
	d->admin_ch_cl_buff[cl_id] = cl_buff_arr;

	// Finally allocate and init actual buffer structs.
	for (buff_idx = 0; buff_idx < cl_buff_count; buff_idx++) {
		struct dce_admin_ch_cl_buff *buff = NULL;
		struct dce_ipc_message *pmsg = NULL;
		dce_os_atomic_t *p_in_use = NULL;

		// Set to NULL so deinit works as intended.
		cl_buff_arr[buff_idx] = NULL;

		buff = dce_os_kzalloc(d, sizeof(*buff), false);
		if (!buff) {
			dce_os_err(d, "Insufficient memory for admin client id [%u] buff idx [%u]",
					cl_id, buff_idx);
			goto fail;
		}

		// Set the allocated buffer.
		cl_buff_arr[buff_idx] = buff;

		// Allocate DCE IPC message tx and rx buffers.
		pmsg = &buff->msg;

		// Set to NULL so deinit works as intended.
		pmsg->tx.data = NULL;

		pmsg->tx.data = dce_os_kzalloc(d, DCE_ADMIN_CMD_SIZE, false);
		if (!pmsg->tx.data) {
			dce_os_err(d, "Insufficient memory for admin msg");
			goto fail;
		}

		// Set to NULL so deinit works as intended.
		pmsg->rx.data = NULL;

		pmsg->rx.data = dce_os_kzalloc(d, DCE_ADMIN_RESP_SIZE, false);
		if (!pmsg->rx.data) {
			dce_os_err(d, "Insufficient memory for admin msg");
			goto fail;
		}

		pmsg->tx.size = DCE_ADMIN_CMD_SIZE;
		pmsg->rx.size = DCE_ADMIN_RESP_SIZE;

		// Init in_use atomic variable.
		p_in_use = &buff->in_use;
		dce_os_atomic_set(p_in_use, 0);
	}

	ret = 0;

fail:
	if (ret != 0)
		dce_admin_channel_client_buffers_deinit(d, cl_id);

	return ret;
}

/**
 * dce_admin_channel_client_buffer_get - Get dce admin msg buffer pointer
 *					for client.
 *
 * @d : Pointer to tegra_dce struct.
 * @cl_id : Admin channel client ID.
 * @flags: This is reserved argument for future use.
 *	Must be set to 0.
 *
 * Return : Pointer to admin message buffer or
 *		NULL if buffer is not available at the moment.
 *
 * Notes:
 * This function shouldn't be called concurrently with below
 * functions with same client id:
 *	- dce_admin_channel_client_buffer_get()
 *	- dce_admin_channel_client_buffers_{init/deinit}()
 *	- dce_admin_channel_client_buffer_put()
 */
struct dce_ipc_message *dce_admin_channel_client_buffer_get(
	struct tegra_dce *d, u32 cl_id, u32 flags)
{
	struct dce_ipc_message *pmsg = NULL;
	u32 cl_buff_count = 0;
	struct dce_admin_ch_cl_buff **cl_buff_arr = NULL;
	u32 buff_idx = 0;

	if (flags != 0) {
		dce_os_err(d, "flags=[%u] must be set to 0 for client [%u]",
			flags, cl_id);
		goto done;
	}

	if (cl_id >= DCE_ADMIN_CH_CL_MAX) {
		dce_os_err(d, "Invalid admin channel client ID [%u]", cl_id);
		goto done;
	}

	cl_buff_arr = d->admin_ch_cl_buff[cl_id];
	if (!cl_buff_arr) {
		dce_os_err(d, "Invalid client buff arr [%u]", cl_id);
		goto done;
	}

	cl_buff_count = dce_admin_channel_client_get_buff_count(d, cl_id);

	for (buff_idx = 0; buff_idx < cl_buff_count; buff_idx++) {
		struct dce_admin_ch_cl_buff *buff = cl_buff_arr[buff_idx];
		dce_os_atomic_t *p_in_use = NULL;
		u32 in_use = 0;

		if (!buff) {
			dce_os_err(d, "Invalid client [%u] buff [%u]", cl_id, buff_idx);
			goto done;
		}

		p_in_use = &buff->in_use;

		// Continue to next buffer if this one is in use.
		in_use = dce_os_atomic_read(p_in_use);
		if (in_use == 1)
			continue;

		// If this buffer is available then mark it as in use.
		dce_os_atomic_set(p_in_use, 1);

		// Return buffer to the caller.
		pmsg = &buff->msg;

		// Ensure that this buffer is valid.
		if (!pmsg->tx.data || !pmsg->rx.data) {
			// This should never happen so fail.
			pmsg = NULL;
		}

		// Search complete.
		goto done;
	}

done:
	return pmsg;
}

/**
 * dce_admin_channel_client_buffer_get - Release admin channel client buffer.
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : void
 *
 * Notes:
 * This function shouldn't be called concurrently with below
 * functions with same client id:
 *	- dce_admin_channel_client_buffer_put()
 *	- dce_admin_channel_client_buffers_{init/deinit}()
 *	- dce_admin_channel_client_buffer_get()
 */
void dce_admin_channel_client_buffer_put(
	struct tegra_dce *d, struct dce_ipc_message *pmsg)
{
	struct dce_admin_ch_cl_buff *buff = NULL;
	dce_os_atomic_t *p_in_use = NULL;
	u32 in_use = 0;

	if (!d || !pmsg) {
		dce_os_err(d, "Invalid input");
		goto done;
	}

	buff = container_of(pmsg, struct dce_admin_ch_cl_buff, msg);
	if (!buff) {
		dce_os_err(d, "Invalid buffer");
		goto done;
	}

	p_in_use = &buff->in_use;

	// Verify that buffer is in use
	in_use = dce_os_atomic_read(p_in_use);
	if (in_use == 0) {
		dce_os_err(d, "Buffer not in use");
		goto done;
	}

	// Release buffer
	dce_os_atomic_set(p_in_use, 0);

done:
	return;
}

/**
 * dce_admin_channel_deinit - Cleans up the channel resources.
 *
 * @d : Pointer to tegra_dce struct
 *
 * Return : Void
 */
static void dce_admin_channel_deinit(struct tegra_dce *d)
{
	u32 loop_cnt;

	for (loop_cnt = 0; loop_cnt < DCE_IPC_CH_KMD_TYPE_MAX; loop_cnt++)
		dce_ipc_channel_deinit_unlocked(d, loop_cnt);
}


/**
 * dce_admin_channel_init - Initializes the admin ivc interface
 *
 * @d : Pointer to tegra_dce.
 *
 * Return : 0 if successful.
 */
static int dce_admin_channel_init(struct tegra_dce *d)
{
	int ret = 0;
	u32 loop_cnt;

	for (loop_cnt = 0; loop_cnt < DCE_IPC_CH_KMD_TYPE_MAX; loop_cnt++) {
		ret = dce_ipc_channel_init_unlocked(d, loop_cnt);
		if (ret) {
			dce_os_err(d, "Channel init failed for type : [%d]",
				loop_cnt);
			goto out;
		}
	}

out:
	if (ret)
		dce_admin_channel_deinit(d);
	return ret;
}

/**
 * dce_admin_init - Sets up resources managed by admin.
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : 0 if successful
 */
int dce_admin_init(struct tegra_dce *d)
{
	int ret = 0;

	d->boot_status |= DCE_EARLY_INIT_START;
	ret = dce_os_ipc_init_region_info(d);
	if (ret) {
		dce_os_err(d, "IPC region allocation failed");
		goto err_ipc_reg_alloc;
	}

	ret = dce_log_buf_mem_init(d);
	if (ret) {
		dce_os_err(d, "dce log buf mem init failed");
		goto err_log_buf_alloc;
	}

	ret = dce_admin_channel_init(d);
	if (ret) {
		dce_os_err(d, "Channel Initialization Failed");
		goto err_channel_init;
	}

	ret = dce_admin_channel_client_buffers_init(d, DCE_ADMIN_CH_CL_ADMIN_BUFF);
	if (ret) {
		dce_os_err(d, "Admin channel client buffers init failed: Admin");
		goto err_channel_clients_buff_init;
	}

	d->boot_status |= DCE_EARLY_INIT_DONE;
	return 0;

err_channel_clients_buff_init:
	dce_admin_channel_deinit(d);
err_channel_init:
	dce_os_ipc_deinit_region_info(d);
err_ipc_reg_alloc:
	d->boot_status |= DCE_EARLY_INIT_FAILED;
err_log_buf_alloc:
	return ret;
}

/**
 * dce_admin_deinit - Releases the resources
 *					associated with admin interface.
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : Void
 */
void dce_admin_deinit(struct tegra_dce *d)
{
	dce_admin_channel_client_buffers_deinit(d, DCE_ADMIN_CH_CL_ADMIN_BUFF);

	dce_admin_channel_deinit(d);

	dce_os_ipc_deinit_region_info(d);

	dce_mailbox_deinit_interface(d,
			DCE_MAILBOX_ADMIN_INTERFACE);
}

/**
 * dce_admin_send_msg - Sends messages on Admin Channel
 *				synchronously and waits for an ack.
 *
 * @d : Pointer to tegra_dce struct.
 * @msg : Pointer to allocated message.
 *
 * Return : 0 if successful
 */
int dce_admin_send_msg(struct tegra_dce *d, struct dce_ipc_message *msg)
{
	int ret = 0;
	struct dce_admin_send_msg_params params;

	params.msg = msg;

	ret = dce_fsm_post_event(d,
				 EVENT_ID_DCE_ADMIN_IPC_MSG_REQUESTED,
				 (void *)&params);
	if (ret)
		dce_os_err(d, "Unable to send msg invalid FSM state");

	return ret;
}

/**
 * dce_admin_send_msg - Sends messages on Admin Channel
 *				synchronously and waits for an ack.
 *
 * @d : Pointer to tegra_dce struct.
 * @msg : Pointer to allocated message.
 *
 * Return : 0 if successful
 */
int dce_admin_handle_ipc_requested_event(struct tegra_dce *d, void *params)
{
	int ret = 0;
	struct dce_ipc_message *msg;
	struct dce_admin_send_msg_params *admin_params =
			(struct dce_admin_send_msg_params *)params;

	/*
	 * Do not handle admin IPC if boot commands are not completed
	 */
	if (!dce_is_bootcmds_done(d)) {
		dce_os_err(d, "Boot commands are not yet completed\n");
		ret = -EINVAL;
		goto out;
	}

	/* Error check on msg */
	msg = admin_params->msg;

	ret = dce_ipc_send_message_sync(d, DCE_IPC_CHANNEL_TYPE_ADMIN, msg);
	if (ret)
		dce_os_err(d, "Error sending admin message on admin interface");

out:
	return ret;
}

int dce_admin_handle_ipc_received_event(struct tegra_dce *d, void *params)
{
	DCE_WARN_ON_NOT_NULL(params);

	dce_wait_cond_signal_interruptible(d, &d->ipc_waits[DCE_WAIT_ADMIN_IPC]);
	return 0;
}

/**
 * dce_admin_get_ipc_channel_info - Provides channel's
 *                                      buff details
 *
 * @d - Pointer to tegra_dce struct.
 * @q_info : Pointer to struct dce_ipc_queue_info
 *
 * Return - 0 if successful
 */
int dce_admin_get_ipc_channel_info(struct tegra_dce *d,
					struct dce_ipc_queue_info *q_info)
{
	int ret;
	u8 channel_id = DCE_IPC_CHANNEL_TYPE_ADMIN;

	ret = dce_ipc_get_channel_info(d, q_info, channel_id);

	return ret;
}

/**
 * dce_admin_send_bootstrap2 - Sends DCE_ADMIN_CMD_RM_BOOTSTRAP2 cmd.
 *
 * @d - Pointer to tegra_dce struct.
 * @msg - Pointer to dce_ipc_msg struct.
 *
 * Return - 0 if successful
 */
int dce_admin_send_cmd_rm_bootstrap(struct tegra_dce *d,
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

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_RM_BOOTSTRAP2;

	ret = dce_admin_send_msg(d, msg);
	if ((ret) || (resp_msg->error != DCE_ERR_CORE_SUCCESS)) {
		dce_os_err(d, "Error sending bootstrap msg : [%d]", ret);
		ret = ret ? ret : resp_msg->error;
		goto out;
	}

out:
	return ret;
}

/**
 * dce_admin_send_cmd_echo - Sends DCE_ADMIN_CMD_ECHO cmd.
 *
 * @d - Pointer to tegra_dce struct.
 * @msg - Pointer to dce_ipc_msg struct.
 *
 * Return - 0 if successful
 */
int dce_admin_send_cmd_echo(struct tegra_dce *d,
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

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_ECHO;

	ret = dce_admin_send_msg(d, msg);
	if ((ret) || (resp_msg->error != DCE_ERR_CORE_SUCCESS)) {
		dce_os_err(d, "Error sending echo msg : [%d]", ret);
		ret = ret ? ret : resp_msg->error;
		goto out;
	}

out:
	return ret;
}

/**
 * dce_admin_send_cmd_ext_test - Sends DCE_ADMIN_CMD_EXT_TEST cmd.
 *
 * @d - Pointer to tegra_dce struct.
 * @msg - Pointer to dce_ipc_msg struct.
 *
 * Return - 0 if successful
 */
int dce_admin_send_cmd_ext_test(struct tegra_dce *d,
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

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_EXT_TEST;

	ret = dce_admin_send_msg(d, msg);
	if (ret) {
		dce_os_err(d, "Error sending test msg : [%d]", ret);
		goto out;
	}

out:
	return ret;
}

/**
 * dce_admin_send_cmd_ver - Sends DCE_ADMIN_CMD_VERSION cmd.
 *
 * @d - Pointer to tegra_dce struct.
 * @msg - Pointer to dce_ipc_msg struct.
 *
 * Return - 0 if successful
 */
static int dce_admin_send_cmd_ver(struct tegra_dce *d,
		struct dce_ipc_message *msg)
{
	int ret = 0;
	struct dce_admin_ipc_cmd *req_msg;
	struct dce_admin_ipc_resp *resp_msg;
	struct dce_admin_version_info *ver_info;

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_VERSION;
	ver_info = (struct dce_admin_version_info *)(&resp_msg->args.version);

	ret = dce_admin_send_msg(d, msg);
	if (ret) {
		dce_os_err(d, "Error sending get version info : [%d]", ret);
		goto out;
	}

	if (resp_msg->error != DCE_ERR_CORE_SUCCESS) {
		dce_os_err(d, "Error in handling DCE_ADMIN_CMD_VERSION on DCE\n");
		ret = resp_msg->error;
		goto out;
	}

	dce_os_info(d, "version : dcefw:[0x%x] dcekmd:[0x%x] err : [0x%x]",
		 ver_info->version, DCE_ADMIN_VERSION, resp_msg->error);

out:
	/**
	 * TODO : Add more error handling here
	 */
	return ret;
}

/**
 * dce_admin_send_prepare_sc7 - Sends DCE_ADMIN_CMD_PREPARE_SC7 cmd.
 *
 * @d - Pointer to tegra_dce struct.
 * @msg - Pointer to dce_ipc_msg struct.
 *
 * Return - 0 if successful
 */
int dce_admin_send_prepare_sc7(struct tegra_dce *d,
			    struct dce_ipc_message *msg)
{
	int ret = -1;
	struct dce_admin_ipc_cmd *req_msg;
	struct dce_admin_ipc_resp *resp_msg;

	if (!msg || !msg->tx.data || !msg->rx.data)
		goto out;

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_PREPARE_SC7;

	ret = dce_admin_send_msg(d, msg);
	if (ret) {
		dce_os_err(d, "Error sending prepare sc7 command [%d]", ret);
		goto out;
	}

out:
	return ret;
}

/**
 * dce_admin_send_enter_sc7 - Sends DCE_ADMIN_CMD_ENTER_SC7 cmd.
 *
 * @d - Pointer to tegra_dce struct.
 * @msg - Pointer to dce_ipc_msg struct.
 *
 * Return - 0 if successful
 */
int dce_admin_send_enter_sc7(struct tegra_dce *d,
			     struct dce_ipc_message *msg)
{
	int ret = -1;
	struct dce_admin_ipc_cmd *req_msg;
	struct dce_admin_ipc_resp *resp_msg;

	if (!msg || !msg->tx.data || !msg->rx.data)
		goto out;

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_ENTER_SC7;

	ret = dce_ipc_send_message(d, DCE_IPC_CHANNEL_TYPE_ADMIN, msg->tx.data, msg->tx.size);
	if (ret) {
		dce_os_err(d, "Error sending enter sc7 command [%d]", ret);
		goto out;
	}

	/* Wait for SC7 Enter done */
	ret = dce_wait_cond_wait_interruptible(d, &d->ipc_waits[DCE_WAIT_SC7_ENTER], true,
						DCE_IPC_TIMEOUT_MS_MAX);
	if (ret) {
		dce_os_err(d, "SC7 Enter wait, interrupted or timedout:%d", ret);
	}

out:
	return ret;
}

int dce_admin_get_log_info(struct tegra_dce *d, struct dce_ipc_message *msg)
{
	int ret = -1;
	struct dce_admin_ipc_cmd *req_msg;
	struct dce_admin_ipc_resp *resp_msg;

	if (msg == NULL) {
		dce_os_err(d, "Invalid params passed in get log call");
		goto out;
	}

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	resp_msg = (struct dce_admin_ipc_resp *)(msg->rx.data);

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_GET_LOG_INFO;

	ret = dce_admin_send_msg(d, msg);
	if (ret) {
		dce_os_err(d, "Error in sending get log call msg : [%d]", ret);
		goto out;
	}

out:
	return ret;
}

int dce_admin_set_log_info(struct tegra_dce *d, struct dce_ipc_message *msg)
{
	int ret = -1;
	struct dce_admin_ipc_cmd *req_msg;

	if (msg == NULL) {
		dce_os_err(d, "Invalid params passed in set log call");
		goto out;
	}

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_SET_LOGGING;

	ret = dce_admin_send_msg(d, msg);
	if (ret) {
		dce_os_err(d, "Error in sending set log call msg : [%d]", ret);
		goto out;
	}

out:
	return ret;
}

static int dce_admin_setup_clients_ipc(struct tegra_dce *d,
		struct dce_ipc_message *msg)
{
	uint32_t i;
	int ret = 0;
	struct dce_admin_ipc_cmd *req_msg;
	struct dce_admin_ipc_resp *resp_msg;
	struct dce_ipc_queue_info q_info;
	struct dce_admin_ipc_create_args *ipc_info;

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_IPC_CREATE;
	ipc_info = (struct dce_admin_ipc_create_args *)
					(&req_msg->args.ipc_create);

	for (i = 0; i < DCE_IPC_CH_KMD_TYPE_MAX; i++) {
		if (i == DCE_IPC_CH_KMD_TYPE_ADMIN)
			continue;
		ret = dce_ipc_get_channel_info(d, &q_info, i);
		if (ret) {
			dce_os_info(d, "Get queue info failed for [%u]", i);
			ret = 0;
			continue;
		}

		ipc_info->type = dce_ipc_get_ipc_type(d, i);
		ipc_info->rd_iova = q_info.tx_iova;
		ipc_info->wr_iova = q_info.rx_iova;
		ipc_info->fsize = q_info.frame_sz;
		ipc_info->n_frames = q_info.nframes;

		ret = dce_admin_send_msg(d, msg);
		if (ret) {
			dce_os_err(d, "Error sending IPC create msg for type [%u]",
				i);
			goto out;
		}

		if (resp_msg->error != DCE_ERR_CORE_SUCCESS) {
			dce_os_err(d, "IPC create for type [%u] failed", i);
			goto out;
		}

		dce_ipc_channel_reset(d, i);
		dce_os_info(d, "Channel Reset Complete for Type [%u] ...", i);
	}

out:
	/**
	 * TODO : Add more error handling here
	 */
	return ret;
}

static int dce_admin_send_rm_bootstrap(struct tegra_dce *d,
		struct dce_ipc_message *msg)
{
	int ret = 0;
	struct dce_admin_ipc_cmd *req_msg;
	struct dce_admin_ipc_resp *resp_msg;
	struct dce_admin_version_info *ver_info;

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);

	req_msg->cmd = (uint32_t)DCE_ADMIN_CMD_RM_BOOTSTRAP;
	ver_info = (struct dce_admin_version_info *)(&resp_msg->args.version);

	ret = dce_admin_send_msg(d, msg);
	if (ret) {
		dce_os_err(d, "Error sending rm bootstrap cmd: [%d]", ret);
		goto out;
	}

	if (resp_msg->error != DCE_ERR_CORE_SUCCESS) {
		dce_os_err(d, "Error in handling rm bootstrap cmd on dce: [0x%x]",
			resp_msg->error);
		ret = -EINVAL;
	}

out:
	/**
	 * TODO : Add more error handling here
	 */
	return ret;
}

int dce_start_admin_seq(struct tegra_dce *d)
{
	int ret = 0;
	struct dce_ipc_message *msg;

	msg = dce_admin_channel_client_buffer_get(d, DCE_ADMIN_CH_CL_ADMIN_BUFF,
			0 /* reserved flags */);
	if (!msg)
		return -1;

	d->boot_status |= DCE_FW_ADMIN_SEQ_START;
	ret = dce_admin_send_cmd_ver(d, msg);
	if (ret) {
		dce_os_err(d, "RPC failed for DCE_ADMIN_CMD_VERSION");
		goto out;
	}

	ret = dce_admin_setup_clients_ipc(d, msg);
	if (ret) {
		dce_os_err(d, "RPC failed for DCE_ADMIN_CMD_IPC_CREATE");
		goto out;
	}

	ret = dce_log_init(d, msg);
	if (ret) {
		dce_os_err(d, "RPC failed for DCE_ADMIN_CMD_SET_LOGGING");
		goto out;
	}

	ret = dce_admin_send_rm_bootstrap(d, msg);
	if (ret) {
		dce_os_err(d, "RPC failed for DCE_ADMIN_CMD_RM_BOOTSTRAP");
		goto out;
	}
	d->boot_status |= DCE_FW_ADMIN_SEQ_DONE;
out:
	if (msg)
		dce_admin_channel_client_buffer_put(d, msg);

	if (ret)
		d->boot_status |= DCE_FW_ADMIN_SEQ_FAILED;

	return ret;
}
