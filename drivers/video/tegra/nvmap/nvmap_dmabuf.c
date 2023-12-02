// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * dma_buf exporter for nvmap
 */

#define pr_fmt(fmt)	"nvmap: %s() " fmt, __func__

#include <nvidia/conftest.h>

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/export.h>
#include <linux/nvmap.h>
#include <linux/dma-buf.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/stringify.h>
#include <linux/of.h>
#include <linux/version.h>
#include <linux/iommu.h>
#if defined(NV_LINUX_IOSYS_MAP_H_PRESENT)
#include <linux/iosys-map.h>
#endif

#include <trace/events/nvmap.h>

#include "nvmap_priv.h"
#include "nvmap_ioctl.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
#define NVMAP_DMABUF_ATTACH  nvmap_dmabuf_attach
#else
#define NVMAP_DMABUF_ATTACH  __nvmap_dmabuf_attach
#endif

struct nvmap_handle_sgt {
	enum dma_data_direction dir;
	struct sg_table *sgt;
	struct device *dev;
	struct list_head maps_entry;
	struct nvmap_handle_info *owner;
} ____cacheline_aligned_in_smp;

static struct kmem_cache *handle_sgt_cache;

/*
 * Initialize a kmem cache for allocating nvmap_handle_sgt's.
 */
int nvmap_dmabuf_stash_init(void)
{
	handle_sgt_cache = KMEM_CACHE(nvmap_handle_sgt, 0);
	if (IS_ERR_OR_NULL(handle_sgt_cache)) {
		pr_err("Failed to make kmem cache for nvmap_handle_sgt.\n");
		return -ENOMEM;
	}

	return 0;
}

void nvmap_dmabuf_stash_deinit(void)
{
	kmem_cache_destroy(handle_sgt_cache);
}

static int __nvmap_dmabuf_attach(struct dma_buf *dmabuf, struct device *dev,
			       struct dma_buf_attachment *attach)
{
	struct nvmap_handle_info *info = dmabuf->priv;

	trace_nvmap_dmabuf_attach(dmabuf, dev);

	dev_dbg(dev, "%s() 0x%p\n", __func__, info->handle);
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
static int nvmap_dmabuf_attach(struct dma_buf *dmabuf,
			       struct dma_buf_attachment *attach)
{
	return __nvmap_dmabuf_attach(dmabuf, attach->dev, attach);
}
#endif

static void nvmap_dmabuf_detach(struct dma_buf *dmabuf,
				struct dma_buf_attachment *attach)
{
	struct nvmap_handle_info *info = dmabuf->priv;

	trace_nvmap_dmabuf_detach(dmabuf, attach->dev);

	dev_dbg(attach->dev, "%s() 0x%p\n", __func__, info->handle);
}

static inline bool access_vpr_phys(struct device *dev)
{
	if (!iommu_get_domain_for_dev(dev))
		return true;

	/*
	 * Assumes gpu nodes always have DT entry, this is valid as device
	 * specifying access-vpr-phys will do so through its DT entry.
	 */
	if (!dev->of_node)
		return false;

	return !!of_find_property(dev->of_node, "access-vpr-phys", NULL);
}

static int nvmap_dmabuf_stash_sgt_locked(struct dma_buf_attachment *attach,
					 enum dma_data_direction dir,
					 struct sg_table *sgt)
{
	struct nvmap_handle_sgt *nvmap_sgt;
	struct nvmap_handle_info *info = attach->dmabuf->priv;

	nvmap_sgt = kmem_cache_alloc(handle_sgt_cache, GFP_KERNEL);
	if (IS_ERR_OR_NULL(nvmap_sgt)) {
		pr_err("Stashing SGT failed.\n");
		return -ENOMEM;
	}

	nvmap_sgt->dir = dir;
	nvmap_sgt->sgt = sgt;
	nvmap_sgt->dev = attach->dev;
	nvmap_sgt->owner = info;
	list_add(&nvmap_sgt->maps_entry, &info->maps);

