// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2011-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * User-space interface to nvmap
 */

#define pr_fmt(fmt)	"nvmap: %s() " fmt, __func__

#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/nvmap.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/mman.h>

#include <asm/io.h>
#include <asm/memory.h>
#include <asm/uaccess.h>
#include <soc/tegra/common.h>
#include <trace/events/nvmap.h>

#ifdef NVMAP_CONFIG_SCIIPC
#include <linux/nvscierror.h>
#include <linux/nvsciipc_interface.h>
#include "nvmap_sci_ipc.h"
#endif

#include "nvmap_ioctl.h"
#include "nvmap_priv.h"
#include "nvmap_alloc.h"

#include <linux/syscalls.h>
#include <linux/nodemask.h>

#if defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif

extern bool vpr_cpu_access;

static ssize_t rw_handle(struct nvmap_client *client, struct nvmap_handle *h,
			 int is_read, unsigned long h_offs,
			 unsigned long sys_addr, unsigned long h_stride,
			 unsigned long sys_stride, unsigned long elem_size,
			 unsigned long count);

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

static int nvmap_install_fd(struct nvmap_client *client,
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

int nvmap_ioctl_getfd(struct file *filp, void __user *arg)
{
	struct nvmap_handle *handle = NULL;
	struct nvmap_create_handle op;
	struct nvmap_client *client = filp->private_data;
	struct dma_buf *dmabuf;
	int ret = 0;
	bool is_ro = false;
	long dmabuf_ref = 0;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	handle = nvmap_handle_get_from_id(client, op.handle);
	if (!IS_ERR_OR_NULL(handle)) {
		ret = is_nvmap_id_ro(client, op.handle, &is_ro);
		if (ret != 0) {
			pr_err("Handle ID RO check failed\n");
			goto fail;
		}

		op.fd = nvmap_get_dmabuf_fd(client, handle, is_ro);
		dmabuf = IS_ERR_VALUE((uintptr_t)op.fd) ?
			 NULL : (is_ro ? handle->dmabuf_ro : handle->dmabuf);
	} else {
		/*
		 * if we get an error, the fd might be non-nvmap dmabuf fd.
		 * Don't attach nvmap handle with this fd.
		 */
		dmabuf = dma_buf_get(op.handle);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);
		op.fd = nvmap_dmabuf_duplicate_gen_fd(client, dmabuf);
	}

	ret = nvmap_install_fd(client, handle,
				op.fd, arg, &op, sizeof(op), 0, dmabuf);

	if (!ret && !IS_ERR_OR_NULL(handle)) {
		mutex_lock(&handle->lock);
		if (dmabuf && dmabuf->file) {
			dmabuf_ref = atomic_long_read(&dmabuf->file->f_count);
		} else {
			dmabuf_ref = 0;
		}
		mutex_unlock(&handle->lock);
		trace_refcount_getfd(handle, dmabuf,
				atomic_read(&handle->ref),
				dmabuf_ref,
				is_ro ? "RO" : "RW");
	}

fail:
	if (!IS_ERR_OR_NULL(handle))
		nvmap_handle_put(handle);

	return ret;
}

int nvmap_ioctl_alloc(struct file *filp, void __user *arg)
{
	struct nvmap_alloc_handle op;
	struct nvmap_client *client = filp->private_data;
	struct nvmap_handle *handle;
	struct dma_buf *dmabuf = NULL;
	bool is_ro = false;
	int err;
	long dmabuf_ref = 0;
	size_t old_size;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	if (op.align & (op.align - 1))
		return -EINVAL;

	if (!op.handle)
		return -EINVAL;

	if (op.numa_nid > MAX_NUMNODES || (op.numa_nid != NUMA_NO_NODE && op.numa_nid < 0)) {
		pr_err("numa id:%d is invalid\n", op.numa_nid);
		return -EINVAL;
	}

	handle = nvmap_handle_get_from_id(client, op.handle);
	if (IS_ERR_OR_NULL(handle))
		return -EINVAL;

	if (op.heap_mask & NVMAP_HEAP_CARVEOUT_GPU) {
		/*
		 * In case of Gpu carveout, the handle size needs to be aligned to 2MB.
		 */
		old_size = handle->size;
		handle->size = ALIGN_2MB(handle->size);
		err = nvmap_alloc_handle_from_va(client, handle, op.va, op.flags, op.heap_mask);
		goto alloc_op_done;
	}

	if (!is_nvmap_memory_available(handle->size, op.heap_mask, op.numa_nid)) {
		nvmap_handle_put(handle);
		return -ENOMEM;
	}

	handle->numa_id = op.numa_nid;
	/* user-space handles are aligned to page boundaries, to prevent
	 * data leakage. */
	op.align = max_t(size_t, op.align, PAGE_SIZE);

	err = nvmap_alloc_handle(client, handle, op.heap_mask, op.align,
				  0, /* no kind */
				  op.flags & (~NVMAP_HANDLE_KIND_SPECIFIED),
				  NVMAP_IVM_INVALID_PEER);

alloc_op_done:
	if (!err && !is_nvmap_id_ro(client, op.handle, &is_ro)) {
		mutex_lock(&handle->lock);
		dmabuf = is_ro ? handle->dmabuf_ro : handle->dmabuf;
		if (dmabuf && dmabuf->file) {
			dmabuf_ref = atomic_long_read(&dmabuf->file->f_count);
		} else {
			dmabuf_ref = 0;
		}
		mutex_unlock(&handle->lock);
		trace_refcount_alloc(handle, dmabuf,
				atomic_read(&handle->ref),
				dmabuf_ref,
				is_ro ? "RO" : "RW");
	}

	if ((op.heap_mask & NVMAP_HEAP_CARVEOUT_GPU) && err)
		handle->size = old_size;
	nvmap_handle_put(handle);
	return err;
}

int nvmap_ioctl_alloc_ivm(struct file *filp, void __user *arg)
{
	struct nvmap_client *client = filp->private_data;
	struct nvmap_alloc_ivm_handle op;
	struct nvmap_handle *handle;
	int err;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	if (op.align & (op.align - 1))
		return -EINVAL;

	handle = nvmap_handle_get_from_id(client, op.handle);
	if (IS_ERR_OR_NULL(handle))
		return -EINVAL;

	/* user-space handles are aligned to page boundaries, to prevent
	 * data leakage. */
	op.align = max_t(size_t, op.align, PAGE_SIZE);

	err = nvmap_alloc_handle(client, handle, op.heap_mask, op.align,
				  0, /* no kind */
				  op.flags & (~NVMAP_HANDLE_KIND_SPECIFIED),
				  op.peer);
	nvmap_handle_put(handle);
	return err;
}

