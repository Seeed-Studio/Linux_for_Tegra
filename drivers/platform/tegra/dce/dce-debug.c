// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <dce.h>
#include <dce-os-log.h>
#include <dce-os-utils.h>
#include <dce-linux-device.h>
#include <dce-debug-perf.h>
#include <dce-debug-logging.h>
#include <interface/dce-interface.h>
#include <interface/dce-core-interface-errors.h>

/**
 * dce_get_fw_name - Gets the dce fw name from platform data.
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : fw_name
 */
static const char *dce_get_fw_name(struct tegra_dce *d)
{
	return pdata_from_dce_linux_device(d)->fw_name;
}

/**
 * dbg_dce_load_fw - loads the fw to DRAM.
 *
 * @d : Pointer to dce struct
 *
 * Return : 0 if successful
 */
static int dbg_dce_load_fw(struct tegra_dce *d)
{
	const char *name = dce_get_fw_name(d);

	d->fw_data = dce_os_request_firmware(d, name);
	if (!d->fw_data) {
		dce_os_err(d, "FW Request Failed");
		return -EBUSY;
	}

	dce_set_load_fw_status(d, true);

	return 0;
}

/**
 * dbg_dce_config_ast - Configures the ast and sets the status.
 *
 * @d : Pointer to dce struct
 *
 * Return : Void
 */
static void dbg_dce_config_ast(struct tegra_dce *d)
{
	dce_config_ast(d);
	dce_set_ast_config_status(d, true);
}

/**
 * dbg_dce_reset_dce - Configures the evp in DCE cluster
 *				and brings dce out of reset.
 *
 * @d : Pointer to dce struct
 *
 * Return : 0 if successful
 */
static int dbg_dce_reset_dce(struct tegra_dce *d)
{
	int ret = 0;


	ret = dce_reset_dce(d);
	if (ret) {
		dce_os_err(d, "DCE Reset Failed");
		return ret;
	}
	dce_set_dce_reset_status(d, true);

	return ret;

}

/**
 * dbg_dce_boot_dce - loads the fw and configures other dce cluster
 *			elements for bringing dce out of reset.
 *
 * @d : Pointer to dce struct
 *
 * Return : 0 if successful
 */
static int dbg_dce_boot_dce(struct tegra_dce *d)
{
	int ret = 0;


	ret = dbg_dce_load_fw(d);
	if (ret) {
		dce_os_err(d, "DCE Load FW Failed");
		return ret;
	}

	dbg_dce_config_ast(d);

	ret = dbg_dce_reset_dce(d);

	if (ret)
		dce_os_err(d, "DCE Reset Failed");

	return ret;

}

static ssize_t dbg_dce_load_fw_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[3];
	struct tegra_dce *d = file->private_data;

	if (d->load_complete)
		buf[0] = 'Y';
	else
		buf[0] = 'N';
	buf[1] = '\n';
	buf[2] = 0x00;

	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t dbg_dce_load_fw_write(struct file *file,
			const char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char buf[32];
	int buf_size;
	bool bv;
	struct tegra_dce *d = file->private_data;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (kstrtobool(buf, &bv) == 0) {
		if (bv == true) {
			ret = dbg_dce_load_fw(d);
			if (ret)
				return ret;
		}
	}

	return count;
}

static const struct file_operations load_firmware_fops = {
	.open =		simple_open,
	.read =		dbg_dce_load_fw_read,
	.write =	dbg_dce_load_fw_write,
};

static const struct file_operations dbg_dce_log_help_fops = {
	.open		=	dbg_dce_log_help_fops_open,
	.read		=	seq_read,
	.llseek		=	seq_lseek,
	.release	=	single_release,
};

static const struct file_operations dbg_dce_log_fops = {
	.open		=	dbg_dce_log_fops_open,
	.read		=	seq_read,
	.llseek		=	seq_lseek,
	.release	=	single_release,
	.write		=	dbg_dce_log_fops_write,
};

static ssize_t dbg_dce_config_ast_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[3];
	struct tegra_dce *d = file->private_data;

	if (d->ast_config_complete)
		buf[0] = 'Y';
	else
		buf[0] = 'N';
	buf[1] = '\n';
	buf[2] = 0x00;

	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t dbg_dce_config_ast_write(struct file *file,
			const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	int buf_size;
	bool bv;
	struct tegra_dce *d = file->private_data;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (kstrtobool(buf, &bv) == 0) {
		if (bv == true)
			dbg_dce_config_ast(d);
	}

	return count;
}

