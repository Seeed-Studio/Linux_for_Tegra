/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef IMX318_H
#define IMX318_H

#include <media/nvc.h>
#include <uapi/media/nvc_image.h>
#include <uapi/media/imx318.h>

#define IMX318_INVALID_COARSE_TIME	-1

#define IMX318_EEPROM_ADDRESS		0x54
#define IMX318_EEPROM_SIZE		256
#define IMX318_EEPROM_STR_SIZE		(IMX318_EEPROM_SIZE * 2)
#define IMX318_EEPROM_BLOCK_SIZE	(1 << 8)
#define IMX318_EEPROM_NUM_BLOCKS \
	(IMX318_EEPROM_SIZE / IMX318_EEPROM_BLOCK_SIZE)

/* Incorrect data, cannot find fuse ID in documentation */
#define IMX318_FUSE_ID_START_ADDR	0x5b
#define IMX318_FUSE_ID_BANK		0
#define IMX318_FUSE_ID_SIZE		8
#define IMX318_FUSE_ID_STR_SIZE		(IMX318_FUSE_ID_SIZE * 2)

/* See notes in the nvc.h file on the GPIO usage */
enum imx318_gpio_type {
	IMX318_GPIO_TYPE_PWRDN = 0,
	IMX318_GPIO_TYPE_RESET,
};

struct imx318_eeprom_data {
	struct i2c_client *i2c_client;
	struct i2c_adapter *adap;
	struct i2c_board_info brd;
	struct regmap *regmap;
};

struct imx318_power_rail {
	struct regulator *dvdd;
	struct regulator *avdd;
	struct regulator *dovdd;
};

struct imx318_regulators {
	const char *avdd;
	const char *dvdd;
	const char *dovdd;
};

#endif /* IMX318_H */