int nvmap_ioctl_create(struct file *filp, unsigned int cmd, void __user *arg)
{
	struct nvmap_create_handle op;
	struct nvmap_handle_ref *ref = NULL;
	struct nvmap_client *client = filp->private_data;
	struct dma_buf *dmabuf = NULL;
	struct nvmap_handle *handle = NULL;
	int fd = -1, ret = 0;
	u32 id = 0;
	bool is_ro = false;
	long dmabuf_ref = 0;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	if (!client)
		return -ENODEV;

	if (cmd == NVMAP_IOC_CREATE)
		op.size64 = op.size;

	if ((cmd == NVMAP_IOC_CREATE) || (cmd == NVMAP_IOC_CREATE_64)) {
		ref = nvmap_create_handle(client, op.size64, false);
		if (!IS_ERR(ref))
			ref->handle->orig_size = op.size64;
	} else if (cmd == NVMAP_IOC_FROM_FD) {
		ref = nvmap_create_handle_from_fd(client, op.fd);
		/* if we get an error, the fd might be non-nvmap dmabuf fd */
		if (IS_ERR_OR_NULL(ref)) {
			dmabuf = dma_buf_get(op.fd);
			if (IS_ERR(dmabuf))
				return PTR_ERR(dmabuf);
			fd = nvmap_dmabuf_duplicate_gen_fd(client, dmabuf);
			if (fd < 0)
				return fd;
		}
	} else {
		return -EINVAL;
	}

	if (!IS_ERR(ref)) {
		/*
		 * Increase reference dup count, so that handle is not freed accidentally
		 * due to other thread calling NvRmMemHandleFree
		 */
		atomic_inc(&ref->dupes);
		is_ro = ref->is_ro;
		handle = ref->handle;
		dmabuf = is_ro ? handle->dmabuf_ro :  handle->dmabuf;

		if (client->ida) {
			if (nvmap_id_array_id_alloc(client->ida,
				&id, dmabuf) < 0) {
				atomic_dec(&ref->dupes);
				if (dmabuf)
					dma_buf_put(dmabuf);
				nvmap_free_handle(client, handle, is_ro);
				return -ENOMEM;
			}
			if (cmd == NVMAP_IOC_CREATE_64)
				op.handle64 = id;
			else
				op.handle = id;

			if (copy_to_user(arg, &op, sizeof(op))) {
				atomic_dec(&ref->dupes);
				if (dmabuf)
					dma_buf_put(dmabuf);
				nvmap_free_handle(client, handle, is_ro);
				nvmap_id_array_id_release(client->ida, id);
				return -EFAULT;
			}
			ret = 0;
			goto out;
		}

		fd = nvmap_get_dmabuf_fd(client, ref->handle, is_ro);
	} else if (!dmabuf) {
		return PTR_ERR(ref);
	}

	if (cmd == NVMAP_IOC_CREATE_64)
		op.handle64 = fd;
	else
		op.handle = fd;

	ret = nvmap_install_fd(client, handle, fd,
				arg, &op, sizeof(op), 1, dmabuf);

out:
	if (!ret && !IS_ERR_OR_NULL(handle)) {
		mutex_lock(&handle->lock);
		if (dmabuf && dmabuf->file) {
			dmabuf_ref = atomic_long_read(&dmabuf->file->f_count);
		} else {
			dmabuf_ref = 0;
		}
		mutex_unlock(&handle->lock);

		if (cmd == NVMAP_IOC_FROM_FD)
			trace_refcount_create_handle_from_fd(handle, dmabuf,
				atomic_read(&handle->ref),
				dmabuf_ref,
				is_ro ? "RO" : "RW");
		else
			trace_refcount_create_handle(handle, dmabuf,
				atomic_read(&handle->ref),
				dmabuf_ref,
				is_ro ? "RO" : "RW");
	}

	if (!IS_ERR(ref))
		atomic_dec(&ref->dupes);
	return ret;
}

int nvmap_ioctl_create_from_va(struct file *filp, void __user *arg)
{
	int fd = -1;
	u32 id = 0;
	int err;
	struct nvmap_create_handle_from_va op;
	struct nvmap_handle_ref *ref = NULL;
	struct nvmap_client *client = filp->private_data;
	struct dma_buf *dmabuf = NULL;
	struct nvmap_handle *handle = NULL;
	bool is_ro = false;
	long dmabuf_ref = 0;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	if (client == NULL)
		return -ENODEV;

	is_ro = op.flags & NVMAP_HANDLE_RO;
	ref = nvmap_create_handle_from_va(client, op.va,
			op.size ? op.size : op.size64,
			op.flags);
	if (IS_ERR(ref))
		return PTR_ERR(ref);
	handle = ref->handle;

	err = nvmap_alloc_handle_from_va(client, handle,
					 op.va, op.flags, 0);
	if (err) {
		nvmap_free_handle(client, handle, is_ro);
		return err;
	}

	/*
	 * Increase reference dup count, so that handle is not freed accidentally
	 * due to other thread calling NvRmMemHandleFree
	 */
	atomic_inc(&ref->dupes);
	dmabuf = is_ro ? ref->handle->dmabuf_ro : ref->handle->dmabuf;
	if (client->ida) {

		err = nvmap_id_array_id_alloc(client->ida, &id,
			dmabuf);
		if (err < 0) {
			atomic_dec(&ref->dupes);
			if (dmabuf)
				dma_buf_put(dmabuf);
			nvmap_free_handle(client, ref->handle, is_ro);
			return -ENOMEM;
		}
		op.handle = id;
		if (copy_to_user(arg, &op, sizeof(op))) {
			atomic_dec(&ref->dupes);
			if (dmabuf)
				dma_buf_put(dmabuf);
			nvmap_free_handle(client, ref->handle, is_ro);
			nvmap_id_array_id_release(client->ida, id);
			return -EFAULT;
		}
		err = 0;
		goto out;
	}

	fd = nvmap_get_dmabuf_fd(client, ref->handle, is_ro);

	op.handle = fd;

	err = nvmap_install_fd(client, ref->handle, fd,
				arg, &op, sizeof(op), 1, dmabuf);

out:
	if (!err) {
		mutex_lock(&handle->lock);
		if (dmabuf && dmabuf->file) {
			dmabuf_ref = atomic_long_read(&dmabuf->file->f_count);
		} else {
			dmabuf_ref = 0;
		}
		mutex_unlock(&handle->lock);
		trace_refcount_create_handle_from_va(handle, dmabuf,
				atomic_read(&handle->ref),
				dmabuf_ref,
				is_ro ? "RO" : "RW");
	}
	atomic_dec(&ref->dupes);
	return err;
}

