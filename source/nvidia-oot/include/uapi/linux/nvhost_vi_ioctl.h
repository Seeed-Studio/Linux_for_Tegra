/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tegra VI Driver
 *
 * Copyright (c) 2013-2022, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __UAPI_LINUX_NVHOST_VI_IOCTL_H
#define __UAPI_LINUX_NVHOST_VI_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#if !defined(__KERNEL__)
#define __user
#endif

#define NVHOST_VI_IOCTL_MAGIC 'V'

/*
 * /dev/nvhost-ctrl-vi devices
 *
 * Opening a '/dev/nvhost-ctrl-vi' device node creates a way to send
 * ctrl ioctl to vi driver.
 *
 * /dev/nvhost-vi is for channel (context specific) operations. We use
 * /dev/nvhost-ctrl-vi for global (context independent) operations on
 * vi device.
 */

#define NVHOST_VI_IOCTL_ENABLE_TPG _IOW(NVHOST_VI_IOCTL_MAGIC, 1, uint)

#endif
