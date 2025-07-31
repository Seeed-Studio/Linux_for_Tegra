// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2009-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Handle allocation and freeing routines for nvmap
 */

#include <nvidia/conftest.h>

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/dma-buf.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/nvmap.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <soc/tegra/fuse.h>
#include <asm/pgtable.h>

#include <trace/events/nvmap.h>

#include "nvmap_dev.h"
#include "nvmap_alloc.h"
#include "nvmap_dmabuf.h"
#include "nvmap_handle.h"
#include "nvmap_handle_int.h"
#include "nvmap_debug.h"

static u32 nvmap_max_handle_count;

u32 nvmap_handle_get_max_handle_count(void)
{
	return nvmap_max_handle_count;
}

struct nvmap_handle *nvmap_handle_get_from_id(struct nvmap_client *client,
		u32 id)
{
	struct nvmap_handle *handle = ERR_PTR(-EINVAL);
	struct nvmap_handle_info *info;
	struct dma_buf *dmabuf;

	if (WARN_ON(!client))
		return ERR_PTR(-EINVAL);

	if (client->ida) {
		dmabuf = dma_buf_get((int)id);
		/*
		 * id is dmabuf fd created from foreign dmabuf
		 * but handle as ID is enabled, hence it doesn't belong
		 * to nvmap_handle, bail out early.
		 */
		if (!IS_ERR_OR_NULL(dmabuf)) {
			dma_buf_put(dmabuf);
			return NULL;
		}

		dmabuf = nvmap_id_array_get_dmabuf_from_id(client->ida, id);
	} else {
		dmabuf = dma_buf_get((int)id);
	}
	if (IS_ERR_OR_NULL(dmabuf))
		return ERR_CAST(dmabuf);

	if (dmabuf_is_nvmap(dmabuf)) {
		info = dmabuf->priv;
		handle = info->handle;
		if (!nvmap_handle_get(handle))
			handle = ERR_PTR(-EINVAL);
	}

	dma_buf_put(dmabuf);

	if (!IS_ERR(handle))
		return handle;

	return	NULL;
}

int nvmap_install_fd(struct nvmap_client *client,
	struct nvmap_handle *handle, int fd, void __user *arg,
	void *op, size_t op_size, bool free, struct dma_buf *dmabuf)
{
	int err = 0;
	struct nvmap_handle_info *info;

	if (!dmabuf) {
		err = -EFAULT;
		goto dmabuf_fail;
	}
	info = dmabuf->priv;
	if (IS_ERR_VALUE((uintptr_t)fd)) {
		err = fd;
		goto fd_fail;
	}

	if (copy_to_user(arg, op, op_size)) {
		err = -EFAULT;
		goto copy_fail;
	}

	fd_install(fd, dmabuf->file);
	return err;

copy_fail:
	put_unused_fd(fd);
fd_fail:
	if (dmabuf)
		dma_buf_put(dmabuf);
	if (free && handle)
		nvmap_free_handle(client, handle, info->is_ro);
dmabuf_fail:
	return err;
}

int find_range_of_handles(struct nvmap_handle **hs, u32 nr,
		struct handles_range *hrange)
{
	u64 tot_sz = 0, rem_sz = 0;
	u64 offs = hrange->offs;
	u64 sum, difference;
	u32 start = 0, end = 0;
	u64 sz = hrange->sz;
	u32 i;

	hrange->offs_start = offs;
	/* Find start handle */
	for (i = 0; i < nr; i++) {
		if (check_add_overflow(tot_sz, (u64)hs[i]->size, &sum))
			return -EOVERFLOW;

		tot_sz = sum;
		if (offs > tot_sz) {
			if (check_sub_overflow(hrange->offs_start, (u64)hs[i]->size, &difference))
				return -EOVERFLOW;

			hrange->offs_start = difference;
			continue;
		} else {
			rem_sz = tot_sz - offs;
			start = i;
			/* Check size in current handle */
			if (rem_sz >= sz) {
				end = i;
				hrange->start = start;
				hrange->end = end;
				return 0;
			}
			/* Though start found but end lies in further handles */
			i++;
			break;
		}
	}
	/* find end handle number */
	for (; i < nr; i++) {
		if (check_add_overflow(rem_sz, (u64)hs[i]->size, &sum))
			return -EOVERFLOW;

		rem_sz = sum;
		if (rem_sz >= sz) {
			end = i;
			hrange->start = start;
			hrange->end = end;
			return 0;
		}
	}
	return -1;
}