int nvmap_ioctl_rw_handle(struct file *filp, int is_read, void __user *arg,
			  size_t op_size)
{
	struct nvmap_client *client = filp->private_data;
	struct nvmap_rw_handle __user *uarg = arg;
	struct nvmap_rw_handle op;
	struct nvmap_handle *h;
	ssize_t copied;
	int err = 0;
	unsigned long addr, offset, elem_size, hmem_stride, user_stride;
	unsigned long count;
	int handle;
	bool is_ro = false;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;
	addr = op.addr;
	handle = op.handle;
	offset = op.offset;
	elem_size = op.elem_size;
	hmem_stride = op.hmem_stride;
	user_stride = op.user_stride;
	count = op.count;

	if (!addr || !count || !elem_size)
		return -EINVAL;

	h = nvmap_handle_get_from_id(client, handle);
	if (IS_ERR_OR_NULL(h))
		return -EINVAL;

	if (is_nvmap_id_ro(client, handle, &is_ro) != 0) {
		pr_err("Handle ID RO check failed\n");
		err = -EINVAL;
		goto fail;
	}

	/* Don't allow write on RO handle */
	if (!is_read && is_ro) {
		pr_err("Write operation is not allowed on RO handle\n");
		err = -EPERM;
		goto fail;
	}

	if (!vpr_cpu_access && is_read &&
	    h->heap_type == NVMAP_HEAP_CARVEOUT_VPR) {
		pr_err("CPU read operation is not allowed on VPR carveout\n");
		err = -EPERM;
		goto fail;
	}

	/*
	 * If Buffer is RO and write operation is asked from the buffer,
	 * return error.
	 */
	if (h->is_ro && !is_read) {
		err = -EPERM;
		goto fail;
	}

	nvmap_kmaps_inc(h);
	trace_nvmap_ioctl_rw_handle(client, h, is_read, offset,
				    addr, hmem_stride,
				    user_stride, elem_size, count);
	copied = rw_handle(client, h, is_read, offset,
			   addr, hmem_stride,
			   user_stride, elem_size, count);
	nvmap_kmaps_dec(h);

	if (copied < 0) {
		err = copied;
		copied = 0;
	} else if (copied < (count * elem_size))
		err = -EINTR;

	__put_user(copied, &uarg->count);

fail:
	nvmap_handle_put(h);
	return err;
}

int nvmap_ioctl_cache_maint(struct file *filp, void __user *arg, int op_size)
{
	struct nvmap_client *client = filp->private_data;
	struct nvmap_cache_op op;
	struct nvmap_cache_op_64 op64;

	if (op_size == sizeof(op)) {
		if (copy_from_user(&op, arg, sizeof(op)))
			return -EFAULT;
		op64.addr = op.addr;
		op64.handle = op.handle;
		op64.len = op.len;
		op64.op = op.op;
	} else {
		if (copy_from_user(&op64, arg, sizeof(op64)))
			return -EFAULT;
	}

	return __nvmap_cache_maint(client, &op64);
}

int nvmap_ioctl_free(struct file *filp, unsigned long arg)
{
	struct nvmap_client *client = filp->private_data;
	struct dma_buf *dmabuf = NULL;

	if (!arg || IS_ERR_OR_NULL(client))
		return 0;

	nvmap_free_handle_from_fd(client, arg);

	if (client->ida) {
		dmabuf = dma_buf_get(arg);
		/* id is dmabuf fd created from foreign dmabuf */
		if (!IS_ERR_OR_NULL(dmabuf)) {
			dma_buf_put(dmabuf);
			goto close_fd;
		}
		return 0;
	}
close_fd:
	return SYS_CLOSE(arg);
}

static ssize_t rw_handle(struct nvmap_client *client, struct nvmap_handle *h,
			 int is_read, unsigned long h_offs,
			 unsigned long sys_addr, unsigned long h_stride,
			 unsigned long sys_stride, unsigned long elem_size,
			 unsigned long count)
{
	ssize_t copied = 0;
	void *tmp = NULL;
	void *addr;
	int ret = 0;

	if ((h->heap_type & nvmap_dev->cpu_access_mask) == 0)
		return -EPERM;

	if (elem_size == 0 || count == 0)
		return -EINVAL;

	if (!h->alloc)
		return -EFAULT;

	if (elem_size == h_stride && elem_size == sys_stride && (h_offs % 8 == 0)) {
		elem_size *= count;
		h_stride = elem_size;
		sys_stride = elem_size;
		count = 1;
	}

	if (elem_size > h->size ||
		h_offs >= h->size ||
		elem_size > sys_stride ||
		elem_size > h_stride ||
		sys_stride > (h->size - h_offs) / count ||
		h_offs + h_stride * (count - 1) + elem_size > h->size)
		return -EINVAL;

	if (h->vaddr == NULL) {
		if (!__nvmap_mmap(h))
			return -ENOMEM;
		__nvmap_munmap(h, h->vaddr);
	}

	addr = h->vaddr + h_offs;

	/* Allocate buffer to cache data for VPR write */
	if (!is_read && h->heap_type == NVMAP_HEAP_CARVEOUT_VPR) {
		tmp = vmalloc(elem_size);
		if (!tmp)
			return -ENOMEM;
	}

	while (count--) {
		if (h_offs + elem_size > h->size) {
			pr_warn("read/write outside of handle\n");
			ret = -EFAULT;
			break;
		}
		if (is_read &&
		    !(h->userflags & NVMAP_HANDLE_CACHE_SYNC_AT_RESERVE))
			__nvmap_do_cache_maint(client, h, h_offs,
				h_offs + elem_size, NVMAP_CACHE_OP_INV, false);

		if (is_read)
			ret = copy_to_user((void __user *)sys_addr, addr, elem_size);
		else {
			if (h->heap_type == NVMAP_HEAP_CARVEOUT_VPR) {
				ret = copy_from_user(tmp, (void __user *)sys_addr,
						     elem_size);
				if (ret == 0)
					kasan_memcpy_toio((void __iomem *)addr, tmp, elem_size);
			} else
				ret = copy_from_user(addr, (void __user *)sys_addr, elem_size);
		}

		if (ret)
			break;

		if (!is_read &&
		    !(h->userflags & NVMAP_HANDLE_CACHE_SYNC_AT_RESERVE))
			__nvmap_do_cache_maint(client, h, h_offs,
				h_offs + elem_size, NVMAP_CACHE_OP_WB_INV,
				false);

		copied += elem_size;
		sys_addr += sys_stride;
		h_offs += h_stride;
		addr += h_stride;
	}

	/* Release the buffer used for VPR write */
	if (!is_read && h->heap_type == NVMAP_HEAP_CARVEOUT_VPR && tmp)
		vfree(tmp);

	return ret ?: copied;
}

