// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.*/

/**
 * @file tegra-hsierrrptinj.c
 * @brief <b> HSI Error Report Injection driver</b>
 *
 * This file will register as client driver to support triggering
 * HSI error reporting from CCPLEX to FSI.
 */

/* ==================[Includes]============================================= */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/debugfs.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/mailbox_client.h>
#include "linux/tegra-hsierrrptinj.h"


/* Format of input buffer */
/* IP ID : Instance ID : Error Code : Reporter ID : Error Attribute */
/* "0x0000:0x0000:0x00000000:0x0000:0x00000000" */
#define ERR_RPT_LEN 43U

#define NUM_INPUTS 5U

#define WRITE_USER 0200 /* S_IWUSR */

/* Max instance of any registred IP */
#define MAX_INSTANCE 11U

/* Timeout in millisec */
#define TIMEOUT		1000

/* Error code for reporting errors to FSI */
#define HSM_ERROR 0x55
#define FSI_SW_ERROR 0xAA
#define SC7_ENTRY_ERROR 0xBB

/* EC Index for reporting errors to FSI */
#define EC_INDEX 0xFFFFFFFF

/* Error report frame for reporting errors to FSI */
struct hsm_error_report_frame {
	unsigned int type;
	unsigned int error_code;
	unsigned int reporter_id;
	unsigned int ec_index;
};

/**
 * Note: Any update to IP instances array should be reflected in the macro MAX_INSTANCE.
 */

/**
 * @brief IP Instances
 * OTHER - 1
 * GPU   - 1
 * EQOS  - 5
 * MGBE  - 20
 * PCIE  - 11
 * PSC   - 1
 * I2C   = 10
 * QSPI  - 2
 * SDMMC - 5
 * TSEC  - 1
 * THERM - 1
 * SMMU  - 1
 * DLA   - 2
 */
static unsigned int ip_instances[NUM_IPS] = {1, 1, 5, 20, 11, 1, 10, 2, 5, 1, 1, 1, 2};

/* This directory entry will point to `/sys/kernel/debug/tegra_hsierrrptinj`. */
static struct dentry *hsierrrptinj_debugfs_root;

/* This file will point to `/sys/kernel/debug/tegra_hsierrrptinj/hsierrrpt`. */
static const char *hsierrrptinj_debugfs_name = "hsierrrpt";

/* Data type to store IP Drivers callbacks and auxiliary data */
struct hsierrrptinj_inj_cb_data {
	hsierrrpt_inj cb;
	void *data;
};

/* This array stores callbacks and auxiliary data registered by IP Drivers */
static struct hsierrrptinj_inj_cb_data ip_driver_cb_data[NUM_IPS][MAX_INSTANCE] = {NULL};

/* Data type for mailbox client and channel details */
struct hsierrrptinj_hsp_sm {
	struct mbox_client client;
	struct mbox_chan *chan;
};

static struct hsierrrptinj_hsp_sm hsierrrptinj_tx;

/* Buffer len to store HSI Err Rpt Inj status */
#define ERR_INJ_STATUS_LEN 31U

/* Store stayus of error report injection */
static bool hsierrrptinj_status = true;

/* Stores the timestamp value when error was injected */
static uint32_t error_report_timestamp = 1U;

