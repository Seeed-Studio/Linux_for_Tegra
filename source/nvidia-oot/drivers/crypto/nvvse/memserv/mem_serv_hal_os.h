/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef _MEM_SERV_HAL_OS_H_
#define _MEM_SERV_HAL_OS_H_

struct MemServDeviceAttributes {
	struct device *dev;
};

struct MemoryAccess {
	int32_t fd;
	dma_addr_t addr;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
};

#endif // _MEM_SERV_HAL_OS_H_