static inline void nvmap_lru_add(struct nvmap_handle *h)
{
	spin_lock(&nvmap_dev->lru_lock);
	BUG_ON(!list_empty(&h->lru));
	list_add_tail(&h->lru, &nvmap_dev->lru_handles);
	spin_unlock(&nvmap_dev->lru_lock);
}

static inline void nvmap_lru_del(struct nvmap_handle *h)
{
	spin_lock(&nvmap_dev->lru_lock);
	list_del(&h->lru);
	INIT_LIST_HEAD(&h->lru);
	spin_unlock(&nvmap_dev->lru_lock);
}

/*
 * Verifies that the passed ID is a valid handle ID. Then the passed client's
 * reference to the handle is returned.
 *
 * Note: to call this function make sure you own the client ref lock.
 */
struct nvmap_handle_ref *__nvmap_validate_locked(struct nvmap_client *c,
						 struct nvmap_handle *h,
						 bool is_ro)
{
	struct rb_node *n = c->handle_refs.rb_node;

	while (n) {
		struct nvmap_handle_ref *ref;
		ref = rb_entry(n, struct nvmap_handle_ref, node);
		if ((ref->handle == h) && (ref->is_ro == is_ro))
			return ref;
		else if ((uintptr_t)h > (uintptr_t)ref->handle)
			n = n->rb_right;
		else
			n = n->rb_left;
	}

	return NULL;
}
/* adds a newly-created handle to the device master tree */
static void nvmap_handle_add(struct nvmap_device *dev, struct nvmap_handle *h)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;

	spin_lock(&dev->handle_lock);
	p = &dev->handles.rb_node;
	while (*p) {
		struct nvmap_handle *b;

		parent = *p;
		b = rb_entry(parent, struct nvmap_handle, node);
		if (h > b)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&h->node, parent, p);
	rb_insert_color(&h->node, &dev->handles);
	nvmap_lru_add(h);
	/*
	 * Set handle's serial_id to global serial id counter and then update the counter.
	 * This operation is done here, so as to protect from concurrency issue, as we take
	 * lock on handle_lock.
	 */
	if (dev->serial_id_counter == U64_MAX)
		dev->serial_id_counter = 0;
	else
		h->serial_id = dev->serial_id_counter++;

	spin_unlock(&dev->handle_lock);
}

/* remove a handle from the device's tree of all handles; called
 * when freeing handles. */
int nvmap_handle_remove(struct nvmap_device *dev, struct nvmap_handle *h)
{
	spin_lock(&dev->handle_lock);

	/* re-test inside the spinlock if the handle really has no clients;
	 * only remove the handle if it is unreferenced */
	if (atomic_add_return(0, &h->ref) > 0) {
		spin_unlock(&dev->handle_lock);
		return -EBUSY;
	}
	smp_rmb();
	BUG_ON(atomic_read(&h->ref) < 0);
	BUG_ON(atomic_read(&h->pin) != 0);

	nvmap_lru_del(h);
	rb_erase(&h->node, &dev->handles);

	spin_unlock(&dev->handle_lock);
	return 0;
}

/* Validates that a handle is in the device master tree and that the
 * client has permission to access it. */
static struct nvmap_handle *nvmap_validate_get(struct nvmap_handle *id)
{
	struct nvmap_handle *h = NULL;
	struct rb_node *n;

	spin_lock(&nvmap_dev->handle_lock);

	n = nvmap_dev->handles.rb_node;

	while (n) {
		h = rb_entry(n, struct nvmap_handle, node);
		if (h == id) {
			h = nvmap_handle_get(h);
			spin_unlock(&nvmap_dev->handle_lock);
			return h;
		}
		if (id > h)
			n = n->rb_right;
		else
			n = n->rb_left;
	}
	spin_unlock(&nvmap_dev->handle_lock);
	return NULL;
}

static void add_handle_ref(struct nvmap_client *client,
			   struct nvmap_handle_ref *ref)
{
	struct rb_node **p, *parent = NULL;