	return 0;
}

static struct sg_table *nvmap_dmabuf_get_sgt_from_stash(struct dma_buf_attachment *attach,
							enum dma_data_direction dir)
{
	struct nvmap_handle_info *info = attach->dmabuf->priv;
	struct nvmap_handle_sgt *nvmap_sgt;
	struct sg_table *sgt = NULL;

	list_for_each_entry(nvmap_sgt, &info->maps, maps_entry) {
		if (nvmap_sgt->dir != dir || nvmap_sgt->dev != attach->dev)
			continue;

		/* found sgt in stash */
		sgt = nvmap_sgt->sgt;
		break;
	}

	return sgt;
}

static struct sg_table *nvmap_dmabuf_map_dma_buf(struct dma_buf_attachment *attach,
						  enum dma_data_direction dir)
{
	struct nvmap_handle_info *info = attach->dmabuf->priv;
	int ents = 0;
	struct sg_table *sgt = NULL;
#ifdef NVMAP_CONFIG_DEBUG_MAPS
	char *device_name = NULL;
	u32 heap_type;
	u64 dma_mask;
#endif /* NVMAP_CONFIG_DEBUG_MAPS */
	DEFINE_DMA_ATTRS(attrs);

	trace_nvmap_dmabuf_map_dma_buf(attach->dmabuf, attach->dev);

	/*
	 * If the exported buffer is foreign buffer(alloc_from_va) and
	 * has RO access, don't map it in device space.
	 * Return error as no access.
	 */
	if (info->handle->from_va && info->handle->is_ro &&
		(dir != DMA_TO_DEVICE))
		return ERR_PTR(-EACCES);

	nvmap_lru_reset(info->handle);
	mutex_lock(&info->maps_lock);

	atomic_inc(&info->handle->pin);

	sgt = nvmap_dmabuf_get_sgt_from_stash(attach, dir);
	if (sgt)
		goto cache_hit;

	sgt = __nvmap_sg_table(NULL, info->handle);
	if (IS_ERR(sgt)) {
		atomic_dec(&info->handle->pin);
		mutex_unlock(&info->maps_lock);
		return sgt;
	}

	if (!info->handle->alloc) {
		goto err_map;
	} else if (!(nvmap_dev->dynamic_dma_map_mask &
			info->handle->heap_type)) {
		sg_dma_address(sgt->sgl) = info->handle->carveout->base;
	} else if (info->handle->heap_type == NVMAP_HEAP_CARVEOUT_VPR &&
			access_vpr_phys(attach->dev)) {
		sg_dma_address(sgt->sgl) = 0;
	} else {
		dma_set_attr(DMA_ATTR_SKIP_CPU_SYNC, __DMA_ATTR(attrs));
		ents = dma_map_sg_attrs(attach->dev, sgt->sgl,
					sgt->nents, dir, __DMA_ATTR(attrs));
		if (ents <= 0)
			goto err_map;
	}

