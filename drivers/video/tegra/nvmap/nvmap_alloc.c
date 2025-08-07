// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2011-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Handle allocation and freeing routines for nvmap
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/moduleparam.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/io.h>
#include <soc/tegra/fuse.h>
#include <trace/events/nvmap.h>
#include <linux/libnvdimm.h>
#include <linux/rtmutex.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "nvmap_stats.h"
#include "nvmap_dev.h"
#include "nvmap_alloc.h"
#include "nvmap_alloc_int.h"
#include "nvmap_dmabuf.h"
#include "nvmap_handle.h"

bool nvmap_convert_carveout_to_iovmm;
bool nvmap_convert_iovmm_to_carveout;

u64 nvmap_big_page_allocs;
u64 nvmap_total_page_allocs;

/* handles may be arbitrarily large (16+MiB), and any handle allocated from
 * the kernel (i.e., not a carveout handle) includes its array of pages. to
 * preserve kmalloc space, if the array of pages exceeds PAGELIST_VMALLOC_MIN,
 * the array is allocated using vmalloc. */
#define PAGELIST_VMALLOC_MIN	(PAGE_SIZE)

void *nvmap_altalloc(size_t len)
{
	if (len > PAGELIST_VMALLOC_MIN)
		return vzalloc(len);
	else
		return kzalloc(len, GFP_KERNEL);
}

void nvmap_altfree(void *ptr, size_t len)
{
	if (!ptr)
		return;

	if (len > PAGELIST_VMALLOC_MIN)
		vfree(ptr);
	else
		kfree(ptr);
}

struct page *nvmap_to_page(struct page *page)
{
	return (struct page *)((unsigned long)page & ~3UL);
}

struct page **nvmap_pages(struct page **pg_pages, u32 nr_pages)
{
	struct page **pages;
	int i;

	pages = nvmap_altalloc(sizeof(*pages) * nr_pages);
	if (pages == NULL)
		return NULL;

	for (i = 0; i < nr_pages; i++)
		pages[i] = nvmap_to_page(pg_pages[i]);

	return pages;
}

static struct page *nvmap_alloc_pages_exact(gfp_t gfp, size_t size, int numa_id)
{
	struct page *page, *p, *e;
	unsigned int order;

	order = get_order(size);
	page = alloc_pages_node(numa_id, gfp, order);

	if (!page)
		return NULL;

	split_page(page, order);
	e = nth_page(page, (1 << order));
	for (p = nth_page(page, (size >> PAGE_SHIFT)); p < e; p++)
		__free_page(p);

	return page;
}

static int handle_page_alloc(struct nvmap_client *client,
			     struct nvmap_handle *h, bool contiguous)
{
	size_t size = h->size;
	size_t nr_page = size >> PAGE_SHIFT;
	int i = 0, page_index = 0, allocated = 0;
	struct page **pages;
	gfp_t gfp = GFP_NVMAP | __GFP_ZERO;
	u64 result;
	size_t tot_size;
#ifdef CONFIG_ARM64_4K_PAGES
	int start_index = 0;
#ifdef NVMAP_CONFIG_PAGE_POOLS
	int pages_per_big_pg = NVMAP_PP_BIG_PAGE_SIZE >> PAGE_SHIFT;
#else
	int pages_per_big_pg = 0;
#endif
#endif /* CONFIG_ARM64_4K_PAGES */

	if (check_mul_overflow(nr_page, sizeof(*pages), &tot_size))
		return -EOVERFLOW;

	pages = nvmap_altalloc(tot_size);
	if (!pages)
		return -ENOMEM;

