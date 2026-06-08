// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2011-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * GPU heap allocator.
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/bug.h>
#include <linux/stat.h>
#include <linux/sizes.h>
#include <linux/io.h>
#include <linux/version.h>
#include <linux/limits.h>
#include <linux/sched/clock.h>
#include <linux/nvmap.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/rtmutex.h>
#include <linux/vmalloc.h>
#include "nvmap_dev.h"
#include "nvmap_alloc.h"
#include "nvmap_handle.h"
#include "nvmap_alloc_int.h"
#include "nvmap_dmabuf.h"

#include "include/linux/nvmap_exports.h"

#ifdef CONFIG_ARM_DMA_IOMMU_ALIGNMENT
#define DMA_BUF_ALIGNMENT CONFIG_ARM_DMA_IOMMU_ALIGNMENT
#else
#define DMA_BUF_ALIGNMENT 8
#endif

/*
 * DMA_ATTR_ALLOC_EXACT_SIZE: This tells the DMA-mapping
 * subsystem to allocate the exact number of pages
 */
#define DMA_ATTR_ALLOC_EXACT_SIZE	(DMA_ATTR_PRIVILEGED << 2)

/*
 * "carveouts" are platform-defined regions of physically contiguous memory
 * which are not managed by the OS. A platform may specify multiple carveouts,
 * for either small special-purpose memory regions or reserved regions of main
 * system memory.
 *
 * The carveout allocator returns allocations which are physically contiguous.
 */

static struct kmem_cache *heap_block_cache;

extern bool nvmap_convert_iovmm_to_carveout;
extern bool nvmap_convert_carveout_to_iovmm;
extern ulong nvmap_init_time;

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

	if (num_online_nodes() > 1) {
		sprintf(meminfo_path, "/sys/devices/system/node/node%d/meminfo", numa_id);
	} else {
		if (numa_id != 0) {
			pr_err("Incorrect input for numa_id:%d\n", numa_id);
			return -EINVAL;
		}
		sprintf(meminfo_path, "/proc/meminfo");
	}

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
		if (num_online_nodes() > 1) {
			if (sscanf(ptr, "Node %d HugePages_Total: %u\n", &nid, &huge_total) == 2)
				total_found = true;
			else if (sscanf(ptr, "Node %d HugePages_Free: %u\n", &nid, &huge_free) == 2)
				free_found = true;
		} else {
			if (sscanf(ptr, "HugePages_Total: %u\n", &huge_total) == 1)
				total_found = true;
			else if (sscanf(ptr, "HugePages_Free: %u\n", &huge_free) == 1)
				free_found = true;
		}
	}

	nvmap_altfree(buf, MEMINFO_SIZE * sizeof(*buf));
	if (nid == numa_id && total_found && free_found) {
		*total = (u64)huge_total * SIZE_2MB;
		*free = (u64)huge_free * SIZE_2MB;
		return 0;
	}
	return -EINVAL;
}

int nvmap_query_heap(struct nvmap_query_heap_params *op, bool is_numa_aware)
{
	unsigned int carveout_mask = NVMAP_HEAP_CARVEOUT_MASK;
	unsigned int iovmm_mask = NVMAP_HEAP_IOVMM;
	struct nvmap_heap *heap;
	unsigned int type;
	int i;
	/*
	 * Default value of numa_id will be 0. It can be overwritten by user's
	 * input only if NvRmMemQueryHeapParamsNuma is called (making
	 * is_numa_aware true).
	 * If NvRmMemQueryHeapParamsNuma is called:
	 *     1. When single numa node present
	 *         a. Return params for only op->numa_id = 0.
	 *         b. Return error if op->numa_id is not 0.
	 *     2. When multiple numa nodes present
	 *         a. Return params for op->numa_id
	 * If NvRmMemQueryHeapParams is called:
	 *     1. When single numa node present
	 *         a. Return params using system_heap_total_mem &
	 *            system_heap_free_mem for iovmm carveout.
	 *         b. Return params for numa id 0 for any other
	 *            carveout heaps.
	 *     2. When multiple numa nodes present
	 *         a. Return params for numa id 0.
	 */
	int numa_id = 0;
	unsigned long free_mem = 0;
	int ret = 0;

	type = op->heap_mask;

	if (type && (type & (type - 1))) {
		ret = -EINVAL;
		goto exit;
	}

	if (is_numa_aware)
		numa_id = op->numa_id;

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
	op->largest_free_block = PAGE_SIZE;

	/*
	 * Special case: GPU heap
	 * When user is querying the GPU heap, that means the buffer was allocated from
	 * hugetlbfs, so we need to return the HugePages_Total, HugePages_Free values
	 */
	if (type & NVMAP_HEAP_CARVEOUT_GPU) {
		ret = compute_hugetlbfs_stat(&op->total, &op->free, numa_id);
		if (ret)
			goto exit;

		op->largest_free_block = SIZE_2MB;
		op->granule_size = SIZE_2MB;
	} else if (type & NVMAP_HEAP_CARVEOUT_MASK) {
		for (i = 0; i < nvmap_dev->nr_carveouts; i++) {
			if ((type & nvmap_get_heap_bit(nvmap_dev->heaps[i])) &&
				(numa_id == nvmap_get_heap_nid(nvmap_get_heap_ptr(
				nvmap_dev->heaps[i])))) {
				heap = nvmap_get_heap_ptr(nvmap_dev->heaps[i]);
				op->total = nvmap_query_heap_size(heap);
				op->free = nvmap_get_heap_free_size(heap);
				break;
			}
		}
		/* If queried heap is not present */
		if (i >= nvmap_dev->nr_carveouts) {
			ret = -ENODEV;
			goto exit;
		}

	} else if (type & iovmm_mask) {
		if (num_online_nodes() > 1) {
			/* multiple numa node exist */
			ret = compute_memory_stat(&op->total, &op->free, numa_id);
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
			op->total = system_heap_total_mem();
			ret = system_heap_free_mem(&free_mem);
			if (ret)
				goto exit;
			op->free = free_mem;
		}
		op->granule_size = PAGE_SIZE;
	}

	/*
	 * Align free size reported to the previous page.
	 * This avoids any AllocAttr failures due to using PAGE_ALIGN
	 * for allocating exactly the free memory reported.
	 */
	op->free = op->free & PAGE_MASK;
exit:
	return ret;
}

