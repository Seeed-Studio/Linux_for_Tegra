// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024, NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#define pr_fmt(fmt)	"nvscic2c-pcie: iova-alloc: " fmt
#include <linux/iommu.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "iova-alloc.h"

struct iova_alloc_domain_t {
	struct device *dev;
	struct iommu_domain *domain;
	struct iova_domain iovad;
};

static int
iovad_init(struct device *dev, struct iova_alloc_domain_t **ivd_h)
{
	int ret = 0;
	dma_addr_t start = 0;
	unsigned long order = 0;
	struct iova_alloc_domain_t *ivd_ctx = NULL;
	struct iommu_domain_geometry *geometry = NULL;

	ivd_ctx = kzalloc(sizeof(*ivd_ctx), GFP_KERNEL);
	if (WARN_ON(!ivd_ctx))
		return -ENOMEM;

	ivd_ctx->dev = dev;
	ret = iova_cache_get();
	if (ret < 0)
		goto free_ivd;

	ivd_ctx->domain = iommu_get_domain_for_dev(dev);
	if (!ivd_ctx->domain) {
		ret = -EINVAL;
		pr_err("iommu_get_domain_for_dev() failed.\n");
		goto put_cache;
	}

	geometry = &ivd_ctx->domain->geometry;
	start = geometry->aperture_start & dev->coherent_dma_mask;
	order = __ffs(ivd_ctx->domain->pgsize_bitmap);
	pr_debug("Order of address allocation for IOVA domain: %lu\n", order);
	init_iova_domain(&ivd_ctx->iovad, 1UL << order, start >> order);

	*ivd_h = ivd_ctx;

	return ret;
put_cache:
	iova_cache_put();
free_ivd:
	kfree(ivd_ctx);
	return ret;
}

static void
iovad_deinit(struct iova_alloc_domain_t **ivd_h)
{
	struct iova_alloc_domain_t *ivd_ctx = NULL;

	ivd_ctx = (struct iova_alloc_domain_t *)(*ivd_h);
	put_iova_domain(&ivd_ctx->iovad);
	iova_cache_put();
	kfree(ivd_ctx);
	*ivd_h = NULL;
}

int
iova_alloc_init(struct device *dev, size_t size, dma_addr_t *dma_handle,
		struct iova_alloc_domain_t **ivd_h)
{
	int ret = 0;
	unsigned long shift = 0U;
	unsigned long iova = 0U;
	unsigned long iova_len = 0U;
	dma_addr_t dma_limit = 0x0;
	struct iova_alloc_domain_t *ivd_ctx = NULL;
	struct iommu_domain_geometry *geometry = NULL;

	if (WARN_ON(!dev || !dma_handle || !ivd_h || *ivd_h))
		return -EINVAL;

	ret = iovad_init(dev, &ivd_ctx);
	if (ret < 0) {
		pr_err("Failed in allocating IOVA domain: %d\n", ret);
		return ret;
	}

	geometry = &ivd_ctx->domain->geometry;
	dma_limit = ivd_ctx->dev->coherent_dma_mask;
	shift = iova_shift(&ivd_ctx->iovad);
	iova_len = size >> shift;

	/* Recommendation is to allocate in power of 2.*/
	if (iova_len < (1U << (IOVA_RANGE_CACHE_MAX_SIZE - 1U)))
		iova_len = roundup_pow_of_two(iova_len);

	if (*ivd_ctx->dev->dma_mask)
		dma_limit &= *ivd_ctx->dev->dma_mask;

	if (geometry->force_aperture)
		dma_limit = min(dma_limit, geometry->aperture_end);

	/* Try to get PCI devices a SAC address */
	if (dma_limit > DMA_BIT_MASK(32) && dev_is_pci(ivd_ctx->dev))
		iova = alloc_iova_fast(&ivd_ctx->iovad, iova_len,
				       DMA_BIT_MASK(32) >> shift, false);
	if (!iova)
		iova = alloc_iova_fast(&ivd_ctx->iovad, iova_len,
				       dma_limit >> shift, true);

	*dma_handle = (dma_addr_t)iova << shift;
	*ivd_h = ivd_ctx;

	return ret;
}

void
iova_alloc_deinit(dma_addr_t dma_handle, size_t size,
		  struct iova_alloc_domain_t **ivd_h)
{
	struct iova_domain *iovad = NULL;
	struct iova_alloc_domain_t *ivd_ctx = NULL;

	if (!ivd_h || !(*ivd_h) || !dma_handle)
		return;

	ivd_ctx = *ivd_h;
	iovad = &ivd_ctx->iovad;

	free_iova_fast(iovad, iova_pfn(iovad, dma_handle),
		       size >> iova_shift(iovad));

	iovad_deinit(ivd_h);
}