	if (nvmap_dmabuf_stash_sgt_locked(attach, dir, sgt))
		WARN(1, "No mem to prep sgt.\n");

cache_hit:
	attach->priv = sgt;

#ifdef NVMAP_CONFIG_DEBUG_MAPS
	/* Insert device name into the carveout's device name rb tree */
	heap_type = info->handle->heap_type;
	device_name = (char *)dev_name(attach->dev);
	dma_mask = *(attach->dev->dma_mask);
	if (device_name && !nvmap_is_device_present(device_name, heap_type)) {
		/* If the device name is not already present in the tree, then only add */
		nvmap_add_device_name(device_name, dma_mask, heap_type);
	}
#endif /* NVMAP_CONFIG_DEBUG_MAPS */
	mutex_unlock(&info->maps_lock);
	return sgt;

err_map:
	__nvmap_free_sg_table(NULL, info->handle, sgt);
	atomic_dec(&info->handle->pin);
	mutex_unlock(&info->maps_lock);
	return ERR_PTR(-ENOMEM);
}

static void __nvmap_dmabuf_unmap_dma_buf(struct nvmap_handle_sgt *nvmap_sgt)
{
	struct nvmap_handle_info *info = nvmap_sgt->owner;
	enum dma_data_direction dir = nvmap_sgt->dir;
	struct sg_table *sgt = nvmap_sgt->sgt;
	struct device *dev = nvmap_sgt->dev;

	if (!(nvmap_dev->dynamic_dma_map_mask & info->handle->heap_type)) {
		sg_dma_address(sgt->sgl) = 0;
	} else if (info->handle->heap_type == NVMAP_HEAP_CARVEOUT_VPR &&
			access_vpr_phys(dev)) {
		sg_dma_address(sgt->sgl) = 0;
	} else {
		dma_unmap_sg_attrs(dev,
				   sgt->sgl, sgt->nents,
				   dir, DMA_ATTR_SKIP_CPU_SYNC);
	}
	__nvmap_free_sg_table(NULL, info->handle, sgt);
}

static void nvmap_dmabuf_unmap_dma_buf(struct dma_buf_attachment *attach,
				       struct sg_table *sgt,
				       enum dma_data_direction dir)
{
	struct nvmap_handle_info *info = attach->dmabuf->priv;
#ifdef NVMAP_CONFIG_DEBUG_MAPS
	char *device_name = NULL;
	u32 heap_type = 0;
#endif /* NVMAP_CONFIG_DEBUG_MAPS */

	trace_nvmap_dmabuf_unmap_dma_buf(attach->dmabuf, attach->dev);