int nvmap_query_heap_peer(struct nvmap_carveout_node *co_heap, unsigned int *peer)
{
	struct nvmap_heap *heap;

	if (co_heap == NULL)
		return -EINVAL;

	heap = co_heap->carveout;
	if (heap == NULL || !heap->is_ivm)
		return -EINVAL;
	*peer = heap->peer;
	return 0;
}

size_t nvmap_query_heap_size(struct nvmap_heap *heap)
{
	if (!heap)
		return 0;

	return heap->len;
}

void nvmap_heap_debugfs_init(struct dentry *heap_root, struct nvmap_heap *heap)
{
	if (sizeof(heap->base) == sizeof(u64))
		debugfs_create_x64("base", S_IRUGO,
			heap_root, (u64 *)&heap->base);
	else
		debugfs_create_x32("base", S_IRUGO,
			heap_root, (u32 *)&heap->base);
	if (sizeof(heap->len) == sizeof(u64))
		debugfs_create_x64("size", S_IRUGO,
			heap_root, (u64 *)&heap->len);
	else
		debugfs_create_x32("size", S_IRUGO,
			heap_root, (u32 *)&heap->len);
	if (sizeof(heap->free_size) == sizeof(u64))
		debugfs_create_x64("free_size", S_IRUGO,
			heap_root, (u64 *)&heap->free_size);
	else
		debugfs_create_x32("free_size", S_IRUGO,
			heap_root, (u32 *)&heap->free_size);
}

static inline struct page **nvmap_kvzalloc_pages(u32 count)
{
	if (count * sizeof(struct page *) <= PAGE_SIZE)
		return kzalloc(count * sizeof(struct page *), GFP_KERNEL);
	else
		return vzalloc(count * sizeof(struct page *));
}

