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

#define SIZE_2MB 0x200000
#define ALIGN_2MB(size) ((size + SIZE_2MB - 1) & ~(SIZE_2MB - 1))

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

#define NVMAP_TP_ARGS_H(handle)					      	      \
	handle,								      \
	atomic_read(&handle->share_count),				      \
	handle->heap_type == NVMAP_HEAP_IOVMM ? 0 : 			      \
			(handle->carveout ? nvmap_get_heap_block_base(handle->carveout) : 0),      \
	handle->size,							      \
	(handle->userflags & 0xFFFF),                                         \
	(handle->userflags >> 16),					      \
	__nvmap_tag_name(nvmap_dev, handle->userflags >> 16)

#define NVMAP_TP_ARGS_CHR(client, handle, ref)			      	      \
	client,                                                               \
	client ? nvmap_client_pid((struct nvmap_client *)client) : 0,         \
	(ref) ? atomic_read(&((struct nvmap_handle_ref *)ref)->dupes) : 1,    \
	NVMAP_TP_ARGS_H(handle)

#define NVMAP_TAG_TRACE(x, ...) 			\
do {                                                    \
	if (x##_enabled()) {                            \
		mutex_lock(&nvmap_dev->tags_lock);      \
		x(__VA_ARGS__);                         \
		mutex_unlock(&nvmap_dev->tags_lock);    \
	}                                               \
} while (0)

/*
 * DMA_ATTR_ALLOC_EXACT_SIZE: This tells the DMA-mapping
 * subsystem to allocate the exact number of pages
 */
#define DMA_ATTR_ALLOC_EXACT_SIZE	(DMA_ATTR_PRIVILEGED << 2)

#define DMA_MEMORY_NOMAP		0x02

#define DMA_ALLOC_FREE_ATTR	DMA_ATTR_ALLOC_SINGLE_PAGES
#define ACCESS_OK(type, addr, size)    access_ok(addr, size)
#define SYS_CLOSE(arg) close_fd(arg)

struct page;
struct nvmap_device;

/* holds max number of handles allocted per process at any time */
extern u32 nvmap_max_handle_count;

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

struct nvmap_carveout_node {
	unsigned int		heap_bit;
	struct nvmap_heap	*carveout;
	int			index;
	phys_addr_t		base;
	size_t			size;
};

