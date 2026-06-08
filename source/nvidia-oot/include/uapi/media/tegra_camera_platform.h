/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2022, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef _UAPI_TEGRA_CAMERA_PLATFORM_H_
#define _UAPI_TEGRA_CAMERA_PLATFORM_H_

#include <linux/ioctl.h>
#include <linux/types.h>

#define TEGRA_CAMERA_IOCTL_SET_BW _IOW('o', 1, struct bw_info)
#define TEGRA_CAMERA_IOCTL_GET_BW _IOR('o', 2, __u64)
#define TEGRA_CAMERA_IOCTL_GET_CURR_REQ_ISO_BW _IOR('o', 3, __u64)

struct bw_info {
	__u8 is_iso;
	__u64 bw;
};

#endif