	if (contiguous) {
		struct page *page;
		page = nvmap_alloc_pages_exact(gfp, size, h->numa_id);
		if (!page)
			goto fail;

		for (i = 0; i < nr_page; i++)
			pages[i] = nth_page(page, i);

	} else {
#ifdef CONFIG_ARM64_4K_PAGES
#ifdef NVMAP_CONFIG_PAGE_POOLS
		/* Get as many big pages from the pool as possible. */
		page_index = nvmap_page_pool_alloc_lots_bp(nvmap_dev->pool, pages,
							nr_page, true, h->numa_id);
		pages_per_big_pg = nvmap_dev->pool->pages_per_big_pg;
#endif
		/* Try to allocate big pages from page allocator */
		start_index = page_index;
		for (i = page_index;
		     i < nr_page && pages_per_big_pg > 1 && (nr_page - i) >= pages_per_big_pg;
		     i += pages_per_big_pg, page_index += pages_per_big_pg) {
			struct page *page;
			int idx;
			/*
			 * set the gfp not to trigger direct/kswapd reclaims and
			 * not to use emergency reserves.
			 */
			gfp_t gfp_no_reclaim = (gfp | __GFP_NOMEMALLOC) & ~__GFP_RECLAIM;

			page = nvmap_alloc_pages_exact(gfp_no_reclaim,
					pages_per_big_pg << PAGE_SHIFT, h->numa_id);
			if (!page)
				break;

			for (idx = 0; idx < pages_per_big_pg; idx++)
				pages[i + idx] = nth_page(page, idx);
		}

		/* Perform cache clean for the pages allocated from page allocator */
		nvmap_clean_cache(&pages[start_index], page_index - start_index);
		if (check_add_overflow(nvmap_big_page_allocs, (u64)page_index, &result))
			goto fail;

		nvmap_big_page_allocs = result;
#endif /* CONFIG_ARM64_4K_PAGES */
#ifdef NVMAP_CONFIG_PAGE_POOLS
			/* Get as many pages from the pool as possible. */
		page_index += nvmap_page_pool_alloc_lots(
			  nvmap_dev->pool, &pages[page_index],
			  nr_page - page_index, true, h->numa_id);
#endif
		allocated = page_index;
		if (page_index < nr_page) {
			int nid = h->numa_id == NUMA_NO_NODE ? numa_mem_id() : h->numa_id;

#if defined(NV__ALLOC_PAGES_BULK_HAS_NO_PAGE_LIST_ARG)
			allocated = __alloc_pages_bulk(gfp, nid, NULL,
					nr_page, pages);
#else
			allocated = __alloc_pages_bulk(gfp, nid, NULL,
					nr_page, NULL, pages);
#endif
		}
		for (i = allocated; i < nr_page; i++) {
			pages[i] = nvmap_alloc_pages_exact(gfp, PAGE_SIZE,
							   h->numa_id);

			if (pages[i] == NULL)
				goto fail;
		}
	}

	if (check_add_overflow(nvmap_total_page_allocs, (u64)nr_page, &result))
		goto fail;

	nvmap_total_page_allocs = result;

	/*
	 * Make sure any data in the caches is cleaned out before
	 * passing these pages to userspace. Many nvmap clients assume that
	 * the buffers are clean as soon as they are allocated. nvmap
	 * clients can pass the buffer to hardware as it is without any
	 * explicit cache maintenance.
	 */
	if (page_index < nr_page)
		nvmap_clean_cache(&pages[page_index], nr_page - page_index);

	h->pgalloc.pages = pages;
	h->pgalloc.contig = contiguous;
	atomic_set(&h->pgalloc.ndirty, 0);
	return 0;

fail:
	while (i--)
		__free_page(pages[i]);
	nvmap_altfree(pages, tot_size);
	wmb();
	return -ENOMEM;
}

static bool nvmap_cpu_map_is_allowed(struct nvmap_handle *handle)
{
	if (handle->heap_type & NVMAP_HEAP_CARVEOUT_VPR)
		return false;
	else
		return handle->heap_type & nvmap_dev->dynamic_dma_map_mask;
}

static void alloc_handle(struct nvmap_client *client,
			 struct nvmap_handle *h, unsigned int type)
{
	unsigned int carveout_mask = NVMAP_HEAP_CARVEOUT_MASK;
	unsigned int iovmm_mask = NVMAP_HEAP_IOVMM;
	int ret;

	/* type should only be non-zero and in power of 2. */
	BUG_ON((!type) || (type & (type - 1)));

	if (nvmap_convert_carveout_to_iovmm) {
		carveout_mask &= ~NVMAP_HEAP_CARVEOUT_GENERIC;
		iovmm_mask |= NVMAP_HEAP_CARVEOUT_GENERIC;
	} else if (nvmap_convert_iovmm_to_carveout) {
		if (type & NVMAP_HEAP_IOVMM) {
			type &= ~NVMAP_HEAP_IOVMM;
			type |= NVMAP_HEAP_CARVEOUT_GENERIC;
		}
	}