/* Register Error callbacks from IP Drivers */
int hsierrrpt_reg_cb(hsierrrpt_ipid_t ip_id, unsigned int instance_id,
		     hsierrrpt_inj cb_func, void *aux_data)
{
	pr_debug("tegra-hsierrrptinj: Register callback for IP Driver 0x%04x\n", ip_id);

	if (cb_func == NULL) {
		pr_err("tegra-hsierrrptinj: Callback function for 0x%04X invalid\n", ip_id);
		return -EINVAL;
	}

	if (ip_id < 0 || ip_id >= NUM_IPS) {
		pr_err("tegra-hsierrrptinj: Invalid IP ID 0x%04x\n", ip_id);
		return -EINVAL;
	}

	if (instance_id >= ip_instances[ip_id]) {
		pr_err("tegra-hsierrrptinj: Invalid instance 0x%04x\n", instance_id);
		return -EINVAL;
	}

	if (ip_driver_cb_data[ip_id][instance_id].cb != NULL) {
		pr_err("tegra-hsierrrptinj: Callback for 0x%04X already registered\n", ip_id);
		return -EINVAL;
	}

	ip_driver_cb_data[ip_id][instance_id].cb = cb_func;
	ip_driver_cb_data[ip_id][instance_id].data = aux_data;

	pr_debug("tegra-hsierrrptinj: Successfully registered callback for 0x%04X\n", ip_id);

	return 0;
}
EXPORT_SYMBOL(hsierrrpt_reg_cb);

/* De-register Error callbacks from IP Drivers */
int hsierrrpt_dereg_cb(hsierrrpt_ipid_t ip_id, unsigned int instance_id)
{
	pr_debug("tegra-hsierrrptinj: De-register callback for IP Driver 0x%04x\n", ip_id);

	if (ip_id >= NUM_IPS || ip_id < 0) {
		pr_err("tegra-hsierrrptinj: Invalid IP ID 0x%04x\n", ip_id);
		return -EINVAL;
	}

	if (instance_id >= ip_instances[ip_id]) {
		pr_err("tegra-hsierrrptinj: Invalid instance 0x%04x\n", instance_id);
		return -EINVAL;
	}

	if (ip_driver_cb_data[ip_id][instance_id].cb == NULL) {
		pr_err("tegra-hsierrrptinj: Callback for 0x%04X has not been registered\n", ip_id);
		return -EINVAL;
	}

	ip_driver_cb_data[ip_id][instance_id].cb = NULL;

	pr_debug("tegra-hsierrrptinj: Successfully de-registered callback for 0x%04X\n", ip_id);

	return 0;
}
EXPORT_SYMBOL(hsierrrpt_dereg_cb);

/* Report errors to FSI */
static int hsierrrpt_report_to_fsi(struct epl_error_report_frame err_rpt_frame, int ip_id)
{
	int ret = -EINVAL;

	/* Bypass path for reporting errors mapped to Category-1 (local EC) IPs */
	struct hsm_error_report_frame error_report = {0};

	if (IS_ERR(hsierrrptinj_tx.chan)) {
		pr_err("tegra-hsierrrptinj: Mailbox channel or client not initiated\n");
		return -ENODEV;
	}


	error_report.error_code = err_rpt_frame.error_code;
	error_report.reporter_id = err_rpt_frame.reporter_id;
	error_report.ec_index = EC_INDEX;

	if (ip_id == IP_HSM || ip_id == IP_EC)
		error_report.type = HSM_ERROR;
	else if (ip_id == IP_FSI)
		error_report.type = FSI_SW_ERROR;
	else if (ip_id == IP_SC7)
		error_report.type = SC7_ENTRY_ERROR;
	else
		return -EINVAL;

	/* Special case where ec_index other than 0xFFFFFFFF,
	 * then FSI SW ignore reporter_id and only uses error_code.
	 */
	if (ip_id == IP_EC)
		error_report.ec_index = error_report.reporter_id;

	ret = mbox_send_message(hsierrrptinj_tx.chan, (void *)&error_report);

	return ret < 0 ? ret : 0;
}

