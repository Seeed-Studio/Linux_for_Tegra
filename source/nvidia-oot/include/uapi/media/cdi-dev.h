// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef __UAPI_CDI_DEV_H__
#define __UAPI_CDI_DEV_H__

#include <linux/ioctl.h>
#include <linux/types.h>

#define CDI_DEV_PKG_FLAG_WR	1

#define CDI_DEV_IOCTL_RW	          _IOW('o', 1, struct cdi_dev_package)
#define CDI_DEV_IOCTL_GET_PWR_INFO    _IOW('o', 2, struct cdi_dev_pwr_ctrl_info)
#define CDI_DEV_IOCTL_FRSYNC_MUX      _IOW('o', 3, struct cdi_dev_fsync_mux)

#define DES_PWR_NVCCP    0U
#define DES_PWR_GPIO     1U
#define DES_PWR_NO_PWR   0xFFU
#define CAM_PWR_NVCCP    0U
#define CAM_PWR_MAX20087 1U
#define CAM_PWR_TPS160   2U
#define CAM_PWR_NO_PWR   0xFFU

#define MAX_POWER_LINKS_PER_BLOCK (4U)

struct __attribute__ ((__packed__)) cdi_dev_pwr_ctrl_info {
	__s8 cam_pwr_method;
	__s8 cam_pwr_i2c_addr;
	__u8 cam_pwr_links[MAX_POWER_LINKS_PER_BLOCK];
};

struct cdi_dev_fsync_mux {
	__s8 mux_sel;
	__s8 cam_grp;
};

struct __attribute__ ((__packed__)) cdi_dev_package {
	__u16 offset;
	__u16 offset_len;
	__u32 size;
	__u32 flags;
	unsigned long buffer;
};

#endif  /* __UAPI_CDI_DEV_H__ */