static void *__nvmap_dma_alloc_from_coherent(struct device *dev,
					     struct dma_coherent_mem_replica *mem,
					     size_t size,
					     dma_addr_t *dma_handle,
					     unsigned long attrs,
					     unsigned long start)
{
	int order = get_order(size);
	unsigned long flags;
	unsigned int count = 0, i = 0, j = 0;
	unsigned int alloc_size;
	unsigned long align, pageno, page_count, first_pageno;
	void *addr = NULL;
	struct page **pages = NULL;
	int do_memset = 0;
	int *bitmap_nos = NULL;

	if (dma_get_attr(DMA_ATTR_ALLOC_EXACT_SIZE, attrs)) {
		page_count = PAGE_ALIGN(size) >> PAGE_SHIFT;
		if (page_count > UINT_MAX) {
			dev_err(dev, "Page count more than max value\n");
			return NULL;
		}
		count = (unsigned int)page_count;
	} else
		count = 1U << order;

	if (!count)
		return NULL;

	bitmap_nos = vzalloc(count * sizeof(int));
	if (!bitmap_nos) {
		dev_err(dev, "failed to allocate memory\n");
		return NULL;
	}
	if ((mem->flags & DMA_MEMORY_NOMAP) &&
	    dma_get_attr(DMA_ATTR_ALLOC_SINGLE_PAGES, attrs)) {
		alloc_size = 1;
		pages = nvmap_kvzalloc_pages(count);

		if (!pages) {
			kvfree(bitmap_nos);
			return NULL;
		}
	} else {
		alloc_size = count;
	}

	spin_lock_irqsave(&mem->spinlock, flags);

	if (unlikely(size > ((u64)mem->size << PAGE_SHIFT)))
		goto err;

	if ((mem->flags & DMA_MEMORY_NOMAP) &&
	    dma_get_attr(DMA_ATTR_ALLOC_SINGLE_PAGES, attrs)) {
		align = 0;
	} else  {
		if (order > DMA_BUF_ALIGNMENT)
			align = (1 << DMA_BUF_ALIGNMENT) - 1;
		else
			align = (1 << order) - 1;
	}

	while (count) {
		pageno = bitmap_find_next_zero_area(mem->bitmap, mem->size,
						    start, alloc_size, align);

		if (pageno >= mem->size)
			goto err;

		if (!i)
			first_pageno = pageno;

		count -= alloc_size;
		if (pages)
			pages[i++] = pfn_to_page(mem->pfn_base + pageno);

		bitmap_set(mem->bitmap, pageno, alloc_size);
		bitmap_nos[j++] = pageno;
	}

	/*
	 * Memory was found in the coherent area.
	 */
	*dma_handle = mem->device_base + (first_pageno << PAGE_SHIFT);
	if (!(mem->flags & DMA_MEMORY_NOMAP)) {
		addr = mem->virt_base + (first_pageno << PAGE_SHIFT);
		do_memset = 1;
	} else if (dma_get_attr(DMA_ATTR_ALLOC_SINGLE_PAGES, attrs)) {
		addr = pages;
	}

	spin_unlock_irqrestore(&mem->spinlock, flags);

	if (do_memset)
		memset(addr, 0, size);

	kvfree(bitmap_nos);
	return addr;
err:
	while (j && j--)
		bitmap_clear(mem->bitmap, bitmap_nos[j], alloc_size);

	spin_unlock_irqrestore(&mem->spinlock, flags);
	kvfree(pages);
	kvfree(bitmap_nos);
	return NULL;
}

void *nvmap_dma_alloc_attrs(struct device *dev, size_t size,
			    dma_addr_t *dma_handle,
			    gfp_t flag, unsigned long attrs)
{
	union dma_coherent_mem_block dma_coherent_mem_type;
	struct dma_coherent_mem_replica *mem;

	if (!dev || !dev->dma_mem)
		return NULL;

	WARN_ON_ONCE(!dev->coherent_dma_mask);

	dma_coherent_mem_type.dma_mem = dev->dma_mem;
	mem = dma_coherent_mem_type.mem;

	if (!mem)
		return NULL;

	return __nvmap_dma_alloc_from_coherent(dev, mem, size, dma_handle,
						   attrs, 0);
}
EXPORT_SYMBOL(nvmap_dma_alloc_attrs);

static void *nvmap_dma_mark_declared_memory_occupied(struct device *dev,
					dma_addr_t device_addr, size_t size)
{
	union dma_coherent_mem_block dma_coherent_mem_type;
	struct dma_coherent_mem_replica *mem;
	unsigned long flags, pageno;
	unsigned int alloc_size;
	int pos;
	dma_addr_t diff;

	if (!dev || !dev->dma_mem)
		return ERR_PTR(-EINVAL);

	dma_coherent_mem_type.dma_mem = dev->dma_mem;
	mem = dma_coherent_mem_type.mem;

	if (!mem)
		return ERR_PTR(-EINVAL);

	BUG_ON(check_add_overflow(size, (size_t)device_addr & ~PAGE_MASK, &size));
	alloc_size = PAGE_ALIGN(size) >> PAGE_SHIFT;

	spin_lock_irqsave(&mem->spinlock, flags);
	if (check_sub_overflow(device_addr, mem->device_base, &diff)) {
		goto error;
	}

	pos = PFN_DOWN(diff);
	pageno = bitmap_find_next_zero_area(mem->bitmap, mem->size, pos, alloc_size, 0);
	if (pageno != pos)
		goto error;
	bitmap_set(mem->bitmap, pageno, alloc_size);
	spin_unlock_irqrestore(&mem->spinlock, flags);
	return mem->virt_base + (pos << PAGE_SHIFT);

error:
	spin_unlock_irqrestore(&mem->spinlock, flags);
	return ERR_PTR(-ENOMEM);
}