	nvmap_ref_lock(client);
	p = &client->handle_refs.rb_node;
	while (*p) {
		struct nvmap_handle_ref *node;
		parent = *p;
		node = rb_entry(parent, struct nvmap_handle_ref, node);
		if (ref->handle > node->handle)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&ref->node, parent, p);
	rb_insert_color(&ref->node, &client->handle_refs);
	BUG_ON(client->handle_count == UINT_MAX);
	client->handle_count++;
	if (client->handle_count > nvmap_max_handle_count)
		nvmap_max_handle_count = client->handle_count;
	atomic_inc(&ref->handle->share_count);
	nvmap_ref_unlock(client);
}

/*
 * Remove handle ref from client's handle_ref rb tree.
 */
static void remove_handle_ref(struct nvmap_client *client,
			   struct nvmap_handle_ref *ref)
{
	nvmap_ref_lock(client);
	atomic_dec(&ref->handle->share_count);
	BUG_ON(client->handle_count == 0);
	client->handle_count--;
	rb_erase(&ref->node, &client->handle_refs);
	nvmap_ref_unlock(client);
}

struct nvmap_handle_ref *nvmap_create_handle_from_va(struct nvmap_client *client,
						     ulong vaddr, size_t size,
						     u32 flags)
{
	struct vm_area_struct *vma;
	struct nvmap_handle_ref *ref;
	vm_flags_t vm_flags;
	struct mm_struct *mm = current->mm;

	/* don't allow non-page aligned addresses. */
	if (vaddr & ~PAGE_MASK)
		return ERR_PTR(-EINVAL);

	nvmap_acquire_mmap_read_lock(mm);
	vma = find_vma(mm, vaddr);
	if (unlikely(!vma)) {
		nvmap_release_mmap_read_lock(mm);
		return ERR_PTR(-EINVAL);
	}

	if (size == 0U)
		size = vma->vm_end - vaddr;

	/* Don't allow exuberantly large sizes. */
	if (!is_nvmap_memory_available(size, NVMAP_HEAP_IOVMM, NUMA_NO_NODE)) {
		pr_debug("Cannot allocate %zu bytes.\n", size);
		nvmap_release_mmap_read_lock(mm);
		return ERR_PTR(-ENOMEM);
	}

	vm_flags = vma->vm_flags;
	if ((vm_flags & VM_EXEC) != 0) {
		pr_err("Executable memory is not allowed\n");
		nvmap_release_mmap_read_lock(mm);
		return ERR_PTR(-EINVAL);
	}

	nvmap_release_mmap_read_lock(mm);

	/*
	 * If buffer is malloc/mprotect as RO but alloc flag is not passed
	 * as RO, don't create handle.
	 */
	if (!(vm_flags & VM_WRITE) && !(flags & NVMAP_HANDLE_RO))
		return ERR_PTR(-EINVAL);

	ref = nvmap_create_handle(client, size, flags & NVMAP_HANDLE_RO);
	if (!IS_ERR(ref))
		ref->handle->orig_size = size;

	return ref;
}

struct nvmap_handle_ref *nvmap_create_handle(struct nvmap_client *client,
					     size_t size, bool ro_buf)
{
	void *err = ERR_PTR(-ENOMEM);
	struct nvmap_handle *h;
	struct nvmap_handle_ref *ref = NULL;
	struct dma_buf *dmabuf;

	if (client == NULL)
		return ERR_PTR(-EINVAL);

	if (!size)
		return ERR_PTR(-EINVAL);

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	ref = kzalloc(sizeof(*ref), GFP_KERNEL);
	if (ref == NULL)
		goto ref_alloc_fail;

	atomic_set(&h->ref, 1);
	atomic_set(&h->pin, 0);
	h->owner = client;
	BUG_ON(!h->owner);
	h->orig_size = size;
	h->size = PAGE_ALIGN(size);
	h->flags = NVMAP_HANDLE_WRITE_COMBINE;
	h->peer = NVMAP_IVM_INVALID_PEER;
	mutex_init(&h->lock);
	INIT_LIST_HEAD(&h->vmas);
	INIT_LIST_HEAD(&h->lru);
	INIT_LIST_HEAD(&h->dmabuf_priv);

	INIT_LIST_HEAD(&h->pg_ref_h);
	init_waitqueue_head(&h->waitq);
	/*
	 * This takes out 1 ref on the dambuf. This corresponds to the
	 * handle_ref that gets automatically made by nvmap_create_handle().
	 */
	dmabuf = __nvmap_make_dmabuf(client, h, ro_buf);
	if (IS_ERR(dmabuf)) {
		err = dmabuf;
		goto make_dmabuf_fail;
	}
	if (!ro_buf)
		h->dmabuf = dmabuf;
	else
		h->dmabuf_ro = dmabuf;

	nvmap_handle_add(nvmap_dev, h);

	/*
	 * Major assumption here: the dma_buf object that the handle contains
	 * is created with a ref count of 1.
	 */
	atomic_set(&ref->dupes, 1);
	ref->handle = h;
	add_handle_ref(client, ref);
	if (ro_buf)
		ref->is_ro = true;
	else
		ref->is_ro = false;
	trace_nvmap_create_handle(client, client->name, h, size, ref);
	return ref;

make_dmabuf_fail:
	kfree(ref);
ref_alloc_fail:
	kfree(h);
	return err;
}

