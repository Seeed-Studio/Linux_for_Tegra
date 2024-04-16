// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include <dce-os-utils.h>
#include <dce-debug-logging.h>
#include <dce-os-log.h>
#include <dce-logging.h>
#include <dce.h>

static int dbg_dce_log_help_fops_show(struct seq_file *s, void *data)
{
/**
 * Writing:
 * '0' to /sys/kernel/debug/tegra_dce/dce_logs/logs
 *    - Clear the circular buffer region data.
 *
 * Reading:
 *  cat /sys/kernel/debug/tegra_dce/dce_logs/logs
 *    - Prints the data stored in the buffer.
 *    - Current size of buffer is 512KB and on overflow starts overwriting the circular
 *		portion of the buffer in FIFO manner
 */
	seq_printf(s, " DCE logs capture\n"
				"---------------------------------------------------------------\n"
				" echo '0' > dce_logs/logs: Clear circular region of buffer\n"
				"---------------------------------------------------------------\n"
				" cat dce_logs/logs: Print buffer data\n"
				"---------------------------------------------------------------\n");
	return 0;
}

int dbg_dce_log_help_fops_open(struct inode *inode, struct file *file)
{
	return single_open(file, dbg_dce_log_help_fops_show,
			   inode->i_private);
}

static int dbg_dce_log_fops_show(struct seq_file *s, void *data)
{
	int ret = 0;
	uint32_t offset;
	uint64_t bytes_written;
	uint32_t cur_buf_idx;
	struct tegra_dce *d				= s->private;
	char *base_addr					= (char *)d->dce_log_buff.cpu_base;
	uint32_t log_buf_size			= d->dce_log_buff.size;
	struct dce_ipc_message *msg		= NULL;
	struct dce_admin_ipc_resp *resp_msg;

	msg = dce_admin_channel_client_buffer_get(d, DCE_ADMIN_CH_CL_DBG_BUFF,
		0 /* reserved flags */);

	if (!msg) {
		ret = -1;
		dce_os_err(d, "IPC msg allocation failed");
		goto out;
	}

	/** Retrieve logging info */
	ret = dce_admin_get_log_info(d, msg);
	if (ret) {
		dce_os_err(d, "Failed to retrieve logging info, ret = 0x%x", ret);
		goto out;
	}

	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);

	bytes_written	= resp_msg->args.log.get_log_info.bytes_written;
	offset			= resp_msg->args.log.get_log_info.offset;

	/** If complete buffer size is zero then buffer is invalid */
	if (log_buf_size == 0) {
		seq_printf(s, "%s", "Invalid log buffer\n");
		goto out;
	}

	if (bytes_written == 0U || (log_buf_size <= offset)) {
		seq_printf(s, "%.*s", offset, base_addr);
		goto out;
	}

	/** Find total bytes written currently in circular region of buffer */
	cur_buf_idx = (bytes_written)%(log_buf_size - offset);

	/** If circular buffer is not yet wrapped around */
	if (bytes_written <= log_buf_size - offset) {
		seq_printf(s, "%.*s", offset, base_addr);
		seq_printf(s, "%.*s", cur_buf_idx, base_addr + offset);
		goto out;
	}

	/** If circular buffer region has been overwritten */
	/** Print initial logs till offset */
	seq_printf(s, "%.*s", offset, base_addr);
	/** Print logs stored in circular buffer region */
	seq_printf(s, "%.*s", log_buf_size - cur_buf_idx - offset, base_addr + offset + cur_buf_idx);
	seq_printf(s, "%.*s", cur_buf_idx, base_addr + offset);

out:
	if (msg)
		dce_admin_channel_client_buffer_put(d, msg);
	return ret;
}

int dbg_dce_log_fops_open(struct inode *inode, struct file *file)
{
	size_t seq_buf_size = 100*PAGE_SIZE;

	return single_open_size(file, &dbg_dce_log_fops_show, inode->i_private, seq_buf_size);
}

ssize_t dbg_dce_log_fops_write(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	int ret = 0;
	char buf[32];
	uint32_t buf_size;
	uint32_t log_op;
	struct dce_ipc_message *msg = NULL;
	struct tegra_dce *d = ((struct seq_file *)file->private_data)->private;

	buf_size = min(count, (sizeof(buf)-1));

	msg = dce_admin_channel_client_buffer_get(d, DCE_ADMIN_CH_CL_DBG_BUFF,
		0 /* reserved flags */);
	if (!msg) {
		dce_os_err(d, "IPC msg allocation failed");
		goto out;
	}

	ret = kstrtou32_from_user(user_buf, buf_size, 10, &log_op);
	if (ret) {
		dce_os_err(d, "Invalid format!");
		goto out;
	}

	if (log_op == 0U) {
		dce_os_err(d, "Clearing circular region of log buffer");
		ret = dce_log_clear_buffer(d, msg);
		if (ret)
			dce_os_err(d, "Failed to clear log buffer!");
	}

out:
	if (msg)
		dce_admin_channel_client_buffer_put(d, msg);
	return count;
}
