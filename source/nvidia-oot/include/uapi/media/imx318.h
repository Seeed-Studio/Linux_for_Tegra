/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef UAPI_IMX318_H
#define UAPI_IMX318_H

#include <media/nvc.h>
#include "nvc_image.h"

#define IMX318_IOCTL_SET_MODE		_IOW('o', 1, struct imx318_mode)
#define IMX318_IOCTL_SET_FRAME_LENGTH	_IOW('o', 2, uint32_t)
#define IMX318_IOCTL_SET_COARSE_TIME	_IOW('o', 3, uint32_t)
#define IMX318_IOCTL_SET_GAIN		_IOW('o', 4, uint16_t)
#define IMX318_IOCTL_GET_STATUS		_IOR('o', 5, uint8_t)
#define IMX318_IOCTL_SET_BINNING	_IOW('o', 6, uint8_t)
#define IMX318_IOCTL_TEST_PATTERN	_IOW('o', 7, \
						enum imx318_test_pattern)
#define IMX318_IOCTL_SET_GROUP_HOLD	_IOW('o', 8, struct imx318_ae)
/* operating mode can be either stereo , leftOnly or rightOnly */
#define IMX318_IOCTL_SET_CAMERA_MODE	_IOW('o', 10, uint32_t)
#define IMX318_IOCTL_SYNC_SENSORS	_IOW('o', 11, uint32_t)
#define IMX318_IOCTL_GET_FUSEID		_IOR('o', 12, struct nvc_fuseid)
#define IMX318_IOCTL_SET_CAL_DATA	_IOW('o', 15, \
						struct imx318_cal_data)
#define IMX318_IOCTL_GET_EEPROM_DATA	_IOR('o', 20, uint8_t *)
#define IMX318_IOCTL_SET_EEPROM_DATA	_IOW('o', 21, uint8_t *)
#define IMX318_IOCTL_GET_CAPS		_IOR('o', 22, \
						struct nvc_imager_cap)
#define IMX318_IOCTL_SET_POWER		_IOW('o', 23, uint32_t)

struct imx318_mode {
	int res_x;
	int res_y;
	int fps;
	uint32_t frame_length;
	uint32_t coarse_time;
	uint32_t coarse_time_short;
	uint16_t gain;
	uint8_t hdr_en;
};

struct imx318_ae {
	uint32_t frame_length;
	uint8_t  frame_length_enable;
	uint32_t coarse_time;
	uint32_t coarse_time_short;
	uint8_t  coarse_time_enable;
	int32_t gain;
	uint8_t  gain_enable;
};

struct imx318_fuseid {
	uint32_t size;
	uint8_t  id[16];
};

struct imx318_hdr {
	uint32_t coarse_time_long;
	uint32_t coarse_time_short;
};

struct imx318_otp_bank {
	uint32_t id;
	uint8_t  buf[16];
};

struct imx318_cal_data {
	int loaded;
	int rg_ratio;
	int bg_ratio;
	int rg_ratio_typical;
	int bg_ratio_typical;
	uint8_t lenc[62];
};

#endif  /* UAPI_IMX318_H */