int nvmap_ioctl_get_ivcid(struct file *filp, void __user *arg)
{
	struct nvmap_client *client = filp->private_data;
	struct nvmap_create_handle op;
	struct nvmap_handle *h = NULL;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	h = nvmap_handle_get_from_id(client, op.ivm_handle);
	if (IS_ERR_OR_NULL(h))
		return -EINVAL;

	if (!h->alloc) { /* || !h->ivm_id) { */
		nvmap_handle_put(h);
		return -EFAULT;
	}

	op.ivm_id = h->ivm_id;

	nvmap_handle_put(h);

	return copy_to_user(arg, &op, sizeof(op)) ? -EFAULT : 0;
}

int nvmap_ioctl_get_ivc_heap(struct file *filp, void __user *arg)
{
	struct nvmap_device *dev = nvmap_dev;
	int i;
	unsigned int heap_mask = 0;

	for (i = 0; i < dev->nr_carveouts; i++) {
		struct nvmap_carveout_node *co_heap = &dev->heaps[i];
		unsigned int peer;

		if (!(co_heap->heap_bit & NVMAP_HEAP_CARVEOUT_IVM))
			continue;

		if (nvmap_query_heap_peer(co_heap->carveout, &peer) < 0)
			return -EINVAL;

		heap_mask |= BIT(peer);
	}

	if (copy_to_user(arg, &heap_mask, sizeof(heap_mask)))
		return -EFAULT;

	return 0;
}

int nvmap_ioctl_create_from_ivc(struct file *filp, void __user *arg)
{
	struct nvmap_create_handle op;
	struct nvmap_handle_ref *ref;
	struct nvmap_client *client = filp->private_data;
	int fd;
	phys_addr_t offs;
	size_t size = 0;
	unsigned int peer;
	struct nvmap_heap_block *block = NULL;

	/* First create a new handle and then fake carveout allocation */
	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	if (client == NULL)
		return -ENODEV;

	ref = nvmap_try_duplicate_by_ivmid(client, op.ivm_id, &block);
	if (IS_ERR_OR_NULL(ref)) {
		/*
		 * See nvmap_heap_alloc() for encoding details.
		 */
		offs = ((op.ivm_id &
		       ~((u64)NVMAP_IVM_IVMID_MASK << NVMAP_IVM_IVMID_SHIFT)) >>
			NVMAP_IVM_LENGTH_WIDTH) << (ffs(NVMAP_IVM_ALIGNMENT) - 1);
		size = (op.ivm_id &
			((1ULL << NVMAP_IVM_LENGTH_WIDTH) - 1)) << PAGE_SHIFT;
		peer = (op.ivm_id >> NVMAP_IVM_IVMID_SHIFT);

		ref = nvmap_create_handle(client, size, false);
		if (IS_ERR(ref)) {
			nvmap_heap_free(block);
			return PTR_ERR(ref);
		}
		ref->handle->orig_size = size;

		ref->handle->peer = peer;
		if (!block)
			block = nvmap_carveout_alloc(client, ref->handle,
					NVMAP_HEAP_CARVEOUT_IVM, &offs);
		if (!block) {
			nvmap_free_handle(client, ref->handle, false);
			return -ENOMEM;
		}

		ref->handle->heap_type = NVMAP_HEAP_CARVEOUT_IVM;
		ref->handle->heap_pgalloc = false;
		ref->handle->ivm_id = op.ivm_id;
		ref->handle->carveout = block;
		nvmap_set_heap_block_handle(block, ref->handle);
		mb();
		ref->handle->alloc = true;
		NVMAP_TAG_TRACE(trace_nvmap_alloc_handle_done,
			NVMAP_TP_ARGS_CHR(client, ref->handle, ref));
	}
	if (client->ida) {
		u32 id = 0;

		if (nvmap_id_array_id_alloc(client->ida, &id,
			ref->handle->dmabuf) < 0) {
			if (ref->handle->dmabuf)
				dma_buf_put(ref->handle->dmabuf);
			nvmap_free_handle(client, ref->handle, false);
			return -ENOMEM;
		}
		op.ivm_handle = id;
		if (copy_to_user(arg, &op, sizeof(op))) {
			if (ref->handle->dmabuf)
				dma_buf_put(ref->handle->dmabuf);
			nvmap_free_handle(client, ref->handle, false);
			nvmap_id_array_id_release(client->ida, id);
			return -EFAULT;
		}
		return 0;
	}

	fd = nvmap_get_dmabuf_fd(client, ref->handle, false);

	op.ivm_handle = fd;
	return nvmap_install_fd(client, ref->handle, fd,
				arg, &op, sizeof(op), 1, ref->handle->dmabuf);
}

int nvmap_ioctl_gup_test(struct file *filp, void __user *arg)
{
	struct nvmap_client *client = filp->private_data;
	int err = -EINVAL;
	struct nvmap_gup_test op;
	struct vm_area_struct *vma;
	struct nvmap_handle *handle;
	size_t i;
	size_t nr_page;
	struct page **pages;
	struct mm_struct *mm = current->mm;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;
	op.result = 1;

	nvmap_acquire_mmap_read_lock(mm);
	vma = find_vma(mm, op.va);
	if (unlikely(vma == NULL) || (unlikely(op.va < vma->vm_start)) ||
		unlikely(op.va >= vma->vm_end)) {
		nvmap_release_mmap_read_lock(mm);
		goto exit;
	}

	handle = nvmap_handle_get_from_id(client, op.handle);
	if (IS_ERR_OR_NULL(handle)) {
		nvmap_release_mmap_read_lock(mm);
		goto exit;
	}

	if (vma->vm_end - vma->vm_start != handle->size) {
		pr_err("handle size(0x%zx) and vma size(0x%lx) don't match\n",
			 handle->size, vma->vm_end - vma->vm_start);
		nvmap_release_mmap_read_lock(mm);
		goto put_handle;
	}

	err = -ENOMEM;
	nr_page = handle->size >> PAGE_SHIFT;
	pages = nvmap_altalloc(nr_page * sizeof(*pages));
	if (IS_ERR_OR_NULL(pages)) {
		err = PTR_ERR(pages);
		nvmap_release_mmap_read_lock(mm);
		goto put_handle;
	}

	err = nvmap_get_user_pages(op.va & PAGE_MASK, nr_page, pages, false, 0);
	if (err) {
		nvmap_release_mmap_read_lock(mm);
		goto free_pages;
	}

	nvmap_release_mmap_read_lock(mm);

	for (i = 0; i < nr_page; i++) {
		if (handle->pgalloc.pages[i] != pages[i]) {
			pr_err("page pointers don't match, %p %p\n",
			       handle->pgalloc.pages[i], pages[i]);
			op.result = 0;
		}
	}

	if (op.result)
		err = 0;

	if (copy_to_user(arg, &op, sizeof(op)))
		err = -EFAULT;

	for (i = 0; i < nr_page; i++) {
		put_page(pages[i]);
	}
free_pages:
	nvmap_altfree(pages, nr_page * sizeof(*pages));
put_handle:
	nvmap_handle_put(handle);
exit:
	pr_info("GUP Test %s\n", err ? "failed" : "passed");
	return err;
}

