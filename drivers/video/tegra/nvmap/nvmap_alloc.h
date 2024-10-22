/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __NVMAP_ALLOC_H
#define __NVMAP_ALLOC_H

#define DMA_MEMORY_NOMAP	0x02

/* bit 31-29: IVM peer
 * bit 28-16: offset (aligned to 32K)
 * bit 15-00: len (aligned to page_size)
 */
#define NVMAP_IVM_LENGTH_SHIFT (0)
#define NVMAP_IVM_LENGTH_WIDTH (16)
#define NVMAP_IVM_LENGTH_MASK  ((1 << NVMAP_IVM_LENGTH_WIDTH) - 1)
#define NVMAP_IVM_OFFSET_SHIFT (NVMAP_IVM_LENGTH_SHIFT + NVMAP_IVM_LENGTH_WIDTH)
#define NVMAP_IVM_OFFSET_WIDTH (13)
#define NVMAP_IVM_OFFSET_MASK  ((1 << NVMAP_IVM_OFFSET_WIDTH) - 1)
#define NVMAP_IVM_IVMID_SHIFT  (NVMAP_IVM_OFFSET_SHIFT + NVMAP_IVM_OFFSET_WIDTH)
#define NVMAP_IVM_IVMID_WIDTH  (3)
#define NVMAP_IVM_IVMID_MASK   ((1 << NVMAP_IVM_IVMID_WIDTH) - 1)
#define NVMAP_IVM_ALIGNMENT    (SZ_32K)
#define NVMAP_IVM_INVALID_PEER		(-1)

struct nvmap_heap;
struct debugfs_info;
struct nvmap_carveout_node;
struct nvmap_client;
struct nvmap_handle;

void *nvmap_altalloc(size_t len);

void nvmap_altfree(void *ptr, size_t len);

int nvmap_alloc_handle(struct nvmap_client *client,
		       struct nvmap_handle *h, unsigned int heap_mask,
		       size_t align, u8 kind,
		       unsigned int flags, unsigned int peer);

int nvmap_alloc_handle_from_va(struct nvmap_client *client,
			       struct nvmap_handle *h,
			       ulong addr,
			       unsigned int flags,
			       unsigned int heap_mask);

int nvmap_get_user_pages(ulong vaddr,
				size_t nr_page, struct page **pages,
				bool is_user_flags, u32 user_foll_flags);

phys_addr_t nvmap_alloc_get_co_base(struct nvmap_handle *h);

void nvmap_alloc_free(struct page **pages, unsigned int nr_page, bool from_va,
		      bool is_subhandle);

int __nvmap_cache_maint(struct nvmap_client *client,
			       struct nvmap_cache_op_64 *op);

int __nvmap_do_cache_maint(struct nvmap_client *client, struct nvmap_handle *h,
			   unsigned long start, unsigned long end,
			   unsigned int op, bool clean_only_dirty);

void inner_cache_maint(unsigned int op, void *vaddr, size_t size);

struct nvmap_heap_block *nvmap_carveout_alloc(struct nvmap_client *dev,
					      struct nvmap_handle *handle,
					      unsigned long type,
					      phys_addr_t *start);

int nvmap_create_carveout(const struct nvmap_platform_carveout *co);

int nvmap_query_heap_peer(struct nvmap_carveout_node *co_heap, unsigned int *peer);

size_t nvmap_query_heap_size(struct nvmap_heap *heap);

int nvmap_query_heap(struct nvmap_query_heap_params *op, bool is_numa_aware);

struct nvmap_heap *nvmap_block_to_heap(struct nvmap_heap_block *b);

void nvmap_heap_free(struct nvmap_heap_block *block);

void nvmap_heap_destroy(struct nvmap_heap *heap);

int system_heap_free_mem(unsigned long *mem_val);

int __init nvmap_heap_init(void);

void nvmap_heap_deinit(void);

int nvmap_dma_declare_coherent_memory(struct device *dev, phys_addr_t phys_addr,
			dma_addr_t device_addr, size_t size, int flags);

struct page **nvmap_pages(struct page **pg_pages, u32 nr_pages);

struct page *nvmap_to_page(struct page *page);

#ifdef NVMAP_CONFIG_PAGE_POOLS
int nvmap_page_pool_clear(void);

int nvmap_page_pool_debugfs_init(struct dentry *nvmap_root);

int nvmap_page_pool_init(struct nvmap_device *dev);

int nvmap_page_pool_fini(struct nvmap_device *dev);
#endif /* NVMAP_CONFIG_PAGE_POOLS */

/* helper functions for nvmap_heap struct */
size_t nvmap_get_heap_free_size(struct nvmap_heap *heap);

int nvmap_get_heap_nid(struct nvmap_heap *heap);

#ifdef NVMAP_CONFIG_DEBUG_MAPS
struct rb_root *nvmap_heap_get_device_ptr(struct nvmap_heap *heap);
#endif /* NVMAP_CONFIG_DEBUG_MAPS */

/* helper functions for nvmap_heap_block struct */
phys_addr_t nvmap_get_heap_block_base(struct nvmap_heap_block *block);

void nvmap_set_heap_block_handle(struct nvmap_heap_block *block, struct nvmap_handle *handle);

/* helper functions for debugfs_info struct */
unsigned int nvmap_get_debug_info_heap(struct debugfs_info *info);

int nvmap_get_debug_info_nid(struct debugfs_info *info);

struct debugfs_info *nvmap_create_debugfs_info(void);

void nvmap_free_debugfs_info(struct debugfs_info *info);

void nvmap_set_debugfs_heap(struct debugfs_info *info, unsigned int heap_bit);

void nvmap_set_debugfs_numa(struct debugfs_info *info, int nid);

/* helper functions for nvmap_carveout_node struct */
unsigned int nvmap_get_heap_bit(struct nvmap_carveout_node *co_heap);

struct nvmap_heap *nvmap_get_heap_ptr(struct nvmap_carveout_node *co_heap);

struct rb_root *nvmap_get_device_names(struct nvmap_carveout_node *co_heap);

static inline bool nvmap_page_dirty(struct page *page)
{
	return (unsigned long)page & 1UL;
}

static inline bool nvmap_page_mkdirty(struct page **page)
{
	if (nvmap_page_dirty(*page))
		return false;
	*page = (struct page *)((unsigned long)*page | 1UL);
	return true;
}

static inline bool nvmap_page_mkclean(struct page **page)
{
	if (!nvmap_page_dirty(*page))
		return false;
	*page = (struct page *)((unsigned long)*page & ~1UL);
	return true;
}

static inline void nvmap_acquire_mmap_read_lock(struct mm_struct *mm)
{
	down_read(&mm->mmap_lock);
}

static inline void nvmap_release_mmap_read_lock(struct mm_struct *mm)
{
	up_read(&mm->mmap_lock);
}
#endif /* __NVMAP_ALLOC_H */
