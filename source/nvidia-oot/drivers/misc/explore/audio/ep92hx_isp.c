// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/errno.h>
#include <linux/regmap.h>
#include <linux/version.h>
#include <linux/of.h>

#include "ep92hx_isp.h"

#define EP_FW_UPDATE_PADDING_NUM	0xFF
#define EP_FW_UPDATE_BLOCK_SIZE		512
#define EP_FW_UPDATE_WRITE_BYTE_NUM	16
#define EP_FW_UPDATE_WRITE_BLOCK_SIZE	(EP_FW_UPDATE_WRITE_BYTE_NUM + 1)
#define EP_FW_UPDATE_WRITE_TIMES	(EP_FW_UPDATE_BLOCK_SIZE / EP_FW_UPDATE_WRITE_BYTE_NUM)
#define EP_FW_UPDATE_WAIT_DELAY_MS	50
#define EP_FW_UPDATE_RETRY_TIMES	2
#define EP_ISP_MODE_ENTER_DELAY_MS	2000
#define EP_ISP_MODE_EXIT_DELAY_MS	2000

#define EP92H1T_FW_PATH			"explore/ep92h1t.bin"
#define EP92H1T_FW_VERSION_OFFSET	0

static const struct reg_default ep_isp_defaults[] = {
	{ EP92H1T_ISP_MODE_STATUS_REG, 0x00 },
	{ EP92H1T_ISP_MODE_DISABLE, 0x00 },
	{ EP92H1T_ISP_BLOCK_INDEX_REG, 0x00 },
	{ EP92H1T_ISP_DATA_REG, 0x00 },
	{ EP92H1T_ISP_FW_CHECKSUM_REG, 0x00 },
	{ EP92H1T_ISP_FW_VERSION_REG, 0x00 },
};

static bool ep_isp_writeable(struct device *dev, unsigned int reg)
{
	return reg == EP92H1T_ISP_MODE_STATUS_REG ||
	       reg == EP92H1T_ISP_MODE_DISABLE ||
	       reg == EP92H1T_ISP_BLOCK_INDEX_REG ||
	       reg == EP92H1T_ISP_DATA_REG;
}

static bool ep_isp_wait_ready(struct regmap *regmap, unsigned int reg,
			      unsigned int timeout_ms)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(timeout_ms);
	unsigned int val = 0U;

	do {
		if (regmap_read(regmap, reg, &val) == 0)
			return true;
		cpu_relax();
		udelay(10);
	} while (!time_after_eq(jiffies, deadline));

	pr_err("Timeout waiting for register: 0x%x, timeout: 0x%x\n", reg, timeout_ms);
	return false;
}

/**
 * ep_isp_write_full_block - Write a complete 512-byte block to the device
 * @client: I2C client for the EP92H1T device
 * @regmap: Regmap for device communication
 * @fw_data: Pointer to firmware data
 * @block_index: Index of the current block to write
 *
 * This function writes one complete 512-byte block to the device by dividing
 * it into 32 writes of 16 bytes each. This function assumes that the firmware
 * data has at least 512 bytes available from the given block index.
 *
 * Returns: EP_FW_UPDATE_BLOCK_SIZE (512) on success,
 *          negative error code on failure
 */
static int ep_isp_write_full_block(struct i2c_client *client,
				    struct regmap *regmap,
				    const u8 *fw_data,
				    int block_index)
{
	const u8 *fw_offset = fw_data + (block_index * EP_FW_UPDATE_BLOCK_SIZE);
	uint8_t temp_data[EP_FW_UPDATE_WRITE_BLOCK_SIZE] = {0};
	uint8_t *data_ptr = temp_data + 1;
	int ret = 0;

	/*
	 * Write the 16-byte chunk to the device.
	 * temp_data[0] contains the number of bytes to write (always 16)
	 */
	temp_data[0] = (uint8_t)EP_FW_UPDATE_WRITE_BYTE_NUM;

	/* Write the block data in chunks (32 writes of 16 bytes each) */
	for (int j = 0; j < EP_FW_UPDATE_WRITE_TIMES; j++) {
		/*
		 * Copy 16 bytes from firmware data to the buffer.
		 * temp_data[0] will be the write count (set in the beginning)
		 * temp_data[1..16] contains the actual firmware data
		 */
		memcpy(data_ptr,
		       fw_offset + j * EP_FW_UPDATE_WRITE_BYTE_NUM,
		       EP_FW_UPDATE_WRITE_BYTE_NUM);

		ret = regmap_bulk_write(regmap, EP92H1T_ISP_DATA_REG,
					temp_data, EP_FW_UPDATE_WRITE_BLOCK_SIZE);
		if (ret != 0) {
			dev_err(&client->dev,
				"Error occurred when flash firmware to EP92H1T, ret: %d\n", ret);
			return ret;
		}
	}

	return EP_FW_UPDATE_BLOCK_SIZE;
}

