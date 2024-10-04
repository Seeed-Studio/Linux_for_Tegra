/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2009-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * structure declarations for nvmem and nvmap user-space ioctls
 */

#ifndef _LINUX_NVMAP_H
#define _LINUX_NVMAP_H

#include <linux/rbtree.h>
#include <linux/file.h>
#include <linux/dma-buf.h>
#include <linux/device.h>
#include <linux/version.h>
#include <uapi/linux/nvmap.h>

int nvmap_register_vidmem_carveout(struct device *dma_dev,
		phys_addr_t base, size_t size);

#endif /* _LINUX_NVMAP_H */
