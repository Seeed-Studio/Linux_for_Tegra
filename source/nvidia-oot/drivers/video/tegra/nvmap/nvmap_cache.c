// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2011-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#define pr_fmt(fmt)	"nvmap: %s() " fmt, __func__

#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/libnvdimm.h>
#include <linux/of.h>
#include <linux/rtmutex.h>
#include <linux/sys_soc.h>
#include <linux/version.h>
#include <soc/tegra/fuse.h>
__weak struct arm64_ftr_reg arm64_ftr_reg_ctrel0;

#include <trace/events/nvmap.h>
#include "nvmap_dev.h"
#include "nvmap_alloc.h"
#include "nvmap_alloc_int.h"
#include "nvmap_handle.h"
#include "nvmap_dmabuf.h"
#include "nvmap_debug.h"

#ifdef CONFIG_ARM64
#define PG_PROT_KERNEL PAGE_KERNEL
#else
#define PG_PROT_KERNEL pgprot_kernel
#endif

extern void __clean_dcache_area_poc(void *addr, size_t len);

/*
 * FIXME:
 *
 *   __clean_dcache_page() is only available on ARM64 (well, we haven't
 *   implemented it on ARMv7).
 */
void nvmap_clean_cache_page(struct page *page)
{
	__clean_dcache_area_poc(page_address(page), PAGE_SIZE);
}

void nvmap_clean_cache(struct page **pages, int numpages)
{
	int i;

	/* Not technically a flush but that's what nvmap knows about. */
	nvmap_stats_inc(NS_CFLUSH_DONE, numpages << PAGE_SHIFT);
	trace_nvmap_cache_flush(numpages << PAGE_SHIFT,
		nvmap_stats_read(NS_ALLOC),
		nvmap_stats_read(NS_CFLUSH_RQ),
		nvmap_stats_read(NS_CFLUSH_DONE));

	for (i = 0; i < numpages; i++)
		nvmap_clean_cache_page(pages[i]);
}

void inner_cache_maint(unsigned int op, void *vaddr, size_t size)
{
	if (op == NVMAP_CACHE_OP_WB_INV)
		arch_invalidate_pmem(vaddr, size);
	else if (op == NVMAP_CACHE_OP_INV)
		__dma_map_area_from_device(vaddr, size);
	else
		__dma_map_area_to_device(vaddr, size);
}

static int heap_page_cache_maint(
	struct nvmap_handle *h, unsigned long start, unsigned long end,
	unsigned int op, bool inner, bool outer, bool clean_only_dirty)
{
	unsigned long difference;

	if (check_sub_overflow(end, start, &difference))
		return -EOVERFLOW;

	/* Don't perform cache maint for RO mapped buffers */
	if (h->from_va && h->is_ro)
		return 0;

	if (h->userflags & NVMAP_HANDLE_CACHE_SYNC) {
		/*
		 * zap user VA->PA mappings so that any access to the pages
		 * will result in a fault and can be marked dirty
		 */
		nvmap_handle_mkclean(h, start, difference);
	}

	if (inner) {
		if (h->vaddr == NULL) {
			if (__nvmap_mmap(h))
				__nvmap_munmap(h, h->vaddr);
			else
				goto per_page_cache_maint;
		}
		/* Fast inner cache maintenance using single mapping */
		inner_cache_maint(op, h->vaddr + start, difference);
		if (!outer)
			return 0;
		/* Skip per-page inner maintenance in loop below */
		inner = false;

	}
per_page_cache_maint:

	while (start < end) {
		struct page *page;
		phys_addr_t paddr;
		unsigned long next;
		unsigned long off;
		size_t size;
		int ret;
		phys_addr_t sum;

		page = nvmap_to_page(h->pgalloc.pages[start >> PAGE_SHIFT]);
		next = min(((start + PAGE_SIZE) & PAGE_MASK), end);
		off = start & ~PAGE_MASK;
		size = next - start;
		if (check_add_overflow((phys_addr_t)page_to_phys(page), (phys_addr_t)off, &sum))
			return -EOVERFLOW;

		paddr = sum;

		if (check_add_overflow(paddr, (phys_addr_t)size, &sum))
			return -EOVERFLOW;

		ret = nvmap_cache_maint_phys_range(op, paddr, sum,
				inner, outer);
		WARN_ON(ret != 0);
		start = next;
	}

	return 0;
}

