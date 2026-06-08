// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/dma-buf.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/mailbox_client.h>
#include <linux/tegra-epl.h>
#include <linux/pm.h>

/* Timeout in milliseconds */
#define TIMEOUT		5U

/* 32bit data Length */
#define MAX_LEN	4

/* Macro indicating total number of Misc Sw generic errors in Misc EC */
#define NUM_SW_GENERIC_ERR 5U

/* signature code for HSP pm notify data */
#define PM_STATE_UNI_CODE	0xFDEF

/* Timestamp validation constants */
#define TIMESTAMP_CNT_PERIOD	0x100000000ULL  /* 32-bit SoC TSC counter period (2^32) */

/* This value is derived from the DOS FDTI (100ms) - EPL propagation delay (10ms) */
#define TIMESTAMP_VALID_RANGE	90000000ULL  /* 90ms in nanoseconds */

/* Timestamp resolution constants (in nanoseconds) */
#define TEGRA234_TIMESTAMP_RESOLUTION_NS	32U
#define TEGRA264_TIMESTAMP_RESOLUTION_NS	1U

/* State Management */
#define EPS_DOS_INIT                0U
#define EPS_DOS_SUSPEND             3U
#define EPS_DOS_RESUME              4U
#define EPS_DOS_DEINIT              5U
#define EPS_DOS_UNKNOWN             255U

enum handshake_state {
	HANDSHAKE_PENDING,
	HANDSHAKE_FAILED,
	HANDSHAKE_DONE
};

/* Data type for mailbox client and channel details */
struct epl_hsp_sm {
	struct mbox_client client;
	struct mbox_chan *chan;
};

/* Data type for accessing TOP2 HSP */
struct epl_hsp {
	struct epl_hsp_sm tx;
	struct device dev;
};

/* Data type to store Misc Sw Generic error configuration */
struct epl_misc_sw_err_cfg {
	void __iomem *err_code_va;
	void __iomem *err_assert_va;
	const char *dev_configured;
};

/* Error index offset in mission status register */
static uint32_t error_index_offset = 3;

/* Timestamp resolution for current SoC (in nanoseconds) */
static uint32_t timestamp_resolution_ns = TEGRA264_TIMESTAMP_RESOLUTION_NS;

static int device_file_major_number;
static const char device_name[] = "epdaemon";

static struct platform_device *pdev_local;

static struct epl_hsp *epl_hsp_v;

static void __iomem *mission_err_status_va;

static bool isAddrMappOk = true;

static struct epl_misc_sw_err_cfg miscerr_cfg[NUM_SW_GENERIC_ERR];

/* State of FSI handshake */
static enum handshake_state hs_state = HANDSHAKE_PENDING;
static const int default_handshake_retry_count = 25;
static uint32_t handshake_retry_count;

static bool enable_deinit_notify;

/* Helper function to SoC TSC timestamp */
static inline uint32_t epl_get_current_timestamp(void)
{
	uint32_t timestamp;

	asm volatile("mrs %0, cntvct_el0" : "=r" (timestamp));
	return timestamp;
}

/* Helper function to convert SoC TSC timestamp ticks to nanoseconds */
static inline uint64_t epl_ticks_to_ns(uint64_t ticks)
{
	return ticks * timestamp_resolution_ns;
}

static void tegra_hsp_tx_empty_notify(struct mbox_client *cl,
					 void *data, int empty_value)
{
	pr_debug("EPL: TX empty callback came\n");
}

static int tegra_hsp_mb_init(struct device *dev)
{
	int err;
	u32 timeout_ms = TIMEOUT;
	int prop_ret = -EINVAL;

	epl_hsp_v = devm_kzalloc(dev, sizeof(*epl_hsp_v), GFP_KERNEL);
	if (!epl_hsp_v)
		return -ENOMEM;

	/* Allow DT to override mailbox TX timeout (in ms) */
	if (dev->of_node) {
		prop_ret = of_property_read_u32(dev->of_node, "nvidia,tx-timeout-ms", &timeout_ms);
		if (!prop_ret)
			dev_info(dev, "tegra-epl: mailbox tx timeout set to %u ms from DT\n", timeout_ms);
		else
			dev_info(dev, "tegra-epl: mailbox tx timeout not provided; using default %u ms\n", timeout_ms);
	}

	epl_hsp_v->tx.client.dev = dev;
	epl_hsp_v->tx.client.tx_block = true;
	epl_hsp_v->tx.client.tx_tout = timeout_ms;
	epl_hsp_v->tx.client.tx_done = tegra_hsp_tx_empty_notify;

	epl_hsp_v->tx.chan = mbox_request_channel_byname(&epl_hsp_v->tx.client,
							"epl-tx");
	if (IS_ERR(epl_hsp_v->tx.chan)) {
		err = PTR_ERR(epl_hsp_v->tx.chan);
		dev_err(dev, "failed to get tx mailbox: %d\n", err);
		return err;
	}

	return 0;
}