struct nvmap_handle_ref *nvmap_try_duplicate_by_ivmid(
		struct nvmap_client *client, u64 ivm_id,
		struct nvmap_heap_block **block)
{
	struct nvmap_handle *h = NULL;
	struct nvmap_handle_ref *ref = NULL;
	struct rb_node *n;

	spin_lock(&nvmap_dev->handle_lock);

	n = nvmap_dev->handles.rb_node;
	for (n = rb_first(&nvmap_dev->handles); n; n = rb_next(n)) {
		h = rb_entry(n, struct nvmap_handle, node);
		if (h->ivm_id == ivm_id) {
			BUG_ON(!virt_addr_valid(h));
			/* get handle's ref only if non-zero */
			if (atomic_inc_not_zero(&h->ref) == 0) {
				*block = h->carveout;
				/* strip handle's block and fail duplication */
				h->carveout = NULL;
				break;
			}
			spin_unlock(&nvmap_dev->handle_lock);
			goto found;
		}
	}

	spin_unlock(&nvmap_dev->handle_lock);
	/* handle is either freed or being freed, don't duplicate it */
	goto finish;

	/*
	 * From this point, handle and its buffer are valid and won't be
	 * freed as a reference is taken on it. The dmabuf can still be
	 * freed anytime till reference is taken on it below.
	 */
found:
	mutex_lock(&h->lock);
	/*
	 * Save this block. If dmabuf's reference is not held in time,
	 * this can be reused to avoid the delay to free the buffer
	 * in this old handle and allocate it for a new handle from
	 * the ivm allocation ioctl.
	 */
	*block = h->carveout;
	if (h->dmabuf == NULL)
		goto fail;
	BUG_ON(!h->dmabuf->file);
	/* This is same as get_dma_buf() if file->f_count was non-zero */
#if defined(NV_FILE_STRUCT_HAS_F_REF) /* Linux v6.13 */
	if (file_ref_get(&h->dmabuf->file->f_ref) == 0)
#else
	if (atomic_long_inc_not_zero(&h->dmabuf->file->f_count) == 0)
#endif
		goto fail;
	mutex_unlock(&h->lock);

	/* h->dmabuf can't be NULL anymore. Duplicate the handle. */
	ref = nvmap_duplicate_handle(client, h, true, false);
	if (IS_ERR_OR_NULL(ref)) {
		pr_err("Failed to duplicate handle\n");
		return ref;
	}
	/* put the extra ref taken using get_dma_buf. */
	dma_buf_put(h->dmabuf);
finish:
	return ref;
fail:
	/* free handle but not its buffer */
	h->carveout = NULL;
	mutex_unlock(&h->lock);
	nvmap_handle_put(h);
	return NULL;
}

