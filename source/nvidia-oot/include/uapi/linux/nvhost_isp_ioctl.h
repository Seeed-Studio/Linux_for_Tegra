/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tegra ISP Driver
 *
 * Copyright (c) 2016-2022, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __UAPI_LINUX_NVHOST_ISP_IOCTL_H
#define __UAPI_LINUX_NVHOST_ISP_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define ISP_SOFT_ISO_CLIENT 0
#define ISP_HARD_ISO_CLIENT 1

#if !defined(__KERNEL__)
#define __user
#endif

struct isp_emc {
	uint isp_bw;
	uint isp_clk;
	uint bpp_input;
	uint bpp_output;
};

struct isp_la_bw {
	/* Total ISP write BW in MBps, either ISO peak BW or non-ISO avg BW */
	__u32 isp_la_bw;
	/* is ISO or non-ISO */
	bool is_iso;
};

#define NVHOST_ISP_IOCTL_MAGIC 'I'

/*
 * /dev/nvhost-ctrl-isp devices
 *
 * Opening a '/dev/nvhost-ctrl-isp' device node creates a way to send
 * ctrl ioctl to isp driver.
 *
 * /dev/nvhost-isp is for channel (context specific) operations. We use
 * /dev/nvhost-ctrl-isp for global (context independent) operations on
 * isp device.
 */

#define NVHOST_ISP_IOCTL_SET_ISP_LA_BW \
		_IOW(NVHOST_ISP_IOCTL_MAGIC, 4, struct isp_la_bw)
#endif

