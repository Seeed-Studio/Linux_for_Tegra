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
#include <dce-ipc.h>
#include <dce-os-utils.h>
#include <dce-client-ipc-internal.h>

#define DCE_IPC_HANDLES_MAX 6U
#define DCE_CLIENT_IPC_HANDLE_INVALID 0U
#define DCE_CLIENT_IPC_HANDLE_VALID ((u32)BIT(31))

static struct tegra_dce_client_ipc client_handles[DCE_CLIENT_IPC_TYPE_MAX];

static uint32_t dce_interface_type_map[DCE_CLIENT_IPC_TYPE_MAX] = {
	[DCE_CLIENT_IPC_TYPE_CPU_RM] = DCE_IPC_TYPE_DISPRM,
	[DCE_CLIENT_IPC_TYPE_HDCP_KMD] = DCE_IPC_TYPE_HDCP,
	[DCE_CLIENT_IPC_TYPE_RM_EVENT] = DCE_IPC_TYPE_RM_NOTIFY,
};

static void dce_client_process_event_ipc(struct tegra_dce *d,
					 struct tegra_dce_client_ipc *cl);

static inline uint32_t dce_client_get_type(uint32_t int_type)
{
	uint32_t lc = 0;

	for (lc = 0; lc < DCE_CLIENT_IPC_TYPE_MAX; lc++)
		if (dce_interface_type_map[lc] == int_type)
			break;

	return lc;
}

static inline u32 client_handle_to_index(u32 handle)
{
	return (u32)(handle & ~DCE_CLIENT_IPC_HANDLE_VALID);
}

static inline bool is_client_handle_valid(u32 handle)
{
	bool valid = false;

	if ((handle & DCE_CLIENT_IPC_HANDLE_VALID) == 0U)
		goto out;

	if (client_handle_to_index(handle) >= DCE_CLIENT_IPC_TYPE_MAX)
		goto out;

	valid = true;

out:
	return valid;
}

static struct tegra_dce_client_ipc *dce_client_ipc_lookup_handle(u32 handle)
{
	struct tegra_dce_client_ipc *cl = NULL;
	u32 index = 0U;

	if (!is_client_handle_valid(handle))
		goto out;

	index = client_handle_to_index(handle);
	if (index >= DCE_CLIENT_IPC_TYPE_MAX)
		goto out;

	cl = &client_handles[index];

out:
	return cl;
}


static int dce_client_ipc_handle_alloc(u32 *handle)
{
	u32 index;
	int ret = -1;

	if (handle == NULL)
		return ret;

	for (index = 0; index < DCE_CLIENT_IPC_TYPE_MAX; index++) {
		if (client_handles[index].valid == false) {
			client_handles[index].valid = true;
			*handle = (u32)(index | DCE_CLIENT_IPC_HANDLE_VALID);
			ret = 0;
			break;
		}
	}

	return ret;
}

static int dce_client_ipc_handle_free(struct tegra_dce_client_ipc *cl)
{
	struct tegra_dce *d;

	if ((cl == NULL) || (cl->valid == false))
		return -EINVAL;

	d = cl->d;
	d->d_clients[cl->type] = NULL;
	memset(cl, 0, sizeof(struct tegra_dce_client_ipc));
	cl->valid = false;

	return 0;
}

static void dce_client_async_event_work(void *data)
{
	struct tegra_dce_client_ipc *cl;
	struct dce_async_work *work = (struct dce_async_work *) data;
	struct tegra_dce *d = NULL;

	if ((work == NULL) || (work->d == NULL)) {
		dce_os_err(d, "Invalid work struct");
		goto fail;
	}

	d = work->d;

	cl = d->d_clients[DCE_CLIENT_IPC_TYPE_RM_EVENT];

	dce_client_process_event_ipc(d, cl);
	dce_os_atomic_set(&work->in_use, 0);

fail:
	return;
}

static inline int dce_client_ipc_wait_bootstrap_complete(struct tegra_dce *d)
{
	int ret = 0;

	/*
	 * Wait for bootstrapping to complete as this is pre-req
	 * to communicate to DCE-FW.
	 */
#define DCE_IPC_REGISTER_BOOT_WAIT	(30U * 1000)
	ret = DCE_OS_COND_WAIT_INTERRUPTIBLE_TIMEOUT(&d->dce_bootstrap_done,
						  dce_is_bootstrap_done(d),
						  DCE_IPC_REGISTER_BOOT_WAIT);
	if (ret) {
		dce_os_err(d, "dce boot wait failed (%d)\n", ret);
		goto out;
	}

out:
	return ret;
}

