// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

/*
 * This driver is used to configure the eARC audio interface on the EP92HX
 * audio codec through its SA IF microcontroller.
 *
 * It uses regmap I2C to communicate with the EP92HX hardware and configure
 * the registers for eARC Rx and Tx use cases along with CEC enablement.
 *
 * Provides an ioctl interface to the user space to configure the eARC audio
 * interface and also notifies the user space about the eARC Rx and Tx HPD
 * events through uevents.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/regmap.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/dev_printk.h>
#include <linux/version.h>

/* Define flags based on kernel version */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0) && \
	LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
#define NV_I2C_DRIVER_STRUCT_HAS_PROBE_NEW
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
#define NV_CLASS_CREATE_HAS_NO_OWNER_ARG
#endif

#include "ep92hx.h"
#include "ep92hx_isp.h"

/* SA_IF Register Defaults */
static const struct reg_default ep_earc_defaults[] = {
	{ REG_VENDOR_ID_0, EP92HX_VENDOR_ID_0 },
	{ REG_VENDOR_ID_1, EP92HX_VENDOR_ID_1 },
	{ REG_DEVICE_ID_0, EP92HX_DEVICE_ID_0 },
	{ REG_DEVICE_ID_1, EP92HX_DEVICE_ID_1 },
	{ REG_VERSION, EP92HX_VERSION },
	{ REG_YEAR, EP92HX_YEAR },
	{ REG_MONTH, EP92HX_MONTH },
	{ REG_DATE, EP92HX_DATE },
	{ REG_GEN_INFO_0, 0x00 },
	{ REG_GEN_INFO_1, 0x00 },
	{ REG_GEN_INFO_2, 0x00 },
	{ REG_GEN_INFO_3, 0x00 },
	{ REG_GEN_INFO_4, 0x00 },
	{ REG_GEN_INFO_5, 0x00 },
	{ REG_GEN_INFO_6, 0x00 },
	{ REG_ISP_MODE, 0x00 },
	{ REG_CTRL_0, 0x20 },
	{ REG_CTRL_1, 0x10 },
	{ REG_CTRL_2, 0x00 },
	{ REG_CTRL_3, 0x10 },
	{ REG_CTRL_4, 0x02 },
	{ REG_CEC_EVT_CODE, 0x00 },
	{ REG_CEC_EVT_PARAM1, 0x00 },
	{ REG_CEC_EVT_PARAM2, 0x00 },
	{ REG_CEC_EVT_PARAM3, 0x00 },
	{ REG_CEC_EVT_PARAM4, 0x00 },
	{ REG_SYSTEM_STATUS_0, 0x00 },
	{ REG_SYSTEM_STATUS_1, 0x00 },
	{ REG_AUDIO_STATUS, 0x00 },
	{ REG_CHANNEL_STATUS_0, 0x00 },
	{ REG_CHANNEL_STATUS_1, 0x00 },
	{ REG_CHANNEL_STATUS_2, 0x00 },
	{ REG_CHANNEL_STATUS_3, 0x00 },
	{ REG_CHANNEL_STATUS_4, 0x00 },
	{ REG_AUD_INFO_FRAME_0, 0x00 },
	{ REG_AUD_INFO_FRAME_1, 0x00 },
	{ REG_AUD_INFO_FRAME_2, 0x00 },
	{ REG_AUD_INFO_FRAME_3, 0x00 },
	{ REG_AUD_INFO_FRAME_4, 0x00 },
	{ REG_AUD_INFO_FRAME_5, 0x00 },
	{ REG_TX_PKT_CTRL_0, 0x00 },
	{ REG_TX_PKT_CTRL_1, 0x00 },
	{ REG_TX_PKT_CTRL_2, 0x00 },
	{ REG_TX_PKT_CTRL_3, 0x00 },
	{ REG_TX_PKT_CTRL_4, 0x00 },
	{ REG_TX_PKT_CTRL_5, 0x00 },
	{ REG_TX_PKT_CTRL_6, 0x00 },
	{ REG_TX_PKT_CTRL_7, 0x00 },
	{ REG_TX_PKT_CTRL_8, 0x00 },
	{ REG_TX_PKT_CTRL_9, 0x00 },
	{ REG_TX_PKT_CTRL_10, 0x00 },
	{ REG_TX_PKT_CTRL_11, 0x00 },
	{ REG_TX_PKT_CTRL_12, 0x00 },
	{ REG_TX_PKT_CTRL_13, 0x00 },
	{ REG_TX_PKT_CTRL_14, 0x00 },
	{ REG_SPDIF_CS_0, 0x00 },
	{ REG_SPDIF_CS_1, 0x00 },
	{ REG_SPDIF_CS_2, 0x00 },
	{ REG_SPDIF_CS_3, 0x00 },
	{ REG_SPDIF_CS_4, 0x00 },
	{ REG_EARC_LATENCY, 0x00 },
	{ REG_EARC_LATENCY_REQ, 0x00 },
	{ REG_AUDIO_CAP, 0x00 },
};

/* SA_IF Register Volatile and Writeable */
static bool ep_earc_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_VERSION:
	case REG_GEN_INFO_0:
	case REG_GEN_INFO_1:
	case REG_SYSTEM_STATUS_0:
	case REG_AUDIO_STATUS:
	case REG_CHANNEL_STATUS_0:
	case REG_CHANNEL_STATUS_1:
	case REG_CHANNEL_STATUS_2:
	case REG_CHANNEL_STATUS_3:
	case REG_CHANNEL_STATUS_4:
	case REG_AUD_INFO_FRAME_0:
	case REG_AUD_INFO_FRAME_1:
	case REG_AUD_INFO_FRAME_2:
	case REG_AUD_INFO_FRAME_3:
	case REG_AUD_INFO_FRAME_4:
	case REG_AUD_INFO_FRAME_5:
	case REG_EARC_LATENCY:
		return true;
	default:
		return (reg >= REG_AUDIO_CAP && reg <= REG_MAX);
	}
};

static bool ep_earc_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_ISP_MODE:
	case REG_CTRL_0:
	case REG_CTRL_1:
	case REG_CTRL_2:
	case REG_CTRL_3:
	case REG_CTRL_4:
	case REG_TX_PKT_CTRL_0:
	case REG_TX_PKT_CTRL_1:
	case REG_TX_PKT_CTRL_2:
	case REG_TX_PKT_CTRL_3:
	case REG_TX_PKT_CTRL_4:
	case REG_TX_PKT_CTRL_5:
	case REG_TX_PKT_CTRL_6:
	case REG_TX_PKT_CTRL_7:
	case REG_TX_PKT_CTRL_8:
	case REG_TX_PKT_CTRL_9:
	case REG_TX_PKT_CTRL_10:
	case REG_TX_PKT_CTRL_11:
	case REG_TX_PKT_CTRL_12:
	case REG_TX_PKT_CTRL_13:
	case REG_TX_PKT_CTRL_14:
	case REG_SPDIF_CS_0:
	case REG_SPDIF_CS_1:
	case REG_SPDIF_CS_2:
	case REG_SPDIF_CS_3:
	case REG_SPDIF_CS_4:
	case REG_EARC_LATENCY_REQ:
		return true;
	default:
		return false;
	}
};

/* eARC driver data structure */
struct earc_driver_data {
	struct i2c_client *client;
	struct i2c_client *isp_client;
	struct task_struct *base_polling_task;
	struct task_struct *earc_tx_polling_task;
	struct task_struct *earc_rx_polling_task;
	struct mutex rx_lock;			/* Protects Rx operations and registers */
	struct mutex tx_lock;			/* Protects Tx operations and registers */
	struct mutex suspend_lock;		/* Protects suspend state */
	struct regmap *regmap;
	int gen_info_0;					/* GEN_INFO_0 value */
	int gen_info_1;					/* GEN_INFO_1 value */
	int system_status_0;			/* SYSTEM_STATUS_0 value */

	uint8_t earc_rx_val;			/* EARC RX ON value */
	uint8_t earc_rx_hotplug;		/* Rx hotplug used for eARC Rx HPD event */
	uint8_t earc_tx_val;			/* EARC TX ON value */
	uint8_t earc_tx_hotplug;		/* Tx hotplug used for eARC Tx HPD event */
	uint8_t spdif_enable;			/* SPDIF output enable/disable */
	uint8_t ado_chf_val;			/* Audio chf change. Used in eARC Rx */
	uint8_t earc_cap_chf_val;		/* eARC capability chf. Used in eARC Tx */
	uint8_t mclk_ok_val;			/* Specifies MCLK output OK or not */

	struct earc_tx_params tx_params;/* eARC Tx params */
	struct earc_sink_cap sink_cap;	/* eARC Tx sink audio capabilities */
	struct earc_rx_params rx_params;/* eARC Rx params */
	struct earc_rx_params last_rx_params; /* Cached last sent RX params */

	wait_queue_head_t earc_tx_wq;	/* Wait queue for eARC Tx HPD event */
	wait_queue_head_t earc_rx_wq;	/* Wait queue for eARC Rx HPD event */
	bool last_earc_tx_val;			/* Last known eARC Tx ON status */
	bool last_arc_tx_val;			/* Last known ARC Tx ON status */
	bool last_earc_rx_val;			/* Last known eARC Rx ON status */
	bool last_arc_rx_val;			/* Last known ARC Rx ON status */
	bool suspended;					/* Track if device is suspended */
};

/* IOCTL variables and functions */
static struct cdev earc_audio_cdev;
static struct class *earc_audio_class;
static struct device *earc_audio_device;
static dev_t dev_num;

/* Forward declarations */
static void process_earc_tx(struct earc_driver_data *earc_data);
static void process_earc_rx(struct earc_driver_data *earc_data);
static void process_ado_chf(struct earc_driver_data *earc_data);
static void process_earc_cap_chf(struct earc_driver_data *earc_data);
static void process_mclk_ok(struct earc_driver_data *earc_data);

/* Helper functions to convert enum values to strings */
static const char *audio_format_to_string(enum AudioFormat fmt)
{
	switch (fmt) {
	case PCM_2CH:
		return "PCM_2CH";
	case PCM_MULTI_CH:
		return "PCM_MULTI_CH";
	case DOLBY_DIGITAL:
		return "DOLBY_DIGITAL";
	case DTS:
		return "DTS";
	case DOLBY_DIGITAL_PLUS:
		return "DOLBY_DIGITAL_PLUS";
	case DOLBY_TRUE_HD:
		return "DOLBY_TRUE_HD";
	case DTS_MA:
		return "DTS_MA";
	case DOLBY_ATMOS:
		return "DOLBY_ATMOS";
	case DTS_X:
		return "DTS_X";
	default:
		return "UNKNOWN_FORMAT";
	}
}

static const char *audio_input_type_to_string(enum AudioInputType inp_type)
{
	switch (inp_type) {
	case SPDIF:
		return "SPDIF";
	case I2S:
		return "I2S";
	case DSD:
		return "DSD";
	case HBR:
		return "HBR";
	default:
		return "UNKNOWN_IP_TYPE";
	}
}

static const char *compression_layout_to_string(enum CompressionLayout layout)
{
	switch (layout) {
	case COMPRESS_LAYOUT_A:
		return "COMPRESS_LAYOUT_A";
	case COMPRESS_LAYOUT_B:
		return "COMPRESS_LAYOUT_B";
	default:
		return "PCM";
	}
}