struct cache_maint_op {
	phys_addr_t start;
	phys_addr_t end;
	unsigned int op;
	struct nvmap_handle *h;
	bool inner;
	bool outer;
	bool clean_only_dirty;
};

int nvmap_cache_maint_phys_range(unsigned int op, phys_addr_t pstart,
		phys_addr_t pend, int inner, int outer)
{
	void __iomem *io_addr;
	phys_addr_t loop;

	if (!inner)
		goto do_outer;

	loop = pstart;
	while (loop < pend) {
		phys_addr_t next = (loop + PAGE_SIZE) & PAGE_MASK;
		void *base;
		next = min(next, pend);
#if defined(CONFIG_GENERIC_IOREMAP)
#if defined(NV_IOREMAP_PROT_HAS_PGPROT_T_ARG) /* Linux v6.15 */
		io_addr = ioremap_prot(loop, PAGE_SIZE, PAGE_KERNEL);
#else
		io_addr = ioremap_prot(loop, PAGE_SIZE, pgprot_val(PAGE_KERNEL));
#endif /* NV_IOREMAP_PROT_HAS_PGPROT_T_ARG */
#else
		io_addr = __ioremap(loop, PAGE_SIZE, PG_PROT_KERNEL);
#endif /* CONFIG_GENERIC_IOREMAP */
		if (io_addr == NULL)
			return -ENOMEM;
		base = (__force void *)io_addr + (loop & ~PAGE_MASK);
		inner_cache_maint(op, base, next - loop);
		iounmap(io_addr);
		loop = next;
	}

do_outer:
	return 0;
}

static int do_cache_maint(struct cache_maint_op *cache_work)
{
	phys_addr_t pstart = cache_work->start;
	phys_addr_t pend = cache_work->end;
	int err = 0;
	struct nvmap_handle *h = cache_work->h;
	unsigned int op = cache_work->op;
	phys_addr_t difference;

	if (!h || !h->alloc)
		return -EFAULT;

	wmb();
	if (h->flags == NVMAP_HANDLE_UNCACHEABLE ||
	    h->flags == NVMAP_HANDLE_WRITE_COMBINE || pstart == pend)
		goto out;

	trace_nvmap_cache_maint(h->owner, h, pstart, pend, op, pend - pstart);
	if (pstart > h->size || pend > h->size) {
		pr_warn("cache maintenance outside handle\n");
		err = -EINVAL;
		goto out;
	}

	if (h->heap_pgalloc) {
		err = heap_page_cache_maint(h, pstart, pend, op, true,
				(h->flags == NVMAP_HANDLE_INNER_CACHEABLE) ?
				false : true, cache_work->clean_only_dirty);
		if (err != 0)
			err = -EOVERFLOW;

		goto out;
	}

	if (!h->vaddr) {
		if (__nvmap_mmap(h))
			__nvmap_munmap(h, h->vaddr);
		else
			goto per_page_phy_cache_maint;
	}
	inner_cache_maint(op, h->vaddr + pstart, pend - pstart);
	goto out;

per_page_phy_cache_maint:
	pstart += h->carveout->base;
	pend += h->carveout->base;

	err = nvmap_cache_maint_phys_range(op, pstart, pend, true,
			h->flags != NVMAP_HANDLE_INNER_CACHEABLE);

out:
	if (!err && !check_sub_overflow(pend, pstart, &difference)) {
		nvmap_stats_inc(NS_CFLUSH_DONE, difference);

		trace_nvmap_cache_flush(difference,
		nvmap_stats_read(NS_ALLOC),
		nvmap_stats_read(NS_CFLUSH_RQ),
		nvmap_stats_read(NS_CFLUSH_DONE));
	}

	return 0;
}

