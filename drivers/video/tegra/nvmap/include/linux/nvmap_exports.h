/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __NVMAP_EXPORTS_H
#define __NVMAP_EXPORTS_H

#ifndef NV_CONFIG_NVMAP_IN_EMBEDDED_LINUX
void *nvmap_dma_alloc_attrs(struct device *dev, size_t size,
			    dma_addr_t *dma_handle, gfp_t flag,
			    unsigned long attrs);

void nvmap_dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
			  dma_addr_t dma_handle, unsigned long attrs);

struct device *nvmap_get_vpr_dev(void);

struct device *nvmap_get_vpr1_dev(void);

#endif /* !NV_CONFIG_NVMAP_IN_EMBEDDED_LINUX */
#endif /* __NVMAP_EXPORTS_H */