/* Parse user entered data via debugfs interface and trigger IP Driver callback */
static ssize_t hsierrrptinj_inject(struct file *file, const char __user *buf,
				   size_t lbuf, loff_t *ppos)
{
	struct epl_error_report_frame error_report = {0};
	int count = 0, ret = -EINVAL;
	unsigned long val = 0;
	unsigned int ip_id = 0;
	unsigned int instance_id = 0x0000;
	char ubuf[ERR_RPT_LEN] = {0};
	char *token, *cur = ubuf;
	const char *delim = ":";

	pr_debug("tegra-hsierrrptinj: Inject Error Report\n");
	if (buf == NULL) {
		pr_err("tegra-hsierrrptinj: Invalid null input.\n");
		hsierrrptinj_status = false;
		error_report_timestamp = 0;
		return -EINVAL;
	}

	if (lbuf != ERR_RPT_LEN) {
		pr_err("tegra-hsierrrptinj: Invalid input length.\n");
		hsierrrptinj_status = false;
		error_report_timestamp = 0;
		return -EINVAL;
	}

	if (copy_from_user(&ubuf, buf, ERR_RPT_LEN)) {
		pr_err("tegra-hsierrrptinj: Failed to copy from input buffer.\n");
		hsierrrptinj_status = false;
		error_report_timestamp = 0;
		return -EFAULT;
	}

	pr_debug("tegra-hsierrrptinj: Input Buffer: %s\n", cur);

	/* Extract data and trigger */
	while (count < NUM_INPUTS) {

		token = strsep(&cur, delim);
		if (token == NULL) {
			pr_err("tegra-hsierrrptinj: Failed to obtain token\n");
			hsierrrptinj_status = false;
			error_report_timestamp = 0;
			return -EFAULT;
		}

		ret = kstrtoul(token, 16, &val);
		if (ret < 0) {
			pr_err("tegra-hsierrrptinj: Parsing failed. Error: %d\n", ret);
			hsierrrptinj_status = false;
			error_report_timestamp = 0;
			return ret;
		}

		switch (count) {

		case 0: /* IP ID */
			pr_debug("tegra-hsierrrptinj: IP ID: 0x%04lx\n", val);
			ip_id = val;
			count++;
			break;
		case 1: /* Instance ID */
			pr_debug("tegra-hsierrrptinj: Instance ID: 0x%04lx\n", val);
			instance_id = val;
			count++;
			break;
		case 2: /* Error Code */
			pr_debug("tegra-hsierrrptinj: HSI Error ID: 0x%08lx\n", val);
			error_report.error_code = val;
			count++;
			break;
		case 3: /* Reporter ID */
			pr_debug("tegra-hsierrrptinj: Reporter ID: 0x%04lx\n", val);
			error_report.reporter_id = val;
			count++;
			break;
		case 4: /* Error Attribute */
			pr_debug("tegra-hsierrrptinj: Error Attribute: 0x%08lx\n", val);
			error_report.error_attribute = val;
			count++;
			break;
		}

	}

	if (count != NUM_INPUTS) {
		pr_err("tegra-hsierrrptinj: Invalid Input format.\n");
		hsierrrptinj_status = false;
		error_report_timestamp = 0;
		return -EINVAL;
	}

	/* Get current timestamp */
	asm volatile("mrs %0, cntvct_el0" : "=r" (error_report.timestamp));

	/* IPs not in the hsierrrpt_ipid_t list normally report HSI errors to the FSI
	 * via their local EC, therefore their controlling drivers do not provide a callback.
	 * Directly send error reports for such IPs to the FSI.
	 */
	if (ip_id >= NUM_IPS) {
		ret = hsierrrpt_report_to_fsi(error_report, ip_id);
		pr_debug("tegra-hsierrrptinj: Reporting error to FSI\n");
		goto done;
	}

	if (instance_id >= ip_instances[ip_id]) {
		pr_err("tegra-hsierrrptinj: Invalid instance for IP Driver 0x%04x\n", ip_id);
		hsierrrptinj_status = false;
		error_report_timestamp = 0;
		return -EINVAL;
	}

	/* Trigger IP driver registered callback. If no callback has been registered,
	 * call the EPD-provided API.
	 *
	 * We want certain logging statements to appear in the kernel log with the
	 * OOTB level configuration. Therefore use pr_err for those statements.
	 */
	if (ip_driver_cb_data[ip_id][instance_id].cb != NULL) {
		ret = ip_driver_cb_data[ip_id][instance_id].cb(instance_id, error_report,
						ip_driver_cb_data[ip_id][instance_id].data);
		pr_err("tegra-hsierrrptinj: Triggered registered error report callback\n");
	} else {
		ret = epl_report_error(error_report);
		pr_err("tegra-hsierrrptinj: No registered error report trigger callback found\n");
		pr_err("tegra-hsierrrptinj: Reporting HSI error to FSI directly\n");
	}

done:
	if (ret != 0) {
		pr_err("tegra-hsierrrptinj: Failed to report HSI error to FSI\n");
		pr_err("tegra-hsierrrptinj: Error code: %d", ret);
		/* Error code has been logged.
		 * Change error code in case of timeout error
		 * to prevent re-triggering of .write fops.
		 */
		if (ret == -ETIME) {
			/* Explicitly change error code to -EFAULT */
			ret = -EFAULT;
		}
	} else {
		pr_err("tegra-hsierrrptinj: Successfully reported HSI error to FSI\n");
		/* On success, return the error report length.
		 * This represents the number of bytes successfully written.
		 * Returning 0, would re-trigger .write fops
		 */
		ret = ERR_RPT_LEN;
	}

	pr_err("tegra-hsierrrptinj: IP ID: 0x%04x\n", ip_id);
	pr_err("tegra-hsierrrptinj: Instance: 0x%04x\n", instance_id);
	pr_err("tegra-hsierrrptinj: HSI Error ID: 0x%08x\n", error_report.error_code);
	pr_err("tegra-hsierrrptinj: Timestamp: %u\n", error_report.timestamp);

	if (ret != ERR_RPT_LEN) {
		hsierrrptinj_status = false;
		error_report_timestamp = 0;
	} else {
		hsierrrptinj_status = true;
		error_report_timestamp = error_report.timestamp;
	}

	return ret;
}

