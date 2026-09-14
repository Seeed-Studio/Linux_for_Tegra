// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2009-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Memory manager for Tegra GPU
 */

#define pr_fmt(fmt)	"nvmap: %s() " fmt, __func__

#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/rbtree.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/export.h>

#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#include <linux/nvmap.h>
#include <trace/events/nvmap.h>
#include <linux/libnvdimm.h>

#include "nvmap_dev.h"
#include "nvmap_alloc.h"
#include "nvmap_handle.h"

#ifdef CONFIG_ARM64
#define PG_PROT_KERNEL PAGE_KERNEL
#else
#define PG_PROT_KERNEL pgprot_kernel
#endif

static phys_addr_t handle_phys(struct nvmap_handle *h)
{
	if (h->heap_pgalloc)
		BUG();
	return nvmap_get_heap_block_base(h->carveout);
}

void *__nvmap_mmap(struct nvmap_handle *h)
{
	pgprot_t prot;
	void *vaddr;
	unsigned long adj_size;
	struct page **pages = NULL;
	int i = 0;

	if (!virt_addr_valid(h))
		return NULL;

	h = nvmap_handle_get(h);
	if (!h)
		return NULL;
	/*
	 * If the handle is RO and virtual mapping is requested in
	 * kernel address space, return error.
	 */
	if (h->from_va && h->is_ro)
		goto put_handle;

	if (!h->alloc)
		goto put_handle;

	if (!(h->heap_type & nvmap_dev->cpu_access_mask))
		goto put_handle;

	if (h->vaddr)
		return h->vaddr;

	nvmap_kmaps_inc(h);
	prot = nvmap_pgprot(h, PG_PROT_KERNEL);

	if (h->heap_pgalloc) {
		pages = nvmap_pages(h->pgalloc.pages, h->size >> PAGE_SHIFT);
		if (!pages)
			goto out;

		vaddr = vmap(pages, h->size >> PAGE_SHIFT, VM_MAP, prot);
		nvmap_altfree(pages, (h->size >> PAGE_SHIFT) * sizeof(*pages));
		pages = NULL;

		if (!vaddr && !h->vaddr)
			goto out;

		if (vaddr && atomic_long_cmpxchg((atomic_long_t *)(void *)&h->vaddr, 0,
				(long)vaddr)) {
			nvmap_kmaps_dec(h);
			vunmap(vaddr);
		}

		return h->vaddr;
	}

	/* carveout - explicitly map the pfns into a vmalloc area */
	adj_size = nvmap_get_heap_block_base(h->carveout) & ~PAGE_MASK;
	if (check_add_overflow(adj_size, h->size, &adj_size))
		goto dec_kmaps;

	adj_size = PAGE_ALIGN(adj_size);

	if (pfn_valid(__phys_to_pfn(nvmap_get_heap_block_base(h->carveout) & PAGE_MASK))) {
		unsigned long pfn;
		struct page *page;
		int nr_pages;

		pfn = ((nvmap_get_heap_block_base(h->carveout)) >> PAGE_SHIFT);
		page = pfn_to_page(pfn);
		nr_pages = h->size >> PAGE_SHIFT;

		pages = vmalloc(nr_pages * sizeof(*pages));
		if (!pages)
			goto out;

		for (i = 0; i < nr_pages; i++)
			pages[i] = nth_page(page, i);

		vaddr = vmap(pages, nr_pages, VM_MAP, prot);
	} else {
#if defined(CONFIG_GENERIC_IOREMAP)
#if defined(NV_IOREMAP_PROT_HAS_PGPROT_T_ARG) /* Linux v6.15 */
		vaddr = (__force void *)ioremap_prot(nvmap_get_heap_block_base(h->carveout),
						adj_size, prot);
#else
		vaddr = (__force void *)ioremap_prot(nvmap_get_heap_block_base(h->carveout),
						adj_size, pgprot_val(prot));
#endif /* NV_IOREMAP_PROT_HAS_PGPROT_T_ARG */
#else
		vaddr = (__force void *)__ioremap(nvmap_get_heap_block_base(h->carveout),
						adj_size, prot);
#endif /* CONFIG_GENERIC_IOREMAP */
	}
	if (vaddr == NULL)
		goto out;

	if (vaddr && atomic_long_cmpxchg((atomic_long_t *)(void *)&h->vaddr,
						 0, (long)vaddr)) {
		vaddr -= (nvmap_get_heap_block_base(h->carveout) & ~PAGE_MASK);
		/*
		 * iounmap calls vunmap for vmalloced address, hence
		 * takes care of vmap/__ioremap freeing part.
		 */
		iounmap((void __iomem *)vaddr);
		nvmap_kmaps_dec(h);
	}

	if (pages)
		vfree(pages);

	/* leave the handle ref count incremented by 1, so that
	 * the handle will not be freed while the kernel mapping exists.
	 * nvmap_handle_put will be called by unmapping this address */
	return h->vaddr;
out:
	if (pages)
		vfree(pages);
dec_kmaps:
	nvmap_kmaps_dec(h);
put_handle:
	nvmap_handle_put(h);
	return NULL;
}

void __nvmap_munmap(struct nvmap_handle *h, void *addr)
{
	if (!h || !h->alloc ||
	    WARN_ON(!virt_addr_valid(h)) ||
	    WARN_ON(!addr) ||
	    !(h->heap_type & nvmap_dev->cpu_access_mask))
		return;

	nvmap_handle_put(h);
}

struct sg_table *__nvmap_sg_table(struct nvmap_client *client,
		struct nvmap_handle *h)
{
	struct sg_table *sgt = NULL;
	int err, npages;
	struct page **pages;

	if (virt_addr_valid(h) == 0)
		return ERR_PTR(-EINVAL);

	h = nvmap_handle_get(h);
	if (h == NULL)
		return ERR_PTR(-EINVAL);

	if (!h->alloc) {
		err = -EINVAL;
		goto put_handle;
	}

	npages = PAGE_ALIGN(h->size) >> PAGE_SHIFT;
	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (sgt == NULL) {
		err = -ENOMEM;
		goto err;
	}

	if (!h->heap_pgalloc) {
		phys_addr_t paddr = handle_phys(h);
		struct page *page = phys_to_page(paddr);

		err = sg_alloc_table(sgt, 1, GFP_KERNEL);
		if (err)
			goto err;

		sg_set_page(sgt->sgl, page, h->size, offset_in_page(paddr));
	} else {
		pages = nvmap_pages(h->pgalloc.pages, npages);
		if (!pages) {
			err = -ENOMEM;
			goto err;
		}
		err = sg_alloc_table_from_pages(sgt, pages,
				npages, 0, h->size, GFP_KERNEL);
		nvmap_altfree(pages, npages * sizeof(*pages));
		if (err)
			goto err;
	}
	nvmap_handle_put(h);
	return sgt;

err:
	kfree(sgt);
put_handle:
	nvmap_handle_put(h);
	return ERR_PTR(err);
}

void __nvmap_free_sg_table(struct nvmap_client *client,
		struct nvmap_handle *h, struct sg_table *sgt)
{
	sg_free_table(sgt);
	kfree(sgt);
}