/**
 * ep_isp_write_partial_block - Write the last partial block to the device
 * @client: I2C client for the EP92H1T device
 * @regmap: Regmap for device communication
 * @fw_data: Pointer to firmware data
 * @remain_fw_size: Remaining firmware size to be written (< 512 bytes)
 * @block_index: Index of the current block to write
 *
 * This function writes the last partial block when the firmware size is not
 * aligned to 512 bytes. The data is padded with 0xFF to complete a full
 * 512-byte block write (32 writes of 16 bytes each).
 *
 * Returns: Number of actual bytes written from firmware data on success,
 *          negative error code on failure
 */
static int ep_isp_write_partial_block(struct i2c_client *client,
				       struct regmap *regmap,
				       const u8 *fw_data,
				       long remain_fw_size,
				       int block_index)
{
	const u8 *fw_offset = fw_data + (block_index * EP_FW_UPDATE_BLOCK_SIZE);
	uint8_t temp_data[EP_FW_UPDATE_WRITE_BLOCK_SIZE] = {0};
	uint8_t *data_ptr = temp_data + 1;
	long remaining_in_block = 0;
	long bytes_written = 0;
	long read_size = 0;
	int ret = 0;

	/*
	 * Write the 16-byte chunk to the device.
	 * temp_data[0] contains the number of bytes to write (always 16)
	 * temp_data[1..16] contains firmware data + padding (if needed)
	 */
	temp_data[0] = (uint8_t)EP_FW_UPDATE_WRITE_BYTE_NUM;

	/* Write the block data in chunks (32 writes of 16 bytes each) */
	for (int j = 0; j < EP_FW_UPDATE_WRITE_TIMES; j++) {
		/*
		 * Initialize the data buffer with padding bytes (0xFF).
		 * This ensures that if the last chunk is smaller than
		 * 16 bytes, the remaining bytes are properly padded.
		 */
		memset(data_ptr, EP_FW_UPDATE_PADDING_NUM, EP_FW_UPDATE_WRITE_BYTE_NUM);

		/* Calculate remaining bytes in this partial block */
		remaining_in_block = remain_fw_size - j * EP_FW_UPDATE_WRITE_BYTE_NUM;
		if (remaining_in_block > 0) {
			/* Copy actual firmware data (up to 16 bytes) */
			read_size = min_t(long, EP_FW_UPDATE_WRITE_BYTE_NUM, remaining_in_block);
			memcpy(data_ptr,
			       fw_offset + j * EP_FW_UPDATE_WRITE_BYTE_NUM,
			       read_size);
		} else {
			/* No more firmware data, write only padding */
			read_size = 0;
		}

		ret = regmap_bulk_write(regmap, EP92H1T_ISP_DATA_REG,
					temp_data, EP_FW_UPDATE_WRITE_BLOCK_SIZE);
		if (ret != 0) {
			dev_err(&client->dev,
				"Error occurred when flash firmware to EP92H1T, ret: %d\n", ret);
			return ret;
		}
		bytes_written += read_size;
	}

	return bytes_written;
}

/**
 * ep_isp_do_firmware_update - Update EP92H1T firmware via ISP mode
 * @client: I2C client for the EP92H1T device
 * @fw: Firmware data to be flashed
 *
 * This function implements the firmware update process for EP92H1T device.
 * The firmware is written in blocks of 512 bytes, with each block subdivided
 * into 32 writes of 16 bytes each.
 *
 * Firmware Update Flow:
 * 1. Calculate the number of full 512-byte blocks
 * 2. For each full block:
 *    a. Set the block index register (with retry mechanism)
 *    b. Write the block data in 32 iterations of 16 bytes each
 *    c. Wait for hardware to process the block (50ms delay)
 * 3. If there's remaining data (partial block), write it with padding
 * 4. Verify the total bytes sent matches the firmware size
 *
 * Returns: 0 on success, negative error code on failure
 */