/**
 * earc_is_suspended - Check if the device is currently suspended
 * @earc_data: Driver private data
 *
 * Returns: true if suspended, false otherwise
 */
static bool earc_is_suspended(struct earc_driver_data *earc_data)
{
	bool suspended;

	if (!earc_data)
		return true;

	mutex_lock(&earc_data->suspend_lock);
	suspended = earc_data->suspended;
	mutex_unlock(&earc_data->suspend_lock);

	return suspended;
}

/**
 * read_i2c_reg - Read a register value via I2C
 * @earc_data: Driver private data
 * @reg: Register address to read
 * @val: Output parameter for register value
 *
 * Returns: 0 on success, negative error code on failure
 */
static int read_i2c_reg(struct earc_driver_data *earc_data, uint8_t reg)
{
	int ret;
	unsigned int val;

	ret = regmap_read(earc_data->regmap, reg, &val);
	if (ret < 0) {
		dev_err_ratelimited(&earc_data->client->dev,
			"regmap read failed for 0x%02x: %d\n", reg, ret);
		return ret;
	}
	return val;
}

/**
 * read_and_validate_i2c_reg - Read a register value and validate it
 * @earc_data: Driver private data
 * @reg: Register address to read
 *
 * Returns: Register value on success, negative error code on failure
 */
static inline int read_and_validate_i2c_reg(struct earc_driver_data *earc_data,
	uint8_t reg)
{
	int val = read_i2c_reg(earc_data, reg);

	if (val < 0)
		dev_err_ratelimited(&earc_data->client->dev,
			"unable to read register 0x%x\n", reg);

	return val;
}

/**
 * write_i2c_reg - Write a register value via I2C
 * @earc_data: Driver private data
 * @reg: Register address to write
 * @val: Value to write
 *
 * Returns: 0 on success, negative error code on failure
 */
static inline int write_i2c_reg(struct earc_driver_data *earc_data, uint8_t reg,
	uint8_t val)
{
	int ret = 0;

	ret = regmap_write(earc_data->regmap, reg, val);
	if (ret < 0) {
		dev_err_ratelimited(&earc_data->client->dev,
			"regmap write failed for 0x%02x: %d\n", reg, ret);
	}
	dev_dbg(&earc_data->client->dev,
		"written register 0x%x with value 0x%x\n", reg, val);
	return ret;
}

/**
 * update_i2c_reg - Update a register value via I2C
 * @earc_data: Driver private data
 * @reg: Register address to update
 * @value: Value to update
 * @mask: Mask to apply to the register
 *
 * Returns: 0 on success, negative error code on failure
 */
static inline int update_i2c_reg(struct earc_driver_data *earc_data,
	uint8_t reg, uint8_t value, uint8_t mask)
{
	int ret;

	ret = regmap_update_bits(earc_data->regmap, reg, mask, value);
	if (ret < 0) {
		dev_err(&earc_data->client->dev, "update register 0x%x failed\n", reg);
		return ret;
	}
	dev_info(&earc_data->client->dev,
		"updated register 0x%x mask 0x%x with value 0x%x\n", reg, mask, value);
	return 0;
}

/**
 * set_cec_disable - Set CEC disable
 * @dev: eARC device pointer to be obtained from i2c client by the caller
 * @disable: Boolean value to enable/disable CEC
 *
 * Returns: 0 on success, negative error code on failure
 */
int set_cec_disable(struct device *dev, bool disable)
{
	struct earc_driver_data *earc_data = dev_get_drvdata(dev);
	uint8_t value = disable ? BIT_CEC_DISABLE : 0;

	if (!earc_data) {
		dev_err(dev, "Failed to get earc_driver_data\n");
		return -ENODEV;
	}
	return update_i2c_reg(earc_data, REG_CTRL_0, value, BIT_CEC_DISABLE);
}
EXPORT_SYMBOL(set_cec_disable);

/**
 * get_cec_disable - Get CEC disable status
 * @dev: eARC device pointer to be obtained from i2c client by the caller
 * @is_disable: pointer to store the CEC disable status
 *
 * Returns: 0 on success, negative error code on failure
 */
int get_cec_disable(struct device *dev, bool *is_disable)
{
	struct earc_driver_data *earc_data = dev_get_drvdata(dev);
	uint8_t ctrl_0_val = 0;

	if (!earc_data) {
		dev_err(dev, "Failed to get earc_driver_data\n");
		return -ENODEV;
	}

	if (!is_disable) {
		dev_err(dev, "Invalid is_disable pointer\n");
		return -EINVAL;
	}

	ctrl_0_val = read_and_validate_i2c_reg(earc_data, REG_CTRL_0);
	if (ctrl_0_val < 0) {
		dev_err(dev, "Failed to read CTRL_0 register\n");
		return -EINVAL;
	}

	*is_disable = (ctrl_0_val & BIT_CEC_DISABLE) ? true : false;
	return 0;
}
EXPORT_SYMBOL(get_cec_disable);

/**
 * enter_chip_stop_mode - Enter chip stop mode for power management
 * @earc_data: Driver private data
 *
 * Sets CEC disable bit to 0 and power down bit to 1 in REG_CTRL_0
 * to put the chip in stop mode for power saving during suspend.
 *
 * Returns: 0 on success, negative error code on failure
 */
static int enter_chip_stop_mode(struct earc_driver_data *earc_data)
{
	int ret = 0;

	if (!earc_data) {
		pr_err("earc_data is NULL\n");
		return -ENODEV;
	}

	/* Enable CEC */
	ret = update_i2c_reg(earc_data, REG_CTRL_0, 0, BIT_CEC_DISABLE);
	if (ret < 0) {
		dev_err(&earc_data->client->dev, "Failed to clear CEC disable bit\n");
		return ret;
	}

	/* Set power down bit to enter stop mode */
	ret = update_i2c_reg(earc_data, REG_CTRL_0, BIT_POWER_DOWN, BIT_POWER_DOWN);
	if (ret < 0) {
		dev_err(&earc_data->client->dev, "Failed to set power down bit\n");
		return ret;
	}

	dev_info(&earc_data->client->dev, "EP92HX Chip set to stop mode\n");
	return 0;
}

/**
 * exit_chip_stop_mode - Exit chip from stop mode for power management
 * @earc_data: Driver private data
 *
 * Clears power down bit to 0 and sets CEC disable bit to 1 in REG_CTRL_0
 * to exit the chip from stop mode and resume normal operation after suspend.
 *
 * Returns: 0 on success, negative error code on failure
 */
static int exit_chip_stop_mode(struct earc_driver_data *earc_data)
{
	int ret = 0;

	if (!earc_data) {
		pr_err("earc_data is NULL\n");
		return -ENODEV;
	}

	/* Clear power down bit to exit stop mode */
	ret = update_i2c_reg(earc_data, REG_CTRL_0, 0, BIT_POWER_DOWN);
	if (ret < 0) {
		dev_err(&earc_data->client->dev, "Failed to clear power down bit\n");
		return ret;
	}

	/* Disable CEC */
	ret = update_i2c_reg(earc_data, REG_CTRL_0, BIT_CEC_DISABLE,
		BIT_CEC_DISABLE);
	if (ret < 0) {
		dev_err(&earc_data->client->dev, "Failed to set CEC disable bit\n");
		return ret;
	}

	dev_info(&earc_data->client->dev, "EP92HX Chip exited from stop mode\n");
	return 0;
}

/**
 * update_earc_status - Unified status updater with error-aware design
 * @earc_data: Driver private data
 * @reg_value: Register value to update
 * @mask: Mask to apply to the register
 * @current_val: Current value of the register
 * @status_name: Name of the status to update
 *
 * Returns: True if status changed, false otherwise
 */
static bool update_earc_status(struct earc_driver_data *earc_data,
	uint8_t reg_value, uint8_t mask, uint8_t *current_val,
	const char *status_name)
{
	uint8_t new_val;

	if (!earc_data || !current_val || !status_name)
		return false;

	new_val = reg_value & mask;
	if (new_val != *current_val) {
		*current_val = new_val;
		dev_info(&earc_data->client->dev, "%s: %s\n",
				status_name, new_val ? "ON" : "OFF");
		return true;  /* Indicate change */
	}
	return false;
}

static void process_mclk_ok(struct earc_driver_data *earc_data)
{
	/* TODO: Check if audio input is to be muted based on MCLK OK status */
	if (earc_data->mclk_ok_val) {
		if (update_i2c_reg(earc_data, REG_CTRL_1, 0, BIT_AIN_MUTE) < 0) {
			dev_err(&earc_data->client->dev, "failed to unmute audio input\n");
			return;
		}
	}
}

static uint32_t get_sample_rate_from_as_sel(uint8_t sample_rate_sel)
{
	switch (sample_rate_sel) {
	case AS_SAMPLE_RATE_SEL_32000:
		return SAMPLE_RATE_32000;
	case AS_SAMPLE_RATE_SEL_44100:
		return SAMPLE_RATE_44100;
	case AS_SAMPLE_RATE_SEL_48000:
		return SAMPLE_RATE_48000;
	case AS_SAMPLE_RATE_SEL_88200:
		return SAMPLE_RATE_88200;
	case AS_SAMPLE_RATE_SEL_96000:
		return SAMPLE_RATE_96000;
	case AS_SAMPLE_RATE_SEL_176400:
		return SAMPLE_RATE_176400;
	case AS_SAMPLE_RATE_SEL_192000:
		return SAMPLE_RATE_192000;
	case AS_SAMPLE_RATE_SEL_768000:
		return SAMPLE_RATE_768000;
	default:
		return SAMPLE_RATE_48000;
	}
}

/**
 * send_hpd_uevent - Send HPD event to userspace
 * @dev: Device pointer
 * @event_type: Event type (eARC/ARC Rx/Tx)
 * @connected: Boolean value to indicate if connected or disconnected
 *
 * Returns: 0 on success, negative error code on failure
 */
static int send_hpd_uevent(struct device *dev, const char *event_type,
	bool connected)
{
	char *envp[3];
	int ret = 0;

	envp[0] = kasprintf(GFP_KERNEL, "HPD_EVENT=%s", event_type);
	if (!envp[0]) {
		dev_err(dev, "Failed to allocate HPD_EVENT string\n");
		return -ENOMEM;
	}

	envp[1] = kasprintf(GFP_KERNEL, "CONNECTED=%s", connected ? "1" : "0");
	if (!envp[1]) {
		dev_err(dev, "Failed to allocate CONNECTED string\n");
		kfree(envp[0]);
		return -ENOMEM;
	}

	envp[2] = NULL;

	ret = kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
	if (ret < 0)
		dev_err(dev, "Failed to send uevent: %d\n", ret);
	else
		dev_info(dev, "%s: eARC/ARC %s HPD %s\n",
			__func__, event_type, connected ? "connected" : "disconnected");

	kfree(envp[0]);
	kfree(envp[1]);

	return ret;
}

/**
 * send_ado_chf_uevent - Send eARC Rx ADO chf event to userspace
 * @dev: Device pointer
 * @event_type: Event type (true/false)
 *
 * Returns: 0 on success, negative error code on failure
 */
