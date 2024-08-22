/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __NVMAP_ALLOC_H
#define __NVMAP_ALLOC_H

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

void _nvmap_handle_free(struct nvmap_handle *h);

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

int nvmap_query_heap_peer(struct nvmap_heap *heap, unsigned int *peer);

size_t nvmap_query_heap_size(struct nvmap_heap *heap);

struct nvmap_heap *nvmap_block_to_heap(struct nvmap_heap_block *b);

void nvmap_heap_free(struct nvmap_heap_block *block);

void nvmap_heap_destroy(struct nvmap_heap *heap);

int __init nvmap_heap_init(void);

void nvmap_heap_deinit(void);

struct page **nvmap_pages(struct page **pg_pages, u32 nr_pages);

struct page *nvmap_to_page(struct page *page);

#ifdef NVMAP_CONFIG_PAGE_POOLS
int nvmap_page_pool_clear(void);

int nvmap_page_pool_debugfs_init(struct dentry *nvmap_root);

int nvmap_page_pool_init(struct nvmap_device *dev);

int nvmap_page_pool_fini(struct nvmap_device *dev);
#endif /* NVMAP_CONFIG_PAGE_POOLS */
#endif /* __NVMAP_ALLOC_H */
