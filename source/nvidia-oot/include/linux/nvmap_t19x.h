/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2009-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * structure declarations for nvmem and nvmap user-space ioctls
 */

#include <linux/types.h>
#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/spinlock.h>

#ifndef _LINUX_NVMAP_T19x_H
#define _LINUX_NVMAP_T19x_H

#define NVMAP_HEAP_CARVEOUT_CVSRAM  (1ul<<25)

int nvmap_register_cvsram_carveout(struct device *dma_dev,
		phys_addr_t base, size_t size,
		int (*pmops_busy)(void), int (*pmops_idle)(void));

struct nvmap_handle_t19x {
	atomic_t nc_pin; /* no. of pins from non io coherent devices */
};

extern bool nvmap_version_t19x;

#endif /* _LINUX_NVMAP_T19x_H */