int tegra_dce_register_ipc_client(u32 type,
		tegra_dce_client_ipc_callback_t callback_fn,
		void *data, u32 *handlep)
{
	int ret;
	uint32_t int_type;
	struct tegra_dce *d = NULL;
	struct tegra_dce_client_ipc *cl = NULL;
	u32 handle = DCE_CLIENT_IPC_HANDLE_INVALID;
	u32 index = 0U;

	if (handlep == NULL) {
		dce_os_err(d, "Invalid handle pointer");
		ret = -EINVAL;
		goto end;
	}

	if (type >= DCE_CLIENT_IPC_TYPE_MAX) {
		dce_os_err(d, "Failed to retrieve client info for type: [%u]", type);
		ret = -EINVAL;
		goto end;
	}

	int_type = dce_interface_type_map[type];

	d = dce_ipc_get_dce_from_ch_unlocked(int_type);
	if (d == NULL) {
		ret = -EINVAL;
		goto out;
	}

	ret = dce_client_ipc_handle_alloc(&handle);
	if (ret)
		goto out;

	index = client_handle_to_index(handle);
	if (index >= DCE_CLIENT_IPC_TYPE_MAX) {
		dce_os_err(d, "Invalid client handle index: %u", index);
		ret = -EINVAL;
		goto out;
	}

	cl = &client_handles[index];

	cl->d = d;
	cl->type = type;
	cl->data = data;
	cl->handle = handle;
	cl->int_type = int_type;
	cl->callback_fn = callback_fn;

	ret = dce_wait_cond_init(d, &cl->recv_wait);
	if (ret) {
		dce_os_err(d, "dce condition initialization failed for int_type: [%u]",
			int_type);
		goto out;
	}

	cl->valid = true;
	d->d_clients[type] = cl;

out:
	if (ret != 0) {
		(void)dce_client_ipc_handle_free(cl);
		handle = DCE_CLIENT_IPC_HANDLE_INVALID;
	}

	*handlep = handle;
end:
	return ret;
}
DCE_EXPORT_SYMBOL(tegra_dce_register_ipc_client);

int tegra_dce_unregister_ipc_client(u32 handle)
{
	struct tegra_dce_client_ipc *cl;

	cl = dce_client_ipc_lookup_handle(handle);
	if (cl == NULL) {
		return -EINVAL;
	}

	dce_wait_cond_deinit(cl->d, &cl->recv_wait);

	return dce_client_ipc_handle_free(cl);
}
DCE_EXPORT_SYMBOL(tegra_dce_unregister_ipc_client);

int tegra_dce_client_ipc_send_recv(u32 handle, struct dce_ipc_message *msg)
{
	int ret;
	struct tegra_dce_client_ipc *cl;

	if (msg == NULL) {
		ret = -1;
		goto out;
	}

	cl = dce_client_ipc_lookup_handle(handle);
	if (cl == NULL) {
		ret = -1;
		goto out;
	}

	/**
	 * This check is moved from tegra_dce_register_ipc_client() to here
	 * because we share this code with HVRTOS. HVRTOS has restriction on
	 * having any waits during init phase in which the register function
	 * is invoked.
	 */
	ret = dce_client_ipc_wait_bootstrap_complete(cl->d);
	if (ret != 0)
		goto out;

	ret = dce_ipc_send_message_sync(cl->d, cl->int_type, msg);

out:
	return ret;
}
DCE_EXPORT_SYMBOL(tegra_dce_client_ipc_send_recv);

int dce_client_init(struct tegra_dce *d)
{
	int ret = 0;
	uint8_t i;
	struct tegra_dce_async_ipc_info *d_aipc = &d->d_async_ipc;

	ret = dce_os_wq_create(d, &d_aipc->async_event_wq, "dce-async-ipc-wq");
	if (ret) {
		dce_os_err(d, "Failed to create async ipc wq. err [%d]", ret);
		goto fail_wq_create;
	}

	for (i = 0; i < DCE_MAX_ASYNC_WORK; i++) {
		struct dce_async_work *d_work = &d_aipc->work[i];

		ret = dce_os_wq_work_init(d, &d_work->async_event_work,
				dce_client_async_event_work, (void *)d_work);
		if (ret) {
			dce_os_err(d, "Failed to init async work [%u] err [%d]", i, ret);
			goto fail_work_init;
		}

		d_work->d = d;
		dce_os_atomic_set(&d_work->in_use, 0);
	}

fail_work_init:
	if (ret && (i > 0)) {
		uint8_t j = 0;

		for (j = i - 1; j >= 0; j--)  {
			struct dce_async_work *d_work = &d_aipc->work[j];

			dce_os_wq_work_deinit(d, d_work->async_event_work);
		}
		dce_os_wq_destroy(d, d_aipc->async_event_wq);
	}
fail_wq_create:
	return ret;
}