	mutex_lock(&info->maps_lock);
	if (!atomic_add_unless(&info->handle->pin, -1, 0)) {
		mutex_unlock(&info->maps_lock);
		WARN(1, "Unpinning handle that has yet to be pinned!\n");
		return;
	}

#ifdef NVMAP_CONFIG_DEBUG_MAPS
	/* Remove the device name from the list of carveout accessing devices */
	heap_type = info->handle->heap_type;
	device_name = (char *)dev_name(attach->dev);
	if (device_name)
		nvmap_remove_device_name(device_name, heap_type);
#endif /* NVMAP_CONFIG_DEBUG_MAPS */
	mutex_unlock(&info->maps_lock);
}

static void nvmap_dmabuf_release(struct dma_buf *dmabuf)
{
	struct nvmap_handle_info *info = dmabuf->priv;
	struct nvmap_handle_sgt *nvmap_sgt;

	trace_nvmap_dmabuf_release(info->handle->owner ?
				   info->handle->owner->name : "unknown",
				   info->handle,
				   dmabuf);

	mutex_lock(&info->maps_lock);
	while (!list_empty(&info->maps)) {
		nvmap_sgt = list_first_entry(&info->maps,
					     struct nvmap_handle_sgt,
					     maps_entry);
		__nvmap_dmabuf_unmap_dma_buf(nvmap_sgt);
		list_del(&nvmap_sgt->maps_entry);
		kmem_cache_free(handle_sgt_cache, nvmap_sgt);
	}
	mutex_unlock(&info->maps_lock);

	mutex_lock(&info->handle->lock);
	if (info->is_ro) {
		BUG_ON(dmabuf != info->handle->dmabuf_ro);
		info->handle->dmabuf_ro = NULL;
		wake_up(&info->handle->waitq);
	} else {
		BUG_ON(dmabuf != info->handle->dmabuf);
		info->handle->dmabuf = NULL;
	}
	mutex_unlock(&info->handle->lock);

	if (!list_empty(&info->handle->pg_ref_h)) {
		struct nvmap_handle *tmp, *src;

		mutex_lock(&info->handle->pg_ref_h_lock);
		/* Closing dmabuf_fd,
		 * 1. Remove all the handles in page_ref_h list
		 * 2. Decreament handle ref count of all the handles in page_ref_h list
		 * 3. NULL page_ref_h list;
		 */
		list_for_each_entry_safe(src, tmp, &info->handle->pg_ref_h,
			pg_ref) {
			list_del(&src->pg_ref);
			nvmap_handle_put(src);
		}

		mutex_unlock(&info->handle->pg_ref_h_lock);
	}

	nvmap_handle_put(info->handle);
	kfree(info);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
static int __nvmap_dmabuf_end_cpu_access(struct dma_buf *dmabuf,
				       enum dma_data_direction dir)
{
	struct nvmap_handle_info *info = dmabuf->priv;

	trace_nvmap_dmabuf_end_cpu_access(dmabuf, 0, dmabuf->size);
	return __nvmap_do_cache_maint(NULL, info->handle,
				   0, dmabuf->size,
				   NVMAP_CACHE_OP_WB, false);
}

static int __nvmap_dmabuf_begin_cpu_access(struct dma_buf *dmabuf,
					  enum dma_data_direction dir)
{
	struct nvmap_handle_info *info = dmabuf->priv;

	trace_nvmap_dmabuf_begin_cpu_access(dmabuf, 0, dmabuf->size);
	return __nvmap_do_cache_maint(NULL, info->handle, 0, dmabuf->size,
				      NVMAP_CACHE_OP_WB_INV, false);
}
#define NVMAP_DMABUF_BEGIN_CPU_ACCESS           __nvmap_dmabuf_begin_cpu_access
#define NVMAP_DMABUF_END_CPU_ACCESS 		__nvmap_dmabuf_end_cpu_access
#else
static int nvmap_dmabuf_begin_cpu_access(struct dma_buf *dmabuf,
					  size_t start, size_t len,
					  enum dma_data_direction dir)
{
	struct nvmap_handle_info *info = dmabuf->priv;

	trace_nvmap_dmabuf_begin_cpu_access(dmabuf, start, len);
	return __nvmap_do_cache_maint(NULL, info->handle, start, start + len,
				      NVMAP_CACHE_OP_WB_INV, false);
}

static void nvmap_dmabuf_end_cpu_access(struct dma_buf *dmabuf,
				       size_t start, size_t len,
				       enum dma_data_direction dir)
{
	struct nvmap_handle_info *info = dmabuf->priv;

	trace_nvmap_dmabuf_end_cpu_access(dmabuf, start, len);
	__nvmap_do_cache_maint(NULL, info->handle,
				   start, start + len,
				   NVMAP_CACHE_OP_WB, false);
}
#define NVMAP_DMABUF_BEGIN_CPU_ACCESS           nvmap_dmabuf_begin_cpu_access
#define NVMAP_DMABUF_END_CPU_ACCESS 		nvmap_dmabuf_end_cpu_access
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
static void *nvmap_dmabuf_kmap(struct dma_buf *dmabuf, unsigned long page_num)
{
	struct nvmap_handle_info *info = dmabuf->priv;

	trace_nvmap_dmabuf_kmap(dmabuf);
	return __nvmap_kmap(info->handle, page_num);
}

static void nvmap_dmabuf_kunmap(struct dma_buf *dmabuf,
		unsigned long page_num, void *addr)
{
	struct nvmap_handle_info *info = dmabuf->priv;

	trace_nvmap_dmabuf_kunmap(dmabuf);
	__nvmap_kunmap(info->handle, page_num, addr);
}

static void *nvmap_dmabuf_kmap_atomic(struct dma_buf *dmabuf,
				      unsigned long page_num)
{
	WARN(1, "%s() can't be called from atomic\n", __func__);
	return NULL;
}
#endif

int __nvmap_map(struct nvmap_handle *h, struct vm_area_struct *vma)
{
	struct nvmap_vma_priv *priv;

	h = nvmap_handle_get(h);
	if (!h)
		return -EINVAL;

	if (!(h->heap_type & nvmap_dev->cpu_access_mask)) {
		nvmap_handle_put(h);
		return -EPERM;
	}

	/*
	 * If the handle is RO and RW mapping is requested, then
	 * return error.
	 */
	if (h->from_va && h->is_ro && (vma->vm_flags & VM_WRITE)) {
		nvmap_handle_put(h);
		return -EPERM;
	}
	/*
	 * Don't allow mmap on VPR memory as it would be mapped
	 * as device memory. User space shouldn't be accessing
	 * device memory.
	 */
	if (h->heap_type == NVMAP_HEAP_CARVEOUT_VPR)  {
		nvmap_handle_put(h);
		return -EPERM;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		nvmap_handle_put(h);
		return -ENOMEM;
	}
	priv->handle = h;

#if defined(NV_VM_AREA_STRUCT_HAS_CONST_VM_FLAGS) /* Linux v6.3 */
	vm_flags_set(vma, VM_SHARED | VM_DONTEXPAND |
			  VM_DONTDUMP | VM_DONTCOPY |
			  (h->heap_pgalloc ? 0 : VM_PFNMAP));
#else
	vma->vm_flags |= VM_SHARED | VM_DONTEXPAND |
			  VM_DONTDUMP | VM_DONTCOPY |
			  (h->heap_pgalloc ? 0 : VM_PFNMAP);
#endif
	vma->vm_ops = &nvmap_vma_ops;
	BUG_ON(vma->vm_private_data != NULL);
	vma->vm_private_data = priv;
	vma->vm_page_prot = nvmap_pgprot(h, vma->vm_page_prot);
	nvmap_vma_open(vma);
	return 0;
}

static int nvmap_dmabuf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct nvmap_handle_info *info = dmabuf->priv;

	trace_nvmap_dmabuf_mmap(dmabuf);

	return __nvmap_map(info->handle, vma);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
static void *nvmap_dmabuf_vmap(struct dma_buf *dmabuf)
{
	struct nvmap_handle_info *info = dmabuf->priv;

	trace_nvmap_dmabuf_vmap(dmabuf);

	/* Don't allow vmap on RO buffers */
	if (info->is_ro)
		return ERR_PTR(-EPERM);

	return __nvmap_mmap(info->handle);
}

static void nvmap_dmabuf_vunmap(struct dma_buf *dmabuf, void *vaddr)
{
	struct nvmap_handle_info *info = dmabuf->priv;

	trace_nvmap_dmabuf_vunmap(dmabuf);
	__nvmap_munmap(info->handle, vaddr);
}
#else
#if defined(NV_LINUX_IOSYS_MAP_H_PRESENT)
static int nvmap_dmabuf_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
#else
static int nvmap_dmabuf_vmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
#endif
{
	struct nvmap_handle_info *info = dmabuf->priv;
	void *res;
	int ret = 0;

	trace_nvmap_dmabuf_vmap(dmabuf);

	/* Don't allow vmap on RO buffers */
	if (info->is_ro)
		return -EPERM;

	res = __nvmap_mmap(info->handle);
	if (res != NULL) {
		map->vaddr = res;
		map->is_iomem = false;
	}
	else {
		ret = -ENOMEM;
	}
	return ret;
}

#if defined(NV_LINUX_IOSYS_MAP_H_PRESENT)
static void nvmap_dmabuf_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
#else
static void nvmap_dmabuf_vunmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
#endif
{
       struct nvmap_handle_info *info = dmabuf->priv;

       trace_nvmap_dmabuf_vunmap(dmabuf);
       __nvmap_munmap(info->handle, info->handle->vaddr);
}
#endif

static struct dma_buf_ops nvmap_dma_buf_ops = {
	.attach		= NVMAP_DMABUF_ATTACH,
	.detach		= nvmap_dmabuf_detach,
	.map_dma_buf	= nvmap_dmabuf_map_dma_buf,
	.unmap_dma_buf	= nvmap_dmabuf_unmap_dma_buf,
	.release	= nvmap_dmabuf_release,
	.begin_cpu_access = NVMAP_DMABUF_BEGIN_CPU_ACCESS,
	.end_cpu_access = NVMAP_DMABUF_END_CPU_ACCESS,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
	.kmap_atomic	= nvmap_dmabuf_kmap_atomic,
	.kmap		= nvmap_dmabuf_kmap,
	.kunmap		= nvmap_dmabuf_kunmap,
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
	.map_atomic	= nvmap_dmabuf_kmap_atomic,
	.map		= nvmap_dmabuf_kmap,
	.unmap		= nvmap_dmabuf_kunmap,
#endif
	.mmap		= nvmap_dmabuf_mmap,
	.vmap		= nvmap_dmabuf_vmap,
	.vunmap		= nvmap_dmabuf_vunmap,
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 4, 0)
	.cache_sgt_mapping = true,
#endif

};

static char dmabuf_name[] = "nvmap_dmabuf";

bool dmabuf_is_nvmap(struct dma_buf *dmabuf)
{
	return dmabuf->exp_name == dmabuf_name;
}

static struct dma_buf *__dma_buf_export(struct nvmap_handle_info *info,
					size_t size, bool ro_buf)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.priv = info;
	exp_info.ops = &nvmap_dma_buf_ops;
	exp_info.size = size;