/* Report the status of HSI error report injection to user */
static ssize_t hsierrrptinj_result(struct file *file, char __user *buf, size_t lbuf, loff_t *ppos)
{
	char ubuf[ERR_INJ_STATUS_LEN] = {0};
	size_t len = 0, space = 0;
	loff_t zero_offset = 0;

	pr_debug("tegra-hsierrrptinj: Error Report Injection status\n");

	if (buf == NULL) {
		pr_err("tegra-hsierrrptinj: Invalid null buffer.\n");
		return -EINVAL;
	}

	space = max(ERR_INJ_STATUS_LEN - *ppos, zero_offset);
	len = min(space, lbuf);
	if (len <= 0)
		return 0;

	if (hsierrrptinj_status) {
		if (snprintf(ubuf, ERR_INJ_STATUS_LEN, "Success. Timestamp: %u",
								error_report_timestamp) <= 0)
			return -EFAULT;
	} else {
		if (snprintf(ubuf, ERR_INJ_STATUS_LEN, "Failure. Timestamp: %u",
								error_report_timestamp) <= 0)
			return -EFAULT;
	}

	if (*ppos < 0) {
		pr_err("tegra-hsierrrptinj: Invalid offset.\n");
		return -EINVAL;
	}

	if (copy_to_user(buf, ubuf + *ppos, len)) {
		pr_err("tegra-hsierrrptinj: Failed to report status to user\n");
		return -EFAULT;
	}

	*ppos += len;

	return len;
}

static const struct file_operations hsierrrptinj_fops = {
		.read  = hsierrrptinj_result,
		.write = hsierrrptinj_inject,
};


static int __maybe_unused hsierrrptinj_suspend(struct device *dev)
{
	pr_debug("tegra-hsierrrptinj: suspend called\n");
	return 0;
}

static int __maybe_unused hsierrrptinj_resume(struct device *dev)
{
	pr_debug("tegra-hsierrrptinj: resume called\n");
	return 0;
}
static SIMPLE_DEV_PM_OPS(hsierrrptinj_pm, hsierrrptinj_suspend, hsierrrptinj_resume);

static const struct of_device_id hsierrrptinj_dt_match[] = {
	{ .compatible = "nvidia,tegra23x-hsierrrptinj", },
	{ }
};
MODULE_DEVICE_TABLE(of, hsierrrptinj_dt_match);