int nvmap_ioctl_set_tag_label(struct file *filp, void __user *arg)
{
	struct nvmap_set_tag_label op;
	struct nvmap_device *dev = nvmap_dev;
	int err;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	if (op.len > NVMAP_TAG_LABEL_MAXLEN)
		op.len = NVMAP_TAG_LABEL_MAXLEN;

	if (op.len)
		err = nvmap_define_tag(dev, op.tag,
			(const char __user *)op.addr, op.len);
	else
		err = nvmap_remove_tag(dev, op.tag);

	return err;
}

int nvmap_ioctl_get_available_heaps(struct file *filp, void __user *arg)
{
	struct nvmap_available_heaps op;
	int i;

	memset(&op, 0, sizeof(op));

	for (i = 0; i < nvmap_dev->nr_carveouts; i++)
		op.heaps |= nvmap_dev->heaps[i].heap_bit;

	if (copy_to_user(arg, &op, sizeof(op))) {
		pr_err("copy_to_user failed\n");
		return -EINVAL;
	}

	return 0;
}

int nvmap_ioctl_get_handle_parameters(struct file *filp, void __user *arg)
{
	struct nvmap_client *client = filp->private_data;
	struct nvmap_handle_parameters op;
	struct nvmap_handle *handle;
	bool is_ro = false;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	handle = nvmap_handle_get_from_id(client, op.handle);
	if (IS_ERR_OR_NULL(handle))
		goto exit;

	if (!handle->alloc) {
		op.heap = 0;
	} else {
		/*
		 * When users specify GPU heap to allocate from, it means the
		 * allocation is done from hugetlbfs. But the heap_type stored
		 * in handle struct would still be IOVMM heap, as the pages are
		 * from system memory and not from any carveout. Also, a lot
		 * of nvmap APIs treat carveout and system memory in different ways
		 * hence it's necessary to store IOVMM heap in heap_type, but while
		 * querying the handle params, return GPU heap for such handles to
		 * be consistent.
		 */
		if (handle->has_hugetlbfs_pages)
			op.heap = NVMAP_HEAP_CARVEOUT_GPU;
		else
			op.heap = handle->heap_type;
	}

	/* heap_number, only valid for IVM carveout */
	op.heap_number = handle->peer;

	op.size = handle->size;

	/*
	 * Check handle is allocated or not while setting contig.
	 * If heap type is IOVMM, check if it has flag set for contiguous memory
	 * allocation request. Otherwise, if handle belongs to any carveout
	 * then all allocations are contiguous, hence set contig flag to true.
	 * When the handle is allocated from hugetlbfs, then return contig as false,
	 * since the entire buffer may not be contiguous.
	 */
	if (handle->alloc && !handle->has_hugetlbfs_pages &&
		((handle->heap_type == NVMAP_HEAP_IOVMM &&
		    handle->userflags & NVMAP_HANDLE_PHYS_CONTIG) ||
		handle->heap_type != NVMAP_HEAP_IOVMM)) {
		op.contig = 1U;
	} else {
		op.contig = 0U;
	}

	op.align = handle->align;
	op.offset = handle->offs;
	op.coherency = handle->flags;

	if (is_nvmap_id_ro(client, op.handle, &is_ro) != 0) {
		pr_err("Handle ID RO check failed\n");
		nvmap_handle_put(handle);
		goto exit;
	}

	if (is_ro)
		op.access_flags = NVMAP_HANDLE_RO;

	op.serial_id = handle->serial_id;

	if (copy_to_user(arg, &op, sizeof(op))) {
		pr_err("Failed to copy to userspace\n");
		nvmap_handle_put(handle);
		goto exit;
	}

	nvmap_handle_put(handle);
	return 0;

exit:
	return -ENODEV;
}

#ifdef NVMAP_CONFIG_SCIIPC
int nvmap_ioctl_get_sci_ipc_id(struct file *filp, void __user *arg)
{
	struct nvmap_client *client = filp->private_data;
	NvSciIpcEndpointVuid pr_vuid, lclu_vuid;
	struct nvmap_handle *handle = NULL;
	struct nvmap_sciipc_map op;
	struct dma_buf *dmabuf = NULL;
	bool is_ro = false;
	int ret = 0;
	long dmabuf_ref = 0;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	handle = nvmap_handle_get_from_id(client, op.handle);
	if (IS_ERR_OR_NULL(handle))
		return -ENODEV;

	if (is_nvmap_id_ro(client, op.handle, &is_ro) != 0) {
		pr_err("Handle ID RO check failed\n");
		ret = -EINVAL;
		goto exit;
	}

	/* Cannot create RW handle from RO handle */
	if (is_ro && (op.flags != PROT_READ)) {
		ret = -EPERM;
		goto exit;
	}

	ret = nvmap_validate_sci_ipc_params(client, op.auth_token,
		&pr_vuid, &lclu_vuid);
	if (ret)
		goto exit;

	ret = nvmap_create_sci_ipc_id(client, handle, op.flags,
			 &op.sci_ipc_id, pr_vuid, is_ro);
	if (ret)
		goto exit;

	if (copy_to_user(arg, &op, sizeof(op))) {
		pr_err("copy_to_user failed\n");
		ret = -EINVAL;
	}

exit:
	if (!ret) {
		mutex_lock(&handle->lock);
		dmabuf = is_ro ? handle->dmabuf_ro : handle->dmabuf;
		if (dmabuf && dmabuf->file) {
			dmabuf_ref = atomic_long_read(&dmabuf->file->f_count);
		} else {
			dmabuf_ref = 0;
		}
		mutex_unlock(&handle->lock);
		trace_refcount_get_sci_ipc_id(handle, dmabuf,
				atomic_read(&handle->ref),
				dmabuf_ref,
				is_ro ? "RO" : "RW");
	}

	nvmap_handle_put(handle);
	return ret;
}