static const struct file_operations config_ast_fops = {
	.open =		simple_open,
	.read =		dbg_dce_config_ast_read,
	.write =	dbg_dce_config_ast_write,
};

static ssize_t dbg_dce_reset_dce_fops_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[3];
	struct tegra_dce *d = file->private_data;

	if (d->reset_complete)
		buf[0] = 'Y';
	else
		buf[0] = 'N';
	buf[1] = '\n';
	buf[2] = 0x00;

	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t dbg_dce_reset_dce_fops_write(struct file *file,
			const char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char buf[32];
	int buf_size;
	bool bv;
	struct tegra_dce *d = file->private_data;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (kstrtobool(buf, &bv) == 0) {
		if (bv == true) {
			ret = dbg_dce_reset_dce(d);
			if (ret)
				return ret;
		}
	}

	return count;
}

static const struct file_operations reset_dce_fops = {
	.open =		simple_open,
	.read =		dbg_dce_reset_dce_fops_read,
	.write =	dbg_dce_reset_dce_fops_write,
};

static ssize_t dbg_dce_admin_echo_fops_write(struct file *file,
			const char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char buf[32];
	int buf_size;
	u32 i, echo_count;
	struct dce_ipc_message *msg = NULL;
	struct dce_admin_ipc_cmd *req_msg;
	struct dce_admin_ipc_resp *resp_msg;
	struct tegra_dce *d = file->private_data;

	buf_size = min(count, (sizeof(buf)-1));
	ret = kstrtou32_from_user(user_buf, buf_size, 10, &echo_count);
	if (ret) {
		dce_os_err(d, "Admin msg count out of range");
		goto out;
	}

	msg = dce_admin_channel_client_buffer_get(d, DCE_ADMIN_CH_CL_DBG_BUFF,
		0 /* reserved flags */);
	if (!msg) {
		dce_os_err(d, "IPC msg allocation failed");
		goto out;
	}

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);

	dce_os_info(d, "Requested %u echo messages", echo_count);

	for (i = 0; i < echo_count; i++) {
		u32 resp;

		req_msg->args.echo.data = i;
		ret = dce_admin_send_cmd_echo(d, msg);
		if (ret) {
			dce_os_err(d, "Admin msg failed for seq No : %u", i);
			goto out;
		}

		resp = resp_msg->args.echo.data;

		if (i == resp) {
			dce_os_info(d, "Received Response:%u for request:%u",
				 resp, i);
		} else {
			dce_os_err(d, "Invalid response, expected:%u received:%u",
				i, resp);
		}
	}

out:
	if (msg)
		dce_admin_channel_client_buffer_put(d, msg);
	return count;
}

static const struct file_operations admin_echo_fops = {
	.open =		simple_open,
	.write =	dbg_dce_admin_echo_fops_write,
};

/*
 * Debugfs nodes for displaying a help message about tests required by external
 * clients (ex: MODS)
 */
static int dbg_dce_tests_external_help_fops_show(struct seq_file *s, void *data)
{
	/* TODO: Add test description? */
	seq_printf(s, "DCE External Test List\n"
		      "----------------------\n"
		      "   - Test #0: MODS ALU Test\n"
		      "   - Test #1: MODS DMA Test\n"
		      "  Usage: echo 0 > run, will run ALU test\n");


	return 0;

}

static int dbg_dce_tests_external_help_fops_open(struct inode *inode,
						 struct file *file)
{
	return single_open(file, dbg_dce_tests_external_help_fops_show,
			   inode->i_private);
}

static const struct file_operations tests_external_help_fops = {
	.open		= dbg_dce_tests_external_help_fops_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* Get status of last external client test run */
static ssize_t dbg_dce_tests_external_status_fops_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	struct tegra_dce *d = file->private_data;
	struct dce_linux_device *d_dev = dce_linux_device_from_dce(d);
	char buf[15];
	ssize_t bytes_printed;

	bytes_printed = snprintf(buf, 15, "%d\n", d_dev->ext_test_status);
	if (bytes_printed < 0) {
		dce_os_err(d, "Unable to return external test status");
		buf[0] = '\0';
		bytes_printed = 0;
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, bytes_printed);
}

static const struct file_operations tests_external_status_fops = {
	.open =		simple_open,
	.read =		dbg_dce_tests_external_status_fops_read,
};