	if (ro_buf) {
		exp_info.flags = O_RDONLY;
	} else {
		exp_info.flags = O_RDWR;
	}

#ifdef NVMAP_DEFERRED_DMABUF_UNMAP
	/* Disable defer unmap feature only for kstable */
	exp_info.exp_flags = DMABUF_CAN_DEFER_UNMAP |
				DMABUF_SKIP_CACHE_SYNC;
#endif
	exp_info.exp_name = dmabuf_name;

	return dma_buf_export(&exp_info);
}

/*
 * Make a dmabuf object for an nvmap handle.
 */
struct dma_buf *__nvmap_make_dmabuf(struct nvmap_client *client,
				    struct nvmap_handle *handle, bool ro_buf)
{
	int err;
	struct dma_buf *dmabuf;
	struct nvmap_handle_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto err_nomem;
	}
	info->handle = handle;
	info->is_ro = ro_buf;
	INIT_LIST_HEAD(&info->maps);
	mutex_init(&info->maps_lock);

	dmabuf = __dma_buf_export(info, handle->size, ro_buf);
	if (IS_ERR(dmabuf)) {
		err = PTR_ERR(dmabuf);
		goto err_export;
	}

	if (!nvmap_handle_get(handle)) {
		err = -EINVAL;
		goto err_export;
	}

	trace_nvmap_make_dmabuf(client->name, handle, dmabuf);
	return dmabuf;