	if (type & carveout_mask) {
		struct nvmap_heap_block *b;

		b = nvmap_carveout_alloc(client, h, type, NULL);
		if (b) {
			h->heap_type = type;
			h->heap_pgalloc = false;
			/* barrier to ensure all handle alloc data
			 * is visible before alloc is seen by other
			 * processors.
			 */
			mb();
			h->alloc = true;

#ifdef NVMAP_CONFIG_CACHE_FLUSH_AT_ALLOC
			/* Clear the allocated buffer */
			if (nvmap_cpu_map_is_allowed(h)) {
				void *cpu_addr;

				cpu_addr = memremap(b->base, h->size,
							MEMREMAP_WB);
				if (cpu_addr != NULL) {
					memset(cpu_addr, 0, h->size);
					arch_invalidate_pmem(cpu_addr, h->size);
					memunmap(cpu_addr);
				}
			}
#endif /* NVMAP_CONFIG_CACHE_FLUSH_AT_ALLOC */
			return;
		}
	} else if (type & iovmm_mask) {
		ret = handle_page_alloc(client, h,
			h->userflags & NVMAP_HANDLE_PHYS_CONTIG);
		if (ret)
			return;
		h->heap_type = NVMAP_HEAP_IOVMM;
		h->heap_pgalloc = true;
		mb();
		h->alloc = true;
	}
}

static int alloc_handle_from_va(struct nvmap_client *client,
				 struct nvmap_handle *h,
				 ulong vaddr,
				 u32 flags,
				 unsigned int heap_mask)
{
	size_t nr_page = h->size >> PAGE_SHIFT;
	struct page **pages;
	int ret = 0;
	struct mm_struct *mm = current->mm;

	pages = nvmap_altalloc(nr_page * sizeof(*pages));
	if (IS_ERR_OR_NULL(pages))
		return PTR_ERR(pages);

	nvmap_acquire_mmap_read_lock(mm);
	ret = nvmap_get_user_pages(vaddr & PAGE_MASK, nr_page, pages, true,
				(flags & NVMAP_HANDLE_RO) ? 0 : FOLL_WRITE);
	nvmap_release_mmap_read_lock(mm);
	if (ret) {
		nvmap_altfree(pages, nr_page * sizeof(*pages));
		return ret;
	}

	if (flags & NVMAP_HANDLE_RO)
		h->is_ro = true;

	nvmap_clean_cache(&pages[0], nr_page);
	h->pgalloc.pages = pages;
	atomic_set(&h->pgalloc.ndirty, 0);
	h->heap_type = NVMAP_HEAP_IOVMM;
	h->heap_pgalloc = true;
	h->from_va = true;
	if (heap_mask & NVMAP_HEAP_CARVEOUT_GPU)
		h->has_hugetlbfs_pages = true;
	mb();
	h->alloc = true;
	return ret;
}

/* small allocations will try to allocate from generic OS memory before
 * any of the limited heaps, to increase the effective memory for graphics
 * allocations, and to reduce fragmentation of the graphics heaps with
 * sub-page splinters */
static const unsigned int heap_policy_small[] = {
	NVMAP_HEAP_CARVEOUT_VPR,
	NVMAP_HEAP_CARVEOUT_MASK,
	NVMAP_HEAP_IOVMM,
	0,
};

static const unsigned int heap_policy_large[] = {
	NVMAP_HEAP_CARVEOUT_VPR,
	NVMAP_HEAP_IOVMM,
	NVMAP_HEAP_CARVEOUT_MASK,
	0,
};

static const unsigned int heap_policy_excl[] = {
	NVMAP_HEAP_CARVEOUT_IVM,
	NVMAP_HEAP_CARVEOUT_VIDMEM,
	0,
};