struct nvmap_handle_ref *nvmap_duplicate_handle(struct nvmap_client *client,
					struct nvmap_handle *h, bool skip_val,
					bool is_ro)
{
	struct nvmap_handle_ref *ref = NULL;

	BUG_ON(!client);

	if (!skip_val)
		/* on success, the reference count for the handle should be
		 * incremented, so the success paths will not call
		 * nvmap_handle_put */
		h = nvmap_validate_get(h);

	if (!h) {
		pr_debug("%s duplicate handle failed\n",
			    current->group_leader->comm);
		return ERR_PTR(-EPERM);
	}

	if (!h->alloc) {
		pr_err("%s duplicating unallocated handle\n",
			current->group_leader->comm);
		nvmap_handle_put(h);
		return ERR_PTR(-EINVAL);
	}

	nvmap_ref_lock(client);
	ref = __nvmap_validate_locked(client, h, is_ro);

	if (ref) {
		atomic_inc(&ref->dupes);
		nvmap_ref_unlock(client);
		goto out;
	}

	nvmap_ref_unlock(client);

	ref = kzalloc(sizeof(*ref), GFP_KERNEL);
	if (!ref) {
		nvmap_handle_put(h);
		return ERR_PTR(-ENOMEM);
	}

	atomic_set(&ref->dupes, 1);
	ref->handle = h;

	/*
	 * When a new reference is created to the handle, save mm, anon_count in ref and
	 * increment ref count of mm.
	 */
	ref->mm = current->mm;
	ref->anon_count = h->anon_count;
	add_handle_ref(client, ref);

	if (ref->anon_count != 0 && ref->mm != NULL) {
		if (!mmget_not_zero(ref->mm))
			goto exit;

		nvmap_add_mm_counter(ref->mm, MM_ANONPAGES, ref->anon_count);
	}

	if (is_ro) {
		ref->is_ro = true;
		if (!h->dmabuf_ro)
			goto exit_mm;
		get_dma_buf(h->dmabuf_ro);
	} else {
		ref->is_ro = false;
		if (!h->dmabuf)
			goto exit_mm;
		get_dma_buf(h->dmabuf);
	}

out:
	NVMAP_TAG_TRACE(trace_nvmap_duplicate_handle,
		NVMAP_TP_ARGS_CHR(client, h, ref));
	return ref;

exit_mm:
	if (ref->anon_count != 0 && ref->mm != NULL) {
		nvmap_add_mm_counter(ref->mm, MM_ANONPAGES, -ref->anon_count);
		mmput(ref->mm);
		ref->mm = NULL;
		ref->anon_count = 0;
	}

exit:
	remove_handle_ref(client, ref);
	pr_err("dmabuf is NULL\n");
	kfree(ref);
	return ERR_PTR(-EINVAL);
}

struct nvmap_handle_ref *nvmap_create_handle_from_id(
			struct nvmap_client *client, u32 id)
{
	struct nvmap_handle *handle;
	struct nvmap_handle_ref *ref;
	bool is_ro = false;

	if (WARN_ON(!client))
		return ERR_PTR(-EINVAL);

	if (is_nvmap_id_ro(client, id, &is_ro) != 0) {
		pr_err("Handle ID RO check failed\n");
		return ERR_PTR(-EINVAL);
	}

	if (is_ro)
		return nvmap_dup_handle_ro(client, id);

	handle = nvmap_handle_get_from_id(client, id);
	if (IS_ERR_OR_NULL(handle)) {
		/* fd might be dmabuf fd received from parent process.
		 * Its entry is not made in id_array.
		 */
		handle = nvmap_handle_get_from_dmabuf_fd(client, id);
		if (IS_ERR(handle))
			return ERR_CAST(handle);
	}

	ref = nvmap_duplicate_handle(client, handle, false, false);
	nvmap_handle_put(handle);
	return ref;
}

struct nvmap_handle_ref *nvmap_create_handle_from_fd(
			struct nvmap_client *client, int fd)
{
	struct nvmap_handle *handle;
	struct nvmap_handle_ref *ref;
	bool is_ro = false;

	if (WARN_ON(client == NULL))
		return ERR_PTR(-EINVAL);

	handle = nvmap_handle_get_from_dmabuf_fd(client, fd);
	if (IS_ERR(handle))
		return ERR_CAST(handle);

	if (is_nvmap_dmabuf_fd_ro(fd, &is_ro) != 0) {
		pr_err("Dmabuf fd RO check failed\n");
		nvmap_handle_put(handle);
		return ERR_PTR(-EINVAL);
	}

	if (is_ro)
		ref = nvmap_duplicate_handle(client, handle, false, true);
	else
		ref = nvmap_duplicate_handle(client, handle, false, false);

	nvmap_handle_put(handle);
	return ref;
}

