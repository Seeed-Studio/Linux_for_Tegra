/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */
#ifndef NVHVIVC_MEMPOOL_IOCTL_H
#define NVHVIVC_MEMPOOL_IOCTL_H

#include <linux/ioctl.h>

/**
 * @defgroup mempool_ioctl Mempool IOCTL Driver
 * @{
 */

/** @brief ivc mempool IOCTL call magic number */
#define TEGRA_MPLUSERSPACE_IOCTL_MAGIC 0xA6


/* IOCTL definitions */

/** @brief IOCTL call to query ivc mempool configuration data */
#define TEGRA_MPLUSERSPACE_IOCTL_GET_INFO \
	_IOR(TEGRA_MPLUSERSPACE_IOCTL_MAGIC, 1, struct ivc_mempool)

/** @brief Maximum IOCTL call number possible */
#define TEGRA_MPLUSERSPACE_IOCTL_NUMBER_MAX 1

/** @} */

#endif /* NVHVIVC_MEMPOOL_IOCTL_H */