int nvmap_ioctl_handle_from_sci_ipc_id(struct file *filp, void __user *arg)
{
	struct nvmap_client *client = filp->private_data;
	NvSciIpcEndpointVuid pr_vuid, lclu_vuid;
	struct nvmap_sciipc_map op;
	int ret = 0;

	if (copy_from_user(&op, arg, sizeof(op))) {
		ret =  -EFAULT;
		goto exit;
	}

	ret = nvmap_validate_sci_ipc_params(client, op.auth_token,
		&pr_vuid, &lclu_vuid);
	if (ret)
		goto exit;

	ret = nvmap_get_handle_from_sci_ipc_id(client, op.flags,
			 op.sci_ipc_id, lclu_vuid, &op.handle);
	if (ret)
		goto exit;

	if (copy_to_user(arg, &op, sizeof(op))) {
		pr_err("copy_to_user failed\n");
		ret = -EINVAL;
	}
exit:
	return ret;
}
#else
int nvmap_ioctl_get_sci_ipc_id(struct file *filp, void __user *arg)
{
	return -EPERM;
}
int nvmap_ioctl_handle_from_sci_ipc_id(struct file *filp, void __user *arg)
{
	return -EPERM;
}
#endif

/*
 * This function calculates total memory and allocable free memory by parsing
 * /sys/devices/system/node/nodeX/meminfo file
 * total memory = value of MemTotal field
 * allocable free memory = Value of MemFree field + (Value of KReclaimable) / 2
 * Note that the above allocable free memory value is an estimate and may not be an
 * exact value and may need further tuning in future.
 */
#define MEMINFO_SIZE 1536
static int compute_memory_stat(u64 *total, u64 *free, int numa_id)
{
	struct file *file;
	char meminfo_path[64] = {'\0'};
	u8 *buf;
	loff_t pos = 0;
	char *buffer, *ptr;
	u64 mem_total, mem_free, reclaimable;
	bool total_found = false, free_found = false, reclaimable_found = false;
	int nid, rc;

	sprintf(meminfo_path, "/sys/devices/system/node/node%d/meminfo", numa_id);
	file = filp_open(meminfo_path, O_RDONLY, 0);
	if (IS_ERR(file)) {
		pr_err("Could not open file:%s\n", meminfo_path);
		return -EINVAL;
	}

	buf = nvmap_altalloc(MEMINFO_SIZE * sizeof(*buf));
	if (!buf) {
		pr_err("Memory allocation failed\n");
		filp_close(file, NULL);
		return -ENOMEM;
	}

	rc = kernel_read(file, buf, MEMINFO_SIZE - 1, &pos);
	buf[rc] = '\n';
	filp_close(file, NULL);
	buffer = buf;
	ptr = buf;
	while ((ptr = strsep(&buffer, "\n")) != NULL) {
		if (!ptr[0])
			continue;
		else if (sscanf(ptr, "Node %d MemTotal: %llu kB\n", &nid, &mem_total) == 2)
			total_found = true;
		else if (sscanf(ptr, "Node %d MemFree: %llu kB\n", &nid, &mem_free) == 2)
			free_found = true;
		else if (sscanf(ptr, "Node %d KReclaimable: %llu kB\n", &nid, &reclaimable) == 2)
			reclaimable_found = true;
	}

	nvmap_altfree(buf, MEMINFO_SIZE * sizeof(*buf));
	if (nid == numa_id && total_found && free_found && reclaimable_found) {
		*total = mem_total * 1024;
		*free = (mem_free + reclaimable / 2) * 1024;
		return 0;
	}
	return -EINVAL;
}

/*
 * This function calculates HugePages_Total and HugePages_Free by parsing
 * /sys/devices/system/node/nodeX/meminfo file
 */
static int compute_hugetlbfs_stat(u64 *total, u64 *free, int numa_id)
{
	struct file *file;
	char meminfo_path[64] = {'\0'};
	u8 *buf;
	loff_t pos = 0;
	char *buffer, *ptr;
	unsigned int huge_total, huge_free;
	bool total_found = false, free_found = false;
	int nid, rc;

	sprintf(meminfo_path, "/sys/devices/system/node/node%d/meminfo", numa_id);
	file = filp_open(meminfo_path, O_RDONLY, 0);
	if (IS_ERR(file)) {
		pr_err("Could not open file:%s\n", meminfo_path);
		return -EINVAL;
	}

	buf = nvmap_altalloc(MEMINFO_SIZE * sizeof(*buf));
	if (!buf) {
		pr_err("Memory allocation failed\n");
		filp_close(file, NULL);
		return -ENOMEM;
	}

	rc = kernel_read(file, buf, MEMINFO_SIZE - 1, &pos);
	buf[rc] = '\n';
	filp_close(file, NULL);
	buffer = buf;
	ptr = buf;
	while ((ptr = strsep(&buffer, "\n")) != NULL) {
		if (!ptr[0])
			continue;
		else if (sscanf(ptr, "Node %d HugePages_Total: %u\n", &nid, &huge_total) == 2)
			total_found = true;
		else if (sscanf(ptr, "Node %d HugePages_Free: %u\n", &nid, &huge_free) == 2)
			free_found = true;
	}

	nvmap_altfree(buf, MEMINFO_SIZE * sizeof(*buf));
	if (nid == numa_id && total_found && free_found) {
		*total = (u64)huge_total * SIZE_2MB;
		*free = (u64)huge_free * SIZE_2MB;
		return 0;
	}
	return -EINVAL;
}

/*
 * This function calculates allocatable free memory using following formula:
 * free_mem = avail mem - cma free
 * free_mem = free_mem - (free_mem / 1000);
 * The CMA memory is not allocatable by NvMap for regular allocations and it
 * is part of Available memory reported, so subtract it from available memory.
 */
