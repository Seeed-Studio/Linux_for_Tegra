/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2009-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __NVMAP_DEV_H
#define __NVMAP_DEV_H

#include <linux/miscdevice.h>

#define NVMAP_HEAP_IOVMM            (1ul << 30)
/* common carveout heaps */
#define NVMAP_HEAP_CARVEOUT_VPR     (1ul << 28)
#define NVMAP_HEAP_CARVEOUT_TSEC    (1ul << 27)
#define NVMAP_HEAP_CARVEOUT_VIDMEM  (1ul << 26)
#define NVMAP_HEAP_CARVEOUT_VI      (1ul << 4)
#define NVMAP_HEAP_CARVEOUT_GPU     (1ul << 3)
#define NVMAP_HEAP_CARVEOUT_FSI     (1ul << 2)
#define NVMAP_HEAP_CARVEOUT_IVM     (1ul << 1)
#define NVMAP_HEAP_CARVEOUT_GENERIC (1ul << 0)

#define NVMAP_HEAP_CARVEOUT_MASK    (NVMAP_HEAP_IOVMM - 1)

/* allocation flags */
#define NVMAP_HANDLE_UNCACHEABLE     (0x0ul << 0)
#define NVMAP_HANDLE_WRITE_COMBINE   (0x1ul << 0)
#define NVMAP_HANDLE_INNER_CACHEABLE (0x2ul << 0)
#define NVMAP_HANDLE_CACHEABLE       (0x3ul << 0)
#define NVMAP_HANDLE_CACHE_FLAG      (0x3ul << 0)

#define NVMAP_HANDLE_SECURE          (0x1ul << 2)
#define NVMAP_HANDLE_KIND_SPECIFIED  (0x1ul << 3)
#define NVMAP_HANDLE_COMPR_SPECIFIED (0x1ul << 4)
#define NVMAP_HANDLE_ZEROED_PAGES    (0x1ul << 5)
#define NVMAP_HANDLE_PHYS_CONTIG     (0x1ul << 6)
#define NVMAP_HANDLE_CACHE_SYNC      (0x1ul << 7)
#define NVMAP_HANDLE_CACHE_SYNC_AT_RESERVE      (0x1ul << 8)
#define NVMAP_HANDLE_RO	             (0x1ul << 9)

/*
 * A heap can be mapped to memory other than DRAM.
 * The HW, controls the memory, can be power gated/ungated
 * based upon the clients using the memory.
 * if no client/alloc happens from the memory, the HW needs
 * to be power gated. Similarly it should power ungated if
 * alloc happens from the memory.
 * int (*busy)(void) - trigger runtime power ungate
 * int (*idle)(void) - trigger runtime power gate
 */
struct nvmap_pm_ops {
	int (*busy)(void);
	int (*idle)(void);
};

struct nvmap_platform_carveout {
	const char *name;
	unsigned int usage_mask;
	phys_addr_t base;
	size_t size;
	struct device *cma_dev;
	bool resize;
	struct device *dma_dev;
	struct device dev;
	bool is_ivm;
	unsigned int peer;
	unsigned int vmid;
	int can_alloc;
	bool enable_static_dma_map;
	bool disable_dynamic_dma_map;
	bool no_cpu_access; /* carveout can't be accessed from cpu at all */
	bool init_done;	/* FIXME: remove once all caveouts use reserved-memory */
	struct nvmap_pm_ops pm_ops;
	int numa_node_id; /* NUMA node id from which the carveout is allocated from */
};

struct nvmap_platform_data {
	const struct nvmap_platform_carveout *carveouts;
	unsigned int nr_carveouts;
};

struct nvmap_pid_data {
	struct rb_node node;
	pid_t pid;
	struct kref refcount;
	struct dentry *handles_file;
};

struct nvmap_device {
	struct rb_root	handles;
	spinlock_t	handle_lock;
	struct miscdevice dev_user;
	struct nvmap_carveout_node **heaps;
	int nr_heaps;
	int nr_carveouts;
#ifdef NVMAP_CONFIG_PAGE_POOLS
	struct nvmap_page_pool *pool;
#endif
	struct list_head clients;
	struct rb_root pids;
	struct mutex	clients_lock;
	struct list_head lru_handles;
	spinlock_t	lru_lock;
	struct dentry *handles_by_pid;
	struct dentry *debug_root;
	struct nvmap_platform_data *plat;
	struct rb_root	tags;
	struct mutex	tags_lock;
	struct mutex carveout_lock; /* needed to serialize carveout creation */
	u32 dynamic_dma_map_mask;
	u32 cpu_access_mask;
#ifdef NVMAP_CONFIG_DEBUG_MAPS
	struct rb_root device_names;
#endif /* NVMAP_CONFIG_DEBUG_MAPS */
	u64 serial_id_counter; /* This is global counter common across different client processes */
	bool support_debug_features;
};

#define NVMAP_TAG_TRACE(x, ...) 			\
do {                                                    \
	if (x##_enabled()) {                            \
		mutex_lock(&nvmap_dev->tags_lock);      \
		x(__VA_ARGS__);                         \
		mutex_unlock(&nvmap_dev->tags_lock);    \
	}                                               \
} while (0)

bool is_nvmap_memory_available(size_t size, uint32_t heap, int numa_nid);

void kasan_memcpy_toio(void __iomem *to, const void *from,
			size_t count);

char *__nvmap_tag_name(struct nvmap_device *dev, u32 tag);

#ifdef NVMAP_CONFIG_DEBUG_MAPS
struct nvmap_device_list {
	struct rb_node node;
	u64 dma_mask;
	char *device_name;
};

struct nvmap_device_list *nvmap_is_device_present(char *device_name, u32 heap_type);
void nvmap_add_device_name(char *device_name, u64 dma_mask, u32 heap_type);
void nvmap_remove_device_name(char *device_name, u32 heap_type);
#endif /* NVMAP_CONFIG_DEBUG_MAPS */
#endif /* __NVMAP_DEV_H */