static ssize_t device_file_ioctl(
			struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct epl_error_report_frame error_frame;
	int ret;

	/* Validate input parameters */
	if (!arg)
		return -EINVAL;

	if (copy_from_user(&error_frame, (void __user *)arg,
				 sizeof(error_frame)))
		return -EACCES;

	switch (cmd) {

	case EPL_REPORT_ERROR_CMD:
		ret = epl_report_error(error_frame);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

int epl_get_misc_ec_err_status(struct device *dev, uint8_t err_number, bool *status)
{
	int ret = -EINVAL;

	if (dev && status && (err_number < NUM_SW_GENERIC_ERR)) {
		uint32_t mission_err_status = 0U;
		uint32_t mask = 0U;
		const char *dev_str;

		if (miscerr_cfg[err_number].dev_configured == NULL || isAddrMappOk == false)
			return -ENODEV;

		/* Validate mission error status register mapping */
		if (!mission_err_status_va)
			return -ENODEV;

		dev_str = dev_driver_string(dev);

		if (strcmp(dev_str, miscerr_cfg[err_number].dev_configured) != 0)
			return -EACCES;

		mask = (1U << ((error_index_offset + err_number) % 32U));
		mission_err_status = readl(mission_err_status_va);

		if ((mission_err_status & mask) != 0U)
			*status = false;
		else
			*status = true;

		ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(epl_get_misc_ec_err_status);

int epl_report_misc_ec_error(struct device *dev, uint8_t err_number,
	uint32_t sw_error_code)
{
	int ret = -EINVAL;
	bool status = false;

	ret = epl_get_misc_ec_err_status(dev, err_number, &status);

	if (ret != 0)
		return ret;

	if (status == false)
		return -EAGAIN;

	/* Validate register mappings before writing */
	if (!miscerr_cfg[err_number].err_code_va || !miscerr_cfg[err_number].err_assert_va)
		return -ENODEV;

	/* Updating error code */
	writel(sw_error_code, miscerr_cfg[err_number].err_code_va);

	/* triggering SW generic error */
	writel(0x1U, miscerr_cfg[err_number].err_assert_va);

	return 0;
}
EXPORT_SYMBOL_GPL(epl_report_misc_ec_error);

int epl_report_error(struct epl_error_report_frame error_report)
{
	int ret = -EINVAL;
	uint64_t current_timestamp_64;
	uint64_t reported_timestamp_64;

	/* Validate input parameters */
	if (epl_hsp_v == NULL || hs_state != HANDSHAKE_DONE)
		return -ENODEV;

	/* Validate HSP channel */
	if (!epl_hsp_v->tx.chan)
		return -ENODEV;

	/* Plausibility check for timestamp - only if timestamp is not zero */
	if (error_report.timestamp != 0) {
		/* Get current timestamp (32-bit LSB) and convert to 64-bit for calculations */
		current_timestamp_64 = (uint64_t)epl_get_current_timestamp();
		reported_timestamp_64 = (uint64_t)error_report.timestamp;

		/* Check for timestamp overflow */
		/* If current timestamp is less than reported timestamp, assume overflow occurred */
		if (current_timestamp_64 < reported_timestamp_64)
			current_timestamp_64 += TIMESTAMP_CNT_PERIOD;

		/* Validate timestamp range - reject if difference is more than ~90ms */
		/* Convert 90ms to counter ticks based on current resolution */
		uint64_t valid_range_ticks = TIMESTAMP_VALID_RANGE / timestamp_resolution_ns;

		if ((current_timestamp_64 - reported_timestamp_64) > valid_range_ticks) {
			dev_warn(&epl_hsp_v->dev, "epl_report_error: Invalid timestamp - difference %llu ticks (%llu ns) exceeds valid range (%llu ticks)\n",
				current_timestamp_64 - reported_timestamp_64,
				epl_ticks_to_ns(current_timestamp_64 - reported_timestamp_64),
				valid_range_ticks);
			return -EINVAL;
		}
	}

	ret = mbox_send_message(epl_hsp_v->tx.chan, (void *)&error_report);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(epl_report_error);

static int epl_client_fsi_pm_notify(u32 state)
{
	int ret;
	u32 pdata[4];

	/* Validate state parameter */
	if (state > EPS_DOS_UNKNOWN)
		return -EINVAL;

	pdata[0] = PM_STATE_UNI_CODE;
	pdata[1] = state;
	pdata[2] = state;
	pdata[3] = PM_STATE_UNI_CODE;

	if (hs_state == HANDSHAKE_DONE && epl_hsp_v && epl_hsp_v->tx.chan)
		ret = mbox_send_message(epl_hsp_v->tx.chan, (void *) pdata);
	else
		ret = -ENODEV;

	return ret < 0 ? ret : 0;
}

static int epl_client_fsi_handshake(void *arg)
{
	uint8_t count = 0;

	if (epl_hsp_v && epl_hsp_v->tx.chan) {
		int ret;
		const uint32_t handshake_data[] = {0x45504C48, 0x414E4453, 0x48414B45,
			0x44415441};

		do {
			ret = mbox_send_message(epl_hsp_v->tx.chan, (void *) handshake_data);

			if (ret < 0) {
				hs_state = HANDSHAKE_FAILED;
				count++;
			} else {
				hs_state = HANDSHAKE_DONE;
				break;
			}
		} while (count < handshake_retry_count);
	} else {
		hs_state = HANDSHAKE_FAILED;
		dev_warn(&pdev_local->dev, "epl_client: handshake failed - no valid HSP channel\n");
	}

	if (hs_state == HANDSHAKE_FAILED)
		dev_warn(&pdev_local->dev, "epl_client: handshake with FSI failed\n");
	else
		dev_info(&pdev_local->dev, "epl_client: handshake done with FSI, try %u\n", count);

	return 0;
}

static int __maybe_unused epl_client_suspend(struct device *dev)
{
	int ret = 0;

	dev_dbg(dev, "tegra-epl: suspend called\n");

	if (enable_deinit_notify) {
		ret = epl_client_fsi_pm_notify(EPS_DOS_SUSPEND);
		if (ret < 0)
			dev_warn(dev, "tegra-epl: suspend notification failed: %d\n", ret);
	}
	hs_state = HANDSHAKE_PENDING;

	return ret;
}

static int __maybe_unused epl_client_resume(struct device *dev)
{
	int ret;

	dev_dbg(dev, "tegra-epl: resume called\n");

	ret = epl_client_fsi_handshake(NULL);
	if (ret < 0) {
		dev_warn(dev, "tegra-epl: handshake failed during resume: %d\n", ret);
		return ret;
	}

	/* Only send PM notify if handshake was successful */
	if (hs_state == HANDSHAKE_DONE) {
		ret = epl_client_fsi_pm_notify(EPS_DOS_RESUME);
		if (ret < 0)
			dev_warn(dev, "tegra-epl: resume notification failed: %d\n", ret);
	} else {
		dev_warn(dev, "tegra-epl: skipping resume notification - handshake not successful\n");
	}

	return ret;
}
static SIMPLE_DEV_PM_OPS(epl_client_pm, epl_client_suspend, epl_client_resume);

static const struct of_device_id epl_client_dt_match[] = {
	{ .compatible = "nvidia,tegra234-epl-client"},
	{ .compatible = "nvidia,tegra264-epl-client"},
	{}
};

MODULE_DEVICE_TABLE(of, epl_client_dt_match);

/* File operations */
static const struct file_operations epl_driver_fops = {
	.owner   = THIS_MODULE,
	.unlocked_ioctl   = device_file_ioctl,
};

static int epl_register_device(void)
{
	int result = 0;
	struct class *dev_class;

	result = register_chrdev(0, device_name, &epl_driver_fops);
	if (result < 0) {
		pr_err("%s> register_chrdev code = %i\n", device_name, result);
		return result;
	}
	device_file_major_number = result;

	dev_class = class_create(device_name);
	if (dev_class == NULL) {
		pr_err("%s> Could not create class for device\n", device_name);
		goto class_fail;
	}

	if ((device_create(dev_class, NULL,
		MKDEV(device_file_major_number, 0),
			 NULL, device_name)) == NULL) {
		pr_err("%s> Could not create device node\n", device_name);
		goto device_failure;
	}
	return 0;

device_failure:
	class_destroy(dev_class);
class_fail:
	unregister_chrdev(device_file_major_number, device_name);
	return -1;
}

static void epl_unregister_device(void)
{
	if (device_file_major_number != 0)
		unregister_chrdev(device_file_major_number, device_name);
}

static int epl_client_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	const struct device_node *np = dev->of_node;
	int iterator = 0;
	char name[32] = "client-misc-sw-generic-err";
	bool is_misc_ec_mapped = false;

	hs_state = HANDSHAKE_PENDING;

	ret = epl_register_device();
	if (ret < 0) {
		dev_err(dev, "Failed to register device: %d\n", ret);
		return ret;
	}

	ret = tegra_hsp_mb_init(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize HSP mailbox: %d\n", ret);
		epl_unregister_device();
		return ret;
	}

	pdev_local = pdev;

	for (iterator = 0; iterator < NUM_SW_GENERIC_ERR; iterator++) {
		name[26] = (char)(iterator+48U);
		name[27] = '\0';
		if (of_property_read_string(np, name, &miscerr_cfg[iterator].dev_configured) == 0) {
			dev_info(dev, "Misc Sw Generic Err #%d configured to client %s\n",
					iterator, miscerr_cfg[iterator].dev_configured);

			/* Mapping registers to process address space */
			miscerr_cfg[iterator].err_code_va =
				devm_platform_ioremap_resource(pdev, (iterator * 2));
			miscerr_cfg[iterator].err_assert_va =
				devm_platform_ioremap_resource(pdev, (iterator * 2) + 1);

			if (IS_ERR(miscerr_cfg[iterator].err_code_va) ||
					IS_ERR(miscerr_cfg[iterator].err_assert_va)) {
				isAddrMappOk = false;
				ret = -1;
				dev_err(&pdev->dev, "error in mapping misc err register for err #%d\n",
						iterator);
			} else {
				is_misc_ec_mapped = true;
			}
		} else {
			dev_info(dev, "Misc Sw Generic Err %d not configured for any client\n",
				 iterator);
		}
	}

	if (of_property_read_bool(np, "enable-deinit-notify"))
		enable_deinit_notify = true;

	if (of_property_read_u32(np, "handshake-retry-count", &handshake_retry_count) < 0) {
		handshake_retry_count = default_handshake_retry_count;
	}

	dev_info(dev, "handshake-retry-count %u\n", handshake_retry_count);

	if (of_device_is_compatible(np, "nvidia,tegra234-epl-client")) {
		error_index_offset = 24;
		timestamp_resolution_ns = TEGRA234_TIMESTAMP_RESOLUTION_NS;
	} else if (of_device_is_compatible(np, "nvidia,tegra264-epl-client")) {
		error_index_offset = 3;
		timestamp_resolution_ns = TEGRA264_TIMESTAMP_RESOLUTION_NS;
	} else {
		dev_err(dev, "tegra-epl: valid dt compatible string not found\n");
		ret = -1;
	}

	if (is_misc_ec_mapped == true) {
		mission_err_status_va = devm_platform_ioremap_resource(pdev, NUM_SW_GENERIC_ERR * 2);
		if (IS_ERR(mission_err_status_va)) {
			isAddrMappOk = false;
			dev_err(&pdev->dev, "error in mapping mission error status register\n");
			return PTR_ERR(mission_err_status_va);
		}
	}

	if (ret == 0) {
		ret = epl_client_fsi_handshake(NULL);
		if (ret < 0) {
			dev_warn(dev, "tegra-epl: handshake failed during probe: %d\n", ret);
			return ret;
		}

		/* Only send PM notify if handshake was successful */
		if (hs_state == HANDSHAKE_DONE) {
			ret = epl_client_fsi_pm_notify(EPS_DOS_INIT);
			if (ret < 0)
				dev_warn(dev, "tegra-epl: init notification failed: %d\n", ret);
		} else {
			dev_warn(dev, "tegra-epl: skipping init notification - handshake not successful\n");
		}
	}

	return ret;
}

static void epl_client_shutdown(struct platform_device *pdev)
{
	int ret;

	dev_dbg(&pdev->dev, "tegra-epl: shutdown called\n");

	if (enable_deinit_notify) {
		ret = epl_client_fsi_pm_notify(EPS_DOS_DEINIT);
		if (ret < 0)
			dev_err(&pdev->dev, "Unable to send notification to fsi: %d\n", ret);
	}

	hs_state = HANDSHAKE_PENDING;

	epl_unregister_device();
}

static int epl_client_remove(struct platform_device *pdev)
{
	epl_unregister_device();
	return 0;
}

static struct platform_driver epl_client = {
	.driver         = {
	.name   = "epl_client",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(epl_client_dt_match),
		.pm = pm_ptr(&epl_client_pm),
	},
	.probe		= epl_client_probe,
	.shutdown	= epl_client_shutdown,
	.remove		= epl_client_remove,
};

module_platform_driver(epl_client);

MODULE_DESCRIPTION("tegra: Error Propagation Library driver");
MODULE_AUTHOR("Prashant Shaw <pshaw@nvidia.com>");
MODULE_LICENSE("GPL v2");
