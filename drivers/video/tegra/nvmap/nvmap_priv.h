/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2009-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * GPU memory management driver for Tegra
 */

#ifndef __VIDEO_TEGRA_NVMAP_NVMAP_H
#define __VIDEO_TEGRA_NVMAP_NVMAP_H

#include <nvidia/conftest.h>

#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/rtmutex.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/dma-buf.h>
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/nvmap.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/version.h>

#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direction.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>

#include <asm/cacheflush.h>
#ifndef CONFIG_ARM64
#include <asm/outercache.h>
#endif
#include "nvmap_stats.h"

#include <linux/fdtable.h>

#define __DMA_ATTR(attrs) attrs
#define DEFINE_DMA_ATTRS(attrs) unsigned long attrs = 0

/**
 * dma_set_attr - set a specific attribute
 * @attr: attribute to set
 * @attrs: struct dma_attrs (may be NULL)
 */
#define dma_set_attr(attr, attrs) (attrs |= attr)

/**
 * dma_get_attr - check for a specific attribute
 * @attr: attribute to set
 * @attrs: struct dma_attrs (may be NULL)
 */
#define dma_get_attr(attr, attrs) (attrs & attr)

#define NVMAP_TAG_LABEL_MAXLEN	(63 - sizeof(struct nvmap_tag_entry))

#define NVMAP_TAG_TRACE(x, ...) 			\
do {                                                    \
	if (x##_enabled()) {                            \
		mutex_lock(&nvmap_dev->tags_lock);      \
		x(__VA_ARGS__);                         \
		mutex_unlock(&nvmap_dev->tags_lock);    \
	}                                               \
} while (0)

#define DMA_MEMORY_NOMAP		0x02

#define ACCESS_OK(type, addr, size)    access_ok(addr, size)
#define SYS_CLOSE(arg) close_fd(arg)

struct page;
struct nvmap_device;

extern bool nvmap_convert_iovmm_to_carveout;
extern bool nvmap_convert_carveout_to_iovmm;

extern struct vm_operations_struct nvmap_vma_ops;

#ifdef CONFIG_ARM64
#define PG_PROT_KERNEL PAGE_KERNEL
#else
#define PG_PROT_KERNEL pgprot_kernel
#endif

struct nvmap_vma_list {
	struct list_head list;
	struct vm_area_struct *vma;
	unsigned long save_vm_flags;
	pid_t pid;
	atomic_t ref;
};

#ifdef NVMAP_CONFIG_DEBUG_MAPS
struct nvmap_device_list {
	struct rb_node node;
	u64 dma_mask;
	char *device_name;
};
#endif /* NVMAP_CONFIG_DEBUG_MAPS */

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

struct nvmap_tag_entry {
	struct rb_node node;
	atomic_t ref;		/* reference count (i.e., # of duplications) */
	u32 tag;
};

#define NVMAP_IVM_INVALID_PEER		(-1)

struct nvmap_vma_priv {
	struct nvmap_handle *handle;
	size_t		offs;
	atomic_t	count;	/* number of processes cloning the VMA */
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
};

extern struct nvmap_device *nvmap_dev;
extern ulong nvmap_init_time;

static inline void nvmap_acquire_mmap_read_lock(struct mm_struct *mm)
{
	down_read(&mm->mmap_lock);
}

static inline void nvmap_release_mmap_read_lock(struct mm_struct *mm)
{
	up_read(&mm->mmap_lock);
}

struct nvmap_carveout_node;

void outer_cache_maint(unsigned int op, phys_addr_t paddr, size_t size);

int is_nvmap_vma(struct vm_area_struct *vma);

void *__nvmap_mmap(struct nvmap_handle *h);
void __nvmap_munmap(struct nvmap_handle *h, void *addr);

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

void nvmap_vma_open(struct vm_area_struct *vma);

struct nvmap_tag_entry *nvmap_search_tag_entry(struct rb_root *root, u32 tag);

int nvmap_define_tag(struct nvmap_device *dev, u32 tag,
	const char __user *name, u32 len);

int nvmap_remove_tag(struct nvmap_device *dev, u32 tag);

/* must hold tag_lock */
static inline char *__nvmap_tag_name(struct nvmap_device *dev, u32 tag)
{
	struct nvmap_tag_entry *entry;

	entry = nvmap_search_tag_entry(&dev->tags, tag);
	return entry ? (char *)(entry + 1) : "";
}

void *nvmap_dmabuf_get_drv_data(struct dma_buf *dmabuf,
		struct device *dev);

#ifdef NVMAP_CONFIG_DEBUG_MAPS
struct nvmap_device_list *nvmap_is_device_present(char *device_name, u32 heap_type);
void nvmap_add_device_name(char *device_name, u64 dma_mask, u32 heap_type);
void nvmap_remove_device_name(char *device_name, u32 heap_type);
#endif /* NVMAP_CONFIG_DEBUG_MAPS */

#endif /* __VIDEO_TEGRA_NVMAP_NVMAP_H */