/* Run an external client test */
static ssize_t dbg_dce_tests_external_run_fops_write(struct file *file,
			const char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	u32 test;
	u64 start_time;
	u64 end_time;
	struct dce_ipc_message *msg = NULL;
	struct dce_admin_ipc_cmd *req_msg = NULL;
	struct dce_admin_ipc_resp *resp_msg = NULL;
	struct tegra_dce *d = file->private_data;
	struct dce_linux_device *d_dev = dce_linux_device_from_dce(d);

	ret = kstrtou32_from_user(user_buf, count, 10, &test);
	if (ret) {
		dce_os_err(d, "Invalid test number!");
		d_dev->ext_test_status = DCE_ERR_CORE_NOT_FOUND;
		return -EINVAL;
	}
	switch (test) {
	case DCE_ADMIN_EXT_TEST_ALU:
		dce_os_info(d, "Running ALU test");
		break;

	case DCE_ADMIN_EXT_TEST_DMA:
		dce_os_info(d, "Running DMA test");
		break;

	default:
		dce_os_err(d, "Test(%u) not found! Check help node for valid test IDs.",
			test);
		d_dev->ext_test_status = DCE_ERR_CORE_NOT_FOUND;
		return -EINVAL;
	}

	msg = dce_admin_channel_client_buffer_get(d, DCE_ADMIN_CH_CL_DBG_BUFF,
		0 /* reserved flags */);
	if (!msg) {
		dce_os_err(d, "IPC msg allocation failed");
		d_dev->ext_test_status = DCE_ERR_CORE_OTHER;
		goto exit;
	}

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);
	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);

	req_msg->args.ext_test.test_cmd  = test;

	start_time = ktime_get_real_ns();

	ret = dce_admin_send_cmd_ext_test(d, msg);
	if (ret) {
		dce_os_err(d, "Admin msg failed");
		d_dev->ext_test_status = DCE_ERR_CORE_IPC_IVC_ERR;
		goto exit;
	}

	end_time = ktime_get_real_ns();

	if (resp_msg->error == DCE_ERR_CORE_SUCCESS) {
		dce_os_err(d, "Test passed!");
		dce_os_err(d, "Took %lld microsecs to finish\n",
			((end_time - start_time) / 1000));
	} else if (resp_msg->error == DCE_ERR_CORE_NOT_IMPLEMENTED)
		dce_os_err(d, "Test not implemented!");
	else
		dce_os_err(d, "Test failed(%d)!", (int32_t)resp_msg->error);
	d_dev->ext_test_status = resp_msg->error;

exit:
	if (msg)
		dce_admin_channel_client_buffer_put(d, msg);
	return count;
}

static const struct file_operations tests_external_run_fops = {
	.open =		simple_open,
	.write =	dbg_dce_tests_external_run_fops_write,
};

static ssize_t dbg_dce_boot_dce_fops_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[3];
	struct tegra_dce *d = file->private_data;

	if (d->ast_config_complete &&
		d->reset_complete && d->load_complete)
		buf[0] = 'Y';
	else
		buf[0] = 'N';
	buf[1] = '\n';
	buf[2] = 0x00;

	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t dbg_dce_boot_dce_fops_write(struct file *file,
			const char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char buf[32];
	int buf_size;
	bool bv;
	struct tegra_dce *d = file->private_data;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (kstrtobool(buf, &bv) == 0) {
		if (bv == true) {
			ret = dbg_dce_boot_dce(d);
			if (ret)
				return ret;
		}
	}

	return count;
}

static const struct file_operations boot_dce_fops = {
	.open =		simple_open,
	.read =		dbg_dce_boot_dce_fops_read,
	.write =	dbg_dce_boot_dce_fops_write,
};

