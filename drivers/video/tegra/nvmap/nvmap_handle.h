/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2009-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * GPU memory management driver for Tegra
 */

#ifndef _NVMAP_HANDLE_H_
#define _NVMAP_HANDLE_H_

#include <linux/nvscierror.h>
#include <linux/nvsciipc_interface.h>

extern struct nvmap_device *nvmap_dev;

/* handles allocated as collection of pages */
struct nvmap_pgalloc {
	struct page **pages;
	bool contig;		/* contiguous system memory */
	atomic_t reserved;
	atomic_t ndirty;	/* count number of dirty pages */
};

struct nvmap_handle {
	struct rb_node node;	/* entry on global handle tree */
	atomic_t ref;		/* reference count (i.e., # of duplications) */
	atomic_t pin;		/* pin count */
	u32 flags;		/* caching flags */
	size_t size;		/* padded (as-allocated) size */
	size_t orig_size;	/* original (as-requested) size */
	size_t align;
	struct nvmap_client *owner;
	struct dma_buf *dmabuf;
	struct dma_buf *dmabuf_ro;
	union {
		struct nvmap_pgalloc pgalloc;
		struct nvmap_heap_block *carveout;
	};
	bool heap_pgalloc;	/* handle is page allocated (sysmem / iovmm) */
	bool alloc;		/* handle has memory allocated */
	bool from_va;		/* handle memory is from VA */
	u32 heap_type;		/* handle heap is allocated from */
	u32 userflags;		/* flags passed from userspace */
	void *vaddr;		/* mapping used inside kernel */
	struct list_head vmas;	/* list of all user vma's */
	atomic_t umap_count;	/* number of outstanding maps from user */
	atomic_t kmap_count;	/* number of outstanding map from kernel */
	atomic_t share_count;	/* number of processes sharing the handle */
	struct list_head lru;	/* list head to track the lru */
	struct mutex lock;
	struct list_head dmabuf_priv;
	u64 ivm_id;
	unsigned int peer;	/* Peer VM number */
	int offs;		/* Offset in IVM mem pool */
	/*
	 * To be set only in handle created from VA case if the handle is
	 * read-only.
	 */
	bool is_ro;

	/* list node in case this handle's pages are referenced */
	struct list_head pg_ref;
	/* list of all the handles whose
	 * pages are refernced in this handle
	 */
	struct list_head pg_ref_h;
	struct mutex pg_ref_h_lock;
	bool is_subhandle;
	/*
	 * waitq to wait on RO dmabuf release completion, if release is already in progress.
	 */
	wait_queue_head_t waitq;
	int numa_id;
	u64 serial_id;
	bool has_hugetlbfs_pages;
};

struct nvmap_handle_info {
	struct nvmap_handle *handle;
	struct list_head maps;
	struct mutex maps_lock;
	bool is_ro;
};

/* handle_ref objects are client-local references to an nvmap_handle;
 * they are distinct objects so that handles can be unpinned and
 * unreferenced the correct number of times when a client abnormally
 * terminates */
struct nvmap_handle_ref {
	struct nvmap_handle *handle;
	struct rb_node	node;
	atomic_t	dupes;	/* number of times to free on file close */
	bool is_ro;
};

struct handles_range {
	u32 start; /* start handle no where buffer range starts */
	u32 end;   /* end handle no where buffer range ends */
	u64 offs_start; /* keep track of intermediate offset */
	u64 offs; /* user passed offset */
	u64 sz; /* user passed size */
};

struct nvmap_handle_dmabuf_priv {
	void *priv;
	struct device *dev;
	void (*priv_release)(void *priv);
	struct list_head list;
};

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

static inline void nvmap_ref_lock(struct nvmap_client *priv)
{
	mutex_lock(&priv->ref_lock);
}

static inline void nvmap_ref_unlock(struct nvmap_client *priv)
{
	mutex_unlock(&priv->ref_lock);
}

static inline pid_t nvmap_client_pid(struct nvmap_client *client)
{
	return client->task ? client->task->pid : 0;
}

