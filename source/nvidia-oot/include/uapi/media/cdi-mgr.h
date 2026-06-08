// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef __UAPI_TEGRA_CDI_MGR_H__
#define __UAPI_TEGRA_CDI_MGR_H__

#include <linux/ioctl.h>
#include <linux/types.h>

#define CDI_MGR_IOCTL_PWR_DN	        _IOW('o',  1, __s16)
#define CDI_MGR_IOCTL_PWR_UP	        _IOR('o',  2, __s16)
#define CDI_MGR_IOCTL_SET_PID	        _IOW('o',  3, struct cdi_mgr_sinfo)
#define CDI_MGR_IOCTL_SIGNAL	        _IOW('o',  4, int)
#define CDI_MGR_IOCTL_DEV_ADD	        _IOW('o',  5, struct cdi_mgr_new_dev)
#define CDI_MGR_IOCTL_DEV_DEL	        _IOW('o',  6, int)
#define CDI_MGR_IOCTL_PWR_INFO	        _IOW('o',  7, struct cdi_mgr_pwr_info)
#define CDI_MGR_IOCTL_PWM_ENABLE        _IOW('o',  8, int)
#define CDI_MGR_IOCTL_PWM_CONFIG        _IOW('o',  9, struct cdi_mgr_pwm_info)
#define CDI_MGR_IOCTL_INTR_CONFIG	_IOW('o',  10, struct cdi_mgr_gpio_info)
#define CDI_MGR_IOCTL_INTR_ENABLE	_IO('o',   11)
#define CDI_MGR_IOCTL_INTR_WAIT		_IOR('o',  12, __u32)
#define CDI_MGR_IOCTL_INTR_WAIT_ABORT	_IO('o',   13)
#define CDI_MGR_IOCTL_GET_EXT_PWR_CTRL	_IOR('o',  14, __u8)
#define CDI_MGR_IOCTL_GET_PWR_INFO _IOW('o', 15, struct cdi_mgr_pwr_ctrl_info)
#define CDI_MGR_IOCTL_ENABLE_DES_POWER	_IO('o',   16)
#define CDI_MGR_IOCTL_DISABLE_DES_POWER	_IO('o',   17)

#define CDI_MGR_POWER_ALL	5
#define MAX_CDI_NAME_LENGTH	32

#define DES_PWR_NVCCP    0U
#define DES_PWR_GPIO     1U
#define DES_PWR_NO_PWR   0xFFU
#define CAM_PWR_NVCCP    0U
#define CAM_PWR_MAX20087 1U
#define CAM_PWR_NO_PWR   0xFFU

struct cdi_mgr_new_dev {
	__u16 addr;
	__u8 reg_bits;
	__u8 val_bits;
	__u8 drv_name[MAX_CDI_NAME_LENGTH];
};

struct cdi_mgr_sinfo {
	__s32 pid;
	__s32 sig_no;
	__u64 context;
};

struct cdi_mgr_pwr_info {
	__s32 pwr_gpio;
	__s32 pwr_status;
};

struct cdi_mgr_pwr_ctrl_info {
	__s8 des_pwr_method;
	__s8 des_pwr_i2c_addr;
};


struct cdi_mgr_gpio_info {
	__u32 idx;
	__u32 timeout_ms;
};

struct cdi_mgr_gpio_intr {
	__u32 idx;
	__u32 code;
};

struct cdi_mgr_pwm_info {
	__u64 duty_ns;
	__u64 period_ns;
};

enum {
	CDI_MGR_GPIO_INTR_UNBLOCK = 0,
	CDI_MGR_GPIO_INTR,
	CDI_MGR_GPIO_INTR_TIMEOUT,
	CDI_MGR_GPIO_INTR_FAULT,
};

enum {
	CDI_MGR_SIGNAL_RESUME = 0,
	CDI_MGR_SIGNAL_SUSPEND,
};

enum {
	CDI_MGR_PWM_DISABLE = 0,
	CDI_MGR_PWM_ENABLE,
};

#endif  /* __UAPI_TEGRA_CDI_MGR_H__ */