static int send_ado_chf_uevent(struct device *dev, const char *event_type)
{
	char *envp[2];
	int ret = 0;

	envp[0] = kasprintf(GFP_KERNEL, "ADO_CHF_UPDATE=%s", event_type);
	if (!envp[0]) {
		dev_err(dev, "Failed to allocate ADO_CHF_UPDATE string\n");
		return -ENOMEM;
	}

	envp[1] = NULL;

	ret = kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
	if (ret < 0)
		dev_err(dev, "Failed to send ADO CHF uevent: %d\n", ret);
	else
		dev_info(dev, "%s: eARC RX ADO chf updated\n", __func__);

	kfree(envp[0]);

	return ret;
}

static uint8_t get_num_chs_from_ch_alloc(uint8_t ch_alloc)
{
	switch (ch_alloc) {
	case 0x01:
	case 0x02:
	case 0x04:
		return NUM_CHANNELS_3;
	case 0x03:
	case 0x05:
	case 0x06:
	case 0x08:
		return NUM_CHANNELS_4;
	case 0x07:
	case 0x09:
	case 0x0A:
	case 0x0C:
		return NUM_CHANNELS_5;
	case 0x0B:
	case 0x0D:
	case 0x0E:
	case 0x10:
		return NUM_CHANNELS_6;
	case 0x0F:
	case 0x11:
	case 0x12:
		return NUM_CHANNELS_7;
	case 0x13:
		return NUM_CHANNELS_8;
	default:
		return NUM_CHANNELS_2;
	}
}

/* Process the audio format changes for eARC RX usecase */
static void process_ado_chf(struct earc_driver_data *earc_data)
{
	bool is_compressed = false, std_ado = false, hbr_audio = false,
		pcm_layout = false;
	int audio_status = 0, system_status_0 = 0;
	int aud_info_frame_4 = 0, ch_status_0 = 0;
	uint8_t num_chs = 0, ch_alloc = 0, as_sample_rate_sel = 0, sample_size = 0;
	uint32_t sample_rate = 0;
	bool changed = false;

	if (!earc_data->ado_chf_val)
		return;

	/* TODO: Check if Mute audio is needed here as ado chf is changing */

	/* Read all required registers at once */
	audio_status = read_and_validate_i2c_reg(earc_data, REG_AUDIO_STATUS);
	system_status_0 = read_and_validate_i2c_reg(earc_data, REG_SYSTEM_STATUS_0);
	aud_info_frame_4 = read_and_validate_i2c_reg(earc_data,
		REG_AUD_INFO_FRAME_4);
	ch_status_0 = read_and_validate_i2c_reg(earc_data, REG_CHANNEL_STATUS_0);

	if (audio_status < 0 || system_status_0 < 0 || aud_info_frame_4 < 0 ||
		ch_status_0 < 0)
		return;

	dev_dbg(&earc_data->client->dev, "audio_status: 0x%02x\n", audio_status);
	dev_dbg(&earc_data->client->dev, "system_status: 0x%02x\n", system_status_0);
	dev_dbg(&earc_data->client->dev, "info frame 4: 0x%02x\n", aud_info_frame_4);
	dev_dbg(&earc_data->client->dev, "ch_status_0: 0x%02x\n", ch_status_0);

	/* Process channel information */
	ch_alloc = aud_info_frame_4 & BIT_CH_ALLOC_SEL;
	dev_dbg(&earc_data->client->dev, "ch_alloc: %u\n", ch_alloc);
	num_chs = get_num_chs_from_ch_alloc(ch_alloc);
	dev_info(&earc_data->client->dev, "Number of channels: %u\n", num_chs);

	/* Extract sample rate */
	as_sample_rate_sel = audio_status & BIT_AS_SAMPLE_RATE_SEL;
	dev_dbg(&earc_data->client->dev, "as_sample_rate_sel: %u\n",
		as_sample_rate_sel);
	sample_rate = get_sample_rate_from_as_sel(as_sample_rate_sel);
	dev_info(&earc_data->client->dev, "sample_rate: %u\n", sample_rate);

	/* Currently EP hardcodes sample size to 32 bits in eARC Rx use case */
	sample_size = SAMPLE_SIZE_32;
	dev_info(&earc_data->client->dev, "sample_size: %u\n", sample_size);

	/* Extract audio format */
	std_ado = audio_status & BIT_STD_ADO;
	hbr_audio = audio_status & BIT_HBR_ADO;
	dev_dbg(&earc_data->client->dev, "std_ado: %u, hbr_audio: %u\n",
		std_ado, hbr_audio);

	if (std_ado) {
		is_compressed = ch_status_0 & BIT_COMPRESSED_AUDIO;
		dev_dbg(&earc_data->client->dev, "is_compressed: %u\n", is_compressed);
		if (!is_compressed) {
			pcm_layout = system_status_0 & BIT_RX_LAYOUT;
			dev_info(&earc_data->client->dev, "PCM: %s, sample rate: %u Hz\n",
					 pcm_layout ? "multi-channel" : "2-channel", sample_rate);
			earc_data->rx_params.fmt = pcm_layout ? PCM_MULTI_CH : PCM_2CH;
		} else {
			dev_info(&earc_data->client->dev,
				"Compressed Audio detected. Dolby Digital 5.1 or DTS etc.\n");
			/*
			 * TODO: Can be DTS or Dolby Digital+ as well.
			 * Check how to identify the exact format. Hardcode to DD for now.
			 */
			earc_data->rx_params.fmt = DOLBY_DIGITAL;
		}
	} else if (hbr_audio) {
		dev_info(&earc_data->client->dev,
			"HBR Audio detected. Dolby TrueHD or DTS-MA etc.\n");
		earc_data->rx_params.fmt = DOLBY_TRUE_HD;
	} else {
		dev_err(&earc_data->client->dev, "DSD or no audio format detected\n");
	}

	earc_data->rx_params.sample_rate = sample_rate;
	earc_data->rx_params.num_chs = num_chs;
	earc_data->rx_params.ch_alloc = ch_alloc;
	earc_data->rx_params.sample_size = sample_size;

	/* Only send uevent if any rx_params field changed */
	if ((earc_data->rx_params.fmt !=
			earc_data->last_rx_params.fmt) ||
		(earc_data->rx_params.sample_rate !=
			earc_data->last_rx_params.sample_rate) ||
		(earc_data->rx_params.num_chs !=
			earc_data->last_rx_params.num_chs) ||
		(earc_data->rx_params.ch_alloc !=
			earc_data->last_rx_params.ch_alloc) ||
		(earc_data->rx_params.sample_size !=
			earc_data->last_rx_params.sample_size))
		changed = true;

	if (changed) {
		dev_info(&earc_data->client->dev, "ADO CHF changed, sending uevent\n");
		if (send_ado_chf_uevent(&earc_data->client->dev, "true") < 0)
			dev_err(&earc_data->client->dev, "Failed to send ADO CHF uevent\n");

		earc_data->last_rx_params = earc_data->rx_params;
	}
}

static void process_earc_rx(struct earc_driver_data *earc_data)
{
	if (earc_data->earc_rx_val) {
		if (!earc_data->last_earc_rx_val) {
			if (send_hpd_uevent(&earc_data->client->dev, "earc_rx", true) < 0)
				dev_err(&earc_data->client->dev,
					"Failed to send eARC Rx HPD event\n");

			earc_data->last_earc_rx_val = true;
			earc_data->last_arc_rx_val = false;
		}
	}
}

/**
 * monitor_earc_rx_regs - Polling thread to monitor the eARC Rx registers
 * Started from process_earc_rx_hpd() on HPD connection event and stopped
 * on HPD disconnect event
 * @data: Driver private data
 *
 * Returns: 0 on success, negative error code on failure
 */
static int monitor_earc_rx_regs(void *data)
{
	struct earc_driver_data *earc_data = data;

	while (!kthread_should_stop()) {
		/* Check if device is suspended */
		if (earc_is_suspended(earc_data)) {
			msleep(POLLING_INTERVAL_MS);
			continue;
		}

		mutex_lock(&earc_data->rx_lock);

		earc_data->system_status_0 = read_and_validate_i2c_reg(earc_data,
			REG_SYSTEM_STATUS_0);

		if (likely(earc_data->gen_info_0 >= 0)) {
			if (update_earc_status(earc_data, earc_data->gen_info_0,
					BIT_EARC_RX_ON, &earc_data->earc_rx_val, "eARC RX ON")) {
				wake_up(&earc_data->earc_rx_wq);
				process_earc_rx(earc_data);
			}
		}

		if (likely(earc_data->gen_info_1 >= 0)) {
			if (update_earc_status(earc_data, earc_data->gen_info_1,
					BIT_ADO_CHF, &earc_data->ado_chf_val, "ADO chf")) {
				process_ado_chf(earc_data);
			}
		}

		if (likely(earc_data->system_status_0 >= 0)) {
			if (update_earc_status(earc_data, earc_data->system_status_0,
					BIT_MCLK_OK, &earc_data->mclk_ok_val,
					"MCLK status")) {
				process_mclk_ok(earc_data);
			}
		}

		mutex_unlock(&earc_data->rx_lock);
		msleep(POLLING_INTERVAL_MS);
	}

	return 0;
}

/* Finalize HPD event implementation */
/*
 * For the eARC OUT Connector (eARC Tx usecase),
 * the HPD is controlled by EP92H1T chip only (not SOC controlled).
 * So SOC can polling eARC_TX_ON - 0x08[5] directly without check HPD.
 * HPD should be always high once EP92H1T chip ready.
 * So, nothing to be done from SOC side.
 */

/*
 * For the HDMI OUT Connector (eARC Rx usecase), HPD goes to EP92H1T & SOC both.
 * So, SOC can check "HPD0_N_GP72" or Tx_Hot_Plug 0x08[2] for the HPD status.
 * Check for Tx_Hot_Plug register bit for now or
 * will have to make the pinctrl API call to get GPIO status if needed.
 */
static void process_earc_rx_hpd(struct earc_driver_data *earc_data)
{
	if (earc_data->earc_rx_hotplug) {
		if (!earc_data->earc_rx_polling_task) {
			struct task_struct *new_task =
				kthread_run(monitor_earc_rx_regs, earc_data, "earc-rx-monitor");
			if (IS_ERR(new_task)) {
				dev_err(&earc_data->client->dev,
					"failed to start eARC RX monitor thread\n");
			} else {
				mutex_lock(&earc_data->rx_lock);
				earc_data->earc_rx_polling_task = new_task;
				mutex_unlock(&earc_data->rx_lock);
			}
		}

		/* Wait up to 600ms for eARC Rx to become active, if not it is ARC Rx */
		if (!wait_event_timeout(earc_data->earc_rx_wq,
			READ_ONCE(earc_data->earc_rx_val),
			msecs_to_jiffies(HPD_DELAY_MS))) {

			dev_info(&earc_data->client->dev,
				"eARC Rx is not active, send ARC Rx HPD event\n");
			mutex_lock(&earc_data->rx_lock);
			earc_data->last_arc_rx_val = true;
			earc_data->last_earc_rx_val = false;
			mutex_unlock(&earc_data->rx_lock);
			if (send_hpd_uevent(&earc_data->client->dev, "arc_rx", true) < 0)
				dev_err(&earc_data->client->dev,
					"Failed to send ARC Rx HPD event\n");
		}
	} else {
		struct task_struct *task_to_stop = NULL;

		mutex_lock(&earc_data->rx_lock);
		task_to_stop = earc_data->earc_rx_polling_task;
		earc_data->earc_rx_polling_task = NULL;
		mutex_unlock(&earc_data->rx_lock);

		if (task_to_stop) {
			int ret = kthread_stop(task_to_stop);

			if (ret != 0 && ret != -EINTR)
				dev_err(&earc_data->client->dev,
					"Failed to stop RX polling thread: %d\n", ret);
		}

		/* Send disconnect events based on last known state */
		if (earc_data->last_earc_rx_val)
			if (send_hpd_uevent(&earc_data->client->dev, "earc_rx", false) < 0)
				dev_err(&earc_data->client->dev,
					"Failed to send eARC Rx disconnect event\n");

		if (earc_data->last_arc_rx_val)
			if (send_hpd_uevent(&earc_data->client->dev, "arc_rx", false) < 0)
				dev_err(&earc_data->client->dev,
					"Failed to send ARC Rx disconnect event\n");

		/* Reset the eARC Rx related register values to avoid false events */
		mutex_lock(&earc_data->rx_lock);
		earc_data->last_arc_rx_val = false;
		earc_data->last_earc_rx_val = false;
		earc_data->earc_rx_val = 0;
		earc_data->ado_chf_val = 0;
		earc_data->mclk_ok_val = 0;
		mutex_unlock(&earc_data->rx_lock);
	}
}

