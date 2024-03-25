/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2024, NVIDIA Corporation. All rights reserved.
 *
 * GPU heap allocator.
 */

#ifndef __NVMAP_HEAP_H
#define __NVMAP_HEAP_H

struct device;
struct nvmap_heap;
struct nvmap_client;

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
	bool is_gpu_co;
	u32 granule_size;
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

struct nvmap_heap *nvmap_heap_create(struct device *parent,
				     const struct nvmap_platform_carveout *co,
				     phys_addr_t base, size_t len, void *arg);

void nvmap_heap_destroy(struct nvmap_heap *heap);

struct nvmap_heap_block *nvmap_heap_alloc(struct nvmap_heap *heap,
					  struct nvmap_handle *handle,
					  phys_addr_t *start);

struct nvmap_heap *nvmap_block_to_heap(struct nvmap_heap_block *b);

void nvmap_heap_free(struct nvmap_heap_block *block);

int __init nvmap_heap_init(void);

void nvmap_heap_deinit(void);

#ifndef NVMAP_CONFIG_CACHE_FLUSH_AT_ALLOC
int nvmap_flush_heap_block(struct nvmap_client *client,
	struct nvmap_heap_block *block, size_t len, unsigned int prot);
#endif /* !NVMAP_CONFIG_CACHE_FLUSH_AT_ALLOC */

void nvmap_heap_debugfs_init(struct dentry *heap_root, struct nvmap_heap *heap);

int nvmap_query_heap_peer(struct nvmap_heap *heap, unsigned int *peer);
size_t nvmap_query_heap_size(struct nvmap_heap *heap);

#endif