int system_heap_free_mem(unsigned long *mem_val)
{
	long available_mem = 0;
	unsigned long free_mem = 0;
	unsigned long cma_free = 0;

	available_mem = si_mem_available();
	if (available_mem <= 0) {
		*mem_val = 0;
		return 0;
	}

	cma_free = global_zone_page_state(NR_FREE_CMA_PAGES) << PAGE_SHIFT;
	if ((available_mem << PAGE_SHIFT) < cma_free) {
		*mem_val = 0;
		return 0;
	}
	free_mem = (available_mem << PAGE_SHIFT) - cma_free;

	/* reduce free_mem by ~ 0.1% */
	free_mem = free_mem - (free_mem / 1000);

	*mem_val = free_mem;
	return 0;
}

static unsigned long system_heap_total_mem(void)
{
	struct sysinfo sys_heap;

	si_meminfo(&sys_heap);

	return sys_heap.totalram << PAGE_SHIFT;
}

static int nvmap_query_heap_params(void __user *arg, bool is_numa_aware)
{
	unsigned int carveout_mask = NVMAP_HEAP_CARVEOUT_MASK;
	unsigned int iovmm_mask = NVMAP_HEAP_IOVMM;
	struct nvmap_query_heap_params op;
	struct nvmap_heap *heap;
	unsigned int type;
	int ret = 0;
	int i;
	int numa_id;
	unsigned long free_mem = 0;

	memset(&op, 0, sizeof(op));
	if (copy_from_user(&op, arg, sizeof(op))) {
		ret =  -EFAULT;
		goto exit;
	}

	type = op.heap_mask;
	if (type & (type - 1)) {
		ret = -EINVAL;
		goto exit;
	}

	if (is_numa_aware)
		numa_id = op.numa_id;

	if (nvmap_convert_carveout_to_iovmm) {
		carveout_mask &= ~NVMAP_HEAP_CARVEOUT_GENERIC;
		iovmm_mask |= NVMAP_HEAP_CARVEOUT_GENERIC;
	} else if (nvmap_convert_iovmm_to_carveout) {
		if (type & NVMAP_HEAP_IOVMM) {
			type &= ~NVMAP_HEAP_IOVMM;
			type |= NVMAP_HEAP_CARVEOUT_GENERIC;
		}
	}

	/* To Do: select largest free block */
	op.largest_free_block = PAGE_SIZE;

	/*
	 * Special case: GPU heap
	 * When user is querying the GPU heap, that means the buffer was allocated from
	 * hugetlbfs, so we need to return the HugePages_Total, HugePages_Free values
	 */
	if (type & NVMAP_HEAP_CARVEOUT_GPU) {
		if (!is_numa_aware)
			numa_id = 0;

		ret = compute_hugetlbfs_stat(&op.total, &op.free, numa_id);
		if (ret)
			goto exit;

		op.largest_free_block = SIZE_2MB;
		op.granule_size = SIZE_2MB;
	} else if (type & NVMAP_HEAP_CARVEOUT_MASK) {
		for (i = 0; i < nvmap_dev->nr_carveouts; i++) {
			if ((type & nvmap_dev->heaps[i].heap_bit) &&
				(is_numa_aware ?
				(numa_id == nvmap_get_heap_nid(nvmap_dev->heaps[i].carveout)) :
				true)) {
				heap = nvmap_dev->heaps[i].carveout;
				op.total = nvmap_query_heap_size(heap);
				op.free = nvmap_get_heap_free_size(heap);
				break;
			}
		}
		/* If queried heap is not present */
		if (i >= nvmap_dev->nr_carveouts)
			return -ENODEV;

	} else if (type & iovmm_mask) {
		if (num_online_nodes() > 1) {
			/* multiple numa node exist */
			ret = compute_memory_stat(&op.total, &op.free, numa_id);
			if (ret)
				goto exit;
		} else {
			/* Single numa node case
			 * Check if input numa_id is zero or not.
			 */
			if (is_numa_aware && numa_id != 0) {
				pr_err("Incorrect input for numa_id:%d\n", numa_id);
				return -EINVAL;
			}
			op.total = system_heap_total_mem();
			ret = system_heap_free_mem(&free_mem);
			if (ret)
				goto exit;
			op.free = free_mem;
		}
		op.granule_size = PAGE_SIZE;
	}

	/*
	 * Align free size reported to the previous page.
	 * This avoids any AllocAttr failures due to using PAGE_ALIGN
	 * for allocating exactly the free memory reported.
	 */
	op.free = op.free & PAGE_MASK;
	if (copy_to_user(arg, &op, sizeof(op)))
		ret = -EFAULT;
exit:
	return ret;
}

int nvmap_ioctl_query_heap_params(struct file *filp, void __user *arg)
{
	return nvmap_query_heap_params(arg, false);
}

int nvmap_ioctl_query_heap_params_numa(struct file *filp, void __user *arg)
{
	return nvmap_query_heap_params(arg, true);
}

int nvmap_ioctl_dup_handle(struct file *filp, void __user *arg)
{
	struct nvmap_client *client = filp->private_data;
	struct nvmap_handle_ref *ref = NULL;
	struct nvmap_handle *handle = NULL;
	struct nvmap_duplicate_handle op;
	struct dma_buf *dmabuf = NULL;
	int fd = -1, ret = 0;
	u32 id = 0;
	bool is_ro = false;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	if (client == NULL)
		return -ENODEV;

	if (is_nvmap_id_ro(client, op.handle, &is_ro) != 0) {
		pr_err("Handle ID RO check failed\n");
		return -EINVAL;
	}

	/* Don't allow duplicating RW handle from RO handle */
	if (is_ro && op.access_flags != NVMAP_HANDLE_RO) {
		pr_err("Duplicating RW handle from RO handle is not allowed\n");
		return -EPERM;
	}

	is_ro = (op.access_flags == NVMAP_HANDLE_RO);
	if (!is_ro)
		ref = nvmap_create_handle_from_id(client, op.handle);
	else
		ref = nvmap_dup_handle_ro(client, op.handle);

	if (!IS_ERR(ref)) {
		/*
		 * Increase reference dup count, so that handle is not freed accidentally
		 * due to other thread calling NvRmMemHandleFree
		 */
		atomic_inc(&ref->dupes);
		dmabuf = is_ro ? ref->handle->dmabuf_ro : ref->handle->dmabuf;
		handle = ref->handle;
		if (client->ida) {
			if (nvmap_id_array_id_alloc(client->ida,
				&id, dmabuf) < 0) {
				atomic_dec(&ref->dupes);
				if (dmabuf)
					dma_buf_put(dmabuf);
				if (handle)
					nvmap_free_handle(client, handle,
					is_ro);
				return -ENOMEM;
			}
			op.dup_handle = id;

			if (copy_to_user(arg, &op, sizeof(op))) {
				atomic_dec(&ref->dupes);
				if (dmabuf)
					dma_buf_put(dmabuf);
				if (handle)
					nvmap_free_handle(client, handle,
					is_ro);
				nvmap_id_array_id_release(client->ida, id);
				return -EFAULT;
			}
			ret = 0;
			goto out;
		}
		fd = nvmap_get_dmabuf_fd(client, ref->handle, is_ro);
	} else {
	/* if we get an error, the fd might be non-nvmap dmabuf fd */
		dmabuf = dma_buf_get(op.handle);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);
		fd = nvmap_dmabuf_duplicate_gen_fd(client, dmabuf);
		if (fd < 0)
			return PTR_ERR(ref);
	}

	op.dup_handle = fd;

	ret = nvmap_install_fd(client, handle,
				op.dup_handle, arg, &op, sizeof(op), 0, dmabuf);