/**
 * configure_audio_info - Configure the audio info control registers for eARC Tx
 * @earc_data: Driver private data
 * @fmt: Audio format enum
 * @inp_type: Audio input type enum
 * @cmp_layout: Compression layout enum
 *
 * Returns: 0 on success, negative error code on failure
 */
static int configure_audio_info(struct earc_driver_data *earc_data,
		enum AudioFormat fmt, enum AudioInputType inp_type,
		enum CompressionLayout cmp_layout)
{
	int layout = LAYOUT_0, a_format = PCM_2CH, a_in = I2S;
	int ret = 0;

	switch (fmt) {
	case PCM_2CH:
		if (inp_type != SPDIF && inp_type != I2S)
			return -EINVAL;
		a_in = inp_type;
		break;
	case PCM_MULTI_CH:
		a_in = I2S;
		layout = LAYOUT_1;
		a_format = PCM_MULTI_CH;
		break;
	case DOLBY_DIGITAL:
	case DTS:
	case DOLBY_DIGITAL_PLUS:
		if (inp_type != SPDIF && inp_type != I2S)
			return -EINVAL;
		a_in = inp_type;
		a_format = cmp_layout;
		break;
	case DOLBY_TRUE_HD:
	case DTS_MA:
	case DOLBY_ATMOS:
	case DTS_X:
		a_in = HBR;
		a_format = COMPRESS_LAYOUT_A;
		layout = LAYOUT_1;
		break;
	default:
		return -EINVAL;
	}

	/* Update input audio type */
	ret = update_i2c_reg(earc_data, REG_CTRL_2, a_in, A_IN_MASK);
	if (ret < 0)
		return ret;

	/* Update input audio layout */
	ret = update_i2c_reg(earc_data, REG_CTRL_2, (layout & 0x01) << 2,
		BIT_TX_LAYOUT);
	if (ret < 0)
		return ret;

	/* Update output audio format */
	ret = update_i2c_reg(earc_data, REG_CTRL_2, (a_format & 0x03) << 4,
		A_FORMAT_MASK);
	if (ret < 0)
		return ret;

	return 0;
}

static uint8_t get_ch_alloc_from_num_chs(uint8_t num_chs)
{
	switch (num_chs) {
	case 3:
		return 0x01;
	case 4:
		return 0x03;
	case 5:
		return 0x07;
	case 6:
		return 0x0D;
	case 7:
		return 0x11;
	case 8:
		return 0x13;
	default:
		return 0x00;
	}
}

static uint8_t get_cs_sample_rate_sel(uint32_t sample_rate)
{
	switch (sample_rate) {
	case SAMPLE_RATE_44100:
		return CS_SAMPLE_RATE_SEL_44100;
	case SAMPLE_RATE_48000:
		return CS_SAMPLE_RATE_SEL_48000;
	case SAMPLE_RATE_32000:
		return CS_SAMPLE_RATE_SEL_32000;
	case SAMPLE_RATE_22050:
		return CS_SAMPLE_RATE_SEL_22050;
	case SAMPLE_RATE_24000:
		return CS_SAMPLE_RATE_SEL_24000;
	case SAMPLE_RATE_88200:
		return CS_SAMPLE_RATE_SEL_88200;
	case SAMPLE_RATE_96000:
		return CS_SAMPLE_RATE_SEL_96000;
	case SAMPLE_RATE_176400:
		return CS_SAMPLE_RATE_SEL_176400;
	case SAMPLE_RATE_192000:
		return CS_SAMPLE_RATE_SEL_192000;
	default:
		return CS_SAMPLE_RATE_SEL_48000;
	}
}

/**
 * configure_sample_size - Configure sample size in packet control register
 * @earc_data: Driver private data
 * @sample_size: Sample size in bits
 *
 * Returns: 0 on success, negative error code on failure
 */
static int configure_sample_size(struct earc_driver_data *earc_data,
	uint8_t sample_size)
{
	uint8_t sample_len_sel = 0;
	int ret = 0;

	dev_dbg(&earc_data->client->dev, "sample_size: %u\n", sample_size);

	/* Configure sample length selection based on sample size */
	switch (sample_size) {
	case SAMPLE_SIZE_32:
		ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_11, BIT_MAX_LEN,
				BIT_MAX_LEN);
		if (ret < 0)
			return ret;
		sample_len_sel = 0x0;
		break;
	case SAMPLE_SIZE_24:
		ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_11, BIT_MAX_LEN,
				BIT_MAX_LEN);
		if (ret < 0)
			return ret;
		sample_len_sel = 0x5;
		break;
	case SAMPLE_SIZE_16:
		sample_len_sel = 0x1;
		break;
	default:
		dev_warn(&earc_data->client->dev, "Unsupported sample size: %u\n",
			sample_size);
		sample_len_sel = 0x0;
		break;
	}

	ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_11, (sample_len_sel << 1),
			BIT_SAMP_LEN_SEL);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * configure_pcm_audio_packet - Configure packet control registers for PCM audio
 * @earc_data: Driver private data
 * @fmt: Audio format (PCM_2CH or PCM_MULTI_CH)
 * @sample_rate: Sample rate
 * @sample_size: Sample size
 * @num_chs: Number of channels
 *
 * Returns: 0 on success, negative error code on failure
 */
static int configure_pcm_audio_packet(struct earc_driver_data *earc_data,
	enum AudioFormat fmt, uint32_t sample_rate, uint8_t sample_size,
	uint8_t num_chs)
{
	uint8_t cs_sample_rate_sel = 0;
	int ret = 0;

	/* Configure sample rate selection */
	cs_sample_rate_sel = get_cs_sample_rate_sel(sample_rate);
	dev_dbg(&earc_data->client->dev, "cs_sample_rate_sel: 0x%02x\n",
		cs_sample_rate_sel);

	ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_10, cs_sample_rate_sel,
		BIT_CS_SAMPLE_RATE_SEL);
	if (ret < 0)
		return ret;

	/* Configure audio type based on format */
	if (fmt == PCM_MULTI_CH) {
		/* Multi-channel PCM: Set audio type to 5 */
		ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_7, BIT_AUDIO_TYPE_5,
			BIT_AUDIO_TYPE_5);
		if (ret < 0)
			return ret;

		/* Set channel allocation based on number of channels */
		ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_0,
			get_ch_alloc_from_num_chs(num_chs), BIT_CH_ALLOC_SEL);
		if (ret < 0)
			return ret;

		/* Set multi-channel selection bit */
		ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_12, BIT_CS_MULTI_CH_SEL,
			BIT_CS_MULTI_CH_SEL);
		if (ret < 0)
			return ret;
	}

	/* Configure sample size */
	ret = configure_sample_size(earc_data, sample_size);
	if (ret < 0)
		return ret;

	return 0;
}


/**
 * configure_compressed_audio_packet - Configure pkt ctrl regs for compressed audio
 * @earc_data: Driver private data
 * @sample_rate: Sample rate
 *
 * Returns: 0 on success, negative error code on failure
 */
static int configure_compressed_audio_packet(struct earc_driver_data *earc_data,
	uint32_t sample_rate)
{
	uint8_t cs_sample_rate_sel = 0;
	int ret = 0;

	/* Set audio type to 1 for compressed audio */
	ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_7, BIT_AUDIO_TYPE_1,
		BIT_AUDIO_TYPE_1);
	if (ret < 0)
		return ret;

	/* Configure sample rate selection */
	cs_sample_rate_sel = get_cs_sample_rate_sel(sample_rate);
	dev_dbg(&earc_data->client->dev, "cs_sample_rate_sel: 0x%02x\n",
		cs_sample_rate_sel);

	ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_10, cs_sample_rate_sel,
		BIT_CS_SAMPLE_RATE_SEL);
	if (ret < 0)
		return ret;

	/* Set max_len bit to 1, sample length to 0 (unindicated) */
	ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_11, BIT_MAX_LEN,
		BIT_MAX_LEN);
	if (ret < 0)
		return ret;

	ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_11, 0, BIT_SAMP_LEN_SEL);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * configure_hbr_audio_packet - Configure packet control registers for HBR audio
 * @earc_data: Driver private data
 *
 * Returns: 0 on success, negative error code on failure
 */
static int configure_hbr_audio_packet(struct earc_driver_data *earc_data)
{
	int ret = 0;

	/* Set audio type to 1 for HBR audio */
	ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_7, BIT_AUDIO_TYPE_1,
		BIT_AUDIO_TYPE_1);
	if (ret < 0)
		return ret;

	/* Set sample rate to 0x09 for HBR audio (TODO: verify this value) */
	ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_10, 0x09,
		BIT_CS_SAMPLE_RATE_SEL);
	if (ret < 0)
		return ret;

	/* Set max len to 1 (and keep other bits as 0 for unindicated) */
	ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_11, BIT_MAX_LEN,
		BIT_MAX_LEN);
	if (ret < 0)
		return ret;

	ret = update_i2c_reg(earc_data, REG_TX_PKT_CTRL_11, 0, BIT_SAMP_LEN_SEL);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * configure_tx_packet_ctrl - Configure the audio packet ctrl regs for eARC Tx
 * @earc_data: Driver private data
 * @fmt: Audio format enum
 * @sample_rate: Sample rate
 * @sample_size: Sample size
 * @num_chs: Number of channels
 *
 * Returns: 0 on success, negative error code on failure
 */
