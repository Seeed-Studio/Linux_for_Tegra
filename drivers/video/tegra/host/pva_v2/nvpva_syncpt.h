/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef __NVPVA_SYNCPT_H__
#define __NVPVA_SYNCPT_H__

void nvpva_syncpt_put_ref_ext(struct platform_device *pdev,
			      u32 id);
dma_addr_t nvpva_syncpt_address(struct platform_device *pdev, u32 id,
				bool rw);
void nvpva_syncpt_unit_interface_deinit(struct platform_device *pdev,
					struct platform_device *paux_dev);
int nvpva_syncpt_unit_interface_init(struct platform_device *pdev,
				     struct platform_device *paux_dev);
u32 nvpva_get_syncpt_client_managed(struct platform_device *pdev,
				    const char *syncpt_name);
int nvpva_map_region(struct device *dev,
		     phys_addr_t start,
		     size_t size,
		     dma_addr_t *sp_start,
		     u32 attr);
int nvpva_unmap_region(struct device *dev,
		       dma_addr_t addr,
		       size_t size,
		       u32 attr);
#endif