err_export:
	kfree(info);
err_nomem:
	return ERR_PTR(err);
}

int __nvmap_dmabuf_fd(struct nvmap_client *client,
		      struct dma_buf *dmabuf, int flags)
{
#if !defined(NVMAP_CONFIG_HANDLE_AS_ID) && !defined(NVMAP_LOADABLE_MODULE)
	int start_fd = NVMAP_CONFIG_FD_START;
#endif
	int ret;

#ifdef NVMAP_CONFIG_DEFER_FD_RECYCLE
	if (client->next_fd < NVMAP_CONFIG_FD_START)
		client->next_fd = NVMAP_CONFIG_FD_START;
	start_fd = client->next_fd++;
	if (client->next_fd >= NVMAP_CONFIG_DEFER_FD_RECYCLE_MAX_FD)
		client->next_fd = NVMAP_CONFIG_FD_START;
#endif
	if (!dmabuf || !dmabuf->file)
		return -EINVAL;
	/* Allocate fd from start_fd(>=1024) onwards to overcome
	 * __FD_SETSIZE limitation issue for select(),
	 * pselect() syscalls.
	 */
#if defined(NVMAP_LOADABLE_MODULE) || defined(NVMAP_CONFIG_HANDLE_AS_ID)
	ret = get_unused_fd_flags(flags);
#else
	ret =  __alloc_fd(current->files, start_fd, sysctl_nr_open, flags);
#endif
	if (ret == -EMFILE)
		pr_err_ratelimited("NvMap: FD limit is crossed for uid %d\n",
				   from_kuid(current_user_ns(), current_uid()));
	return ret;
}

