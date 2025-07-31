/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __NVMAP_ALLOC_INT_H
#define __NVMAP_ALLOC_INT_H

#define SIZE_2MB 0x200000

#define DMA_ERROR_CODE	(~(dma_addr_t)0)

#define GFP_NVMAP       (GFP_KERNEL | __GFP_HIGHMEM | __GFP_NOWARN | __GFP_ACCOUNT | __GFP_NORETRY)

#ifdef CONFIG_ARM64_4K_PAGES
#define NVMAP_PP_BIG_PAGE_SIZE           (0x10000)
#endif /* CONFIG_ARM64_4K_PAGES */

struct dma_coherent_mem_replica {
	void		*virt_base;
	dma_addr_t	device_base;
	unsigned long	pfn_base;
	int		size;
	int		flags;
	unsigned long	*bitmap;
	spinlock_t	spinlock;
	bool		use_dev_dma_pfn_offset;
};

union dma_coherent_mem_block {
	struct dma_coherent_mem *dma_mem;
	struct dma_coherent_mem_replica *mem;
};

struct nvmap_heap_block {
	phys_addr_t	base;
	unsigned int	type;
	struct nvmap_handle *handle;
};

/*
 * Info to be passed to debugfs nodes, so as to provide heap type and
 * numa node id.
 */
struct debugfs_info {
	unsigned int heap_bit;
	int numa_id;
};

struct nvmap_heap {
	struct list_head all_list;
	struct mutex lock;
	const char *name;
	void *arg;
	/* heap base */
	phys_addr_t base;
	/* heap size */
	size_t len;
	size_t free_size;
	struct device *cma_dev;
	struct device *dma_dev;
	bool is_ivm;
	int numa_node_id;
	bool can_alloc; /* Used only if is_ivm == true */
	unsigned int peer; /* Used only if is_ivm == true */
	unsigned int vm_id; /* Used only if is_ivm == true */
	struct nvmap_pm_ops pm_ops;
#ifdef NVMAP_CONFIG_DEBUG_MAPS
	struct rb_root device_names;
#endif /* NVMAP_CONFIG_DEBUG_MAPS */
	struct debugfs_info *carevout_debugfs_info; /* Used for storing debugfs info */
};

struct nvmap_carveout_node {
	unsigned int		heap_bit;
	struct nvmap_heap	*carveout;
	int			index;
	phys_addr_t		base;
	size_t			size;
};

struct list_block {
	struct nvmap_heap_block block;
	struct list_head all_list;
	unsigned int mem_prot;
	phys_addr_t orig_addr;
	size_t size;
	size_t align;
	struct nvmap_heap *heap;
	struct list_head free_list;
};


struct nvmap_page_pool {
	struct rt_mutex lock;
	u32 count;      /* Number of pages in the page & dirty list. */
	u32 max;        /* Max no. of pages in all lists. */
	u32 to_zero;    /* Number of pages on the zero list */
	u32 under_zero; /* Number of pages getting zeroed */
#ifdef CONFIG_ARM64_4K_PAGES
	u32 big_pg_sz;  /* big page size supported(64k, etc.) */
	u32 big_page_count;   /* Number of zeroed big pages avaialble */
	u32 pages_per_big_pg; /* Number of pages in big page */
#endif /* CONFIG_ARM64_4K_PAGES */
	struct list_head page_list;
	struct list_head zero_list;
#ifdef CONFIG_ARM64_4K_PAGES
	struct list_head page_list_bp;
#endif /* CONFIG_ARM64_4K_PAGES */

#ifdef NVMAP_CONFIG_PAGE_POOL_DEBUG
	u64 allocs;
	u64 fills;
	u64 hits;
	u64 misses;
#endif
};

int nvmap_cache_maint_phys_range(unsigned int op, phys_addr_t pstart,
		phys_addr_t pend, int inner, int outer);

void nvmap_clean_cache(struct page **pages, int numpages);

void nvmap_clean_cache_page(struct page *page);

void __dma_map_area_from_device(const void *cpu_va, size_t size);
void __dma_map_area_to_device(const void *cpu_va, size_t size);

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
