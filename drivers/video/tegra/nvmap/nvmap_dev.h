/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2009-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __NVMAP_DEV_H
#define __NVMAP_DEV_H
#define NVMAP_HEAP_IOVMM   (1ul<<30)

/* common carveout heaps */
#define NVMAP_HEAP_CARVEOUT_VPR     (1ul<<28)
#define NVMAP_HEAP_CARVEOUT_TSEC    (1ul<<27)
#define NVMAP_HEAP_CARVEOUT_VIDMEM  (1ul<<26)
#define NVMAP_HEAP_CARVEOUT_GPU (1ul << 3)
#define NVMAP_HEAP_CARVEOUT_FSI   (1ul<<2)
#define NVMAP_HEAP_CARVEOUT_IVM     (1ul<<1)
#define NVMAP_HEAP_CARVEOUT_GENERIC (1ul<<0)

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

bool is_nvmap_memory_available(size_t size, uint32_t heap, int numa_nid);

void kasan_memcpy_toio(void __iomem *to, const void *from,
			size_t count);

#endif /* __NVMAP_DEV_H */