static int ep_isp_do_firmware_update(struct i2c_client *client, const struct firmware *fw)
{
	struct ep_isp_data *ep_isp_data = i2c_get_clientdata(client);
	const u8 *fw_data = fw->data;
	long fw_size = fw->size;
	long full_block_count = 0;
	long remain_size = 0;
	long sent_size = 0;
	int retry = 0;
	int ret = 0;

	/*
	 * Calculate the number of full 512-byte blocks.
	 * Any remaining bytes will be handled separately after the loop.
	 */
	full_block_count = fw_size / EP_FW_UPDATE_BLOCK_SIZE;
	remain_size = fw_size % EP_FW_UPDATE_BLOCK_SIZE;

	dev_dbg(&client->dev, "Firmware size: %ld bytes, %ld full blocks + %ld remaining bytes\n",
		 fw_size, full_block_count, remain_size);
	dev_dbg(&client->dev, "Starting to update firmware...\n");

	/* Main loop: Process each full 512-byte block */
	for (int i = 0; i < full_block_count; i++) {
		/*
		 * Step 1: Set the block index register to tell the device
		 * which block we are about to write. This uses a retry
		 * mechanism because the device may be busy processing the
		 * previous block.
		 */
		retry = 0;

		do {
			ret = regmap_write(ep_isp_data->regmap, EP92H1T_ISP_BLOCK_INDEX_REG, (uint8_t)i);
			if (ret == 0)
				break;
			msleep(EP_FW_UPDATE_WAIT_DELAY_MS);
		} while (ret != 0 && retry++ < EP_FW_UPDATE_RETRY_TIMES);
		if (ret != 0) {
			dev_err(&client->dev, "Error occurred when set block index, ret: %d\n", ret);
			break;
		}

		/*
		 * Step 2: Write the full block data.
		 * Each 512-byte block is divided into 32 writes of 16 bytes each.
		 */
		ret = ep_isp_write_full_block(client, ep_isp_data->regmap, fw_data, i);
		if (ret < 0) {
			/* Error occurred during block write */
			break;
		}
		sent_size += ret;

		/*
		 * Step 3: Wait for the device to process the block.
		 * EP hardware needs this delay for processing each block of
		 * firmware, theoretically the execute time should be 30ms,
		 * but in practice it will takes over 40ms.
		 * Before EP hardware is able to process the next block, all
		 * registers' R/W operations will return an error, so we add
		 * some retry logic to handle this.
		 */
		msleep(EP_FW_UPDATE_WAIT_DELAY_MS);
	}

	/*
	 * Handle the last partial block if there's any remaining data.
	 * This block will be padded with 0xFF to complete a full 512-byte write.
	 */
	if (ret == 0 && remain_size > 0) {
		retry = 0;

		/* Set the block index for the partial block */
		do {
			ret = regmap_write(ep_isp_data->regmap, EP92H1T_ISP_BLOCK_INDEX_REG, (uint8_t)full_block_count);
			if (ret == 0)
				break;
			msleep(EP_FW_UPDATE_WAIT_DELAY_MS);
		} while (ret != 0 && retry++ < EP_FW_UPDATE_RETRY_TIMES);
		if (ret != 0) {
			dev_err(&client->dev, "Error occurred when set partial block index, ret: %d\n", ret);
		} else {
			/* Write the partial block with padding */
			ret = ep_isp_write_partial_block(client, ep_isp_data->regmap,
							  fw_data, remain_size, full_block_count);
			if (ret < 0) {
				dev_err(&client->dev, "Error occurred when writing partial block\n");
			} else {
				sent_size += ret;
				msleep(EP_FW_UPDATE_WAIT_DELAY_MS);
			}
		}
	}

	/*
	 * Final verification: Check if the firmware was successfully flashed.
	 * We verify that the total bytes sent matches the expected firmware size.
	 */
	if (ret < 0) {
		dev_err(&client->dev, "Flash firmware to EP92H1T failed!\n");
	} else if (sent_size != fw_size) {
		dev_err(&client->dev, "Flash firmware to EP92H1T failed! sent_size: %ld, file_size: %ld\n",
			sent_size, fw_size);
		ret = -EIO;
	} else {
		/* If the firmware is flashed successfully, return 0 */
		ret = 0;
		dev_info(&client->dev, "Flash firmware to EP92H1T complete\n");
	}

	return ret;
}

int ep_exit_isp_mode(struct i2c_client *client)
{
	struct ep_isp_data *ep_isp_data = i2c_get_clientdata(client);
	int ret = 0;

	ret = regmap_write(ep_isp_data->regmap, EP92H1T_ISP_MODE_STATUS_REG, EP92H1T_ISP_MODE_DISABLE);
	if (ret) {
		dev_err(&client->dev, "Failed to exit ISP mode\n");
	} else {
		dev_info(&client->dev, "Exit ISP mode\n");
		/*
		 * This delay is required for EP hardware:
		 *   1. exit ISP mode
		 *   2. load new firmware
		 *   3. enter to normal mode
		 */
		msleep(EP_ISP_MODE_EXIT_DELAY_MS);
	}

	return ret;
}