static int configure_tx_packet_ctrl(struct earc_driver_data *earc_data,
		enum AudioFormat fmt, uint32_t sample_rate, uint8_t sample_size,
		uint8_t num_chs)
{
	int ret = 0;

	/*
	 * eARC Channel status 136-191 bits correspond to Tx Packet Ctrl 0-6
	 * registers, Refer to CTG-861G, 6.6 Audio InfoFrame.
	 * eARC Channel Status 0-71 bits correspond to Tx Packet Ctrl 7-13
	 * registers, Refer to HDMI 2.1 spec, 9.5.2.2.
	 */

	switch (fmt) {
	case PCM_2CH:
	case PCM_MULTI_CH:
		dev_dbg(&earc_data->client->dev, "%s PCM, num_chs: %u\n",
			(fmt == PCM_MULTI_CH) ? "Multi-channel" : "2-channel", num_chs);
		ret = configure_pcm_audio_packet(earc_data, fmt, sample_rate,
			sample_size, num_chs);
		break;

	case DOLBY_DIGITAL:
	case DTS:
	case DOLBY_DIGITAL_PLUS:
		dev_dbg(&earc_data->client->dev, "Compressed Audio\n");
		ret = configure_compressed_audio_packet(earc_data, sample_rate);
		break;

	case DOLBY_TRUE_HD:
	case DTS_MA:
	case DOLBY_ATMOS:
	case DTS_X:
		dev_dbg(&earc_data->client->dev, "HBR Audio\n");
		ret = configure_hbr_audio_packet(earc_data);
		break;

	default:
		dev_err(&earc_data->client->dev, "Unsupported audio format: %d\n", fmt);
		return -EINVAL;
	}

	return ret;
}

/**
 * configure_audio_output_settings - Prerequisites before enabling eARC Tx audio
 * Invoked from ioctl handler to configure audio output settings
 * @earc_data: Driver private data
 *
 * Returns: 0 on success, negative error code on failure
 */