static inline pgprot_t nvmap_pgprot(struct nvmap_handle *h, pgprot_t prot)
{
	if (h->flags == NVMAP_HANDLE_UNCACHEABLE) {
#ifdef CONFIG_ARM64
		if (h->heap_type != NVMAP_HEAP_CARVEOUT_VPR &&
		    h->owner && !h->owner->warned) {
			char task_comm[TASK_COMM_LEN];
			h->owner->warned = 1;
			get_task_comm(task_comm, h->owner->task);
			pr_err("PID %d: %s: TAG: 0x%04x WARNING: "
				"NVMAP_HANDLE_WRITE_COMBINE "
				"should be used in place of "
				"NVMAP_HANDLE_UNCACHEABLE on ARM64\n",
				h->owner->task->pid, task_comm,
				h->userflags >> 16);
		}
#endif
		return pgprot_noncached(prot);
	} else if (h->flags == NVMAP_HANDLE_WRITE_COMBINE) {
		return pgprot_writecombine(prot);
	} else {
		/* Do nothing */
	}
	return prot;
}

static inline bool nvmap_page_mkdirty(struct page **page);
static inline bool nvmap_page_mkclean(struct page **page);

/*
 * FIXME: assume user space requests for reserve operations
 * are page aligned
 */
static inline int nvmap_handle_mk(struct nvmap_handle *h,
				  u32 offset, u32 size,
				  bool (*fn)(struct page **),
				  bool locked)
{
	int i, nchanged = 0;
	u32 start_page = offset >> PAGE_SHIFT;
	u32 end_page;
	u32 offset_size_sum;
	int nchanged_inc;

	if (check_add_overflow(offset, size, &offset_size_sum))
		return 0;

	end_page = PAGE_ALIGN(offset_size_sum) >> PAGE_SHIFT;

	if (!locked)
		mutex_lock(&h->lock);

	if (h->heap_pgalloc &&
		(offset < h->size) &&
		(size <= h->size) &&
		(offset <= (h->size - size))) {
		for (i = start_page; i < end_page; i++) {
			if (fn(&h->pgalloc.pages[i])) {
				if (check_add_overflow(nchanged, 1, &nchanged_inc))
					return 0;

				nchanged = nchanged_inc;
			}
		}
	}

	if (!locked)
		mutex_unlock(&h->lock);

	return nchanged;
}

static inline void nvmap_handle_mkclean(struct nvmap_handle *h,
					u32 offset, u32 size)
{
	int nchanged;

	if (h->heap_pgalloc && !atomic_read(&h->pgalloc.ndirty))
		return;
	if (size == 0)
		size = h->size;

	nchanged = nvmap_handle_mk(h, offset, size, nvmap_page_mkclean, false);
	if (h->heap_pgalloc)
		atomic_sub(nchanged, &h->pgalloc.ndirty);
}

static inline void _nvmap_handle_mkdirty(struct nvmap_handle *h,
					u32 offset, u32 size)
{
	int nchanged;

	if (h->heap_pgalloc &&
		(atomic_read(&h->pgalloc.ndirty) == (h->size >> PAGE_SHIFT)))
		return;

	nchanged = nvmap_handle_mk(h, offset, size, nvmap_page_mkdirty, true);
	if (h->heap_pgalloc)
		atomic_add(nchanged, &h->pgalloc.ndirty);
}

static inline void nvmap_kmaps_inc(struct nvmap_handle *h)
{
	mutex_lock(&h->lock);
	atomic_inc(&h->kmap_count);
	mutex_unlock(&h->lock);
}

static inline void nvmap_kmaps_inc_no_lock(struct nvmap_handle *h)
{
	atomic_inc(&h->kmap_count);
}

static inline void nvmap_kmaps_dec(struct nvmap_handle *h)
{
	atomic_dec(&h->kmap_count);
}

static inline void nvmap_umaps_inc(struct nvmap_handle *h)
{
	mutex_lock(&h->lock);
	atomic_inc(&h->umap_count);
	mutex_unlock(&h->lock);
}

static inline void nvmap_umaps_dec(struct nvmap_handle *h)
{
	atomic_dec(&h->umap_count);
}

static inline void nvmap_lru_reset(struct nvmap_handle *h)
{
	spin_lock(&nvmap_dev->lru_lock);
	BUG_ON(list_empty(&h->lru));
	list_del(&h->lru);
	list_add_tail(&h->lru, &nvmap_dev->lru_handles);
	spin_unlock(&nvmap_dev->lru_lock);
}

static inline bool nvmap_handle_track_dirty(struct nvmap_handle *h)
{
	if (!h->heap_pgalloc)
		return false;

	return h->userflags & (NVMAP_HANDLE_CACHE_SYNC |
			       NVMAP_HANDLE_CACHE_SYNC_AT_RESERVE);
}

struct nvmap_handle_ref *nvmap_create_handle(struct nvmap_client *client,
					     size_t size, bool ro_buf);