static void nvmap_dma_mark_declared_memory_unoccupied(struct device *dev,
					 dma_addr_t device_addr, size_t size)
{
	union dma_coherent_mem_block dma_coherent_mem_type;
	struct dma_coherent_mem_replica *mem;
	unsigned long flags;
	unsigned int alloc_size;
	int pos;

	if (!dev || !dev->dma_mem)
		return;

	dma_coherent_mem_type.dma_mem = dev->dma_mem;
	mem = dma_coherent_mem_type.mem;

	if (!mem)
		return;

	BUG_ON(check_add_overflow(size, (size_t)device_addr & ~PAGE_MASK, &size));
	alloc_size = PAGE_ALIGN(size) >> PAGE_SHIFT;

	spin_lock_irqsave(&mem->spinlock, flags);
	BUG_ON(device_addr < mem->device_base);
	pos = PFN_DOWN(device_addr - mem->device_base);
	bitmap_clear(mem->bitmap, pos, alloc_size);
	spin_unlock_irqrestore(&mem->spinlock, flags);
}

static phys_addr_t nvmap_alloc_mem(struct nvmap_heap *h, size_t len,
				   phys_addr_t *start)
{
	phys_addr_t pa = DMA_MAPPING_ERROR;
	struct device *dev = h->dma_dev;
	phys_addr_t sum;

	if (start && h->is_ivm) {
		void *ret;
		if (check_add_overflow(h->base, (*start), &sum))
			return DMA_ERROR_CODE;

		pa = sum;
		ret = nvmap_dma_mark_declared_memory_occupied(dev, pa, len);
		if (IS_ERR(ret)) {
			dev_err(dev, "Failed to reserve (%pa) len(%zu)\n",
					&pa, len);
			return DMA_ERROR_CODE;
		}
	} else {
		(void)nvmap_dma_alloc_attrs(dev, len, &pa,
				GFP_KERNEL, DMA_ATTR_ALLOC_EXACT_SIZE);
		if (!dma_mapping_error(dev, pa)) {
			dev_dbg(dev, "Allocated addr (%pa) len(%zu)\n",
					&pa, len);
		}
	}

	return pa;
}

void nvmap_dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
			  dma_addr_t dma_handle, unsigned long attrs)
{
	void *mem_addr;
	unsigned long flags;
	unsigned long pageno;
	union dma_coherent_mem_block dma_coherent_mem_type;
	struct dma_coherent_mem_replica *mem;

	if (!dev || !dev->dma_mem)
		return;

	dma_coherent_mem_type.dma_mem = dev->dma_mem;
	mem = dma_coherent_mem_type.mem;

	if (!mem)
		return;

	if ((mem->flags & DMA_MEMORY_NOMAP) &&
	    dma_get_attr(DMA_ATTR_ALLOC_SINGLE_PAGES, attrs)) {
		struct page **pages = cpu_addr;
		int i;

		spin_lock_irqsave(&mem->spinlock, flags);
		for (i = 0; i < (size >> PAGE_SHIFT); i++) {
			BUG_ON(check_sub_overflow(page_to_pfn(pages[i]), mem->pfn_base, &pageno));
			if (WARN_ONCE(pageno > mem->size,
				      "invalid pageno:%lu\n", pageno))
				continue;
			bitmap_clear(mem->bitmap, pageno, 1);
		}
		spin_unlock_irqrestore(&mem->spinlock, flags);
		kvfree(pages);
		return;
	}

	if (mem->flags & DMA_MEMORY_NOMAP)
		mem_addr =  (void *)(uintptr_t)mem->device_base;
	else
		mem_addr =  mem->virt_base;

	if (cpu_addr >= mem_addr &&
	    cpu_addr - mem_addr < (u64)mem->size << PAGE_SHIFT) {
		unsigned int page = (cpu_addr - mem_addr) >> PAGE_SHIFT;
		unsigned long flags;
		unsigned int count;

		if (DMA_ATTR_ALLOC_EXACT_SIZE & attrs)
			count = PAGE_ALIGN(size) >> PAGE_SHIFT;
		else
			count = 1U << get_order(size);

		spin_lock_irqsave(&mem->spinlock, flags);
		bitmap_clear(mem->bitmap, page, count);
		spin_unlock_irqrestore(&mem->spinlock, flags);
	}
}
EXPORT_SYMBOL(nvmap_dma_free_attrs);

static void nvmap_free_mem(struct nvmap_heap *h, phys_addr_t base,
			   size_t len)
{
	struct device *dev = h->dma_dev;

	dev_dbg(dev, "Free base (%pa) size (%zu)\n", &base, len);

	if (h->is_ivm && !h->can_alloc) {
		nvmap_dma_mark_declared_memory_unoccupied(dev, base, len);
	} else {
		nvmap_dma_free_attrs(dev, len,
				     (void *)(uintptr_t)base,
				     (dma_addr_t)base,
				     DMA_ATTR_ALLOC_EXACT_SIZE);
	}
}