static int nvmap_page_from_vma(struct vm_area_struct *vma, ulong vaddr, struct page **page)
{
#if defined(NV_FOLLOW_PFNMAP_START_PRESENT)
	unsigned long pfn;
	struct follow_pfnmap_args args = {
		.vma = vma,
		.address = vaddr,
	};

	if (follow_pfnmap_start(&args)) {
		pr_err("follow_pfnmap_start failed\n");
		goto fail;
	}

	pfn = args.pfn;
	if (!pfn_is_map_memory(pfn)) {
		follow_pfnmap_end(&args);
		pr_err("pfn_is_map_memory failed\n");
		goto fail;
	}

	*page = pfn_to_page(pfn);
	get_page(*page);
	follow_pfnmap_end(&args);
	return 0;

fail:
	return -EINVAL;
#elif defined(NV_FOLLOW_PFN_PRESENT)
	unsigned long pfn;

	if (follow_pfn(vma, vaddr, &pfn)) {
		pr_err("follow_pfn failed\n");
		goto fail;
	}

	if (!pfn_is_map_memory(pfn)) {
		pr_err("no-map memory not allowed\n");
		goto fail;
	}

	*page = pfn_to_page(pfn);
	get_page(*page);
	return 0;

fail:
	return -EINVAL;
#else
	unsigned long pfn;
	spinlock_t *ptl;
	pte_t *ptep;

	mmap_read_lock(current->mm);
	if (follow_pte(vma, vaddr, &ptep, &ptl)) {
		pr_err("follow_pte failed\n");
		goto fail;
	}

	pfn = pte_pfn(ptep_get(ptep));
	if (!pfn_is_map_memory(pfn)) {
		pr_err("no-map memory not allowed\n");
		pte_unmap_unlock(ptep, ptl);
		goto fail;
	}

	*page = pfn_to_page(pfn);
	get_page(*page);
	pte_unmap_unlock(ptep, ptl);
	mmap_read_unlock(current->mm);
	return 0;

fail:
	mmap_read_unlock(current->mm);
	return -EINVAL;
#endif
}

/* must be called with mmap_sem held for read or write */
int nvmap_get_user_pages(ulong vaddr,
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
		pr_debug("get_user_pages requested/got: %zu/%ld]\n", nr_page,
				user_pages);
		while (--user_pages >= 0)
			put_page(pages[user_pages]);

		/*
		 * OpenRM case: When buffer is mapped using remap_pfn_range
		 */
		if (vma->vm_flags & VM_PFNMAP) {
			user_pages = 0;
			while (user_pages < nr_page) {
				ret = nvmap_page_from_vma(vma, vaddr, &pages[user_pages]);
				if (ret)
					break;

				vaddr += PAGE_SIZE;
				user_pages++;
			}

			if (ret) {
				while (--user_pages >= 0)
					put_page(pages[user_pages]);
			}
		}
	}

	return ret;
}

int nvmap_alloc_handle(struct nvmap_client *client,
		       struct nvmap_handle *h, unsigned int heap_mask,
		       size_t align,
		       u8 kind,
		       unsigned int flags,
		       unsigned int peer)
{
	const unsigned int *alloc_policy;
	size_t nr_page;
	int err = -ENOMEM;
	int tag, i;
	bool alloc_from_excl = false;

	h = nvmap_handle_get(h);

	if (!h)
		return -EINVAL;

	if (h->alloc) {
		nvmap_handle_put(h);
		return -EEXIST;
	}

	nvmap_stats_inc(NS_TOTAL, h->size);
	nvmap_stats_inc(NS_ALLOC, h->size);
	trace_nvmap_alloc_handle(client, h,
		h->size, heap_mask, align, flags,
		nvmap_stats_read(NS_TOTAL),
		nvmap_stats_read(NS_ALLOC));
	h->userflags = flags;
	nr_page = ((h->size + PAGE_SIZE - 1) >> PAGE_SHIFT);
	/* Force mapping to uncached for VPR memory. */
	if (heap_mask & (NVMAP_HEAP_CARVEOUT_VPR | ~nvmap_dev->cpu_access_mask))
		h->flags = NVMAP_HANDLE_UNCACHEABLE;
	else
		h->flags = (flags & NVMAP_HANDLE_CACHE_FLAG);
	h->align = max_t(size_t, align, L1_CACHE_BYTES);
	h->peer = peer;
	tag = flags >> 16;

	if (!tag && client && !client->tag_warned) {
		char task_comm[TASK_COMM_LEN];
		client->tag_warned = 1;
		get_task_comm(task_comm, client->task);
		pr_err("PID %d: %s: WARNING: "
			"All NvMap Allocations must have a tag "
			"to identify the subsystem allocating memory."
			"Please pass the tag to the API call"
			" NvRmMemHanldeAllocAttr() or relevant. \n",
			client->task->pid, task_comm);
	}

	/*
	 * If user specifies one of the exclusive carveouts, allocation
	 * from no other heap should be allowed.
	 */
	for (i = 0; i < ARRAY_SIZE(heap_policy_excl); i++) {
		if (!(heap_mask & heap_policy_excl[i]))
			continue;

		if (heap_mask & ~(heap_policy_excl[i])) {
			pr_err("%s alloc mixes exclusive heap %d and other heaps\n",
			       current->group_leader->comm, heap_policy_excl[i]);
			err = -EINVAL;
			goto out;
		}
		alloc_from_excl = true;
	}

	if (!heap_mask) {
		err = -EINVAL;
		goto out;
	}

	alloc_policy = alloc_from_excl ? heap_policy_excl :
			(nr_page == 1) ? heap_policy_small : heap_policy_large;

	while (!h->alloc && *alloc_policy) {
		unsigned int heap_type;

		heap_type = *alloc_policy++;
		heap_type &= heap_mask;

		if (!heap_type)
			continue;

		heap_mask &= ~heap_type;

		while (heap_type && !h->alloc) {
			unsigned int heap;

			/* iterate possible heaps MSB-to-LSB, since higher-
			 * priority carveouts will have higher usage masks */
			heap = 1 << __fls(heap_type);
			alloc_handle(client, h, heap);
			heap_type &= ~heap;
		}
	}

out:
	if (h->alloc) {
		if (client->kernel_client)
			nvmap_stats_inc(NS_KALLOC, h->size);
		else
			nvmap_stats_inc(NS_UALLOC, h->size);
		NVMAP_TAG_TRACE(trace_nvmap_alloc_handle_done,
			NVMAP_TP_ARGS_CHR(client, h, NULL));
		err = 0;
	} else {
		nvmap_stats_dec(NS_TOTAL, h->size);
		nvmap_stats_dec(NS_ALLOC, h->size);
	}
	nvmap_handle_put(h);
	return err;
}