static int configure_audio_output_settings(struct earc_driver_data *earc_data)
{
	int ret = 0;
	enum AudioFormat fmt = earc_data->tx_params.fmt;
	enum AudioInputType inp_type = earc_data->tx_params.inp_type;
	enum CompressionLayout cmp_layout = earc_data->tx_params.cmp_layout;
	uint32_t sample_rate = earc_data->tx_params.sample_rate;
	uint8_t num_chs = earc_data->tx_params.num_chs;
	uint8_t sample_size = earc_data->tx_params.sample_size;

	dev_info(&earc_data->client->dev, "fmt: %s, inp_type: %s, cmp_layout: %s\n",
		audio_format_to_string(fmt), audio_input_type_to_string(inp_type),
		compression_layout_to_string(cmp_layout));
	dev_info(&earc_data->client->dev, "rate: %d, chs: %d, sample_size: %d\n",
		sample_rate, num_chs, sample_size);
	/* Set the eARC Tx Mute bit to mute audio before setting the audio info */
	if (update_i2c_reg(earc_data, REG_CTRL_1, BIT_EARC_TX_MUTE,
			BIT_EARC_TX_MUTE) < 0)
		return -EINVAL;

	/* Add a sleep of 100ms to ensure after muting */
	mutex_unlock(&earc_data->tx_lock);
	msleep(DIFF_OUTPUT_DELAY_MS);
	mutex_lock(&earc_data->tx_lock);

	/* Disable differential output */
	if (update_i2c_reg(earc_data, REG_CTRL_2, 0, BIT_DF_EN) < 0)
		return -EINVAL;

	/* With the info from HAL, configure the eARC Tx Input registers */
	ret = configure_audio_info(earc_data, fmt, inp_type, cmp_layout);
	if (ret < 0)
		return ret;

	/* Set the input sample size bit based on the sample size */
	if (sample_size == SAMPLE_SIZE_32 || sample_size == SAMPLE_SIZE_24)
		ret = update_i2c_reg(earc_data, REG_CTRL_4, BIT_TDM_IN_SS_SEL,
			BIT_TDM_IN_SS_SEL);
	else
		ret = update_i2c_reg(earc_data, REG_CTRL_4, 0, BIT_TDM_IN_SS_SEL);

	if (ret < 0)
		return ret;

	/* Fill in the audio channel status and info (0x31 - 0x40) registers */
	ret = configure_tx_packet_ctrl(earc_data, fmt, sample_rate, sample_size,
		num_chs);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * enable_audio_output - Enables the audio output for eARC Tx
 * Invoked from ioctl handler to enable audio output
 * @earc_data: Driver private data
 *
 * Returns: 0 on success, negative error code on failure
 */
static int enable_audio_output(struct earc_driver_data *earc_data)
{
	/* Enable differential output */
	if (update_i2c_reg(earc_data, REG_CTRL_2, BIT_DF_EN, BIT_DF_EN) < 0)
		return -EINVAL;

	/* Add a sleep of 100ms to ensure before unmuting */
	mutex_unlock(&earc_data->tx_lock);
	msleep(DIFF_OUTPUT_DELAY_MS);
	mutex_lock(&earc_data->tx_lock);

	/* Clear the eARC Tx Mute bit to unmute audio */
	return update_i2c_reg(earc_data, REG_CTRL_1, 0, BIT_EARC_TX_MUTE);
}

/**
 * disable_audio_output - Mutes and disables the audio output for eARC Tx
 * Resets all the info and packet control registers
 * Invoked from ioctl handler to disable audio output
 * @earc_data: Driver private data
 *
 * Returns: 0 on success, negative error code on failure
 */
static int disable_audio_output(struct earc_driver_data *earc_data)
{
	int i;

	/* Disable differential output */
	if (update_i2c_reg(earc_data, REG_CTRL_2, 0, BIT_DF_EN) < 0)
		return -EINVAL;

	/* Set the eARC Tx Mute bit to mute audio */
	if (update_i2c_reg(earc_data, REG_CTRL_1, BIT_EARC_TX_MUTE,
			BIT_EARC_TX_MUTE) < 0)
		return -EINVAL;

	/* Reset the channel status and audio info registers for every playback */
	for (i = REG_TX_PKT_CTRL_0; i <= REG_TX_PKT_CTRL_14; i++)
		write_i2c_reg(earc_data, i, 0);

	/* Reset the sample size to 32 bits */
	update_i2c_reg(earc_data, REG_CTRL_4, BIT_TDM_IN_SS_SEL, BIT_TDM_IN_SS_SEL);

	/* Reset the format, layout, input type for every playback */
	if (update_i2c_reg(earc_data, REG_CTRL_2, 0,
			A_IN_MASK | BIT_TX_LAYOUT | A_FORMAT_MASK) < 0)
		return -EINVAL;

	return 0;
}

/**
 * enable_spdif_output - Enables the SPDIF output for eARC Tx
 * Invoked from ioctl handler to enable SPDIF output
 * @earc_data: Driver private data
 *
 * Returns: 0 on success, negative error code on failure
 */
static int enable_spdif_output(struct earc_driver_data *earc_data)
{
	int ret = 0;

	if (earc_data->spdif_enable)
		ret = update_i2c_reg(earc_data, REG_CTRL_3, 0, BIT_SPDIF_OUT_OFF);
	else
		ret = update_i2c_reg(earc_data, REG_CTRL_3, BIT_SPDIF_OUT_OFF,
			BIT_SPDIF_OUT_OFF);

	return ret;
}

/**
 * parse_and_copy_earc_capabilities - Parses the eARC Tx sink capabilities
 * and copies them to the driver data structure
 * Invoked from process_earc_cap_chf() to parse and store the eARC Tx sink
 * capabilities
 * @earc_data: Driver private data
 *
 * Returns: 0 on success, negative error code on failure
 */
static int parse_and_copy_earc_capabilities(struct earc_driver_data *earc_data)
{
	int ret = 0;
	uint8_t block_id = 0, cap_addr = REG_AUDIO_CAP, block_size = 0;
	int audio_cap = 0, payload_length = 0, i = 0;
	size_t total_cap_size = 0, buffer_index = 0;

	/* Free the existing audio capabilities data */
	if (earc_data->sink_cap.audio_cap_data) {
		kfree(earc_data->sink_cap.audio_cap_data);
		earc_data->sink_cap.audio_cap_data = NULL;
		earc_data->sink_cap.audio_cap_size = 0;
	}
	/* Allocate memory for the capabilities */
	earc_data->sink_cap.audio_cap_data = kzalloc(256 * sizeof(uint8_t),
		GFP_KERNEL);
	if (!earc_data->sink_cap.audio_cap_data)
		return -ENOMEM;

	/*
	 * Read the eARC capabilities byte by byte from the register
	 * starting from REG_AUDIO_CAP
	 */
	while (1) {
		audio_cap = read_and_validate_i2c_reg(earc_data, cap_addr);
		if (audio_cap < 0) {
			ret = -EINVAL;
			break;
		}
		block_id = (audio_cap & 0x1F);
		earc_data->sink_cap.audio_cap_data[buffer_index++] = block_id;

		if (block_id == 0) /* End marker found, stop parsing */
			break;

		payload_length = read_and_validate_i2c_reg(earc_data, cap_addr + 1);
		if (payload_length < 0) {
			ret = -EINVAL;
			break;
		}
		earc_data->sink_cap.audio_cap_data[buffer_index++] = payload_length;

		/* Copy the payload data to the buffer */
		for (i = 0; i < payload_length; i++) {
			earc_data->sink_cap.audio_cap_data[buffer_index++] =
				read_and_validate_i2c_reg(earc_data, cap_addr + 2 + i);
		}

		block_size = 2 + payload_length;
		total_cap_size += block_size;

		if (cap_addr + block_size < cap_addr) {
			dev_dbg(&earc_data->client->dev, "Address wraparound detected\n");
			kfree(earc_data->sink_cap.audio_cap_data);
			earc_data->sink_cap.audio_cap_data = NULL;
			ret = 0;
			break;
		}

		cap_addr += block_size;
	}

	earc_data->sink_cap.audio_cap_size = total_cap_size;
	dev_dbg(&earc_data->client->dev, "total eARC capabilities size: %zu\n",
		total_cap_size);
	return ret;
}

/* Audio capability for eARC TX use case */
static void process_earc_cap_chf(struct earc_driver_data *earc_data)
{
	int ret = 0;

	if (earc_data->earc_cap_chf_val) {
		/* Read audio capabilities of the connected sink device */
		dev_dbg(&earc_data->client->dev, "Reading eARC capabilities\n");
		ret = parse_and_copy_earc_capabilities(earc_data);
		if (ret)
			dev_err(&earc_data->client->dev, "parse eARC cap failed:%d\n",
				ret);
	}
}

static void process_earc_tx(struct earc_driver_data *earc_data)
{
	if (earc_data->earc_tx_val) {
		if (!earc_data->last_earc_tx_val) {
			if (send_hpd_uevent(&earc_data->client->dev, "earc_tx", true) < 0)
				dev_err(&earc_data->client->dev,
					"Failed to send eARC Tx HPD event\n");

			earc_data->last_earc_tx_val = true;
			earc_data->last_arc_tx_val = false;
		}
	}
}

/**
 * monitor_earc_tx_regs - Polling thread to monitor the eARC Tx registers
 * Started from process_earc_tx_hpd() on HPD connection event and stopped
 * on HPD disconnect event
 * @data: Driver private data
 *
 * Returns: 0 on success, negative error code on failure
 */
static int monitor_earc_tx_regs(void *data)
{
	struct earc_driver_data *earc_data = data;

	while (!kthread_should_stop()) {
		/* Check if device is suspended */
		if (earc_is_suspended(earc_data)) {
			msleep(POLLING_INTERVAL_MS);
			continue;
		}

		mutex_lock(&earc_data->tx_lock);

		earc_data->gen_info_0 = read_and_validate_i2c_reg(earc_data,
			REG_GEN_INFO_0);
		if (likely(earc_data->gen_info_0 >= 0)) {
			if (update_earc_status(earc_data, earc_data->gen_info_0,
				BIT_EARC_TX_ON, &earc_data->earc_tx_val, "eARC TX ON")) {
				wake_up(&earc_data->earc_tx_wq);
				process_earc_tx(earc_data);
			}
		}

		if (likely(earc_data->gen_info_1 >= 0)) {
			if (update_earc_status(earc_data, earc_data->gen_info_1,
				BIT_EARC_CAP_CHF, &earc_data->earc_cap_chf_val,
				"eARC CAP chf")) {
				process_earc_cap_chf(earc_data);
			}
		}

		mutex_unlock(&earc_data->tx_lock);
		msleep(POLLING_INTERVAL_MS);
	}

	return 0;
}

static void process_earc_tx_hpd(struct earc_driver_data *earc_data)
{
	if (earc_data->earc_tx_hotplug) {
		if (!earc_data->earc_tx_polling_task) {
			struct task_struct *new_task =
				kthread_run(monitor_earc_tx_regs, earc_data, "earc-tx-monitor");
			if (IS_ERR(new_task)) {
				dev_err(&earc_data->client->dev,
					"failed to start eARC TX monitor thread\n");
			} else {
				mutex_lock(&earc_data->tx_lock);
				earc_data->earc_tx_polling_task = new_task;
				mutex_unlock(&earc_data->tx_lock);
			}
		}

		/* Wait up to 600ms for eARC to become active */
		if (!wait_event_timeout(earc_data->earc_tx_wq,
			READ_ONCE(earc_data->earc_tx_val),
			msecs_to_jiffies(HPD_DELAY_MS))) {

			dev_info(&earc_data->client->dev,
				"eARC Tx is not active, send ARC Tx HPD event\n");
			mutex_lock(&earc_data->tx_lock);
			earc_data->last_arc_tx_val = true;
			earc_data->last_earc_tx_val = false;
			mutex_unlock(&earc_data->tx_lock);
			update_i2c_reg(earc_data, REG_CTRL_2, BIT_ARC_EARC_SEL,
				BIT_ARC_EARC_SEL);
			if (send_hpd_uevent(&earc_data->client->dev, "arc_tx", true) < 0)
				dev_err(&earc_data->client->dev,
					"Failed to send ARC Tx HPD event\n");
		}
	} else {
		struct task_struct *task_to_stop = NULL;

		mutex_lock(&earc_data->tx_lock);
		task_to_stop = earc_data->earc_tx_polling_task;
		earc_data->earc_tx_polling_task = NULL;
		mutex_unlock(&earc_data->tx_lock);

		if (task_to_stop) {
			int ret = kthread_stop(task_to_stop);

			if (ret != 0 && ret != -EINTR)
				dev_err(&earc_data->client->dev,
					"Failed to stop TX polling thread: %d\n", ret);
		}

		/* Send disconnect events based on last known state */
		if (earc_data->last_earc_tx_val)
			if (send_hpd_uevent(&earc_data->client->dev, "earc_tx", false) < 0)
				dev_err(&earc_data->client->dev,
					"Failed to send eARC Tx disconnect event\n");

		if (earc_data->last_arc_tx_val)
			if (send_hpd_uevent(&earc_data->client->dev, "arc_tx", false) < 0)
				dev_err(&earc_data->client->dev,
					"Failed to send ARC Tx disconnect event\n");

		update_i2c_reg(earc_data, REG_CTRL_2, 0, BIT_ARC_EARC_SEL);
		mutex_lock(&earc_data->tx_lock);
		earc_data->last_arc_tx_val = false;
		earc_data->last_earc_tx_val = false;
		earc_data->earc_tx_val = 0;
		earc_data->earc_cap_chf_val = 0;
		mutex_unlock(&earc_data->tx_lock);
	}
}

/**
 * poll_hpd_regs - Base polling thread for eARC Rx and Tx HPD events
 * Invoked on bootup in probe and keeps running until the driver is removed
 * @data: Driver private data
 *
 * Returns: 0 on success, negative error code on failure
 */
static int poll_hpd_regs(void *data)
{
	struct earc_driver_data *earc_data = data;

	while (!kthread_should_stop()) {
		/* Check if device is suspended */
		if (earc_is_suspended(earc_data)) {
			msleep(POLLING_INTERVAL_MS);
			continue;
		}

		mutex_lock(&earc_data->rx_lock);
		mutex_lock(&earc_data->tx_lock);
		earc_data->gen_info_0 = read_and_validate_i2c_reg(earc_data,
			REG_GEN_INFO_0);
		earc_data->gen_info_1 = read_and_validate_i2c_reg(earc_data,
			REG_GEN_INFO_1);
		mutex_unlock(&earc_data->tx_lock);
		mutex_unlock(&earc_data->rx_lock);

		mutex_lock(&earc_data->rx_lock);
		if (likely(earc_data->gen_info_0 >= 0)) {
			if (update_earc_status(earc_data, earc_data->gen_info_0,
					BIT_RX_HOTPLUG, &earc_data->earc_rx_hotplug,
					"Rx Hot Plug")) {
				mutex_unlock(&earc_data->rx_lock);
				process_earc_rx_hpd(earc_data);
				mutex_lock(&earc_data->rx_lock);
			}
		}
		mutex_unlock(&earc_data->rx_lock);

		mutex_lock(&earc_data->tx_lock);
		if (likely(earc_data->gen_info_0 >= 0)) {
			if (update_earc_status(earc_data, earc_data->gen_info_0,
					BIT_TX_HOTPLUG, &earc_data->earc_tx_hotplug,
					"Tx Hot Plug")) {
				mutex_unlock(&earc_data->tx_lock);
				process_earc_tx_hpd(earc_data);
				mutex_lock(&earc_data->tx_lock);
			}
		}
		mutex_unlock(&earc_data->tx_lock);

		msleep(POLLING_INTERVAL_MS);
	}
	return 0;
}

static int earc_audio_open(struct inode *inode, struct file *file)
{
	struct earc_driver_data *earc_data;

	earc_data = i2c_get_clientdata(to_i2c_client(earc_audio_device->parent));
	if (!earc_data) {
		pr_err("%s: failed to retrieve earc_driver_data\n", __func__);
		return -ENODEV;
	}

	file->private_data = earc_data;
	pr_info("%s: eARC Audio driver opened\n", __func__);
	return 0;
}

static int earc_audio_release(struct inode *inode, struct file *file)
{
	pr_info("%s: eARC Audio driver closed\n", __func__);
	return 0;
}

/**
 * earc_audio_ioctl - Ioctl handler for eARC audio device
 * @file: File pointer
 * @cmd: Command to execute
 * @arg: Argument to the command
 *
 * Returns: 0 on success, negative error code on failure
 */
static long earc_audio_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	struct earc_tx_params tx_params;
	struct earc_driver_data *earc_data = file->private_data;
	struct earc_sink_cap user_cap;
	int ret = 0;
	uint8_t spdif_enable = 0, rx_tdm_enable = 0, tx_tdm_enable = 0;
	size_t to_copy;

	if (!earc_data) {
		pr_err("%s: earc_data is NULL\n", __func__);
		return -ENODEV;
	}

	switch (cmd) {
	case EARC_AUDIO_SET_TX_PARAMS:
		if (!access_ok((void __user *)arg, sizeof(tx_params)))
			return -EFAULT;
		if (copy_from_user(&tx_params, (void __user *)arg, sizeof(tx_params)))
			return -EFAULT;

		/* Check if device is suspended */
		if (earc_is_suspended(earc_data)) {
			dev_err(&earc_data->client->dev, "Device in suspend, can't set\n");
			return -EBUSY;
		}

		mutex_lock(&earc_data->tx_lock);
		earc_data->tx_params = tx_params;

		ret = configure_audio_output_settings(earc_data);
		if (ret) {
			dev_err(&earc_data->client->dev,
				"configure audio settings failed:%d\n", ret);
			mutex_unlock(&earc_data->tx_lock);
			break;
		}

		ret = enable_audio_output(earc_data);
		if (ret)
			dev_err(&earc_data->client->dev,
				"enable audio output failed:%d\n", ret);
		mutex_unlock(&earc_data->tx_lock);
		break;

	case EARC_AUDIO_SET_SPDIF_ENABLE:
		if (!access_ok((void __user *)arg, sizeof(spdif_enable)))
			return -EFAULT;
		if (copy_from_user(&spdif_enable, (void __user *)arg,
			sizeof(spdif_enable)))
			return -EFAULT;

		/* Check if device is suspended */
		if (earc_is_suspended(earc_data)) {
			dev_err(&earc_data->client->dev, "Device in suspend, can't set\n");
			return -EBUSY;
		}

		mutex_lock(&earc_data->tx_lock);
		earc_data->spdif_enable = spdif_enable;

		ret = enable_spdif_output(earc_data);
		if (ret)
			dev_err(&earc_data->client->dev, "enable spdif output failed:%d\n",
				ret);
		mutex_unlock(&earc_data->tx_lock);
		break;

	case EARC_AUDIO_SET_TX_TDM_ENABLE:
		if (!access_ok((void __user *)arg, sizeof(tx_tdm_enable)))
			return -EFAULT;
		if (copy_from_user(&tx_tdm_enable, (void __user *)arg,
			sizeof(tx_tdm_enable)))
			return -EFAULT;

		/* Check if device is suspended */
		if (earc_is_suspended(earc_data)) {
			dev_err(&earc_data->client->dev, "Device in suspend, can't set\n");
			return -EBUSY;
		}

		mutex_lock(&earc_data->tx_lock);
		if (tx_tdm_enable)
			update_i2c_reg(earc_data, REG_CTRL_4, BIT_TDM_IN_EN,
				BIT_TDM_IN_EN);
		else
			update_i2c_reg(earc_data, REG_CTRL_4, 0, BIT_TDM_IN_EN);
		mutex_unlock(&earc_data->tx_lock);
		break;

	case EARC_AUDIO_SET_RX_TDM_ENABLE:
		if (!access_ok((void __user *)arg, sizeof(rx_tdm_enable)))
			return -EFAULT;
		if (copy_from_user(&rx_tdm_enable, (void __user *)arg,
			sizeof(rx_tdm_enable)))
			return -EFAULT;

		/* Check if device is suspended */
		if (earc_is_suspended(earc_data)) {
			dev_err(&earc_data->client->dev, "Device in suspend, can't set\n");
			return -EBUSY;
		}

		mutex_lock(&earc_data->rx_lock);
		if (rx_tdm_enable)
			update_i2c_reg(earc_data, REG_CTRL_4, BIT_TDM_OUT_EN,
				BIT_TDM_OUT_EN);
		else
			update_i2c_reg(earc_data, REG_CTRL_4, 0, BIT_TDM_OUT_EN);
		mutex_unlock(&earc_data->rx_lock);
		break;

	case EARC_AUDIO_GET_SINK_CAP:
		/* Check if device is suspended */
		if (earc_is_suspended(earc_data)) {
			dev_err(&earc_data->client->dev, "Device in suspend, can't get\n");
			return -EBUSY;
		}

		mutex_lock(&earc_data->tx_lock);
		if (!earc_data->sink_cap.audio_cap_data) {
			dev_err(&earc_data->client->dev, "kernel audio cap data is NULL\n");
			mutex_unlock(&earc_data->tx_lock);
			return -EFAULT;
		}

		if (!access_ok((void __user *)arg, sizeof(struct earc_sink_cap))) {
			dev_err(&earc_data->client->dev, "user sink cap pointer invalid\n");
			mutex_unlock(&earc_data->tx_lock);
			return -EFAULT;
		}
		if (copy_from_user(&user_cap, (void __user *)arg,
			sizeof(struct earc_sink_cap))) {
			dev_err(&earc_data->client->dev, "failed to copy user sink cap\n");
			mutex_unlock(&earc_data->tx_lock);
			return -EFAULT;
		}
		to_copy = min(user_cap.audio_cap_size,
			earc_data->sink_cap.audio_cap_size);
		if (!user_cap.audio_cap_data ||
			!access_ok(user_cap.audio_cap_data, to_copy)) {
			dev_err(&earc_data->client->dev, "user audio cap access failed\n");
			mutex_unlock(&earc_data->tx_lock);
			return -EFAULT;
		}
		dev_dbg(&earc_data->client->dev, "sink cap size: %zu\n",
			earc_data->sink_cap.audio_cap_size);

		/* Copy just the audio_cap_size back to user (actual kernel size) */
		if (put_user(earc_data->sink_cap.audio_cap_size,
			&((struct earc_sink_cap __user *)arg)->audio_cap_size)) {
			dev_err(&earc_data->client->dev, "failed to copy audio_cap_size\n");
			mutex_unlock(&earc_data->tx_lock);
			return -EFAULT;
		}
		/* Copy only as much as the user buffer can hold */
		if (copy_to_user(user_cap.audio_cap_data,
			earc_data->sink_cap.audio_cap_data,
			to_copy)) {
			dev_err(&earc_data->client->dev, "failed to copy audio cap data\n");
			mutex_unlock(&earc_data->tx_lock);
			return -EFAULT;
		}
		mutex_unlock(&earc_data->tx_lock);
		break;

	case EARC_AUDIO_STOP_TX:
		/* Check if device is suspended */
		if (earc_is_suspended(earc_data)) {
			dev_err(&earc_data->client->dev, "Device in suspend, can't stop\n");
			return -EBUSY;
		}

		mutex_lock(&earc_data->tx_lock);
		/* Disable audio output for eARC Tx usecase */
		ret = disable_audio_output(earc_data);
		if (ret)
			dev_err(&earc_data->client->dev, "disable audio failed:%d\n", ret);
		mutex_unlock(&earc_data->tx_lock);
		break;

	case EARC_AUDIO_STOP_RX:
		/* TODO: Check if it is really needed or not */
		break;

	case EARC_AUDIO_GET_RX_PARAMS:
		if (!access_ok((void __user *)arg, sizeof(earc_data->rx_params)))
			return -EFAULT;

		mutex_lock(&earc_data->rx_lock);
		if (copy_to_user((void __user *)arg, &earc_data->rx_params,
			sizeof(earc_data->rx_params))) {
			mutex_unlock(&earc_data->rx_lock);
			return -EFAULT;
		}
		mutex_unlock(&earc_data->rx_lock);
		break;

	default:
		return -ENOTTY;
	}
	return ret;
}

