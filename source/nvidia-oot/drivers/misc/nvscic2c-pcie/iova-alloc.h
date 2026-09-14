/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef __IOVA_ALLOC_H__
#define __IOVA_ALLOC_H__

#include <linux/device.h>
#include <linux/iova.h>

struct iova_alloc_domain_t;

/*
 * iova_alloc_init
 *
 * With NvSciC2c usecase IOVA range needs to be allocated without
 * physical backing. Existing DMA API framework does not allow this.
 * Hence allocate new IOVA domain to allocate IOVA range.
 *
 * Allocate IOVA range using new IOVA domain.
 * Use this IOVA range in iommu_map() with existing IOMMU domain.
 */
int
iova_alloc_init(struct device *dev, size_t size, dma_addr_t *dma_handle,
		struct iova_alloc_domain_t **ivd_h);

/*
 * iova_alloc_deinit
 *
 * Free IOVA range allocated using iova_alloc_init.
 * Client needs to make sure that if physical mapping was created
 * then it is released before calling iova_alloc_deinit.

 * Release IOVA domain allocated in iova_alloc_init.
 */
void
iova_alloc_deinit(dma_addr_t dma_handle, size_t size,
		  struct iova_alloc_domain_t **ivd_h);

#ifndef IOVA_RANGE_CACHE_MAX_SIZE
#define IOVA_RANGE_CACHE_MAX_SIZE 6U /* log of max cached IOVA range size (in pages) */
#endif

#endif //__IOVA_ALLOC_H__
