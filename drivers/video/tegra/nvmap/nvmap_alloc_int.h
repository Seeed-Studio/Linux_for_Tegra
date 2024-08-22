/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __NVMAP_ALLOC_INT_H
#define __NVMAP_ALLOC_INT_H

int nvmap_cache_maint_phys_range(unsigned int op, phys_addr_t pstart,
		phys_addr_t pend, int inner, int outer);

void nvmap_clean_cache(struct page **pages, int numpages);

void nvmap_clean_cache_page(struct page *page);

void __dma_map_area(const void *cpu_va, size_t size, int dir);

void nvmap_heap_debugfs_init(struct dentry *heap_root, struct nvmap_heap *heap);

struct nvmap_heap_block *nvmap_heap_alloc(struct nvmap_heap *heap,
					  struct nvmap_handle *handle,
					  phys_addr_t *start);

struct nvmap_heap *nvmap_heap_create(struct device *parent,
				     const struct nvmap_platform_carveout *co,
				     phys_addr_t base, size_t len, void *arg);

#ifdef NVMAP_CONFIG_PAGE_POOLS
int nvmap_page_pool_alloc_lots(struct nvmap_page_pool *pool,
		struct page **pages, u32 nr, bool use_numa, int numa_id);

#ifdef CONFIG_ARM64_4K_PAGES
int nvmap_page_pool_alloc_lots_bp(struct nvmap_page_pool *pool,
		struct page **pages, u32 nr, bool use_numa, int numa_id);
#endif /* CONFIG_ARM64_4K_PAGES */

u32 nvmap_page_pool_fill_lots(struct nvmap_page_pool *pool,
				       struct page **pages, u32 nr);
#endif /* NVMAP_CONFIG_PAGE_POOLS */
#endif /* __NVMAP_ALLOC_INT_H */
