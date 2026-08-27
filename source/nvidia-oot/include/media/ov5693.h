/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2013-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */

#ifndef __OV5693_H__
#define __OV5693_H__

#include <media/nvc.h>
#include <uapi/media/nvc_image.h>
#include <uapi/media/ov5693.h>

#define OV5693_INVALID_COARSE_TIME  -1

#define OV5693_EEPROM_ADDRESS		0x54
#define OV5693_EEPROM_SIZE		1024
#define OV5693_EEPROM_STR_SIZE		(OV5693_EEPROM_SIZE * 2)
#define OV5693_EEPROM_BLOCK_SIZE	(1 << 8)
#define OV5693_EEPROM_NUM_BLOCKS \
	(OV5693_EEPROM_SIZE / OV5693_EEPROM_BLOCK_SIZE)

#define OV5693_OTP_LOAD_CTRL_ADDR	0x3D81
#define OV5693_OTP_BANK_SELECT_ADDR	0x3D84
#define OV5693_OTP_BANK_START_ADDR	0x3D00
#define OV5693_OTP_BANK_END_ADDR	0x3D0F
#define OV5693_OTP_NUM_BANKS		(32)
#define OV5693_OTP_BANK_SIZE \
	 (OV5693_OTP_BANK_END_ADDR - OV5693_OTP_BANK_START_ADDR + 1)
#define OV5693_OTP_SIZE \
	 (OV5693_OTP_BANK_SIZE * OV5693_OTP_NUM_BANKS)
#define OV5693_OTP_STR_SIZE (OV5693_OTP_SIZE * 2)

/* See notes in the nvc.h file on the GPIO usage */
enum ov5693_gpio_type {
	OV5693_GPIO_TYPE_PWRDN = 0,
	OV5693_GPIO_TYPE_RESET,
};

struct ov5693_eeprom_data {
	struct i2c_client *i2c_client;
	struct i2c_adapter *adap;
	struct i2c_board_info brd;
	struct regmap *regmap;
};

struct ov5693_power_rail {
	struct regulator *dvdd;
	struct regulator *avdd;
	struct regulator *dovdd;
};

struct ov5693_regulators {
	const char *avdd;
	const char *dvdd;
	const char *dovdd;
};

#endif  /* __OV5693_H__ */