/* handles allocated as collection of pages */
struct nvmap_pgalloc {
	struct page **pages;
	bool contig;			/* contiguous system memory */
	atomic_t reserved;
	atomic_t ndirty;	/* count number of dirty pages */
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

struct nvmap_handle_dmabuf_priv {
	void *priv;
	struct device *dev;
	void (*priv_release)(void *priv);
	struct list_head list;
};

struct nvmap_tag_entry {
	struct rb_node node;
	atomic_t ref;		/* reference count (i.e., # of duplications) */
	u32 tag;
};

#define NVMAP_IVM_INVALID_PEER		(-1)

struct nvmap_client {
	const char			*name;
	struct rb_root			handle_refs;
	struct mutex			ref_lock;
	bool				kernel_client;
	atomic_t			count;
	struct task_struct		*task;
	struct list_head		list;
	u32				handle_count;
	u32				next_fd;
	int				warned;
	int				tag_warned;
	struct xarray			id_array;
	struct xarray			*ida;
};

struct nvmap_vma_priv {
	struct nvmap_handle *handle;
	size_t		offs;
	atomic_t	count;	/* number of processes cloning the VMA */
};

struct nvmap_device {
	struct rb_root	handles;
	spinlock_t	handle_lock;
	struct miscdevice dev_user;
	struct nvmap_carveout_node *heaps;
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

static inline void nvmap_ref_lock(struct nvmap_client *priv)
{
	mutex_lock(&priv->ref_lock);
}

static inline void nvmap_ref_unlock(struct nvmap_client *priv)
{
	mutex_unlock(&priv->ref_lock);
}

static inline void nvmap_acquire_mmap_read_lock(struct mm_struct *mm)
{
	down_read(&mm->mmap_lock);
}

static inline void nvmap_release_mmap_read_lock(struct mm_struct *mm)
{
	up_read(&mm->mmap_lock);
}

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

int nvmap_dma_declare_coherent_memory(struct device *dev, phys_addr_t phys_addr,
			dma_addr_t device_addr, size_t size, int flags);
int nvmap_probe(struct platform_device *pdev);
int nvmap_remove(struct platform_device *pdev);
int nvmap_init(struct platform_device *pdev);

int nvmap_co_setup(struct reserved_mem *rmem);

struct nvmap_carveout_node;

struct nvmap_handle *nvmap_handle_get(struct nvmap_handle *h);
void nvmap_handle_put(struct nvmap_handle *h);

void outer_cache_maint(unsigned int op, phys_addr_t paddr, size_t size);

int is_nvmap_vma(struct vm_area_struct *vma);

struct nvmap_handle *nvmap_handle_get_from_fd(int fd);

/* MM definitions. */
extern void v7_flush_kern_cache_all(void);
extern void v7_clean_kern_cache_all(void *);

void nvmap_flush_cache(struct page **pages, int numpages);

struct sg_table *__nvmap_sg_table(struct nvmap_client *client,
				  struct nvmap_handle *h);
void __nvmap_free_sg_table(struct nvmap_client *client,
			   struct nvmap_handle *h, struct sg_table *sgt);
void *__nvmap_mmap(struct nvmap_handle *h);
void __nvmap_munmap(struct nvmap_handle *h, void *addr);
struct nvmap_client *__nvmap_create_client(struct nvmap_device *dev,
					   const char *name);

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

void nvmap_zap_handle(struct nvmap_handle *handle, u64 offset, u64 size);

void nvmap_vma_open(struct vm_area_struct *vma);

int nvmap_reserve_pages(struct nvmap_handle **handles, u64 *offsets,
			u64 *sizes, u32 nr, u32 op, bool is_32);

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
static inline pid_t nvmap_client_pid(struct nvmap_client *client)
{
	return client->task ? client->task->pid : 0;
}

/* must be called with mmap_sem held for read or write */
static inline int nvmap_get_user_pages(ulong vaddr,
				size_t nr_page, struct page **pages,
				bool is_user_flags, u32 user_foll_flags)
{
	u32 foll_flags = FOLL_FORCE;
	struct vm_area_struct *vma;
	vm_flags_t vm_flags;
	long user_pages = 0;
	int ret = 0;

	vma = find_vma(current->mm, vaddr);
	if (vma) {
		if (is_user_flags) {
			foll_flags |= user_foll_flags;
		} else {
			vm_flags = vma->vm_flags;
			/*
			 * If the vaddr points to writable page then only
			 * pass FOLL_WRITE flag
			 */
			if (vm_flags & VM_WRITE)
				foll_flags |= FOLL_WRITE;
		}
		pr_debug("vaddr %lu is_user_flags %d user_foll_flags %x foll_flags %x.\n",
			vaddr, is_user_flags?1:0, user_foll_flags, foll_flags);
#if defined(NV_GET_USER_PAGES_HAS_ARGS_FLAGS) /* Linux v6.5 */
		user_pages = get_user_pages(vaddr & PAGE_MASK, nr_page,
					    foll_flags, pages);
#else
		user_pages = get_user_pages(vaddr & PAGE_MASK, nr_page,
					    foll_flags, pages, NULL);
#endif
	}
	if (user_pages != nr_page) {
		ret = user_pages < 0 ? user_pages : -ENOMEM;
		pr_err("get_user_pages requested/got: %zu/%ld]\n", nr_page,
				user_pages);
		while (--user_pages >= 0)
			put_page(pages[user_pages]);
	}
	return ret;
}

#define device_node_from_iter(iter) \
	iter.node

extern struct of_device_id __nvmapcache_of_table;

#define NVMAP_CACHE_OF_DECLARE(compat, fn) \
	_OF_DECLARE(nvmapcache, nvmapcache_of, compat, fn, \
			nvmap_setup_chip_cache_fn)

void *nvmap_dmabuf_get_drv_data(struct dma_buf *dmabuf,
		struct device *dev);
bool is_nvmap_memory_available(size_t size, uint32_t heap, int numa_nid);
int system_heap_free_mem(unsigned long *mem_val);

#ifdef NVMAP_CONFIG_DEBUG_MAPS
struct nvmap_device_list *nvmap_is_device_present(char *device_name, u32 heap_type);
void nvmap_add_device_name(char *device_name, u64 dma_mask, u32 heap_type);
void nvmap_remove_device_name(char *device_name, u32 heap_type);
#endif /* NVMAP_CONFIG_DEBUG_MAPS */

struct nvmap_handle *nvmap_handle_get_from_id(struct nvmap_client *client,
		u32 id);

#ifdef CONFIG_TEGRA_VIRTUALIZATION
void *nvmap_dma_mark_declared_memory_occupied(struct device *dev,
					dma_addr_t device_addr, size_t size);
void nvmap_dma_mark_declared_memory_unoccupied(struct device *dev,
					 dma_addr_t device_addr, size_t size);
#endif /* CONFIG_TEGRA_VIRTUALIZATION */

void nvmap_dma_release_coherent_memory(struct dma_coherent_mem_replica *mem);
#endif /* __VIDEO_TEGRA_NVMAP_NVMAP_H */