/*
 * base_max limits position of allocated chunk in memory.
 * if base_max is 0 then there is no such limitation.
 */
static struct nvmap_heap_block *do_heap_alloc(struct nvmap_heap *heap,
					      size_t len, size_t align,
					      unsigned int mem_prot,
					      phys_addr_t base_max,
					      phys_addr_t *start)
{
	struct list_block *heap_block = NULL;
	dma_addr_t dev_base;
	struct device *dev = heap->dma_dev;

	/* since pages are only mappable with one cache attribute,
	 * and most allocations from carveout heaps are DMA coherent
	 * (i.e., non-cacheable), round cacheable allocations up to
	 * a page boundary to ensure that the physical pages will
	 * only be mapped one way. */
	if (mem_prot == NVMAP_HANDLE_CACHEABLE ||
	    mem_prot == NVMAP_HANDLE_INNER_CACHEABLE) {
		align = max_t(size_t, align, PAGE_SIZE);
		len = PAGE_ALIGN(len);
	}

	if (heap->is_ivm)
		align = max_t(size_t, align, NVMAP_IVM_ALIGNMENT);

	heap_block = kmem_cache_zalloc(heap_block_cache, GFP_KERNEL);
	if (!heap_block) {
		dev_err(dev, "%s: failed to alloc heap block %s\n",
			__func__, dev_name(dev));
		goto fail_heap_block_alloc;
	}

	dev_base = nvmap_alloc_mem(heap, len, start);
	if (dma_mapping_error(dev, dev_base)) {
		dev_err(dev, "failed to alloc mem of size (%zu)\n",
			len);
		goto fail_dma_alloc;
	}

	heap_block->block.base = dev_base;
	heap_block->orig_addr = dev_base;
	heap_block->size = len;

	list_add_tail(&heap_block->all_list, &heap->all_list);
	heap_block->heap = heap;
	BUG_ON(heap->free_size < len);
	heap->free_size -= len;
	heap_block->mem_prot = mem_prot;
	heap_block->align = align;
	return &heap_block->block;

fail_dma_alloc:
	kmem_cache_free(heap_block_cache, heap_block);
fail_heap_block_alloc:
	return NULL;
}

static void do_heap_free(struct nvmap_heap_block *block)
{
	struct list_block *b = container_of(block, struct list_block, block);
	struct nvmap_heap *heap = b->heap;

	list_del(&b->all_list);

	nvmap_free_mem(heap, block->base, b->size);
	BUG_ON(check_add_overflow(b->size, heap->free_size, &heap->free_size));
	kmem_cache_free(heap_block_cache, b);
}

/* nvmap_heap_alloc: allocates a block of memory of len bytes, aligned to
 * align bytes. */
struct nvmap_heap_block *nvmap_heap_alloc(struct nvmap_heap *h,
					  struct nvmap_handle *handle,
					  phys_addr_t *start)
{
	struct nvmap_heap_block *b;
	size_t len        = handle->size;
	size_t align      = handle->align;
	unsigned int prot = handle->flags;

	mutex_lock(&h->lock);

	if (h->is_ivm) { /* Is IVM carveout? */
		/* Check if this correct IVM heap */
		if (handle->peer != h->peer) {
			mutex_unlock(&h->lock);
			return NULL;
		} else {
			if (h->can_alloc && start) {
				/* If this partition does actual allocation, it
				 * should not specify start_offset.
				 */
				mutex_unlock(&h->lock);
				return NULL;
			} else if (!h->can_alloc && !start) {
				/* If this partition does not do actual
				 * allocation, it should specify start_offset.
				 */
				mutex_unlock(&h->lock);
				return NULL;
			}
		}
	}

	/*
	 * If this HEAP has pm_ops defined and powering on the
	 * RAM attached with the HEAP returns error, don't
	 * allocate from the heap and return NULL.
	 */
	if (h->pm_ops.busy) {
		if (h->pm_ops.busy() < 0) {
			pr_err("Unable to power on the heap device\n");
			mutex_unlock(&h->lock);
			return NULL;
		}
	}

	align = max_t(size_t, align, L1_CACHE_BYTES);
	b = do_heap_alloc(h, len, align, prot, 0, start);
	if (b) {
		b->handle = handle;
		handle->carveout = b;
		/* Generate IVM for partition that can alloc */
		if (h->is_ivm && h->can_alloc) {
			unsigned int offs = (b->base - h->base);

			BUG_ON(offs & (NVMAP_IVM_ALIGNMENT - 1));
			BUG_ON((offs >> ffs(NVMAP_IVM_ALIGNMENT)) &
				~((1 << NVMAP_IVM_OFFSET_WIDTH) - 1));
			BUG_ON(h->vm_id & ~(NVMAP_IVM_IVMID_MASK));
			/* So, page alignment is sufficient check.
			 */
			BUG_ON(len & ~(PAGE_MASK));

			/* Copy offset from IVM mem pool in nvmap handle.
			 * The offset will be exported via ioctl.
			 */
			handle->offs = offs;

			handle->ivm_id = ((u64)h->vm_id << NVMAP_IVM_IVMID_SHIFT);
			handle->ivm_id |= (((offs >> (ffs(NVMAP_IVM_ALIGNMENT) - 1)) &
					 ((1ULL << NVMAP_IVM_OFFSET_WIDTH) - 1)) <<
					  NVMAP_IVM_OFFSET_SHIFT);
			handle->ivm_id |= (len >> PAGE_SHIFT);
		}
	}
	mutex_unlock(&h->lock);
	return b;
}