static ssize_t dbg_dce_boot_status_fops_read(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	u8 fsb;
	char buf[32];
	u32 last_status;
	ssize_t len = 0;
	unsigned long bitmap;
	struct tegra_dce *d = file->private_data;
	u32 boot_status = d->boot_status;
	hsp_sema_t ss = d->hsp.ss_get_state(d, d->hsp_id, DCE_BOOT_SEMA);

	if (ss & DCE_BOOT_COMPLETE)
		goto core_boot_done;

	/* Clear BOOT_COMPLETE bit and bits set by OS */
	ss &= ~(DCE_OS_BITMASK | DCE_BOOT_COMPLETE);
	bitmap = ss;

	fsb = find_first_bit(&bitmap, 32U);
	if (fsb > 31U) {
		dce_os_info(d, "dce-fw boot not started yet");
		goto core_boot_done;
	}

	last_status = DCE_BIT(fsb);

	switch (last_status) {
	case DCE_HALTED:
		strcpy(buf, "DCE_HALTED");
		break;
	case DCE_BOOT_TCM_COPY:
		strcpy(buf, "TCM_COPY");
		break;
	case DCE_BOOT_HW_INIT:
		strcpy(buf, "HW_INIT");
		break;
	case DCE_BOOT_MPU_INIT:
		strcpy(buf, "MPU_INIT:");
		break;
	case DCE_BOOT_CACHE_INIT:
		strcpy(buf, "CACHE_INIT");
		break;
	case DCE_BOOT_R5_INIT:
		strcpy(buf, "R5_INIT");
		break;
	case DCE_BOOT_DRIVER_INIT:
		strcpy(buf, "DRIVER_INIT");
		break;
	case DCE_BOOT_MAIN_STARTED:
		strcpy(buf, "MAIN_STARTED");
		break;
	case DCE_BOOT_TASK_INIT_START:
		strcpy(buf, "TASK_INIT_STARTED");
		break;
	case DCE_BOOT_TASK_INIT_DONE:
		strcpy(buf, "TASK_INIT_DONE");
		break;
	default:
		strcpy(buf, "STATUS_UNKNOWN");
		break;
	}
	goto done;

core_boot_done:
	/* Clear DCE_STATUS_FAILED bit get actual failure reason*/
	boot_status &= ~DCE_STATUS_FAILED;
	bitmap = boot_status;
	last_status = DCE_BIT(find_first_bit(&bitmap, 32));

	switch (last_status) {
	case DCE_FW_SUSPENDED:
		strcpy(buf, "DCE_FW_SUSPENDED");
		break;
	case DCE_FW_BOOT_DONE:
		strcpy(buf, "DCE_FW_BOOT_DONE");
		break;
	case DCE_FW_ADMIN_SEQ_DONE:
		strcpy(buf, "DCE_FW_ADMIN_SEQ_DONE");
		break;
	case DCE_FW_ADMIN_SEQ_FAILED:
		strcpy(buf, "DCE_FW_ADMIN_SEQ_FAILED");
		break;
	case DCE_FW_ADMIN_SEQ_START:
		strcpy(buf, "DCE_FW_ADMIN_SEQ_STARTED");
		break;
	case DCE_FW_BOOTSTRAP_DONE:
		strcpy(buf, "DCE_FW_BOOTSTRAP_DONE");
		break;
	case DCE_FW_BOOTSTRAP_FAILED:
		strcpy(buf, "DCE_FW_BOOTSTRAP_FAILED");
		break;
	case DCE_FW_BOOTSTRAP_START:
		strcpy(buf, "DCE_FW_BOOTSTRAP_STARTED");
		break;
	case DCE_FW_EARLY_BOOT_FAILED:
		strcpy(buf, "DCE_FW_EARLY_BOOT_FAILED");
		break;
	case DCE_FW_EARLY_BOOT_DONE:
		strcpy(buf, "DCE_FW_EARLY_BOOT_DONE");
		break;
	case DCE_AST_CONFIG_DONE:
		strcpy(buf, "DCE_AST_CONFIG_DONE");
		break;
	case DCE_AST_CONFIG_START:
		strcpy(buf, "DCE_AST_CONFIG_STARTED");
		break;
	case DCE_EARLY_INIT_DONE:
		strcpy(buf, "DCE_EARLY_INIT_DONE");
		break;
	case DCE_EARLY_INIT_FAILED:
		strcpy(buf, "DCE_EARLY_INIT_FAILED");
		break;
	case DCE_EARLY_INIT_START:
		strcpy(buf, "DCE_EARLY_INIT_STARTED");
		break;
	default:
		strcpy(buf, "STATUS_UNKNOWN");
	}

done:
	len = strlen(buf);
	buf[len] = '\0';
	dce_os_info(d, "boot status:%s status_val:0x%x\n", buf, d->boot_status);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len + 1);
}

static const struct file_operations boot_status_fops = {
	.open	= simple_open,
	.read	= dbg_dce_boot_status_fops_read,
};

static const struct file_operations perf_format_fops = {
	.open		= dbg_dce_perf_format_fops_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= dbg_dce_perf_format_fops_write,
};

static const struct file_operations perf_stats_stats_fops = {
	.open		= dbg_dce_perf_stats_stats_fops_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= dbg_dce_perf_stats_stats_fops_write,
};

