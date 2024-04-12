/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2024, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __NVMAP_EXPORTS_H
#define __NVMAP_EXPORTS_H

void *nvmap_dma_alloc_attrs(struct device *dev, size_t size,
			    dma_addr_t *dma_handle, gfp_t flag,
			    unsigned long attrs);
void nvmap_dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
			  dma_addr_t dma_handle, unsigned long attrs);
extern struct device tegra_vpr_dev;
extern struct device tegra_vpr_dev1;
#endif /* __NVMAP_EXPORTS_H */