struct nvmap_handle_ref *nvmap_create_handle_from_va(struct nvmap_client *client,
						     ulong addr, size_t size,
						     unsigned int access_flags);

struct nvmap_handle_ref *nvmap_dup_handle_ro(struct nvmap_client *client,
					int fd);

struct nvmap_handle_ref *nvmap_try_duplicate_by_ivmid(
			struct nvmap_client *client, u64 ivm_id,
			struct nvmap_heap_block **block);

struct nvmap_handle_ref *nvmap_create_handle_from_id(
			struct nvmap_client *client, u32 id);

struct nvmap_handle_ref *nvmap_create_handle_from_fd(
			struct nvmap_client *client, int fd);

int nvmap_install_fd(struct nvmap_client *client,
	struct nvmap_handle *handle, int fd, void __user *arg,
	void *op, size_t op_size, bool free, struct dma_buf *dmabuf);

int find_range_of_handles(struct nvmap_handle **hs, u32 nr,
		struct handles_range *hrange);

void nvmap_free_handle(struct nvmap_client *c, struct nvmap_handle *h, bool is_ro);

void nvmap_free_handle_from_fd(struct nvmap_client *c, int fd);

int nvmap_handle_remove(struct nvmap_device *dev, struct nvmap_handle *h);

int is_nvmap_id_ro(struct nvmap_client *client, int id, bool *is_ro);

int nvmap_assign_pages_to_handle(struct nvmap_client *client,
		struct nvmap_handle **hs, struct nvmap_handle *h,
		struct handles_range *rng);

int nvmap_validate_sci_ipc_params(struct nvmap_client *client,
			NvSciIpcEndpointAuthToken auth_token,
			NvSciIpcEndpointVuid *pr_vuid,
			NvSciIpcEndpointVuid *localusr_vuid);

int nvmap_create_sci_ipc_id(struct nvmap_client *client,
				struct nvmap_handle *h,
				u32 flags,
				u64 *sci_ipc_id,
				NvSciIpcEndpointVuid pr_vuid,
				bool is_ro);

int nvmap_get_handle_from_sci_ipc_id(struct nvmap_client *client,
				u32 flags,
				u64 sci_ipc_id,
				NvSciIpcEndpointVuid localusr_vuid,
				u32 *h);

struct nvmap_handle *nvmap_handle_get_from_fd(int fd);

struct sg_table *__nvmap_sg_table(struct nvmap_client *client,
				  struct nvmap_handle *h);

void __nvmap_free_sg_table(struct nvmap_client *client,
			   struct nvmap_handle *h, struct sg_table *sgt);

struct nvmap_handle *nvmap_handle_get(struct nvmap_handle *h);

void nvmap_handle_put(struct nvmap_handle *h);

struct nvmap_handle *nvmap_handle_get_from_id(struct nvmap_client *client,
		u32 id);

u32 nvmap_handle_get_max_handle_count(void);

void *__nvmap_mmap(struct nvmap_handle *h);

void __nvmap_munmap(struct nvmap_handle *h, void *addr);

#ifdef NVMAP_CONFIG_SCIIPC
int nvmap_sci_ipc_init(void);
void nvmap_sci_ipc_exit(void);
#else
__weak int nvmap_sci_ipc_init(void)
{
	return 0;
}
__weak void nvmap_sci_ipc_exit(void)
{
}
#endif

#ifdef NVMAP_CONFIG_HANDLE_AS_ID
void nvmap_id_array_init(struct xarray *xarr);
void nvmap_id_array_exit(struct xarray *xarr);
struct dma_buf *nvmap_id_array_get_dmabuf_from_id(struct xarray *xarr, u32 id);
int nvmap_id_array_id_alloc(struct xarray *xarr, u32 *id, struct dma_buf *dmabuf);
struct dma_buf *nvmap_id_array_id_release(struct xarray *xarr, u32 id);
#else
static inline void nvmap_id_array_init(struct xarray *xarr)
{

}

static inline void nvmap_id_array_exit(struct xarray *xarr)
{

}

static inline struct dma_buf *nvmap_id_array_get_dmabuf_from_id(struct xarray *xarr, u32 id)
{
	return NULL;
}

static inline int nvmap_id_array_id_alloc(struct xarray *xarr, u32 *id, struct dma_buf *dmabuf)
{
	return 0;
}

static inline struct dma_buf *nvmap_id_array_id_release(struct xarray *xarr, u32 id)
{
	return NULL;
}
#endif /* NVMAP_CONFIG_HANDLE_AS_ID */

#endif /* _NVMAP_HANDLE_H_ */