struct nvmap_heap *nvmap_block_to_heap(struct nvmap_heap_block *b)
{
	struct list_block *lb;
	lb = container_of(b, struct list_block, block);
	return lb->heap;
}

/* nvmap_heap_free: frees block b*/
void nvmap_heap_free(struct nvmap_heap_block *b)
{
	struct nvmap_heap *h;
	struct list_block *lb;

	if (!b)
		return;

	h = nvmap_block_to_heap(b);
	mutex_lock(&h->lock);

	lb = container_of(b, struct list_block, block);
#ifndef NVMAP_CONFIG_CACHE_FLUSH_AT_ALLOC
	/*
	 * For carveouts, if cache flush is done at buffer allocation time
	 * then no need to do it during buffer release time.
	 */
	nvmap_flush_heap_block(NULL, b, lb->size, lb->mem_prot);
#endif /* !NVMAP_CONFIG_CACHE_FLUSH_AT_ALLOC */
	do_heap_free(b);
	/*
	 * If this HEAP has pm_ops defined and powering off the
	 * RAM attached with the HEAP returns error, raise warning.
	 */
	if (h->pm_ops.idle) {
		if (h->pm_ops.idle() < 0)
			WARN_ON(1);
	}

	mutex_unlock(&h->lock);
}

static int nvmap_dma_init_coherent_memory(
	phys_addr_t phys_addr, dma_addr_t device_addr, size_t size, int flags,
	struct dma_coherent_mem_replica **mem)
{
	struct dma_coherent_mem_replica *dma_mem = NULL;
	void *mem_base = NULL;
	int pages = size >> PAGE_SHIFT;
	int bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);
	int ret;

	if (!size)
		return -EINVAL;

	if ((flags & DMA_MEMORY_NOMAP) == 0) {
		mem_base = memremap(phys_addr, size, MEMREMAP_WC);
		if (!mem_base)
			return -EINVAL;
	}

	dma_mem = kzalloc(sizeof(struct dma_coherent_mem_replica), GFP_KERNEL);
	if (!dma_mem) {
		ret = -ENOMEM;
		goto err_memunmap;
	}

	dma_mem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (dma_mem->bitmap == NULL) {
		ret = -ENOMEM;
		goto err_free_dma_mem;
	}

	dma_mem->virt_base = mem_base;
	dma_mem->device_base = device_addr;
	dma_mem->pfn_base = PFN_DOWN(device_addr);
	dma_mem->size = pages;
	dma_mem->flags = flags;
	spin_lock_init(&dma_mem->spinlock);

	*mem = dma_mem;
	return 0;

err_free_dma_mem:
	kfree(dma_mem);

err_memunmap:
	memunmap(mem_base);
	return ret;
}

static int nvmap_dma_assign_coherent_memory(struct device *dev,
				      struct dma_coherent_mem_replica *mem)
{
	if (!dev)
		return -ENODEV;

	if (dev->dma_mem)
		return -EBUSY;

	dev->dma_mem = (struct dma_coherent_mem *)mem;
	return 0;
}

static void nvmap_dma_release_coherent_memory(struct dma_coherent_mem_replica *mem)
{
	if (!mem)
		return;
	if (!(mem->flags & DMA_MEMORY_NOMAP))
		memunmap(mem->virt_base);
	kfree(mem->bitmap);
	kfree(mem);
}