struct nvmap_handle_ref *nvmap_dup_handle_ro(struct nvmap_client *client,
					     int id)
{
	struct nvmap_handle *h;
	struct nvmap_handle_ref *ref = NULL;
	long remain;

	if (client == NULL)
		return ERR_PTR(-EINVAL);

	h = nvmap_handle_get_from_id(client, id);
	if (IS_ERR_OR_NULL(h)) {
		/* fd might be dmabuf fd received from parent process.
		 * Its entry is not made in id_array.
		 */
		h = nvmap_handle_get_from_dmabuf_fd(client, id);
		if (IS_ERR(h))
			return ERR_CAST(h);
	}

	mutex_lock(&h->lock);
	if (h->dmabuf_ro == NULL) {
		h->dmabuf_ro = __nvmap_make_dmabuf(client, h, true);
		if (IS_ERR(h->dmabuf_ro)) {
			nvmap_handle_put(h);
			mutex_unlock(&h->lock);
			return ERR_CAST(h->dmabuf_ro);
		}
	} else {
#if defined(NV_GET_FILE_RCU_HAS_DOUBLE_PTR_FILE_ARG) /* Linux 6.7 */
		if (!get_file_rcu(&h->dmabuf_ro->file)) {
#else
		if (!get_file_rcu(h->dmabuf_ro->file)) {
#endif
			mutex_unlock(&h->lock);
			remain = wait_event_interruptible_timeout(h->waitq,
					!h->dmabuf_ro, (long)msecs_to_jiffies(100U));
			if (remain > 0 && !h->dmabuf_ro) {
				mutex_lock(&h->lock);
				h->dmabuf_ro = __nvmap_make_dmabuf(client, h, true);
				if (IS_ERR(h->dmabuf_ro)) {
					nvmap_handle_put(h);
					mutex_unlock(&h->lock);
					return ERR_CAST(h->dmabuf_ro);
				}
			} else {
				nvmap_handle_put(h);
				return ERR_PTR(-EINVAL);
			}
		}
	}
	mutex_unlock(&h->lock);

	ref = nvmap_duplicate_handle(client, h, false, true);
	/*
	 * When new RO dmabuf created or duplicated, one extra dma_buf refcount is taken so to
	 * avoid getting it freed by another process, until duplication completes. Decrement that
	 * extra refcount here.
	 */
	dma_buf_put(h->dmabuf_ro);
	nvmap_handle_put(h);

	return ref;
}

void nvmap_free_handle(struct nvmap_client *client,
		       struct nvmap_handle *handle, bool is_ro)
{
	struct nvmap_handle_ref *ref;
	struct nvmap_handle *h;

	nvmap_ref_lock(client);

	ref = __nvmap_validate_locked(client, handle, is_ro);
	if (!ref) {
		nvmap_ref_unlock(client);
		return;
	}

	BUG_ON(!ref->handle);
	h = ref->handle;

	if (atomic_dec_return(&ref->dupes)) {
		NVMAP_TAG_TRACE(trace_nvmap_free_handle,
			NVMAP_TP_ARGS_CHR(client, h, ref));
		nvmap_ref_unlock(client);
		goto out;
	}

	smp_rmb();
	rb_erase(&ref->node, &client->handle_refs);
	BUG_ON(client->handle_count == 0U);
	client->handle_count--;
	atomic_dec(&ref->handle->share_count);

	nvmap_ref_unlock(client);

	if (h->owner == client)
		h->owner = NULL;

	/*
	 * When a reference is freed, decrement rss counter of the process corresponding
	 * to this ref and do mmput so that mm_struct can be freed, if required.
	 */
	if (ref->mm != NULL && ref->anon_count != 0) {
		nvmap_add_mm_counter(ref->mm, MM_ANONPAGES, -ref->anon_count);
		mmput(ref->mm);
		ref->mm = NULL;
		ref->anon_count = 0;
	}

	if (is_ro)
		dma_buf_put(ref->handle->dmabuf_ro);
	else
		dma_buf_put(ref->handle->dmabuf);
	NVMAP_TAG_TRACE(trace_nvmap_free_handle,
		NVMAP_TP_ARGS_CHR(client, h, ref));
	kfree(ref);

out:
	BUG_ON(!atomic_read(&h->ref));
	nvmap_handle_put(h);
}

void nvmap_free_handle_from_fd(struct nvmap_client *client,
			       int id)
{
	bool is_ro = false;
	struct nvmap_handle *handle;
	struct dma_buf *dmabuf = NULL;
	int handle_ref = 0;
	long dmabuf_ref = 0;

	handle = nvmap_handle_get_from_id(client, id);
	if (IS_ERR_OR_NULL(handle))
		return;

	if (is_nvmap_id_ro(client, id, &is_ro) != 0) {
		nvmap_handle_put(handle);
		return;
	}

	if (client->ida)
		nvmap_id_array_id_release(client->ida, id);

	nvmap_free_handle(client, handle, is_ro);
	mutex_lock(&handle->lock);
	dmabuf = is_ro ? handle->dmabuf_ro : handle->dmabuf;
	if (dmabuf && dmabuf->file) {
		dmabuf_ref = file_count(dmabuf->file);
	} else {
		dmabuf_ref = 0;
	}
	mutex_unlock(&handle->lock);
	handle_ref = atomic_read(&handle->ref);

	trace_refcount_free_handle(handle, dmabuf, handle_ref, dmabuf_ref,
				is_ro ? "RO" : "RW");
	nvmap_handle_put(handle);
}

static int nvmap_assign_pages_per_handle(struct nvmap_handle *src_h,
			struct nvmap_handle *dest_h, u64 src_h_start,
			u64 src_h_end, u32 *pg_cnt)
{
	/* Increament ref count of source handle as its pages
	 * are referenced here to create new nvmap handle.
	 * By increamenting the ref count of source handle,
	 * source handle pages are not freed until new handle's fd is not closed.
	 * Note: nvmap_dmabuf_release, need to decreement source handle ref count
	 */
	u32 sum, current_pg_cnt, initial_pg_cnt;

	src_h = nvmap_handle_get(src_h);
	if (!src_h)
		return -EINVAL;

	initial_pg_cnt = *pg_cnt;

	while (src_h_start < src_h_end) {
		unsigned long next;
		struct page *dest_page;

		dest_h->pgalloc.pages[*pg_cnt] =
			src_h->pgalloc.pages[src_h_start >> PAGE_SHIFT];
		dest_page = nvmap_to_page(dest_h->pgalloc.pages[*pg_cnt]);
		get_page(dest_page);

		next = min(((src_h_start + PAGE_SIZE) & PAGE_MASK),
				src_h_end);
		src_h_start = next;

		if (check_add_overflow(*pg_cnt, 1U, &sum)) {
			current_pg_cnt = *pg_cnt;

			while (current_pg_cnt >= 0U) {
				dest_page = nvmap_to_page(
							dest_h->pgalloc.pages[current_pg_cnt]);
				put_page(dest_page);

				if (current_pg_cnt == initial_pg_cnt)
					break;

				current_pg_cnt--;
			}

			return -EOVERFLOW;
		}

		*pg_cnt = sum;
	}

	mutex_lock(&dest_h->pg_ref_h_lock);
	list_add_tail(&src_h->pg_ref, &dest_h->pg_ref_h);
	mutex_unlock(&dest_h->pg_ref_h_lock);

	return 0;
}

int nvmap_assign_pages_to_handle(struct nvmap_client *client,
			       struct nvmap_handle **hs, struct nvmap_handle *h,
			       struct handles_range *rng)
{
	size_t nr_page = h->size >> PAGE_SHIFT;
	struct page **pages;
	u64 end_cur = 0;
	u64 start = 0;
	u64 end = 0;
	u32 pg_cnt = 0;
	u32 i;
	int err = 0;
	u64 diff, sum;

	h = nvmap_handle_get(h);
	if (!h)
		return -EINVAL;

	if (h->alloc) {
		nvmap_handle_put(h);
		return -EEXIST;
	}

	pages = nvmap_altalloc(nr_page * sizeof(*pages));
	if (!pages) {
		nvmap_handle_put(h);
		return -ENOMEM;
	}
	h->pgalloc.pages = pages;

	start = rng->offs_start;
	end = rng->sz;

	for (i = rng->start; i <= rng->end; i++) {
		if (check_sub_overflow((u64)hs[i]->size, start, &diff)) {
			err = -EOVERFLOW;
			nvmap_altfree(pages, nr_page * sizeof(*pages));
			goto err_h;
		}

		end_cur = (end >= hs[i]->size) ? (diff) : end;
		if (check_add_overflow(start, end_cur, &sum)) {
			err = -EOVERFLOW;
			nvmap_altfree(pages, nr_page * sizeof(*pages));
			goto err_h;
		}

		err = nvmap_assign_pages_per_handle(hs[i], h, start, sum, &pg_cnt);
		if (err) {
			nvmap_altfree(pages, nr_page * sizeof(*pages));
			goto err_h;
		}
		end -= (hs[i]->size - start);
		start = 0;
	}

	h->flags = hs[0]->flags;
	h->heap_type = NVMAP_HEAP_IOVMM;
	h->heap_pgalloc = true;
	h->alloc = true;
	h->is_subhandle = true;
	mb();
	return err;
err_h:
	nvmap_handle_put(h);
	return err;
}

int is_nvmap_id_ro(struct nvmap_client *client, int id, bool *is_ro)
{
	struct nvmap_handle_info *info = NULL;
	struct dma_buf *dmabuf = NULL;

	if (WARN_ON(!client))
		goto fail;

	if (client->ida)
		dmabuf = nvmap_id_array_get_dmabuf_from_id(client->ida,
				id);
	else
		dmabuf = dma_buf_get(id);

	if (IS_ERR_OR_NULL(dmabuf))
		goto fail;

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
	pr_err("Handle RO check failed\n");
	return -EINVAL;
}

static void _nvmap_handle_free(struct nvmap_handle *h)
{
	unsigned int nr_page;
	struct nvmap_handle_dmabuf_priv *curr, *next;

	list_for_each_entry_safe(curr, next, &h->dmabuf_priv, list) {
		curr->priv_release(curr->priv);
		list_del(&curr->list);
		kfree_sensitive(curr);
	}

	if (nvmap_handle_remove(nvmap_dev, h) != 0)
		return;

	if (!h->alloc)
		goto out;

	nvmap_stats_inc(NS_RELEASE, h->size);
	nvmap_stats_dec(NS_TOTAL, h->size);
	if (!h->heap_pgalloc) {
		if (h->vaddr) {
			void *addr = h->vaddr;
			phys_addr_t base = nvmap_alloc_get_co_base(h);

			addr -= (base & ~PAGE_MASK);
			iounmap((void __iomem *)addr);
		}

		nvmap_heap_free(h->carveout);
		nvmap_kmaps_dec(h);
		h->carveout = NULL;
		h->vaddr = NULL;
		goto out;
	}

	nr_page = DIV_ROUND_UP(h->size, PAGE_SIZE);

	BUG_ON(h->size & ~PAGE_MASK);
	BUG_ON(!h->pgalloc.pages);

	if (h->vaddr) {
		nvmap_kmaps_dec(h);
		vunmap(h->vaddr);

		h->vaddr = NULL;
	}

	nvmap_alloc_free(h->pgalloc.pages, nr_page, h->from_va, h->is_subhandle);
out:
	NVMAP_TAG_TRACE(trace_nvmap_destroy_handle,
		NULL, get_current()->pid, 0, NVMAP_TP_ARGS_H(h));
	kfree(h);
}

/*
 * NOTE: this does not ensure the continued existence of the underlying
 * dma_buf. If you want ensure the existence of the dma_buf you must get an
 * nvmap_handle_ref as that is what tracks the dma_buf refs.
 */
struct nvmap_handle *nvmap_handle_get(struct nvmap_handle *h)
{
	int cnt;

	if (WARN_ON(!virt_addr_valid(h))) {
		pr_err("%s: invalid handle\n", current->group_leader->comm);
		return NULL;
	}

	cnt = atomic_inc_return(&h->ref);
	NVMAP_TAG_TRACE(trace_nvmap_handle_get, h, cnt);

	if (unlikely(cnt <= 1)) {
		pr_err("%s: %s attempt to get a freed handle\n",
			__func__, current->group_leader->comm);
		atomic_dec(&h->ref);
		return NULL;
	}

	return h;
}

void nvmap_handle_put(struct nvmap_handle *h)
{
	int cnt;

	if (WARN_ON(!virt_addr_valid(h)))
		return;
	cnt = atomic_dec_return(&h->ref);
	NVMAP_TAG_TRACE(trace_nvmap_handle_put, h, cnt);

	if (WARN_ON(cnt < 0)) {
		pr_err("%s: %s put to negative references\n",
			__func__, current->comm);
	} else if (cnt == 0)
		_nvmap_handle_free(h);
}