static const struct file_operations perf_stats_help_fops = {
	.open		= dbg_dce_perf_stats_help_fops_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations perf_events_events_fops = {
	.open		= dbg_dce_perf_events_events_fops_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= dbg_dce_perf_events_events_fops_write,
};

static const struct file_operations perf_events_help_fops = {
	.open		= dbg_dce_perf_events_help_fops_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void dce_remove_debug(struct tegra_dce *d)
{
	struct dce_linux_device *d_dev = dce_linux_device_from_dce(d);

	dce_admin_channel_client_buffers_deinit(d, DCE_ADMIN_CH_CL_DBG_BUFF);

	/**
	 * TODO: Separate out below debug perf debug deinit code to separate
	 * debug perf deinit function.
	 */
	dce_admin_channel_client_buffers_deinit(d, DCE_ADMIN_CH_CL_DBG_PERF_BUFF);

	debugfs_remove(d_dev->debugfs);

	d_dev->debugfs = NULL;
}

static int dump_hsp_regs_show(struct seq_file *s, void *unused)
{
	u8 i = 0;
	struct tegra_dce *d = s->private;

	/**
	 * Dump Boot Semaphore Value
	 */
	dce_os_info(d, "DCE_BOOT_SEMA : 0x%x",
				d->hsp.ss_get_state(d, d->hsp_id, DCE_BOOT_SEMA));

	/**
	 * Dump Shared Mailboxes Values
	 */
	dce_os_info(d, "DCE_MBOX_FROM_DCE_RM : 0x%x",
			d->hsp.smb_read(d, d->hsp_id, DCE_MBOX_FROM_DCE_RM));
	dce_os_info(d, "DCE_MBOX_TO_DCE_RM: 0x%x",
			d->hsp.smb_read(d, d->hsp_id, DCE_MBOX_TO_DCE_RM));
	dce_os_info(d, "DCE_MBOX_FROM_DCE_RM_EVENT_NOTIFY: 0x%x",
			d->hsp.smb_read(d, d->hsp_id, DCE_MBOX_FROM_DCE_RM_EVENT_NOTIFY));
	dce_os_info(d, "DCE_MBOX_TO_DCE_RM_EVENT_NOTIFY: 0x%x",
			d->hsp.smb_read(d, d->hsp_id, DCE_MBOX_TO_DCE_RM_EVENT_NOTIFY));
	dce_os_info(d, "DCE_MBOX_FROM_DCE_ADMIN: 0x%x",
			d->hsp.smb_read(d, d->hsp_id, DCE_MBOX_FROM_DCE_ADMIN));
	dce_os_info(d, "DCE_MBOX_BOOT_CMD: 0x%x",
			d->hsp.smb_read(d, d->hsp_id, DCE_MBOX_BOOT_CMD));
	dce_os_info(d, "DCE_MBOX_IRQ: 0x%x",
			d->hsp.smb_read(d, d->hsp_id, DCE_MBOX_IRQ));

	/**
	 * Dump HSP IE registers Values
	 */

#define DCE_MAX_IE_REGS 5U
	for (i = 0; i < DCE_MAX_IE_REGS; i++)
		dce_os_info(d, "DCE_HSP_IE_%d : 0x%x", i, d->hsp.hsp_ie_read(d, d->hsp_id, i));
#undef DCE_MAX_IE_REGS

	/**
	 * Dump HSP IE registers Values
	 */
#define DCE_MAX_SM_FULL_REGS 8U
	for (i = 0; i < DCE_MAX_SM_FULL_REGS; i++) {
		dce_os_info(d, "DCE_HSP_SM_FULL_%d : 0x%x", i,
			 d->hsp.smb_read_full_ie(d, d->hsp_id, i));
	}
#undef DCE_MAX_SM_FULL_REGS

	dce_os_info(d, "DCE_HSP_IR : 0x%x",
			d->hsp.hsp_ir_read(d, d->hsp_id));
	return 0;
}

static int dump_hsp_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, dump_hsp_regs_show, inode->i_private);
}