int nvmap_dma_declare_coherent_memory(struct device *dev, phys_addr_t phys_addr,
			dma_addr_t device_addr, size_t size, int flags)
{
	struct dma_coherent_mem_replica *mem;
	int ret;

	ret = nvmap_dma_init_coherent_memory(phys_addr, device_addr, size, flags, &mem);
	if (ret)
		return ret;

	ret = nvmap_dma_assign_coherent_memory(dev, mem);
	if (ret)
		nvmap_dma_release_coherent_memory(mem);
	return ret;
}

/* nvmap_heap_create: create a heap object of len bytes, starting from
 * address base.
 */
struct nvmap_heap *nvmap_heap_create(struct device *parent,
				     const struct nvmap_platform_carveout *co,
				     phys_addr_t base, size_t len, void *arg)
{
	struct nvmap_heap *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h) {
		pr_err("%s: out of memory\n", __func__);
		return NULL;
	}

	h->dma_dev = co->dma_dev;
	if (co->cma_dev) {
#ifndef CONFIG_DMA_CMA
		pr_err("invalid resize config for carveout %s\n",
				co->name);
		goto fail;
#endif
	} else if (!co->init_done) {
		int err;

		/* declare Non-CMA heap */
		err = nvmap_dma_declare_coherent_memory(h->dma_dev, 0, base, len,
				DMA_MEMORY_NOMAP);
		if (!err) {
			pr_info("%s :dma coherent mem declare %pa,%zu\n",
				co->name, &base, len);
		} else {
			pr_err("%s: dma coherent declare fail %pa,%zu\n",
				co->name, &base, len);
			goto fail;
		}
	}

	dev_set_name(h->dma_dev, "%s", co->name);
	set_dev_node(co->dma_dev, co->numa_node_id);
	dma_set_coherent_mask(h->dma_dev, DMA_BIT_MASK(64));
	h->name = co->name;
	h->arg = arg;
	h->base = base;
	h->can_alloc = !!co->can_alloc;
	h->is_ivm = co->is_ivm;
	h->numa_node_id = co->numa_node_id;
	h->len = len;
	h->free_size = len;
	h->peer = co->peer;
	h->vm_id = co->vmid;
	if (co->pm_ops.busy)
		h->pm_ops.busy = co->pm_ops.busy;

	if (co->pm_ops.idle)
		h->pm_ops.idle = co->pm_ops.idle;

	h->carevout_debugfs_info = kmalloc(sizeof(struct debugfs_info), GFP_KERNEL);
	INIT_LIST_HEAD(&h->all_list);
	mutex_init(&h->lock);
#ifdef NVMAP_CONFIG_DEBUG_MAPS
	h->device_names = RB_ROOT;
#endif /* NVMAP_CONFIG_DEBUG_MAPS */
#ifndef NVMAP_CONFIG_CACHE_FLUSH_AT_ALLOC
	/*
	 * For carveouts, if cache flush is done at buffer allocation time
	 * then no need to do it during carveout creation time.
	 */
	if (!co->no_cpu_access && co->usage_mask != NVMAP_HEAP_CARVEOUT_VPR
		&& nvmap_cache_maint_phys_range(NVMAP_CACHE_OP_WB_INV,
				base, base + len, true, true)) {
		pr_err("cache flush failed\n");
		goto fail;
	}
#endif /* !NVMAP_CONFIG_CACHE_FLUSH_AT_ALLOC */
	wmb();

	if (co->disable_dynamic_dma_map)
		nvmap_dev->dynamic_dma_map_mask &= ~co->usage_mask;

	if (co->no_cpu_access)
		nvmap_dev->cpu_access_mask &= ~co->usage_mask;

	pr_info("created heap %s base 0x%px size (%zuKiB)\n",
		co->name, (void *)(uintptr_t)base, len/1024);
	return h;
fail:
	kfree(h->carevout_debugfs_info);
	if (h->dma_dev->kobj.name)
		kfree_const(h->dma_dev->kobj.name);
	kfree(h);
	return NULL;
}

/* nvmap_heap_destroy: frees all resources in heap */
void nvmap_heap_destroy(struct nvmap_heap *heap)
{
	WARN_ON(!list_empty(&heap->all_list));
	if (heap->dma_dev->kobj.name)
		kfree_const(heap->dma_dev->kobj.name);

	if (heap->is_ivm)
		kfree(heap->name);

	kfree(heap->carevout_debugfs_info);
	nvmap_dma_release_coherent_memory((struct dma_coherent_mem_replica *)
					  heap->dma_dev->dma_mem);

	while (!list_empty(&heap->all_list)) {
		struct list_block *l;
		l = list_first_entry(&heap->all_list, struct list_block,
				     all_list);
		list_del(&l->all_list);
		kmem_cache_free(heap_block_cache, l);
	}
	kfree(heap);
}