int nvmap_alloc_handle_from_va(struct nvmap_client *client,
			       struct nvmap_handle *h,
			       ulong addr,
			       unsigned int flags,
			       unsigned int heap_mask)
{
	int err = -ENOMEM;
	int tag;

	h = nvmap_handle_get(h);
	if (h == NULL)
		return -EINVAL;

	if (h->alloc) {
		nvmap_handle_put(h);
		return -EEXIST;
	}

	h->userflags = flags;
	h->flags = (flags & NVMAP_HANDLE_CACHE_FLAG);
	if ((heap_mask & NVMAP_HEAP_CARVEOUT_GPU) != 0)
		h->align = SIZE_2MB;
	else
		h->align = PAGE_SIZE;

	tag = flags >> 16;

	if (!tag && client && !client->tag_warned) {
		char task_comm[TASK_COMM_LEN];
		client->tag_warned = 1;
		get_task_comm(task_comm, client->task);
		pr_err("PID %d: %s: WARNING: "
			"All NvMap Allocations must have a tag "
			"to identify the subsystem allocating memory."
			"Please pass the tag to the API call"
			" NvRmMemHanldeAllocAttr() or relevant. \n",
			client->task->pid, task_comm);
	}

	err = alloc_handle_from_va(client, h, addr, flags, heap_mask);
	if (err) {
		pr_err("alloc_handle_from_va failed %d", err);
		nvmap_handle_put(h);
		return -EINVAL;
	}

	if (h->alloc) {
		NVMAP_TAG_TRACE(trace_nvmap_alloc_handle_done,
			NVMAP_TP_ARGS_CHR(client, h, NULL));
		err = 0;
	}
	nvmap_handle_put(h);
	return err;
}

void nvmap_alloc_free(struct page **pages, unsigned int nr_page, bool from_va,
		      bool is_subhandle)
{
	unsigned int i, page_index = 0U;

	for (i = 0; i < nr_page; i++)
		pages[i] = nvmap_to_page(pages[i]);

#ifdef NVMAP_CONFIG_PAGE_POOLS
	if (!from_va && !is_subhandle)
		page_index = nvmap_page_pool_fill_lots(nvmap_dev->pool,
							pages, nr_page);
#endif

	for (i = page_index; i < nr_page; i++) {
		if (from_va)
			put_page(pages[i]);
		/* Knowingly kept in "else if" handle for subrange */
		else if (is_subhandle)
			put_page(pages[i]);
		else
			__free_page(pages[i]);
	}

	nvmap_altfree(pages, nr_page * sizeof(struct page *));
}

phys_addr_t nvmap_alloc_get_co_base(struct nvmap_handle *h)
{
	return h->carveout->base;
}