int ep_update_fw(struct i2c_client *client)
{
	const struct firmware *fw;
	int ret = 0;

	if (client == NULL) {
		pr_err("Invalid parameters for ep_check_update_fw\n");
		return -EINVAL;
	}

	/* Request firmware */
	ret = request_firmware(&fw, EP92H1T_FW_PATH, &client->dev);
	if (ret) {
		dev_err(&client->dev, "Failed to request firmware: %s\n", EP92H1T_FW_PATH);
		return ret;
	}

	/* Perform firmware update */
	ret = ep_isp_do_firmware_update(client, fw);

	release_firmware(fw);
	return ret;
}

int ep_get_latest_fw_version(uint8_t *version)
{
	const struct firmware *fw;
	int ret = 0;

	if (version == NULL) {
		pr_err("Invalid parameters for %s\n", __func__);
		return -EINVAL;
	}

	ret = request_firmware(&fw, EP92H1T_FW_PATH, NULL);
	if (ret) {
		pr_err("Failed to request firmware: %s\n", EP92H1T_FW_PATH);
		return ret;
	}

	/* Check if firmware data is available */
	if (fw == NULL || fw->data == NULL || fw->size == 0) {
		pr_err("Invalid firmware data for %s\n", EP92H1T_FW_PATH);
		goto release_fw;
		ret = -EINVAL;
	}

	/* Get the firmware version from the first byte */
	*version = fw->data[EP92H1T_FW_VERSION_OFFSET];
	pr_debug("Latest firmware version: 0x%02x\n", *version);

release_fw:
	release_firmware(fw);
	return ret;
}

static const struct regmap_config ep_isp_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= EP92H1T_ISP_FW_VERSION_REG,
	.cache_type		= REGCACHE_NONE,
	.reg_defaults		= ep_isp_defaults,
	.num_reg_defaults	= ARRAY_SIZE(ep_isp_defaults),
	.writeable_reg		= ep_isp_writeable,
};

static int ep_isp_i2c_init(struct i2c_client *client)
{
	struct ep_isp_data *ep_isp_data = NULL;
	struct regmap *regmap = NULL;

	ep_isp_data = devm_kzalloc(&client->dev, sizeof(*ep_isp_data), GFP_KERNEL);
	if (ep_isp_data == NULL)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &ep_isp_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to initialize regmap: %ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}
	ep_isp_data->regmap = regmap;

	i2c_set_clientdata(client, ep_isp_data);

	dev_info(&client->dev, "EP92H1T ISP mode initialized\n");
	return 0;
}

/**
 * ep_enter_isp_mode - Enter ISP mode for firmware update
 * @client: Main I2C client for the EP92HX device
 * @isp_client: Pointer to store the created ISP client
 * @regmap: Regmap for the main device to write ISP mode register
 *
 * This function:
 * 1. Reads ISP client address from device tree
 * 2. Creates a dummy I2C client for ISP communication
 * 3. Initializes ISP mode
 * 4. Writes ISP mode enable register
 *
 * Returns: 0 on success, negative error code on failure
 */
int ep_enter_isp_mode(struct i2c_client *client, struct i2c_client **ret_client,
		      struct regmap *regmap)
{
	struct device_node *np = client->dev.of_node;
	struct ep_isp_data *ep_isp_data = NULL;
	struct i2c_client *isp_client = NULL;
	uint32_t isp_addr = 0;
	int ret = 0;

	if (!client || !ret_client || !regmap) {
		dev_err(&client->dev, "Invalid parameters\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "isp_client", &isp_addr);
	if (ret != 0) {
		dev_err(&client->dev, "Failed to read ISP client address\n");
		return -EINVAL;
	}

	isp_client = devm_i2c_new_dummy_device(&client->dev, client->adapter, isp_addr);
	if (IS_ERR(isp_client)) {
		dev_err(&client->dev, "Failed to create ISP client\n");
		return PTR_ERR(isp_client);
	}

	ret = ep_isp_i2c_init(isp_client);
	if (ret) {
		dev_err(&isp_client->dev, "Failed to initialize ISP client\n");
		return ret;
	}

	ret = regmap_write(regmap, EP92HX_REG_ISP_MODE, EP92HX_ISP_MODE_ENABLE);
	if (ret < 0) {
		ret = 0;
		dev_warn(&client->dev, "EP maybe already in ISP mode, continue...\n");
	} else {
		ep_isp_data = i2c_get_clientdata(isp_client);
		if (!ep_isp_wait_ready(ep_isp_data->regmap, EP92H1T_ISP_FW_VERSION_REG,
				       EP_ISP_MODE_ENTER_DELAY_MS)) {
			dev_err(&client->dev, "Timeout waiting for ISP mode ready\n");
			ret = -ETIMEDOUT;
		}
	}

	*ret_client = isp_client;
	return ret;
}