int nvmap_heap_init(void)
{
	ulong start_time = sched_clock();
	ulong result;

	heap_block_cache = KMEM_CACHE(list_block, 0);
	if (!heap_block_cache) {
		pr_err("%s: unable to create heap block cache\n", __func__);
		return -ENOMEM;
	}
	pr_info("%s: created heap block cache\n", __func__);
	if (check_sub_overflow((ulong)sched_clock(), start_time, &result) ||
		check_add_overflow(nvmap_init_time, result, &result))
		return -EOVERFLOW;

	nvmap_init_time = result;
	return 0;
}

void nvmap_heap_deinit(void)
{
	if (heap_block_cache)
		kmem_cache_destroy(heap_block_cache);

	heap_block_cache = NULL;
}
#ifndef NVMAP_CONFIG_CACHE_FLUSH_AT_ALLOC
/*
 * This routine is used to flush the carveout memory from cache.
 * Why cache flush is needed for carveout? Consider the case, where a piece of
 * carveout is allocated as cached and released. After this, if the same memory is
 * allocated for uncached request and the memory is not flushed out from cache.
 * In this case, the client might pass this to H/W engine and it could start modify
 * the memory. As this was cached earlier, it might have some portion of it in cache.
 * During cpu request to read/write other memory, the cached portion of this memory
 * might get flushed back to main memory and would cause corruptions, if it happens
 * after H/W writes data to memory.
 *
 * But flushing out the memory blindly on each carveout allocation is redundant.
 *
 * In order to optimize the carveout buffer cache flushes, the following
 * strategy is used.
 *
 * The whole Carveout is flushed out from cache during its initialization.
 * During allocation, carveout buffers are not flused from cache.
 * During deallocation, carveout buffers are flushed, if they were allocated as cached.
 * if they were allocated as uncached/writecombined, no cache flush is needed.
 * Just draining store buffers is enough.
 */
static int nvmap_flush_heap_block(struct nvmap_client *client,
	struct nvmap_heap_block *block, size_t len, unsigned int prot)
{
	phys_addr_t phys = block->base;
	phys_addr_t end = block->base + len;
	int ret = 0;

	if (prot == NVMAP_HANDLE_UNCACHEABLE || prot == NVMAP_HANDLE_WRITE_COMBINE)
		goto out;

	ret = nvmap_cache_maint_phys_range(NVMAP_CACHE_OP_WB_INV, phys, end,
				true, prot != NVMAP_HANDLE_INNER_CACHEABLE);

out:
	wmb();
	return ret;
}
#endif /* !NVMAP_CONFIG_CACHE_FLUSH_AT_ALLOC */

size_t nvmap_get_heap_free_size(struct nvmap_heap *heap)
{
	return heap->free_size;
}

int nvmap_get_heap_nid(struct nvmap_heap *heap)
{
	return heap->numa_node_id;
}

#ifdef NVMAP_CONFIG_DEBUG_MAPS
struct rb_root *nvmap_heap_get_device_ptr(struct nvmap_heap *heap)
{
	return &heap->device_names;
}
#endif /* NVMAP_CONFIG_DEBUG_MAPS */

phys_addr_t nvmap_get_heap_block_base(struct nvmap_heap_block *block)
{
	return block->base;
}

unsigned int nvmap_get_debug_info_heap(struct debugfs_info *info)
{
	return info->heap_bit;
}

int nvmap_get_debug_info_nid(struct debugfs_info *info)
{
	return info->numa_id;
}

void nvmap_set_heap_block_handle(struct nvmap_heap_block *block, struct nvmap_handle *handle)
{
	block->handle = handle;
}

struct debugfs_info *nvmap_create_debugfs_info(void)
{
	struct debugfs_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	return info;
}

void nvmap_free_debugfs_info(struct debugfs_info *info)
{
	if (info != NULL)
		kfree(info);
}

void nvmap_set_debugfs_heap(struct debugfs_info *info, unsigned int heap_bit)
{
	info->heap_bit = heap_bit;
}

void nvmap_set_debugfs_numa(struct debugfs_info *info, int nid)
{
	info->numa_id = nid;
}

unsigned int nvmap_get_heap_bit(struct nvmap_carveout_node *co_heap)
{
	return co_heap->heap_bit;
}

struct nvmap_heap *nvmap_get_heap_ptr(struct nvmap_carveout_node *co_heap)
{
	return co_heap->carveout;
}

#ifdef NVMAP_CONFIG_DEBUG_MAPS
struct rb_root *nvmap_get_device_names(struct nvmap_carveout_node *co_heap)
{
	return &co_heap->carveout->device_names;
}
#endif /* NVMAP_CONFIG_DEBUG_MAPS */