static const struct file_operations earc_audio_fops = {
	.owner = THIS_MODULE,
	.open = earc_audio_open,
	.release = earc_audio_release,
	.unlocked_ioctl = earc_audio_ioctl,
#if defined(CONFIG_COMPAT)
	.compat_ioctl = earc_audio_ioctl,
#endif
};

static const struct regmap_config earc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_MAX,
	.cache_type = REGCACHE_FLAT,
	.reg_defaults = ep_earc_defaults,
	.num_reg_defaults = ARRAY_SIZE(ep_earc_defaults),
	.volatile_reg = ep_earc_volatile,
	.writeable_reg = ep_earc_writeable,
	.use_single_read = true,
	.use_single_write = true,
};

static ssize_t rx_hpd_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct earc_driver_data *earc_data = dev_get_drvdata(dev);
	bool rx_hpd;

	mutex_lock(&earc_data->rx_lock);
	rx_hpd = (earc_data->earc_rx_hotplug == 0) ? false : true;
	mutex_unlock(&earc_data->rx_lock);

	return sysfs_emit(buf, "%d\n", rx_hpd);
}
static DEVICE_ATTR_RO(rx_hpd);

static ssize_t tx_hpd_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct earc_driver_data *earc_data = dev_get_drvdata(dev);
	bool tx_hpd;

	mutex_lock(&earc_data->tx_lock);
	tx_hpd = (earc_data->earc_tx_hotplug == 0) ? false : true;
	mutex_unlock(&earc_data->tx_lock);

	return sysfs_emit(buf, "%d\n", tx_hpd);
}
static DEVICE_ATTR_RO(tx_hpd);

static int create_sysfs_entries(struct earc_driver_data *earc_data)
{
	int ret;

	ret = device_create_file(&earc_data->client->dev, &dev_attr_rx_hpd);
	if (ret) {
		dev_err(&earc_data->client->dev, "create rx_hpd sysfs attr failed\n");
		return ret;
	}

	ret = device_create_file(&earc_data->client->dev, &dev_attr_tx_hpd);
	if (ret) {
		dev_err(&earc_data->client->dev, "create tx_hpd sysfs attr failed\n");
		device_remove_file(&earc_data->client->dev, &dev_attr_rx_hpd);
		return ret;
	}

	return 0;
}

static void remove_sysfs_entries(struct earc_driver_data *earc_data)
{
	device_remove_file(&earc_data->client->dev, &dev_attr_rx_hpd);
	device_remove_file(&earc_data->client->dev, &dev_attr_tx_hpd);
}

/**
 * earc_suspend - Suspend callback to stop I2C transactions
 * @dev: Device pointer
 *
 * Stops all polling threads to prevent I2C transactions during suspend
 */
static int earc_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct earc_driver_data *earc_data = i2c_get_clientdata(client);
	struct task_struct *task_to_stop = NULL;
	int ret = 0;

	if (!earc_data) {
		dev_err(dev, "earc_data is NULL in suspend\n");
		return -ENODEV;
	}

	dev_dbg(dev, "Suspending eARC driver\n");

	mutex_lock(&earc_data->suspend_lock);
	earc_data->suspended = true;
	mutex_unlock(&earc_data->suspend_lock);

	/* Send disconnect events and reset HPD state for suspend */
	mutex_lock(&earc_data->rx_lock);
	if (earc_data->last_earc_rx_val) {
		if (send_hpd_uevent(&earc_data->client->dev, "earc_rx", false) < 0)
			dev_err(dev,
				"Failed to send eARC Rx disconnect event during suspend\n");

		earc_data->last_earc_rx_val = false;
	}

	if (earc_data->last_arc_rx_val) {
		if (send_hpd_uevent(&earc_data->client->dev, "arc_rx", false) < 0)
			dev_err(dev,
				"Failed to send ARC Rx disconnect event during suspend\n");

		earc_data->last_arc_rx_val = false;
	}

	earc_data->earc_rx_hotplug = 0;
	mutex_unlock(&earc_data->rx_lock);

	mutex_lock(&earc_data->tx_lock);
	if (earc_data->last_earc_tx_val) {
		if (send_hpd_uevent(&earc_data->client->dev, "earc_tx", false) < 0)
			dev_err(dev,
				"Failed to send eARC Tx disconnect event during suspend\n");

		earc_data->last_earc_tx_val = false;
	}

	if (earc_data->last_arc_tx_val) {
		if (send_hpd_uevent(&earc_data->client->dev, "arc_tx", false) < 0)
			dev_err(dev,
				"Failed to send ARC Tx disconnect event during suspend\n");

		earc_data->last_arc_tx_val = false;
	}
	earc_data->earc_tx_hotplug = 0;
	mutex_unlock(&earc_data->tx_lock);

	/* Stop base polling task */
	if (earc_data->base_polling_task) {
		dev_dbg(dev, "Stopping base polling task\n");
		ret = kthread_stop(earc_data->base_polling_task);
		if (ret != 0 && ret != -EINTR)
			dev_err(dev, "Failed to stop base polling task: %d\n", ret);
		earc_data->base_polling_task = NULL;
	}

	/* Stop RX polling task safely */
	mutex_lock(&earc_data->rx_lock);
	task_to_stop = earc_data->earc_rx_polling_task;
	earc_data->earc_rx_polling_task = NULL;
	mutex_unlock(&earc_data->rx_lock);

	if (task_to_stop) {
		dev_dbg(dev, "Stopping RX polling task\n");
		ret = kthread_stop(task_to_stop);
		if (ret != 0 && ret != -EINTR)
			dev_err(dev, "Failed to stop RX polling task: %d\n", ret);
	}

	/* Stop TX polling task safely */
	mutex_lock(&earc_data->tx_lock);
	task_to_stop = earc_data->earc_tx_polling_task;
	earc_data->earc_tx_polling_task = NULL;
	mutex_unlock(&earc_data->tx_lock);

	if (task_to_stop) {
		dev_dbg(dev, "Stopping TX polling task\n");
		ret = kthread_stop(task_to_stop);
		if (ret != 0 && ret != -EINTR)
			dev_err(dev, "Failed to stop TX polling task: %d\n", ret);
	}

	/* Set chip to stop mode for power saving */
	ret = enter_chip_stop_mode(earc_data);
	if (ret < 0)
		dev_err(dev, "Failed to set chip to stop mode: %d\n", ret);

	dev_info(dev, "eARC driver suspended\n");
	return 0;
}

/**
 * earc_resume - Resume callback to restart I2C transactions
 * @dev: Device pointer
 *
 * Restarts the base polling thread to resume I2C transactions
 */
static int earc_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct earc_driver_data *earc_data = i2c_get_clientdata(client);
	int ret = 0;

	if (!earc_data) {
		dev_err(dev, "earc_data is NULL in resume\n");
		return -ENODEV;
	}

	dev_dbg(dev, "Resuming eARC driver\n");

	/* Exit chip from stop mode first */
	ret = exit_chip_stop_mode(earc_data);
	if (ret < 0) {
		dev_err(dev, "Failed to exit chip from stop mode: %d\n", ret);
		return ret;
	}

	mutex_lock(&earc_data->suspend_lock);
	earc_data->suspended = false;
	mutex_unlock(&earc_data->suspend_lock);

	/* Reset HPD state variables to ensure proper event detection after resume */
	mutex_lock(&earc_data->rx_lock);
	earc_data->earc_rx_hotplug = 0;
	mutex_unlock(&earc_data->rx_lock);

	mutex_lock(&earc_data->tx_lock);
	earc_data->earc_tx_hotplug = 0;
	mutex_unlock(&earc_data->tx_lock);

	/* Restart base polling task if not already running */
	if (!earc_data->base_polling_task) {
		earc_data->base_polling_task = kthread_run(poll_hpd_regs, earc_data,
			"earc-poll");
		if (IS_ERR(earc_data->base_polling_task)) {
			dev_err(dev, "failed to restart polling thread\n");
			ret = PTR_ERR(earc_data->base_polling_task);
			earc_data->base_polling_task = NULL;
		} else {
			dev_dbg(dev, "Base polling task restarted\n");
		}
	} else {
		dev_dbg(dev, "Base polling task already running, skipping restart\n");
	}

	dev_info(dev, "eARC driver resumed\n");
	return ret;
}

