/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */
#ifndef __EP_ISP_H_
#define __EP_ISP_H_

#include <linux/i2c.h>

/* Normal Mode Register */
#define EP92HX_REG_ISP_MODE		0x0F

/* ISP Mode Register */
#define EP92H1T_ISP_MODE_STATUS_REG	0x02
#define EP92H1T_ISP_BLOCK_INDEX_REG	0x01
#define EP92H1T_ISP_DATA_REG		0x41
#define EP92H1T_ISP_FW_CHECKSUM_REG	0x8A
#define EP92H1T_ISP_FW_VERSION_REG	0x8B

/* ISP Mode operation values */
#define EP92HX_ISP_MODE_ENABLE		0x40
#define EP92H1T_ISP_MODE_DISABLE	0x00

struct ep_isp_data {
	struct i2c_client *client;
	struct regmap *regmap;
};

/**
 * ep_enter_isp_mode - Enter ISP mode for firmware update
 * @client: Main I2C client for the EP92HX device
 * @isp_client: Pointer to store the created ISP client
 * @regmap: Regmap for the main device to write ISP mode register
 *
 * Returns: 0 on success, negative error code on failure
 */
int ep_enter_isp_mode(struct i2c_client *client, struct i2c_client **ret_client,
		      struct regmap *regmap);

/**
 * ep_exit_isp_mode - Exit ISP mode
 * @client: I2C client
 *
 * Returns: 0 on success, negative error code on failure
 */
int ep_exit_isp_mode(struct i2c_client *client);

/**
 * ep_update_fw- Check and update firmware for EP92H1T
 * @client: I2C client
 *
 * This function performs the complete firmware update process:
 * 1. Finds and enters ISP mode
 * 2. Reads original firmware version
 * 3. Updates firmware
 * 4. Reads new firmware version
 *
 * Returns: 0 on success, negative error code on failure
 */
int ep_update_fw(struct i2c_client *client);

/**
 * ep_get_latest_fw_version - Get the latest firmware version
 * @version: Pointer to store the latest firmware version
 *
 * Returns: 0 on success, negative error code on failure
 */
int ep_get_latest_fw_version(uint8_t *version);


#endif /* __EP_ISP_H_ */