static void tegra_hsp_tx_empty_notify(struct mbox_client *cl, void *data, int empty_value)
{
	pr_debug("tegra-hsierrrptinj: TX empty callback came\n");
}

static int tegra_hsp_mb_init(struct device *dev)
{
	int err;

	hsierrrptinj_tx.client.dev = dev;
	hsierrrptinj_tx.client.tx_block = true;
	hsierrrptinj_tx.client.tx_tout = TIMEOUT;
	hsierrrptinj_tx.client.tx_done = tegra_hsp_tx_empty_notify;

	hsierrrptinj_tx.chan = mbox_request_channel_byname(&hsierrrptinj_tx.client,
							    "hsierrrptinj-tx");
	if (IS_ERR(hsierrrptinj_tx.chan)) {
		err = PTR_ERR(hsierrrptinj_tx.chan);
		pr_err("tegra-hsierrrptinj: Failed to get tx mailbox: %d\n", err);
		return err;
	}

	return 0;
}

static int hsierrrptinj_probe(struct platform_device *pdev)
{
	int ret = -EFAULT;
	struct dentry *dent = NULL;
	struct device *dev = &pdev->dev;

	/* Initiate TX Mailbox */
	ret = tegra_hsp_mb_init(dev);
	if (ret != 0) {
		pr_err("tegra-hsierrrptinj: Failed initiating tx mailbox\n");
		goto abort;
	}
	pr_err("tegra-hsierrrptinj: Successfully initiated TX Mailbox\n");

	/* Create a directory 'tegra_hsierrrptinj' under 'sys/kernel/debug'
	 * to hold the set of debug files
	 */
	pr_debug("tegra-hsierrrptinj: Create debugfs directory\n");
	hsierrrptinj_debugfs_root = debugfs_create_dir("tegra_hsierrrptinj", NULL);
	if (IS_ERR_OR_NULL(hsierrrptinj_debugfs_root)) {
		pr_err("tegra-hsierrrptinj: Failed to create debug directory\n");
		ret = -EFAULT;
		goto abort;
	}

	/* Create a debug node 'hsierrrpt' under 'sys/kernel/debug/tegra_hsierrrptinj' */
	pr_debug("tegra-hsierrrptinj: Create debugfs node\n");
	dent = debugfs_create_file(hsierrrptinj_debugfs_name, WRITE_USER,
				   hsierrrptinj_debugfs_root, NULL, &hsierrrptinj_fops);
	if (IS_ERR_OR_NULL(dent)) {
		pr_err("tegra-hsierrrptinj: Failed to create debugfs node\n");
		ret = -EFAULT;
		goto abort;
	}
	pr_err("tegra-hsierrrptinj: Debug node created successfully\n");

	pr_debug("tegra-hsierrrptinj: probe success");
	return 0;

abort:
	pr_err("tegra-hsierrrptinj: Failed to create debug node/directory or setup mailbox.\n");
	return ret;
}

static int hsierrrptinj_remove(struct platform_device *pdev)
{
	/* We must explicitly remove the debugfs entries we created. They are not
	 * automatically removed upon module removal.
	 */
	pr_debug("tegra-hsierrrptinj: Recursively remove directory and node created\n");
	debugfs_remove_recursive(hsierrrptinj_debugfs_root);
	return 0;
}

static struct platform_driver hsierrrptinj = {
	.driver         = {
		.name   = "hsierrrptinj",
		.of_match_table = of_match_ptr(hsierrrptinj_dt_match),
		.pm = pm_ptr(&hsierrrptinj_pm),
	},
	.probe          = hsierrrptinj_probe,
	.remove         = hsierrrptinj_remove,
};
module_platform_driver(hsierrrptinj);

MODULE_DESCRIPTION("tegra: HSI Error Report Injection driver");
MODULE_AUTHOR("Prasun Kumar <prasunk@nvidia.com>");
MODULE_LICENSE("GPL v2");