static const struct file_operations dump_hsp_regs_fops = {
	.open		= dump_hsp_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * dce_init_debug - Initializes the debug features of dce
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : Void
 */
void dce_init_debug(struct tegra_dce *d)
{
	struct dentry *retval;
	struct device *dev = dev_from_dce_linux_device(d);
	struct dce_linux_device *d_dev = dce_linux_device_from_dce(d);
	struct dentry *debugfs_dir = NULL;
	struct dentry *perf_debugfs_dir = NULL;
	struct dentry *dce_logging_dir = NULL;
	int ret = 0;

	ret = dce_admin_channel_client_buffers_init(d, DCE_ADMIN_CH_CL_DBG_BUFF);
	if (ret) {
		dce_os_err(d, "Admin channel clients buffer init failed: DBG");
		goto err_handle;
	}

	d_dev->debugfs = debugfs_create_dir("tegra_dce", NULL);
	if (!d_dev->debugfs)
		return;

	retval = debugfs_create_file("load_fw", 0444,
				     d_dev->debugfs, d, &load_firmware_fops);
	if (!retval)
		goto err_handle;

	dce_logging_dir = debugfs_create_dir("dce_logs", d_dev->debugfs);
	if (!dce_logging_dir)
		goto err_handle;

	retval = debugfs_create_file("help", 0444,
					dce_logging_dir, d, &dbg_dce_log_help_fops);
	if (!retval)
		goto err_handle;

	retval = debugfs_create_file("logs", 0644,
					dce_logging_dir, d, &dbg_dce_log_fops);
	if (!retval)
		goto err_handle;

	retval = debugfs_create_file("config_ast", 0444,
				     d_dev->debugfs, d, &config_ast_fops);
	if (!retval)
		goto err_handle;

	retval = debugfs_create_file("reset", 0444,
				     d_dev->debugfs, d, &reset_dce_fops);
	if (!retval)
		goto err_handle;

	retval = debugfs_create_file("boot", 0444,
				     d_dev->debugfs, d, &boot_dce_fops);
	if (!retval)
		goto err_handle;

	retval = debugfs_create_file("admin_echo", 0444,
				     d_dev->debugfs, d, &admin_echo_fops);
	if (!retval)
		goto err_handle;

	retval = debugfs_create_file("boot_status", 0444,
				     d_dev->debugfs, d, &boot_status_fops);
	if (!retval)
		goto err_handle;

	/**
	 * TODO: Separate out below debug perf init code to separate debug perf
	 *	init function.
	 */
	ret = dce_admin_channel_client_buffers_init(d, DCE_ADMIN_CH_CL_DBG_PERF_BUFF);
	if (ret) {
		dce_os_err(d, "Admin channel clients buffer init failed: DBG_PERF");
		goto err_handle;
	}

	perf_debugfs_dir = debugfs_create_dir("perf", d_dev->debugfs);
	if (!perf_debugfs_dir)
		goto err_handle;

	retval = debugfs_create_file("format", 0644,
				     perf_debugfs_dir, d, &perf_format_fops);
	if (!retval)
		goto err_handle;

	debugfs_dir = debugfs_create_dir("stats", perf_debugfs_dir);
	if (!debugfs_dir)
		goto err_handle;

	retval = debugfs_create_file("stats", 0644,
				     debugfs_dir, d, &perf_stats_stats_fops);
	if (!retval)
		goto err_handle;

	retval = debugfs_create_file("help", 0444,
				     debugfs_dir, d, &perf_stats_help_fops);
	if (!retval)
		goto err_handle;

	debugfs_dir = debugfs_create_dir("events", perf_debugfs_dir);
	if (!debugfs_dir)
		goto err_handle;

	retval = debugfs_create_file("events", 0644,
				     debugfs_dir, d, &perf_events_events_fops);
	if (!retval)
		goto err_handle;

	retval = debugfs_create_file("help", 0444,
				     debugfs_dir, d, &perf_events_help_fops);
	if (!retval)
		goto err_handle;

	retval = debugfs_create_file("dump_hsp_regs", 0444,
				     d_dev->debugfs, d, &dump_hsp_regs_fops);
	if (!retval)
		goto err_handle;

	/* Tests */
	debugfs_dir = debugfs_create_dir("tests", d_dev->debugfs);
	if (!debugfs_dir)
		goto err_handle;
	debugfs_dir = debugfs_create_dir("external", debugfs_dir);
	if (!debugfs_dir)
		goto err_handle;
	retval = debugfs_create_file("help", 0444,
				     debugfs_dir, d, &tests_external_help_fops);
	if (!retval)
		goto err_handle;
	retval = debugfs_create_file("run", 0220,
				     debugfs_dir, d, &tests_external_run_fops);
	if (!retval)
		goto err_handle;
	retval = debugfs_create_file("status", 0444,
				     debugfs_dir, d, &tests_external_status_fops);
	if (!retval)
		goto err_handle;

	return;

err_handle:
	dev_err(dev, "could not create debugfs\n");
	dce_remove_debug(d);
}
