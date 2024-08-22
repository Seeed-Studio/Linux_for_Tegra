/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2010-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#endif
