// SPDX-License-Identifier: MIT
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include <dce-logging.h>
#include <dce-os-utils.h>

int dce_log_buf_mem_init(struct tegra_dce *d)
{
	int ret = 0;

	ret = dce_os_init_log_buffer(d);
	if (ret) {
		dce_os_err(d, "Failed to allocate memory for DCE log buffer");
		goto out;
	}

out:
	if (ret)
		dce_os_deinit_log_buffer(d);
	return ret;
}

int dce_log_init(struct tegra_dce *d, struct dce_ipc_message *msg)
{
	int ret = -1;
	struct dce_log_buffer *buffer;
	struct dce_admin_ipc_cmd		*req_msg;
	struct dce_admin_set_log_info	*set_info;

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	set_info = (struct dce_admin_set_log_info *)(&req_msg->args.log.set_log_info);

	buffer				= &d->dce_log_buff;
	set_info->flags		= DCE_ADMIN_LOG_FL_SET_IOVA_ADDR | DCE_ADMIN_LOG_FL_SET_LOG_LVL;
	set_info->iova		= buffer->iova_addr;
	set_info->buff_size	= buffer->size;
	set_info->log_level	= (uint32_t)DCE_LOG_DEFAULT_LOG_LVL;
	set_info->stream_id	= dce_os_get_dce_stream_id(d);

	ret = dce_admin_set_log_info(d, msg);
	if (ret) {
		dce_os_err(d, "Failed to initialize logging, ret = %d\n", ret);
		goto out;
	}

out:
	return ret;
}

int dce_log_clear_buffer(struct tegra_dce *d, struct dce_ipc_message *msg)
{
	int ret = -1;
	struct dce_admin_ipc_cmd		*req_msg;
	struct dce_admin_set_log_info	*ipc_info;

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	ipc_info = (struct dce_admin_set_log_info *)(&req_msg->args.log.set_log_info);

	ipc_info->flags |= DCE_ADMIN_LOG_FL_CLEAR_BUFFER;

	ret = dce_admin_set_log_info(d, msg);
	if (ret) {
		dce_os_err(d, "Failed while making set admin call, ret = %d\n", ret);
		goto out;
	}

out:
	return ret;
}

int dce_log_set_log_level(struct tegra_dce *d, struct dce_ipc_message *msg,
						u32 log_lvl)
{
	int ret = -1;
	struct dce_admin_ipc_cmd		*req_msg;
	struct dce_admin_set_log_info	*ipc_info;

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	ipc_info = (struct dce_admin_set_log_info *)(&req_msg->args.log.set_log_info);

	ipc_info->flags |= DCE_ADMIN_LOG_FL_SET_LOG_LVL;
	ipc_info->log_level = log_lvl;

	ret = dce_admin_set_log_info(d, msg);
	if (ret) {
		dce_os_err(d, "Failed while making set admin call, ret = %d\n", ret);
		goto out;
	}

out:
	return ret;
}