void dce_client_deinit(struct tegra_dce *d)
{
	struct tegra_dce_async_ipc_info *d_aipc = &d->d_async_ipc;
	uint8_t i = 0;

	for (i = 0; i < DCE_MAX_ASYNC_WORK; i++) {
		struct dce_async_work *d_work = &d_aipc->work[i];

		dce_os_wq_work_deinit(d, d_work->async_event_work);
	}

	dce_os_wq_destroy(d, d_aipc->async_event_wq);
}

int dce_client_ipc_wait(struct tegra_dce *d, u32 int_type)
{
	uint32_t type;
	struct tegra_dce_client_ipc *cl;
	int ret = 0;

	type = dce_client_get_type(int_type);
	if (type >= DCE_CLIENT_IPC_TYPE_MAX) {
		dce_os_err(d, "Failed to retrieve client info for int_type: [%d]",
			int_type);
		ret = -EINVAL;
		goto fail;
	}

	cl = d->d_clients[type];
	if ((cl == NULL) || (cl->int_type != int_type)) {
		dce_os_err(d, "Failed to retrieve client info for int_type: [%d]",
			int_type);
		ret = -EINVAL;
		goto fail;
	}

retry_wait:
	ret = dce_wait_cond_wait_interruptible(d, &cl->recv_wait, true, DCE_IPC_TIMEOUT_MS_MAX);
	if (ret) {
		if (ret == -ERESTARTSYS) { /* Interrupt. */
			dce_os_debug(d, "Client [%u] wait interrupted: retrying.", type);
			goto retry_wait;
		} else { /* Unexpected error. */
			dce_os_err(d, "Client [%u] unexpected err: [%d]", type, ret);
			goto fail;
		}
	}

fail:
	return ret;
}

static void dce_client_process_event_ipc(struct tegra_dce *d,
					 struct tegra_dce_client_ipc *cl)
{
	void *msg_data = NULL;
	u32 msg_length;
	int ret = 0;

	if ((cl == NULL) || (cl->callback_fn == NULL)) {
		dce_os_err(d, "Invalid arg tegra_dce_client_ipc");
		return;
	}

	if (cl->type != DCE_CLIENT_IPC_TYPE_RM_EVENT) {
		dce_os_err(d, "Invalid arg for DCE_CLIENT_IPC_TYPE_RM_EVENT type:[%u]", cl->type);
		return;
	}

	msg_data = dce_os_kzalloc(d, DCE_CLIENT_MAX_IPC_MSG_SIZE, false);
	if (msg_data == NULL) {
		dce_os_err(d, "Could not allocate msg read buffer");
		goto done;
	}
	msg_length = DCE_CLIENT_MAX_IPC_MSG_SIZE;

	while (dce_ipc_is_data_available(d, cl->int_type)) {
		ret = dce_ipc_read_message(d, cl->int_type, msg_data, msg_length);
		if (ret) {
			dce_os_info(d, "Error in reading DCE msg for ch_type [%d]",
				cl->int_type);
			goto done;
		}

		cl->callback_fn(cl->handle, cl->type, msg_length, msg_data, cl->data);
	}

done:
	if (msg_data)
		dce_os_kfree(d, msg_data);
}

static void dce_client_schedule_event_work(struct tegra_dce *d)
{
	struct tegra_dce_async_ipc_info *async_work_info = &d->d_async_ipc;
	uint8_t i;
	int ret = 0;

	for (i = 0; i < DCE_MAX_ASYNC_WORK; i++) {
		struct dce_async_work *d_work = &async_work_info->work[i];

		if (dce_os_atomic_add_unless(&d_work->in_use, 1, 1) > 0) {
			ret = dce_os_wq_work_schedule(d, async_work_info->async_event_wq,
					d_work->async_event_work);
			if (ret) {
				dce_os_err(d, "Failed to schedule Async work. id [%u] err [%d]", i, ret);
				goto fail;
			}
			break;
		}
	}

	if (i == DCE_MAX_ASYNC_WORK)
		dce_os_err(d, "Failed to schedule Async event Queue Full!");

fail:
	return;
}

void dce_client_ipc_wakeup(struct tegra_dce *d, u32 ch_type)
{
	uint32_t type;
	struct tegra_dce_client_ipc *cl;

	type = dce_client_get_type(ch_type);
	if (type == DCE_CLIENT_IPC_TYPE_MAX) {
		dce_os_err(d, "Failed to retrieve client info for ch_type: [%d]",
			ch_type);
		return;
	}

	cl = d->d_clients[type];
	if ((cl == NULL) || (cl->valid == false) || (cl->int_type != ch_type)) {
		dce_os_err(d, "Failed to retrieve client info for ch_type: [%d]",
			ch_type);
		return;
	}

	if (type == DCE_CLIENT_IPC_TYPE_RM_EVENT)
		return dce_client_schedule_event_work(d);

	dce_wait_cond_signal_interruptible(d, &cl->recv_wait);
}