out:
	if (!ret && !IS_ERR_OR_NULL(handle))
		trace_refcount_dup_handle(handle, dmabuf,
				atomic_read(&handle->ref),
				atomic_long_read(&dmabuf->file->f_count),
				is_ro ? "RO" : "RW");

	if (!IS_ERR(ref))
		atomic_dec(&ref->dupes);
	return ret;
}

static int find_range_of_handles(struct nvmap_handle **hs, u32 nr,
		struct handles_range *hrange)
{
	u64 tot_sz = 0, rem_sz = 0;
	u64 offs = hrange->offs;
	u32 start = 0, end = 0;
	u64 sz = hrange->sz;
	u32 i;

	hrange->offs_start = offs;
	/* Find start handle */
	for (i = 0; i < nr; i++) {
		tot_sz += hs[i]->size;
		if (offs > tot_sz) {
			hrange->offs_start -= tot_sz;
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
		rem_sz += hs[i]->size;
		if (rem_sz >= sz) {
			end = i;
			hrange->start = start;
			hrange->end = end;
			return 0;
		}
	}
	return -1;
}

int nvmap_ioctl_get_fd_from_list(struct file *filp, void __user *arg)
{
	struct nvmap_client *client = filp->private_data;
	struct nvmap_fd_for_range_from_list op = {0};
	struct nvmap_handle_ref *ref = NULL;
	struct nvmap_handle **hs = NULL;
	struct dma_buf *dmabuf = NULL;
	struct nvmap_handle *h = NULL;
	struct handles_range hrange = {0};
	size_t tot_hs_size = 0;
	u32 i, count = 0, flags = 0;
	size_t bytes;
	int err = 0;
	int fd = -1;
	u32 *hndls;

	if (!client)
		return -ENODEV;

	if (copy_from_user(&op, arg, sizeof(op)))
		return -EFAULT;

	if (!op.handles || !op.num_handles
		 || !op.size || op.num_handles > U32_MAX / sizeof(u32))
		return -EINVAL;

	hrange.offs = op.offset;
	hrange.sz = op.size;

	/* memory for nvmap_handle pointers */
	bytes =  op.num_handles * sizeof(*hs);
	if (!ACCESS_OK(VERIFY_READ, (const void __user *)op.handles,
		op.num_handles * sizeof(u32)))
		return -EFAULT;

	/* memory for handles passed by userspace */
	bytes += op.num_handles * sizeof(u32);
	hs = nvmap_altalloc(bytes);
	if (!hs) {
		pr_err("memory allocation failed\n");
		return -ENOMEM;
	}
	hndls = (u32 *)(hs + op.num_handles);
	if (!IS_ALIGNED((ulong)hndls, sizeof(u32))) {
		pr_err("handle pointer is not properly aligned!!\n");
		err = -EINVAL;
		goto free_mem;
	}

	if (copy_from_user(hndls, (void __user *)op.handles,
		op.num_handles * sizeof(u32))) {
		pr_err("Can't copy from user pointer op.handles\n");
		err = -EFAULT;
		goto free_mem;
	}

	for (i = 0; i < op.num_handles; i++) {
		hs[i] = nvmap_handle_get_from_id(client, hndls[i]);
		if (IS_ERR_OR_NULL(hs[i])) {
			pr_err("invalid handle_ptr[%d] = %u\n",
				i, hndls[i]);
			while (--i >= 0)
				nvmap_handle_put(hs[i]);
			err = -EINVAL;
			goto free_mem;
		}
		tot_hs_size += hs[i]->size;
	}

	/* Add check for sizes of all the handles should be > offs and size */
	if (tot_hs_size < (hrange.offs + hrange.sz)) {
		err = -EINVAL;
		goto free_hs;
	}

	flags = hs[0]->flags;
	/*
	 * Check all of the handles from system heap, are RW, not from VA
	 * and having same cache coherency
	 */
	for (i = 0; i < op.num_handles; i++)
		if (hs[i]->heap_pgalloc && !hs[i]->from_va &&
			!hs[i]->is_ro && hs[i]->flags == flags)
			count++;
	if (!count || (op.num_handles && count % op.num_handles)) {
		pr_err("all of the handles should be from system heap, of type RW,"
			" not from VA and having same cache coherency\n");
		err = -EINVAL;
		goto free_hs;
	}

	/* Find actual range of handles where the offset/size range is lying */
	if (find_range_of_handles(hs, op.num_handles, &hrange)) {
		err = -EINVAL;
		goto free_hs;
	}

	if (hrange.start > op.num_handles || hrange.end > op.num_handles) {
		err = -EINVAL;
		goto free_hs;
	}
	/* Create new handle for the size */
	ref = nvmap_create_handle(client, hrange.sz, false);
	if (IS_ERR_OR_NULL(ref)) {
		err = -EINVAL;
		goto free_hs;
	}

	ref->handle->orig_size = hrange.sz;
	h = ref->handle;
	mutex_init(&h->pg_ref_h_lock);

	/* Assign pages from the handles to newly created nvmap handle */
	err =  nvmap_assign_pages_to_handle(client, hs, h, &hrange);
	if (err)
		goto free_hs;

	dmabuf = h->dmabuf;
	/* Create dmabuf fd out of dmabuf */
	fd = nvmap_get_dmabuf_fd(client, h, false);
	op.fd = fd;
	err = nvmap_install_fd(client, h, fd,
				arg, &op, sizeof(op), 1, dmabuf);
free_hs:
	for (i = 0; i < op.num_handles; i++)
		nvmap_handle_put(hs[i]);

	if (h) {
		nvmap_handle_put(h);
		nvmap_free_handle(client, h, false);
	}
free_mem:
	nvmap_altfree(hs, bytes);
	return err;
}
