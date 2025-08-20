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
#include <interface/dce-log-header.h>

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
	uint64_t bytes_written 						= 0U;
	struct tegra_dce *d							= s->private;
	char *base_addr								= (char *)(d->dce_log_buff.cpu_base);
	char *buff_start_addr						= NULL;
	uint32_t log_buf_size						= d->dce_log_buff.size;
	uint32_t circ_buf_size						= 0U;
	struct dce_log_buffer_header *header_info	= NULL;
	uint32_t cur_buf_idx						= 0U;

	header_info = (struct dce_log_buffer_header *)base_addr;
	bytes_written = header_info->total_bytes_written;
	buff_start_addr = (char *)(header_info->buff);
	circ_buf_size = header_info->circ_buf_size;

	/** If complete buffer size is zero then buffer is invalid */
	if ((log_buf_size == 0) || (circ_buf_size == 0U)) {
		dce_os_err(d, "%s", "Invalid log buffer\n");
		goto out;
	}

	cur_buf_idx = (bytes_written % circ_buf_size);

	if (bytes_written == 0U) {
		dce_os_err(d, "No logs available!\n");
		goto out;
	}

	if (header_info->is_encoded_log) {
		/** Write buffer content to file in binary format */
		seq_write(s, buff_start_addr, bytes_written);
	} else {
		/** If circular buffer is not yet wrapped around */
		if (bytes_written <= circ_buf_size) {
			seq_printf(s, "%.*s", cur_buf_idx, buff_start_addr);
			goto out;
		}

		/** If circular buffer region has been overwritten */
		/** Print logs stored in circular buffer region */
		seq_printf(s, "%.*s", log_buf_size - cur_buf_idx, buff_start_addr + cur_buf_idx);
		seq_printf(s, "%.*s", cur_buf_idx, buff_start_addr);
	}

out:
	return 0;
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