static int ep92hx_configure_registers(struct earc_driver_data *earc_data)
{
	/* Enable TDM output and input on boot up, disable when not needed */
	if (update_i2c_reg(earc_data, REG_CTRL_4, BIT_TDM_OUT_EN,
			BIT_TDM_OUT_EN) < 0)
		return -EINVAL;

	if (update_i2c_reg(earc_data, REG_CTRL_4, BIT_TDM_IN_EN,
			BIT_TDM_IN_EN) < 0)
		return -EINVAL;

	dev_dbg(&earc_data->client->dev, "EP92HX initial reg config done\n");
	return 0;
}

/**
 * ep92hx_check_update_fw - Check and update EP92Hx firmware if needed
 * @earc_data: Pointer to the eARC driver data
 *
 * This function performs the following operations:
 * 1. Reads the current firmware version from the device
 * 2. Retrieves the latest available firmware version
 * 3. Compares versions and updates firmware if they differ
 * 4. Verifies the firmware update was successful
 *
 * Firmware Update Process:
 * - Enter ISP (In-System Programming) mode
 * - Flash the new firmware to the device
 * - Exit ISP mode and return to normal operation
 * - Read back the updated version for verification
 *
 * Returns: 0 on success (either firmware already up-to-date or updated successfully),
 *          negative error code on failure
 */
static int ep92hx_check_update_fw(struct earc_driver_data *earc_data)
{
	uint8_t latest_version = 0;
	uint8_t cur_version = 0;
	int ret = 0;

	/*
	 * Step 1: Read the current firmware version from the device.
	 * If reading fails, assume version 0 (invalid/corrupted firmware)
	 * and proceed with firmware update to recover the device.
	 */
	ret = read_and_validate_i2c_reg(earc_data, REG_VERSION);
	if (ret < 0) {
		dev_warn(&earc_data->client->dev, "Failed to read current version, going to reset firmware\n");
		cur_version = 0;
	} else {
		cur_version = ret;
		dev_info(&earc_data->client->dev, "EP92Hx current FW version: 0x%02x\n", cur_version);
	}

	/*
	 * Step 2: Get the latest firmware version from the firmware file.
	 * This reads the version information from the binary without loading
	 * the entire firmware into memory.
	 */
	ret = ep_get_latest_fw_version(&latest_version);
	if (ret != 0) {
		dev_err(&earc_data->client->dev, "Failed to get latest firmware version\n");
		return ret;
	}
	dev_dbg(&earc_data->client->dev, "EP92Hx latest FW version: 0x%02x\n", latest_version);

	/*
	 * Step 3: Compare versions and perform firmware update if necessary.
	 * Skip update if current version matches the latest version.
	 */
	if (cur_version != latest_version) {
		/*
		 * Step 3a: Enter ISP mode to enable firmware programming.
		 * This creates a new I2C client for ISP communication and
		 * switches the device into programming mode.
		 */
		ret = ep_enter_isp_mode(earc_data->client, &earc_data->isp_client,
					earc_data->regmap);
		if (ret != 0)
			return ret;

		/*
		 * Step 3b: Perform the actual firmware update.
		 * This writes the new firmware to the device's flash memory.
		 */
		ret = ep_update_fw(earc_data->isp_client);
		if (ret != 0) {
			dev_err(&earc_data->client->dev, "Failed to update firmware\n");
			/*
			 * On failure, attempt to exit ISP mode to restore device
			 * to normal operation. Cast to void since we're already
			 * handling an error and will return the original error.
			 */
			(void)ep_exit_isp_mode(earc_data->isp_client);
			return ret;
		}

		/*
		 * Step 3c: Exit ISP mode to return device to normal operation.
		 * This triggers the device to load and run the new firmware.
		 */
		ret = ep_exit_isp_mode(earc_data->isp_client);
		if (ret != 0) {
			dev_err(&earc_data->client->dev, "Failed to exit ISP mode\n");
			return ret;
		}

		/*
		 * Step 4: Verify the firmware update was successful by reading
		 * the version register again. The device should now report the
		 * new firmware version.
		 */
		ret = read_and_validate_i2c_reg(earc_data, REG_VERSION);
		if (ret < 0) {
			dev_err(&earc_data->client->dev, "Failed to read updated FW version\n");
			return ret;
		}
		cur_version = ret;
		dev_info(&earc_data->client->dev,
			 "EP92Hx updated FW version: 0x%02x\n", cur_version);
	}

	return ret;
}

static int earc_i2c_probe(struct i2c_client *client)
{
	struct earc_driver_data *earc_data;
	int ret = 0;

	earc_data = devm_kzalloc(&client->dev, sizeof(*earc_data), GFP_KERNEL);
	if (!earc_data)
		return -ENOMEM;

	earc_data->client = client;
	i2c_set_clientdata(client, earc_data);

	earc_data->regmap = devm_regmap_init_i2c(client, &earc_regmap_config);
	if (IS_ERR(earc_data->regmap)) {
		dev_err(&client->dev, "failed to initialize regmap\n");
		return PTR_ERR(earc_data->regmap);
	}

	ret = ep92hx_check_update_fw(earc_data);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to check or update firmware\n");
		return ret;
	}

	/* Configure required registers with init values during boot up */
	ret = ep92hx_configure_registers(earc_data);
	if (ret) {
		dev_err(&client->dev, "Failed to configure initial registers\n");
		return ret;
	}

	/* Initialize all mutexes */
	mutex_init(&earc_data->rx_lock);
	mutex_init(&earc_data->tx_lock);
	mutex_init(&earc_data->suspend_lock);

	/* Initialize wait queues used for HPD handling */
	init_waitqueue_head(&earc_data->earc_tx_wq);
	init_waitqueue_head(&earc_data->earc_rx_wq);

	/* Initialize suspend state */
	earc_data->suspended = false;

	ret = alloc_chrdev_region(&dev_num, 0, 1, EARC_IOCTL_DEVICE_NAME);
	if (ret) {
		dev_err(&client->dev, "failed to allocate device number\n");
		goto err_chrdev_alloc;
	}

	cdev_init(&earc_audio_cdev, &earc_audio_fops);
	ret = cdev_add(&earc_audio_cdev, dev_num, 1);
	if (ret) {
		dev_err(&client->dev, "failed to add cdev\n");
		goto err_cdev_add;
	}

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG)
	earc_audio_class = class_create("earc_audio_class");
#else
	earc_audio_class = class_create(THIS_MODULE, "earc_audio_class");
#endif
	if (IS_ERR(earc_audio_class)) {
		pr_err("failed to create device class.\n");
		ret = PTR_ERR(earc_audio_class);
		goto err_class_create;
	}

	/* Create the device with the i2c_client's device as the parent */
	earc_audio_device = device_create(earc_audio_class, &client->dev, dev_num,
		NULL, "earc_audio_dev");
	if (IS_ERR(earc_audio_device)) {
		pr_err("failed to create device node.\n");
		ret = PTR_ERR(earc_audio_device);
		goto err_device_create;
	}

	ret = create_sysfs_entries(earc_data);
	if (ret) {
		dev_err(&client->dev, "failed to create sysfs entries\n");
		goto err_sysfs_create;
	}

	/*
	 * Create a base polling thread to monitor eARC Rx and Tx HPD events
	 * NOTE: Always ensure this thread only runs after all other operations
	 * are completed in probe to avoid any corner case scenarios
	 */
	earc_data->base_polling_task = kthread_run(poll_hpd_regs, earc_data,
		"earc-poll");
	if (IS_ERR(earc_data->base_polling_task)) {
		dev_err(&client->dev, "failed to start polling thread\n");
		ret = PTR_ERR(earc_data->base_polling_task);
		goto err_base_polling;
	}

	dev_info(&client->dev, "eARC Audio driver initialized successfully\n");
	return 0;

err_base_polling:
	remove_sysfs_entries(earc_data);
err_sysfs_create:
	device_destroy(earc_audio_class, dev_num);
err_device_create:
	class_destroy(earc_audio_class);
err_class_create:
	cdev_del(&earc_audio_cdev);
err_cdev_add:
	unregister_chrdev_region(dev_num, 1);
err_chrdev_alloc:
	mutex_destroy(&earc_data->rx_lock);
	mutex_destroy(&earc_data->tx_lock);
	mutex_destroy(&earc_data->suspend_lock);
	return ret;
}

static void earc_i2c_remove(struct i2c_client *client)
{
	struct earc_driver_data *earc_data = i2c_get_clientdata(client);
	struct task_struct *task;
	int ret = 0;

	/* Remove sysfs entries */
	remove_sysfs_entries(earc_data);

	/* Stop base polling task */
	if (earc_data->base_polling_task) {
		ret = kthread_stop(earc_data->base_polling_task);
		if (ret != 0 && ret != -EINTR)
			dev_err(&earc_data->client->dev,
				"Failed to stop base polling task: %d\n", ret);
	}

	/* Stop RX polling task safely */
	mutex_lock(&earc_data->rx_lock);
	task = earc_data->earc_rx_polling_task;
	earc_data->earc_rx_polling_task = NULL;
	mutex_unlock(&earc_data->rx_lock);

	if (task) {
		ret = kthread_stop(task);
		if (ret != 0 && ret != -EINTR)
			dev_err(&earc_data->client->dev,
				"Failed to stop RX polling task: %d\n", ret);
	}

	/* Stop TX polling task safely */
	mutex_lock(&earc_data->tx_lock);
	task = earc_data->earc_tx_polling_task;
	earc_data->earc_tx_polling_task = NULL;
	mutex_unlock(&earc_data->tx_lock);

	if (task) {
		ret = kthread_stop(task);
		if (ret != 0 && ret != -EINTR)
			dev_err(&earc_data->client->dev,
				"Failed to stop TX polling task: %d\n", ret);
	}

	/* Free audio capabilities data */
	if (earc_data->sink_cap.audio_cap_data) {
		kfree(earc_data->sink_cap.audio_cap_data);
		earc_data->sink_cap.audio_cap_data = NULL;
		earc_data->sink_cap.audio_cap_size = 0;
	}

	/* Destroy all mutexes */
	mutex_destroy(&earc_data->rx_lock);
	mutex_destroy(&earc_data->tx_lock);
	mutex_destroy(&earc_data->suspend_lock);

	/* Clean up device and class */
	device_destroy(earc_audio_class, dev_num);
	class_destroy(earc_audio_class);
	cdev_del(&earc_audio_cdev);
	unregister_chrdev_region(dev_num, 1);
}

static SIMPLE_DEV_PM_OPS(earc_pm_ops, earc_suspend, earc_resume);

static const struct of_device_id earc_of_match[] = {
	{ .compatible = "nv,ep-earc" }, {}
};
MODULE_DEVICE_TABLE(of, earc_of_match);

static const struct i2c_device_id earc_i2c_id[] = {
	{ "ep92hx", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, earc_i2c_id);

static struct i2c_driver earc_i2c_driver = {
	.driver = {
		.name = "ep_earc_i2c",
		.of_match_table = earc_of_match,
#if defined(CONFIG_PM)
		.pm = &earc_pm_ops,
#endif
	},
#if defined(NV_I2C_DRIVER_STRUCT_HAS_PROBE_NEW)
	.probe_new = earc_i2c_probe,
#else
	.probe = earc_i2c_probe,
#endif
	.remove = earc_i2c_remove,
	.id_table = earc_i2c_id,
};
module_i2c_driver(earc_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aditya Bavanari <abavanari@nvidia.com>");
MODULE_DESCRIPTION("Explore EP92H1T/2T eARC I2C Driver");