static struct dma_buf *__nvmap_dmabuf_export(struct nvmap_client *client,
				 struct nvmap_handle *handle, bool is_ro)
{
	struct dma_buf *buf;

	handle = nvmap_handle_get(handle);
	if (!handle)
		return ERR_PTR(-EINVAL);
	if (is_ro)
		buf = handle->dmabuf_ro;
	else
		buf = handle->dmabuf;

	if (WARN(!buf, "Attempting to get a freed dma_buf!\n")) {
		nvmap_handle_put(handle);
		return NULL;
	}

	get_dma_buf(buf);

	/*
	 * Don't want to take out refs on the handle here.
	 */
	nvmap_handle_put(handle);

	return buf;
}

int nvmap_get_dmabuf_fd(struct nvmap_client *client, struct nvmap_handle *h,
			bool is_ro)
{
	int fd;
	struct dma_buf *dmabuf;

	dmabuf = __nvmap_dmabuf_export(client, h, is_ro);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	fd = __nvmap_dmabuf_fd(client, dmabuf, O_CLOEXEC);
	if (fd < 0)
		dma_buf_put(dmabuf);
	return fd;
}

/*
 * Returns the nvmap handle ID associated with the passed dma_buf's fd. This
 * does not affect the ref count of the dma_buf.
 * NOTE: Callers of this utility function must invoke nvmap_handle_put after
 * using the returned nvmap_handle. Call to nvmap_handle_get is required in
 * this utility function to avoid race conditions in code where nvmap_handle
 * returned by this function is freed concurrently while the caller is still
 * using it.
 */
struct nvmap_handle *nvmap_handle_get_from_dmabuf_fd(
					struct nvmap_client *client, int fd)
{
	struct nvmap_handle *handle = ERR_PTR(-EINVAL);
	struct dma_buf *dmabuf;
	struct nvmap_handle_info *info;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf))
		return ERR_CAST(dmabuf);
	if (dmabuf_is_nvmap(dmabuf)) {
		info = dmabuf->priv;
		handle = info->handle;
		if (!nvmap_handle_get(handle))
			handle = ERR_PTR(-EINVAL);
	}
	dma_buf_put(dmabuf);
	return handle;
}

int is_nvmap_dmabuf_fd_ro(int fd, bool *is_ro)
{
	struct dma_buf *dmabuf;
	struct nvmap_handle_info *info = NULL;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf)) {
		goto fail;
	}

	if (dmabuf_is_nvmap(dmabuf))
		info = dmabuf->priv;

	if (!info) {
		dma_buf_put(dmabuf);
		/*
		 * Ideally, we should return error from here,
		 * but this is done intentionally to handle foreign buffers.
		 */
		return 0;
	}

	*is_ro = info->is_ro;
	dma_buf_put(dmabuf);
	return 0;

fail:
	pr_err("Dmabuf fd RO check failed\n");
	return -EINVAL;
}

/*
 * Duplicates a generic dma_buf fd. nvmap dma_buf fd has to be duplicated
 * using existing code paths to preserve memory accounting behavior, so this
 * function returns -EINVAL on dma_buf fds created by nvmap.
 */
int nvmap_dmabuf_duplicate_gen_fd(struct nvmap_client *client,
		struct dma_buf *dmabuf)
{
	int ret = 0;

	if (dmabuf_is_nvmap(dmabuf)) {
		ret = -EINVAL;
		goto error;
	}

	ret = __nvmap_dmabuf_fd(client, dmabuf, O_CLOEXEC);
	if (ret < 0)
		goto error;

	return ret;

error:
	dma_buf_put(dmabuf);
	return ret;
}