static void nvmap_handle_get_cacheability(struct nvmap_handle *h,
		bool *inner, bool *outer)
{
	*inner = h->flags == NVMAP_HANDLE_CACHEABLE ||
		 h->flags == NVMAP_HANDLE_INNER_CACHEABLE;
	*outer = h->flags == NVMAP_HANDLE_CACHEABLE;
}

int __nvmap_do_cache_maint(struct nvmap_client *client,
			struct nvmap_handle *h,
			unsigned long start, unsigned long end,
			unsigned int op, bool clean_only_dirty)
{
	int err;
	struct cache_maint_op cache_op;

	h = nvmap_handle_get(h);
	if (!h)
		return -EFAULT;

	if ((start >= h->size) || (end > h->size)) {
		pr_debug("%s start: %ld end: %ld h->size: %zu\n", __func__,
				start, end, h->size);
		nvmap_handle_put(h);
		return -EFAULT;
	}

	if (!(h->heap_type & nvmap_dev->cpu_access_mask)) {
		pr_debug("%s heap_type %u access_mask 0x%x\n", __func__,
				h->heap_type, nvmap_dev->cpu_access_mask);
		nvmap_handle_put(h);
		return -EPERM;
	}

	nvmap_kmaps_inc(h);
	if (op == NVMAP_CACHE_OP_INV)
		op = NVMAP_CACHE_OP_WB_INV;

	/* clean only dirty is applicable only for Write Back operation */
	if (op != NVMAP_CACHE_OP_WB)
		clean_only_dirty = false;

	cache_op.h = h;
	cache_op.start = start ? start : 0;
	cache_op.end = end ? end : h->size;
	cache_op.op = op;
	nvmap_handle_get_cacheability(h, &cache_op.inner, &cache_op.outer);
	cache_op.clean_only_dirty = clean_only_dirty;

	nvmap_stats_inc(NS_CFLUSH_RQ, end - start);
	err = do_cache_maint(&cache_op);
	nvmap_kmaps_dec(h);
	nvmap_handle_put(h);
	return err;
}

int __nvmap_cache_maint(struct nvmap_client *client,
			       struct nvmap_cache_op_64 *op)
{
	struct vm_area_struct *vma;
	struct nvmap_vma_priv *priv;
	struct nvmap_handle *handle;
	unsigned long start;
	unsigned long end;
	unsigned long sum;
	int err = 0;

	if (!op->addr || op->op < NVMAP_CACHE_OP_WB ||
	    op->op > NVMAP_CACHE_OP_WB_INV)
		return -EINVAL;

	handle = nvmap_handle_get_from_id(client, op->handle);
	if (IS_ERR_OR_NULL(handle))
		return -EINVAL;

	nvmap_acquire_mmap_read_lock(current->mm);

	vma = find_vma(current->active_mm, (unsigned long)op->addr);
	if (vma == NULL || is_nvmap_vma(vma) == 0 ||
	    (ulong)op->addr < vma->vm_start ||
	    (ulong)op->addr >= vma->vm_end ||
	    op->len > vma->vm_end - (ulong)op->addr) {
		err = -EADDRNOTAVAIL;
		goto out;
	}

	priv = (struct nvmap_vma_priv *)vma->vm_private_data;

	if (priv->handle != handle) {
		err = -EFAULT;
		goto out;
	}

	start = (unsigned long)op->addr - vma->vm_start +
		(vma->vm_pgoff << PAGE_SHIFT);
	if (check_add_overflow(start, (unsigned long)op->len, &sum)) {
		err = -EOVERFLOW;
		goto out;
	}

	end = sum;

	err = __nvmap_do_cache_maint(client, priv->handle, start, end, op->op,
				     false);
out:
	nvmap_release_mmap_read_lock(current->mm);
	nvmap_handle_put(handle);
	return err;
}
