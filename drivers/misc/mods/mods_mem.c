// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2008-2023, NVIDIA CORPORATION.  All rights reserved. */

#include "mods_internal.h"

#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/sched.h>

#if defined(MODS_HAS_SET_DMA_MASK)
#include <linux/dma-mapping.h>
#include <linux/of.h>
#endif

#ifdef CONFIG_ARM64
#include <linux/cache.h>
#endif

#define MODS_MEM_MAX_RESERVATIONS 16

/* Structure used by this module to track existing reservations */
struct MODS_MEM_RESERVATION {
	struct MODS_MEM_INFO *p_mem_info;
	u8                   client_id;
};
static struct MODS_MEM_RESERVATION mem_reservations[MODS_MEM_MAX_RESERVATIONS];
DEFINE_MUTEX(mem_reservation_mtx);

static struct MODS_MEM_INFO *get_mem_handle(struct mods_client *client,
					    u64                 handle)
{
	/* For now just check if we hit first or last page, i.e. if
	 * we have a valid pointer.  In the future, add proper handle
	 * accounting.
	 */
	if (unlikely((handle + PAGE_SIZE) < (2 * PAGE_SIZE))) {
		cl_error("invalid memory handle 0x%llx\n", (unsigned long long)handle);
		return NULL;
	}

	return (struct MODS_MEM_INFO *)(size_t)handle;
}

static bool validate_mem_handle(struct mods_client   *client,
				struct MODS_MEM_INFO *p_mem_info)
{
	struct list_head *head = &client->mem_alloc_list;
	struct list_head *iter;

	if (unlikely(!p_mem_info))
		return false;

	list_for_each(iter, head) {
		struct MODS_MEM_INFO *p_mem = list_entry(iter, struct MODS_MEM_INFO, list);

		if (p_mem == p_mem_info)
			return true;
	}

	return false;
}

/****************************
 * DMA MAP HELPER FUNCTIONS *
 ****************************/

/*
 * Starting on Power9 systems, DMA addresses for NVLink are no longer
 * the same as used over PCIE.
 *
 * Power9 supports a 56-bit Real Address. This address range is compressed
 * when accessed over NvLink to allow the GPU to access all of memory using
 * its 47-bit Physical address.
 *
 * If there is an NPU device present on the system, it implies that NvLink
 * sysmem links are present and we need to apply the required address
 * conversion for NvLink within the driver. This is intended to be temporary
 * to ease the transition to kernel APIs to handle NvLink DMA mappings
 * via the NPU device.
 *
 * Note, a deviation from the documented compression scheme is that the
 * upper address bits (i.e. bit 56-63) instead of being set to zero are
 * preserved during NvLink address compression so the orignal PCIE DMA
 * address can be reconstructed on expansion. These bits can be safely
 * ignored on NvLink since they are truncated by the GPU.
 */
#if defined(CONFIG_PPC64) && defined(CONFIG_PCI)
static dma_addr_t compress_nvlink_addr(struct pci_dev *dev, dma_addr_t addr)
{
	dma_addr_t addr47 = addr;

	/* Note, one key difference from the documented compression scheme
	 * is that BIT59 used for TCE bypass mode on PCIe is preserved during
	 * NVLink address compression to allow for the resulting DMA address to
	 * be used transparently on PCIe.
	 */
	if (dev && has_npu_dev(dev, 0)) {
		addr47 = addr & (1LLU << 59);
		addr47 |= ((addr >> 45) & 0x3) << 43;
		addr47 |= ((addr >> 49) & 0x3) << 45;
		addr47 |= addr & ((1LLU << 43) - 1);
	}

	return addr47;
}
#else
#define compress_nvlink_addr(dev, addr) (addr)
#endif

static void copy_wc_bitmap(struct MODS_MEM_INFO *p_dest_mem_info,
			   unsigned long         first_dst_chunk,
			   struct MODS_MEM_INFO *p_src_mem_info,
			   unsigned long         num_chunks)
{
	unsigned long src_pos = 0;

	WARN_ON(p_dest_mem_info->cache_type != p_src_mem_info->cache_type);

	if (p_src_mem_info->cache_type == MODS_ALLOC_CACHED)
		return;

	WARN_ON(!p_dest_mem_info->wc_bitmap);
	WARN_ON(!p_src_mem_info->wc_bitmap);

	for (;;) {
		src_pos = find_next_bit(p_src_mem_info->wc_bitmap,
					num_chunks,
					src_pos);

		if (src_pos >= num_chunks)
			break;

		set_bit(src_pos + first_dst_chunk, p_dest_mem_info->wc_bitmap);

		++src_pos;
	}
}

static inline bool is_chunk_wc(struct MODS_MEM_INFO *p_mem_info, u32 ichunk)
{
	return p_mem_info->wc_bitmap && test_bit(ichunk, p_mem_info->wc_bitmap);
}

static void mark_chunk_wc(struct MODS_MEM_INFO *p_mem_info, u32 ichunk)
{
	WARN_ON(p_mem_info->cache_type == MODS_ALLOC_CACHED);
	WARN_ON(!p_mem_info->wc_bitmap);
	set_bit(ichunk, p_mem_info->wc_bitmap);
}

static void print_map_info(struct mods_client *client,
			   const char         *action,
			   struct scatterlist *sg,
			   u32                 nents,
			   struct device      *dev)
{
	u32 i;

	for_each_sg(sg, sg, nents, i) {
		cl_debug(DEBUG_MEM_DETAILED,
			 "dma %s iova=0x%llx dma_len=0x%x phys=0x%llx size=0x%x on dev %s\n",
			 action,
			 (unsigned long long)sg_dma_address(sg),
			 sg_dma_len(sg),
			 (unsigned long long)sg_phys(sg),
			 sg->length,
			 dev_name(dev));
	}
}

static int map_sg(struct mods_client *client,
		  struct device      *dev,
		  struct scatterlist *sg,
		  u32                 num_chunks,
		  u32                 num_pages)
{
	const u32 max_pages = (u32)(0x100000000ULL >> PAGE_SHIFT);

	if (num_pages >= max_pages)
		cl_warn("requested to map %u pages in %u chunks\n",
			num_pages, num_chunks);

	do {
		u32 chunks_to_map = num_chunks;
		u32 pages_to_map  = num_pages;
		int mapped;

		/* Some HW IOMMU drivers can coalesce multiple chunks into
		 * a single contiguous VA mapping, which is exposed via the
		 * first chunk.  However, dma_length field is unsigned int
		 * and not able to represent mappings which exceed 4GB.
		 * To alleviate it, split large allocations into multiple
		 * mappings.
		 */
		if (num_pages >= max_pages) {

			struct scatterlist *cur_sg;

			pages_to_map = 0;

			for_each_sg(sg, cur_sg, num_chunks, chunks_to_map) {

				const unsigned int len = cur_sg->length;
				const u32 cur_pages = len >> PAGE_SHIFT;

				if ((u64)pages_to_map + cur_pages >= max_pages)
					break;

				pages_to_map += cur_pages;
			}
		}

		mapped = dma_map_sg(dev, sg, (int)chunks_to_map,
				    DMA_BIDIRECTIONAL);

		if (mapped == 0) {
			cl_error(
				"failed to dma map %u chunks at 0x%llx to dev %s with dma mask 0x%llx\n",
				num_chunks,
				(unsigned long long)sg_phys(sg),
				dev_name(dev),
				(unsigned long long)dma_get_mask(dev));

			return -EIO;
		}

		sg         += chunks_to_map;
		num_chunks -= chunks_to_map;
		num_pages  -= pages_to_map;

	} while (num_chunks);

	return OK;
}

static void unmap_sg(struct device      *dev,
		     struct scatterlist *sg,
		     u32                 num_chunks)
{
	do {
		struct scatterlist *cur_sg;
		u32                 chunks_to_unmap = 0;

		for_each_sg(sg, cur_sg, num_chunks, chunks_to_unmap)
			if (!sg_dma_len(cur_sg))
				break;

		dma_unmap_sg(dev, sg, (int)chunks_to_unmap, DMA_BIDIRECTIONAL);

		sg         += chunks_to_unmap;
		num_chunks -= chunks_to_unmap;

		/* Skip chunks which don't maintain any DMA mappings.
		 * This can happen for large allocations with the workaround
		 * in map_sg().
		 */
		if (num_chunks) {
			for_each_sg(sg, sg, num_chunks, chunks_to_unmap)
				if (sg_dma_len(sg))
					break;
			num_chunks -= chunks_to_unmap;
		}

	} while (num_chunks);
}

/* Unmap and delete the specified DMA mapping */
static void dma_unmap_and_free(struct mods_client   *client,
			       struct MODS_MEM_INFO *p_mem_info,
			       struct MODS_DMA_MAP  *p_del_map)

{
	const u32 nents = get_num_chunks(p_mem_info);

	print_map_info(client, "unmap", p_del_map->sg, nents, p_del_map->dev);

	unmap_sg(p_del_map->dev, p_del_map->sg, nents);

	pci_dev_put(p_del_map->pcidev);

	list_del(&p_del_map->list);

	kfree(p_del_map);
	atomic_dec(&client->num_allocs);
}

/* Unmap and delete all DMA mappings for the specified allocation */
static int dma_unmap_all(struct mods_client   *client,
			 struct MODS_MEM_INFO *p_mem_info,
			 struct device        *dev)
{
	int               err  = OK;
	struct list_head *head = &p_mem_info->dma_map_list;
	struct list_head *iter;
	struct list_head *tmp;

#ifdef CONFIG_PCI
	if (sg_dma_address(p_mem_info->sg) &&
	    (dev == &p_mem_info->dev->dev || !dev)) {

		unmap_sg(&p_mem_info->dev->dev,
			 p_mem_info->sg,
			 get_num_chunks(p_mem_info));

		sg_dma_address(p_mem_info->sg) = 0;
	}
#endif

	list_for_each_safe(iter, tmp, head) {
		struct MODS_DMA_MAP *p_dma_map;

		p_dma_map = list_entry(iter, struct MODS_DMA_MAP, list);

		if (!dev || (p_dma_map->dev == dev)) {
			dma_unmap_and_free(client, p_mem_info, p_dma_map);
			if (dev)
				break;
		}
	}

	return err;
}

/* Create a DMA map on the specified allocation for the pci device.
 * Lazy-initialize the map list structure if one does not yet exist.
 */
static int create_dma_map(struct mods_client   *client,
			  struct MODS_MEM_INFO *p_mem_info,
			  struct pci_dev       *pcidev,
			  struct device        *dev)
{
	struct MODS_DMA_MAP *p_dma_map;
	struct scatterlist  *sg;
	const u32            num_chunks = get_num_chunks(p_mem_info);
	size_t               alloc_size;
	u32                  i;
	int                  err;

	alloc_size = sizeof(struct MODS_DMA_MAP) +
		     num_chunks * sizeof(struct scatterlist);

	p_dma_map = kzalloc(alloc_size, GFP_KERNEL | __GFP_NORETRY);

	if (unlikely(!p_dma_map)) {
		cl_error("failed to allocate device map data\n");
		return -ENOMEM;
	}
	atomic_inc(&client->num_allocs);

#ifdef CONFIG_PCI
	p_dma_map->pcidev = pcidev ? pci_dev_get(pcidev) : NULL;
#endif
	p_dma_map->dev    = dev;

	sg_init_table(p_dma_map->sg, num_chunks);

	for_each_sg(p_mem_info->sg, sg, num_chunks, i)
		sg_set_page(&p_dma_map->sg[i], sg_page(sg), sg->length, 0);

	err = map_sg(client, dev, p_dma_map->sg, num_chunks,
		     p_mem_info->num_pages);

	print_map_info(client, "map", p_dma_map->sg, num_chunks, dev);

	if (unlikely(err)) {
		pci_dev_put(pcidev);
		kfree(p_dma_map);
		atomic_dec(&client->num_allocs);
	} else {
		list_add(&p_dma_map->list, &p_mem_info->dma_map_list);
	}

	return err;
}

#ifdef CONFIG_PCI
/* DMA-map memory to the device for which it has been allocated, if it hasn't
 * been mapped already.
 */
static int dma_map_to_default_dev(struct mods_client   *client,
				  struct MODS_MEM_INFO *p_mem_info)
{
	struct device *const dev        = &p_mem_info->dev->dev;
	const u32            num_chunks = get_num_chunks(p_mem_info);
	int                  err;

	if (sg_dma_address(p_mem_info->sg)) {
		cl_debug(DEBUG_MEM_DETAILED,
			 "memory %p already mapped to dev %s\n",
			 p_mem_info,
			 dev_name(dev));
		return OK;
	}

	err = map_sg(client, dev, p_mem_info->sg, num_chunks,
		     p_mem_info->num_pages);

	print_map_info(client, "map default", p_mem_info->sg, num_chunks, dev);

	return err;
}
#endif /* CONFIG_PCI */

#ifdef CONFIG_ARM64
static void clear_contiguous_cache(struct mods_client *client,
				   u64                 virt_start,
				   u32                 size);
#endif

static int setup_cache_attr(struct mods_client   *client,
			    struct MODS_MEM_INFO *p_mem_info,
			    u32                   ichunk)
{
	const bool need_wc = p_mem_info->cache_type != MODS_ALLOC_CACHED;
	int        err = 0;

	if (need_wc && !is_chunk_wc(p_mem_info, ichunk)) {
		struct scatterlist *sg = &p_mem_info->alloc_sg[ichunk];
		unsigned int        offs;

		for (offs = 0; offs < sg->length; offs += PAGE_SIZE) {
			void *ptr;

			ptr = MODS_KMAP(sg_page(sg) + (offs >> PAGE_SHIFT));
			if (unlikely(!ptr)) {
				cl_error("kmap failed\n");
				return -ENOMEM;
			}
#ifdef CONFIG_ARM64
			clear_contiguous_cache(client,
					       (u64)(size_t)ptr,
					       PAGE_SIZE);
#else
			if (p_mem_info->cache_type == MODS_ALLOC_WRITECOMBINE)
				err = MODS_SET_MEMORY_WC((unsigned long)ptr, 1);
			else
				err = MODS_SET_MEMORY_UC((unsigned long)ptr, 1);
#endif
			MODS_KUNMAP(ptr);
			if (unlikely(err)) {
				cl_error("set cache type failed\n");
				return err;
			}

			/* Set this flag early, so that when an error occurs,
			 * release_chunks() will restore cache attributes
			 * for all pages.  It's OK to restore cache attributes
			 * even for chunks where we haven't change them.
			 */
			mark_chunk_wc(p_mem_info, ichunk);

			/* Avoid superficial lockups */
			cond_resched();
		}
	}

	return err;
}

/* Find the dma mapping chunk for the specified memory. */
static struct MODS_DMA_MAP *find_dma_map(struct MODS_MEM_INFO  *p_mem_info,
					 struct mods_pci_dev_2 *pcidev)
{
	struct MODS_DMA_MAP *p_dma_map = NULL;
	struct list_head    *head      = &p_mem_info->dma_map_list;
	struct list_head    *iter;

	if (!head)
		return NULL;

	list_for_each(iter, head) {
		p_dma_map = list_entry(iter, struct MODS_DMA_MAP, list);

		if (mods_is_pci_dev(p_dma_map->pcidev, pcidev))
			return p_dma_map;
	}

	return NULL;
}

/* In order to map pages as UC or WC to the CPU, we need to change their
 * attributes by calling set_memory_uc()/set_memory_wc(), respectively.
 * On some CPUs this operation is extremely slow.  In order to incur
 * this penalty only once, we save pages mapped as UC or WC so that
 * we can reuse them later.
 */
static void save_non_wb_chunks(struct mods_client   *client,
			       struct MODS_MEM_INFO *p_mem_info)
{
	struct scatterlist *sg  = NULL;
	u32                 ichunk;

	if (p_mem_info->cache_type == MODS_ALLOC_CACHED)
		return;

	if (unlikely(mutex_lock_interruptible(&client->mtx)))
		return;

	/* Steal the chunks from MODS_MEM_INFO and put them on free list. */

	for_each_sg(p_mem_info->alloc_sg, sg, p_mem_info->num_chunks, ichunk) {

		struct MODS_FREE_PHYS_CHUNK *free_chunk;
		u32                          order;

		if (!sg)
			break;

		WARN_ON(!sg_page(sg));

		if (!is_chunk_wc(p_mem_info, ichunk))
			continue;

		free_chunk = kzalloc(sizeof(struct MODS_FREE_PHYS_CHUNK),
				     GFP_KERNEL | __GFP_NORETRY);

		if (unlikely(!free_chunk))
			break;
		atomic_inc(&client->num_allocs);

		order = get_order(sg->length);
		WARN_ON((PAGE_SIZE << order) != sg->length);

		free_chunk->numa_node  = p_mem_info->numa_node;
		free_chunk->order      = order;
		free_chunk->cache_type = p_mem_info->cache_type;
		free_chunk->dma32      = p_mem_info->dma32;
		free_chunk->p_page     = sg_page(sg);

		sg_set_page(sg, NULL, 0, 0);

		cl_debug(DEBUG_MEM_DETAILED,
			 "save %p 2^%u pages %s\n",
			 free_chunk->p_page,
			 order,
			 p_mem_info->cache_type == MODS_ALLOC_WRITECOMBINE
			     ? "WC" : "UC");

		list_add(&free_chunk->list, &client->free_mem_list);
	}

	mutex_unlock(&client->mtx);
}

static int restore_cache_one_chunk(struct page *p_page, u8 order)
{
	int final_err = 0;
	u32 num_pages = 1U << order;
	u32 i;

	for (i = 0; i < num_pages; i++) {
		void *ptr = MODS_KMAP(p_page + i);
		int   err = -ENOMEM;

		if (likely(ptr))
			err = MODS_SET_MEMORY_WB((unsigned long)ptr, 1);

		MODS_KUNMAP(ptr);

		if (likely(!final_err))
			final_err = err;

		/* Avoid superficial lockups */
		cond_resched();
	}

	return final_err;
}

static int release_free_chunks(struct mods_client *client)
{
	struct list_head *head;
	struct list_head *iter;
	struct list_head *next;
	unsigned long     num_restored = 0;
	unsigned long     num_failed   = 0;
	unsigned long     pages_freed  = 0;
	int               final_err    = 0;

	mutex_lock(&client->mtx);

	head = &client->free_mem_list;

	list_for_each_prev_safe(iter, next, head) {

		struct MODS_FREE_PHYS_CHUNK *free_chunk;
		int                          err;

		free_chunk = list_entry(iter,
					struct MODS_FREE_PHYS_CHUNK,
					list);

		list_del(iter);

		err = restore_cache_one_chunk(free_chunk->p_page,
					      free_chunk->order);
		if (likely(!final_err))
			final_err = err;

		if (unlikely(err))
			++num_failed;
		else
			++num_restored;

		pages_freed += 1u << free_chunk->order;

		__free_pages(free_chunk->p_page, free_chunk->order);
		atomic_sub(1 << free_chunk->order, &client->num_pages);

		kfree(free_chunk);
		atomic_dec(&client->num_allocs);
	}

	mutex_unlock(&client->mtx);

	if (pages_freed) {
		cl_debug(DEBUG_MEM, "released %lu free WC/UC pages, restored cache on %lu free chunks\n",
			 pages_freed, num_restored);
		if (unlikely(num_failed))
			cl_error("failed to restore cache on %lu (out of %lu) free chunks\n",
				 num_failed, num_failed + num_restored);
	}

	return final_err;
}

static int restore_cache(struct mods_client   *client,
			 struct MODS_MEM_INFO *p_mem_info)
{
	struct scatterlist *sg;
	unsigned int        i;
	int                 final_err = 0;

	if (p_mem_info->cache_type == MODS_ALLOC_CACHED)
		return 0;

	for_each_sg(p_mem_info->alloc_sg, sg, p_mem_info->num_chunks, i) {

		const u32 order = get_order(sg->length);
		int       err;

		WARN_ON((PAGE_SIZE << order) != sg->length);

		if (!sg_page(sg) || !is_chunk_wc(p_mem_info, i))
			continue;

		err = restore_cache_one_chunk(sg_page(sg), order);
		if (likely(!final_err))
			final_err = err;
	}

	if (unlikely(final_err))
		cl_error("failed to restore cache attributes\n");

	return final_err;
}

static void release_chunks(struct mods_client   *client,
			   struct MODS_MEM_INFO *p_mem_info)
{
	u32 i;

	WARN_ON(sg_dma_address(p_mem_info->sg));
	WARN_ON(!list_empty(&p_mem_info->dma_map_list));

	restore_cache(client, p_mem_info);

	/* release in reverse order */
	for (i = p_mem_info->num_chunks; i > 0; ) {
		struct scatterlist *sg;
		u32                 order;

		--i;
		sg = &p_mem_info->alloc_sg[i];
		if (!sg_page(sg))
			continue;

		order = get_order(sg->length);
		WARN_ON((PAGE_SIZE << order) != sg->length);

		__free_pages(sg_page(sg), order);
		atomic_sub(1u << order, &client->num_pages);

		sg_set_page(sg, NULL, 0, 0);
	}
}

static gfp_t get_alloc_flags(struct MODS_MEM_INFO *p_mem_info, u32 order)
{
	gfp_t flags = GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN;

	if (p_mem_info->force_numa)
		flags |= __GFP_THISNODE;

	if (order)
		flags |= __GFP_COMP;

	if (p_mem_info->dma32)
#ifdef CONFIG_ZONE_DMA32
		flags |= __GFP_DMA32;
#else
		flags |= __GFP_DMA;
#endif
	else
		flags |= __GFP_HIGHMEM;

	return flags;
}

static struct page *alloc_chunk(struct mods_client   *client,
				struct MODS_MEM_INFO *p_mem_info,
				u32                   order,
				int                  *need_cup)
{
	struct page *p_page     = NULL;
	u8           cache_type = p_mem_info->cache_type;
	u8           dma32      = p_mem_info->dma32;
	int          numa_node  = p_mem_info->numa_node;

	if ((cache_type != MODS_MEMORY_CACHED) &&
	    likely(!mutex_lock_interruptible(&client->mtx))) {

		struct list_head            *iter;
		struct list_head            *head = &client->free_mem_list;
		struct MODS_FREE_PHYS_CHUNK *free_chunk = NULL;

		list_for_each(iter, head) {
			free_chunk = list_entry(iter,
						struct MODS_FREE_PHYS_CHUNK,
						list);

			if (free_chunk->cache_type == cache_type &&
			    free_chunk->dma32      == dma32 &&
			    free_chunk->numa_node  == numa_node &&
			    free_chunk->order      == order) {

				list_del(iter);
				break;
			}

			free_chunk = NULL;
		}

		mutex_unlock(&client->mtx);

		if (free_chunk) {
			p_page = free_chunk->p_page;
			kfree(free_chunk);
			atomic_dec(&client->num_allocs);

			cl_debug(DEBUG_MEM_DETAILED, "reuse %p 2^%u pages %s\n",
				 p_page, order,
				 cache_type == MODS_ALLOC_WRITECOMBINE
				     ? "WC" : "UC");

			*need_cup = 0;
			return p_page;
		}
	}

	p_page = alloc_pages_node(p_mem_info->numa_node,
				  get_alloc_flags(p_mem_info, order),
				  order);

	*need_cup = 1;

	if (likely(p_page))
		atomic_add(1 << order, &client->num_pages);

	return p_page;
}

static int alloc_contig_sys_pages(struct mods_client   *client,
				  struct MODS_MEM_INFO *p_mem_info)
{
	const unsigned long req_bytes = (unsigned long)p_mem_info->num_pages
					<< PAGE_SHIFT;
	struct page *p_page;
	u64          phys_addr;
	u64          end_addr = 0;
	u32          order    = 0;
	int          is_wb    = 1;
	int          err      = -ENOMEM;

	LOG_ENT();

	while ((1U << order) < p_mem_info->num_pages)
		order++;

	p_page = alloc_chunk(client, p_mem_info, order, &is_wb);

	if (unlikely(!p_page))
		goto failed;

	p_mem_info->num_pages = 1U << order;

	sg_set_page(p_mem_info->alloc_sg, p_page, PAGE_SIZE << order, 0);

	if (!is_wb)
		mark_chunk_wc(p_mem_info, 0);

	phys_addr = sg_phys(p_mem_info->alloc_sg);
	if (unlikely(phys_addr == 0)) {
		cl_error("failed to determine physical address\n");
		goto failed;
	}

	cl_debug(DEBUG_MEM,
		 "alloc contig 0x%lx bytes, 2^%u pages, %s, node %d,%s phys 0x%llx\n",
		 req_bytes,
		 order,
		 mods_get_prot_str(p_mem_info->cache_type),
		 p_mem_info->numa_node,
		 p_mem_info->dma32 ? " dma32," : "",
		 (unsigned long long)phys_addr);

	end_addr = phys_addr +
		   ((unsigned long)p_mem_info->num_pages << PAGE_SHIFT);
	if (unlikely(p_mem_info->dma32 && (end_addr > 0x100000000ULL))) {
		cl_error("allocation exceeds 32-bit addressing\n");
		goto failed;
	}

	err = setup_cache_attr(client, p_mem_info, 0);

failed:
	LOG_EXT();
	return err;
}

static u32 get_max_order_needed(u32 num_pages)
{
	const u32 order = min(10, get_order(num_pages << PAGE_SHIFT));

	return ((1u << order) <= num_pages) ? order : (order >> 1u);
}

static int alloc_noncontig_sys_pages(struct mods_client   *client,
				     struct MODS_MEM_INFO *p_mem_info)
{
	const unsigned long req_bytes = (unsigned long)p_mem_info->num_pages
					<< PAGE_SHIFT;
	u32 pages_needed = p_mem_info->num_pages;
	u32 num_chunks   = 0;
	int err;

	LOG_ENT();

	p_mem_info->num_pages = 0;

	for (; pages_needed > 0; ++num_chunks) {
		struct scatterlist *sg = &p_mem_info->alloc_sg[num_chunks];
		u64 phys_addr       = 0;
		u32 order           = get_max_order_needed(pages_needed);
		u32 allocated_pages = 0;
		int is_wb           = 1;

		/* Fail if memory fragmentation is very high */
		if (unlikely(num_chunks >= p_mem_info->num_chunks)) {
			cl_error("detected high memory fragmentation\n");
			err = -ENOMEM;
			goto failed;
		}

		for (;;) {
			struct page *p_page = alloc_chunk(client,
							  p_mem_info,
							  order,
							  &is_wb);
			if (p_page) {
				sg_set_page(sg, p_page, PAGE_SIZE << order, 0);
				allocated_pages = 1u << order;
				break;
			}
			if (order == 0)
				break;
			--order;
		}

		if (unlikely(!allocated_pages)) {
			cl_error("out of memory\n");
			err = -ENOMEM;
			goto failed;
		}

		if (!is_wb)
			mark_chunk_wc(p_mem_info, num_chunks);

		pages_needed -= min(allocated_pages, pages_needed);
		p_mem_info->num_pages += allocated_pages;

		phys_addr = sg_phys(sg);
		if (unlikely(phys_addr == 0)) {
			cl_error("phys addr lookup failed\n");
			err = -ENOMEM;
			goto failed;
		}

		cl_debug(DEBUG_MEM,
			 "alloc 0x%lx bytes [%u], 2^%u pages, %s, node %d,%s phys 0x%llx\n",
			 req_bytes,
			 (unsigned int)num_chunks,
			 order,
			 mods_get_prot_str(p_mem_info->cache_type),
			 p_mem_info->numa_node,
			 p_mem_info->dma32 ? " dma32," : "",
			 (unsigned long long)phys_addr);

		err = setup_cache_attr(client, p_mem_info, num_chunks);
		if (unlikely(err))
			goto failed;
	}

	err = 0;

failed:
	if (num_chunks)
		sg_mark_end(&p_mem_info->alloc_sg[num_chunks - 1]);

	LOG_EXT();
	return err;
}

static int register_alloc(struct mods_client   *client,
			  struct MODS_MEM_INFO *p_mem_info)
{
	const int err = mutex_lock_interruptible(&client->mtx);

	if (likely(!err)) {

		list_add(&p_mem_info->list, &client->mem_alloc_list);

		mutex_unlock(&client->mtx);
	}

	return err;
}

static int unregister_and_free_alloc(struct mods_client   *client,
				     struct MODS_MEM_INFO *p_del_mem)
{
	struct MODS_MEM_INFO *p_mem_info = NULL;
	struct list_head     *head;
	struct list_head     *iter;
	int                   err;

	cl_debug(DEBUG_MEM_DETAILED, "free %p\n", p_del_mem);

	mutex_lock(&client->mtx);

	head = &client->mem_alloc_list;

	list_for_each(iter, head) {
		p_mem_info = list_entry(iter, struct MODS_MEM_INFO, list);

		if (p_del_mem == p_mem_info) {
			list_del(iter);
			break;
		}

		p_mem_info = NULL;
	}

	mutex_unlock(&client->mtx);

	if (likely(p_mem_info)) {
		dma_unmap_all(client, p_mem_info, NULL);
		if (likely(!p_mem_info->reservation_tag)) {
			save_non_wb_chunks(client, p_mem_info);
			release_chunks(client, p_mem_info);

			pci_dev_put(p_mem_info->dev);

			kfree(p_mem_info);
		} else {
			/* Decrement client num_pages manually if not releasing chunks */
			atomic_sub((int)p_mem_info->num_pages, &client->num_pages);
			mutex_lock(&mem_reservation_mtx);
			/* Clear the client_id in the associated reservation */
			mem_reservations[p_mem_info->reservation_tag-1].client_id = 0;
			mutex_unlock(&mem_reservation_mtx);
		}
		atomic_dec(&client->num_allocs); /* always decrement to avoid leak */
		err = OK;
	} else {
		cl_error("failed to unregister allocation %p\n", p_del_mem);
		err = -EINVAL;
	}

	return err;
}

int mods_unregister_all_alloc(struct mods_client *client)
{
	int               final_err = OK;
	int               err;
	struct list_head *head      = &client->mem_alloc_list;
	struct list_head *iter;
	struct list_head *tmp;

	list_for_each_safe(iter, tmp, head) {

		struct MODS_MEM_INFO *p_mem_info;

		p_mem_info = list_entry(iter, struct MODS_MEM_INFO, list);
		err = unregister_and_free_alloc(client, p_mem_info);
		if (likely(!final_err))
			final_err = err;
	}

	err = release_free_chunks(client);
	if (likely(!final_err))
		final_err = err;

	return final_err;
}

static int get_addr_range(struct mods_client                 *client,
			  struct MODS_GET_PHYSICAL_ADDRESS_3 *p,
			  struct mods_pci_dev_2              *pcidev)
{
	struct scatterlist   *sg;
	struct MODS_MEM_INFO *p_mem_info;
	struct MODS_DMA_MAP  *p_dma_map = NULL;
	u64                   offs;
	u32                   num_chunks;
	u32                   ichunk;
	int                   err       = OK;

	LOG_ENT();

	p->physical_address = 0;

	p_mem_info = get_mem_handle(client, p->memory_handle);
	if (unlikely(!p_mem_info)) {
		LOG_EXT();
		return -EINVAL;
	}

	if (unlikely(pcidev && (pcidev->bus > 0xFFU ||
				pcidev->device > 0xFFU))) {
		cl_error("dev %04x:%02x:%02x.%x not found\n",
			 pcidev->domain,
			 pcidev->bus,
			 pcidev->device,
			 pcidev->function);
		LOG_EXT();
		return -EINVAL;
	}

	sg         = p_mem_info->sg;
	num_chunks = get_num_chunks(p_mem_info);

	err = mutex_lock_interruptible(&client->mtx);
	if (err) {
		LOG_EXT();
		return err;
	}

	/* If pcidev was specified, retrieve IOVA,
	 * otherwise retrieve physical address.
	 */
	if (pcidev) {
		if (mods_is_pci_dev(p_mem_info->dev, pcidev)) {
			if (!sg_dma_address(sg))
				err = -EINVAL;
		} else {
			p_dma_map = find_dma_map(p_mem_info, pcidev);
			if (!p_dma_map)
				err = -EINVAL;
			else
				sg = p_dma_map->sg;
		}

		if (err) {
			mutex_unlock(&client->mtx);

			cl_error(
				"allocation %p is not mapped to dev %04x:%02x:%02x.%x\n",
				p_mem_info,
				pcidev->domain,
				pcidev->bus,
				pcidev->device,
				pcidev->function);

			LOG_EXT();
			return err;
		}
	}

	offs = p->offset;
	err  = -EINVAL;

	for_each_sg(sg, sg, num_chunks, ichunk) {
		unsigned int size;

		if (!sg)
			break;

		size = pcidev ? sg_dma_len(sg) : sg->length;
		if (size <= offs) {
			offs -= size;
			continue;
		}

		if (pcidev) {
			dma_addr_t addr = sg_dma_address(sg) + offs;

			addr = compress_nvlink_addr(p_mem_info->dev, addr);

			p->physical_address = (u64)addr;
		} else {
			p->physical_address = (u64)sg_phys(sg) + offs;
		}

		err = OK;
		break;
	}

	mutex_unlock(&client->mtx);

	if (err && pcidev) {
		cl_error(
			"invalid offset 0x%llx requested for va on dev %04x:%02x:%02x.%x in allocation %p of size 0x%llx\n",
			(unsigned long long)p->offset,
			pcidev->domain,
			pcidev->bus,
			pcidev->device,
			pcidev->function,
			p_mem_info,
			(unsigned long long)p_mem_info->num_pages << PAGE_SHIFT);
	} else if (err && !pcidev) {
		cl_error(
			"invalid offset 0x%llx requested for pa in allocation %p of size 0x%llx\n",
			(unsigned long long)p->offset,
			p_mem_info,
			(unsigned long long)p_mem_info->num_pages << PAGE_SHIFT);
	}

	LOG_EXT();
	return err;
}

/* Returns an offset within an allocation deduced from physical address.
 * If physical address doesn't belong to the allocation, returns non-zero.
 */
static int get_alloc_offset(struct MODS_MEM_INFO *p_mem_info,
			    u64                   phys_addr,
			    u64                  *ret_offs)
{
	struct scatterlist *sg;
	u64                 offset     = 0;
	const u32           num_chunks = get_num_chunks(p_mem_info);
	u32                 ichunk;

	for_each_sg(p_mem_info->sg, sg, num_chunks, ichunk) {
		dma_addr_t   addr;
		unsigned int size;

		addr = sg_phys(sg);
		size = sg->length;

		if (phys_addr >= addr && phys_addr < addr + size) {
			*ret_offs = phys_addr - addr + offset;
			return 0;
		}

		offset += size;
	}

	/* The physical address doesn't belong to the allocation */
	return -EINVAL;
}

struct MODS_MEM_INFO *mods_find_alloc(struct mods_client *client, u64 phys_addr)
{
	struct list_head     *plist_head = &client->mem_alloc_list;
	struct list_head     *plist_iter;
	struct MODS_MEM_INFO *p_mem_info;
	u64                   offset;

	list_for_each(plist_iter, plist_head) {
		p_mem_info = list_entry(plist_iter,
					struct MODS_MEM_INFO,
					list);

		if (!get_alloc_offset(p_mem_info, phys_addr, &offset))
			return p_mem_info;
	}

	/* The physical address doesn't belong to any allocation */
	return NULL;
}

/* Estimate the initial number of chunks supported, assuming medium memory
 * fragmentation.
 */
static u32 estimate_num_chunks(u32 num_pages)
{
	u32 num_chunks = 0;
	u32 bit_scan;

	/* Count each contiguous block <=256KB */
	for (bit_scan = num_pages; bit_scan && num_chunks < 6; bit_scan >>= 1)
		++num_chunks;

	/* Count remaining contiguous blocks >256KB */
	num_chunks += bit_scan;

	/* 4x slack for medium memory fragmentation, except huge allocs */
	if (num_chunks < 32 * 1024)
		num_chunks <<= 2;
	else if (num_chunks < 64 * 1024)
		num_chunks <<= 1;

	/* No sense to allocate more chunks than pages */
	if (num_chunks > num_pages)
		num_chunks = num_pages;

	return num_chunks;
}

static inline size_t calc_mem_info_size_no_bitmap(u32 num_chunks)
{
	return sizeof(struct MODS_MEM_INFO) +
	       num_chunks * sizeof(struct scatterlist);
}

static inline u32 calc_mem_info_size(u32 num_chunks, u8 cache_type)
{
	size_t size = calc_mem_info_size_no_bitmap(num_chunks);

	if (cache_type != MODS_ALLOC_CACHED)
		size += sizeof(long) * BITS_TO_LONGS(num_chunks);

	return (u32)size;
}

static void init_mem_info(struct MODS_MEM_INFO *p_mem_info,
			  u32                   num_chunks,
			  u8                    cache_type)
{
	p_mem_info->sg         = p_mem_info->alloc_sg;
	p_mem_info->num_chunks = num_chunks;
	p_mem_info->cache_type = cache_type;

	if (cache_type != MODS_ALLOC_CACHED)
		p_mem_info->wc_bitmap = (unsigned long *)
			&p_mem_info->alloc_sg[num_chunks];

	INIT_LIST_HEAD(&p_mem_info->dma_map_list);
}

static struct MODS_MEM_INFO *alloc_mem_info(struct mods_client *client,
					    u32                 num_chunks,
					    u8                  cache_type,
					    u32                *alloc_size)
{
	struct MODS_MEM_INFO *p_mem_info = NULL;

	const u32 calc_size = calc_mem_info_size(num_chunks, cache_type);

	*alloc_size = calc_size;

	p_mem_info = kzalloc(calc_size, GFP_KERNEL | __GFP_NORETRY);

	if (likely(p_mem_info)) {
		atomic_inc(&client->num_allocs);

		sg_init_table(&p_mem_info->contig_sg, 1);
		sg_init_table(p_mem_info->alloc_sg,   num_chunks);
	}

	return p_mem_info;
}

/* For large non-contiguous allocations, we typically use significantly less
 * chunks than originally estimated.  This function reallocates the
 * MODS_MEM_INFO struct so that it uses only as much memory as it needs.
 */
static struct MODS_MEM_INFO *optimize_chunks(struct mods_client   *client,
					     struct MODS_MEM_INFO *p_mem_info)
{
	struct scatterlist   *sg;
	struct MODS_MEM_INFO *p_new_mem_info = NULL;
	u32                   num_chunks     = 0;
	u32                   alloc_size     = 0;

	for_each_sg(p_mem_info->alloc_sg, sg,
		    p_mem_info->num_chunks, num_chunks) {
		if (!sg || !sg_page(sg))
			break;
	}

	if (num_chunks < p_mem_info->num_chunks)
		p_new_mem_info = alloc_mem_info(client, num_chunks,
						p_mem_info->cache_type,
						&alloc_size);

	if (p_new_mem_info) {
		const size_t copy_size =
			calc_mem_info_size_no_bitmap(num_chunks);

		memcpy(p_new_mem_info, p_mem_info, copy_size);
		init_mem_info(p_new_mem_info, num_chunks,
			      p_mem_info->cache_type);
		copy_wc_bitmap(p_new_mem_info, 0, p_mem_info, num_chunks);

		kfree(p_mem_info);
		atomic_dec(&client->num_allocs);

		p_mem_info = p_new_mem_info;
	}

	return p_mem_info;
}

/************************
 * ESCAPE CALL FUNCTONS *
 ************************/

int esc_mods_alloc_pages_2(struct mods_client        *client,
			   struct MODS_ALLOC_PAGES_2 *p)
{
	struct MODS_MEM_INFO *p_mem_info = NULL;
	u32                   num_pages;
	u32                   alloc_size;
	u32                   num_chunks;
	int                   err        = -EINVAL;
	u8                    cache_type;

	LOG_ENT();

	p->memory_handle = 0;

	cl_debug(DEBUG_MEM_DETAILED,
		 "alloc 0x%llx bytes flags=0x%x (%s %s%s%s%s%s) node=%d on dev %04x:%02x:%02x.%x\n",
		 (unsigned long long)p->num_bytes,
		 p->flags,
		 mods_get_prot_str(p->flags & MODS_ALLOC_CACHE_MASK),
		 (p->flags & MODS_ALLOC_CONTIGUOUS) ? "contiguous" :
						      "noncontiguous",
		 (p->flags & MODS_ALLOC_DMA32) ? " dma32" : "",
		 (p->flags & MODS_ALLOC_USE_NUMA) ? " usenuma" : "",
		 (p->flags & MODS_ALLOC_FORCE_NUMA) ? " forcenuma" : "",
		 (p->flags & MODS_ALLOC_MAP_DEV) ? " dmamap" : "",
		 p->numa_node,
		 p->pci_device.domain,
		 p->pci_device.bus,
		 p->pci_device.device,
		 p->pci_device.function);

	if (unlikely(!p->num_bytes)) {
		cl_error("zero bytes requested\n");
		goto failed;
	}

	num_pages = (u32)((p->num_bytes + PAGE_SIZE - 1) >> PAGE_SHIFT);
	if (p->flags & MODS_ALLOC_CONTIGUOUS)
		num_chunks = 1;
	else
		num_chunks = estimate_num_chunks(num_pages);

	if (unlikely(((u64)num_pages << PAGE_SHIFT) < p->num_bytes)) {
		cl_error("invalid allocation size requested: 0x%llx\n",
			 (unsigned long long)p->num_bytes);
		goto failed;
	}

	if (unlikely((p->flags & MODS_ALLOC_USE_NUMA) &&
		     (p->numa_node != MODS_ANY_NUMA_NODE) &&
		      ((unsigned int)p->numa_node >=
		       (unsigned int)num_possible_nodes()))) {

		cl_error("invalid NUMA node: %d\n", p->numa_node);
		goto failed;
	}

#ifdef CONFIG_PPC64
	if (unlikely((p->flags & MODS_ALLOC_CACHE_MASK) != MODS_ALLOC_CACHED)) {
		cl_error("unsupported cache attr %u (%s)\n",
			 p->flags & MODS_ALLOC_CACHE_MASK,
			 mods_get_prot_str(p->flags & MODS_ALLOC_CACHE_MASK));
		err = -ENOMEM;
		goto failed;
	}
#endif

	cache_type = (u8)(p->flags & MODS_ALLOC_CACHE_MASK);

	p_mem_info = alloc_mem_info(client, num_chunks, cache_type,
				    &alloc_size);

	if (unlikely(!p_mem_info)) {
		cl_error("failed to allocate auxiliary 0x%x bytes for %u chunks to hold %u pages\n",
			 alloc_size, num_chunks, num_pages);
		err = -ENOMEM;
		goto failed;
	}

	init_mem_info(p_mem_info, num_chunks, cache_type);

	p_mem_info->num_pages       = num_pages;
	p_mem_info->dma32           = (p->flags & MODS_ALLOC_DMA32) ? true : false;
	p_mem_info->force_numa      = (p->flags & MODS_ALLOC_FORCE_NUMA)
				      ? true : false;
	p_mem_info->reservation_tag = 0;
#ifdef MODS_HASNT_NUMA_NO_NODE
	p_mem_info->numa_node       = numa_node_id();
#else
	p_mem_info->numa_node       = NUMA_NO_NODE;
#endif
	p_mem_info->dev             = NULL;

	if ((p->flags & MODS_ALLOC_USE_NUMA) &&
	    p->numa_node != MODS_ANY_NUMA_NODE)
		p_mem_info->numa_node = p->numa_node;

#ifdef CONFIG_PCI
	if (!(p->flags & MODS_ALLOC_USE_NUMA) ||
	    (p->flags & MODS_ALLOC_MAP_DEV)) {

		struct pci_dev *dev = NULL;

		err = mods_find_pci_dev(client, &p->pci_device, &dev);
		if (unlikely(err)) {
			cl_error("dev %04x:%02x:%02x.%x not found\n",
				 p->pci_device.domain,
				 p->pci_device.bus,
				 p->pci_device.device,
				 p->pci_device.function);
			goto failed;
		}

		p_mem_info->dev = dev;
		if (!(p->flags & MODS_ALLOC_USE_NUMA))
			p_mem_info->numa_node = dev_to_node(&dev->dev);

#ifdef CONFIG_PPC64
		if (!mods_is_nvlink_sysmem_trained(client, dev)) {
			/* Until NvLink is trained, we must use memory
			 * on node 0.
			 */
			if (has_npu_dev(dev, 0))
				p_mem_info->numa_node = 0;
		}
#endif
		cl_debug(DEBUG_MEM_DETAILED,
			 "affinity dev %04x:%02x:%02x.%x node %d\n",
			 p->pci_device.domain,
			 p->pci_device.bus,
			 p->pci_device.device,
			 p->pci_device.function,
			 p_mem_info->numa_node);
	}
#endif

	if (p->flags & MODS_ALLOC_CONTIGUOUS)
		err = alloc_contig_sys_pages(client, p_mem_info);
	else {
		err = alloc_noncontig_sys_pages(client, p_mem_info);

		if (likely(!err))
			p_mem_info = optimize_chunks(client, p_mem_info);
	}

	if (unlikely(err)) {
		cl_error("failed to alloc 0x%lx %s bytes, %s, node %d%s\n",
			 ((unsigned long)num_pages) << PAGE_SHIFT,
			 (p->flags & MODS_ALLOC_CONTIGUOUS) ? "contiguous" :
							      "non-contiguous",
			 mods_get_prot_str(p_mem_info->cache_type),
			 p_mem_info->numa_node,
			 p_mem_info->dma32 ? ", dma32" : "");
		goto failed;
	}

	err = register_alloc(client, p_mem_info);
	if (unlikely(err))
		goto failed;

	p->memory_handle = (u64)(size_t)p_mem_info;

	cl_debug(DEBUG_MEM_DETAILED, "alloc %p: %u chunks, %u pages\n",
		 p_mem_info, p_mem_info->num_chunks, p_mem_info->num_pages);

failed:
	if (unlikely(err && p_mem_info)) {
		dma_unmap_all(client, p_mem_info, NULL);
		release_chunks(client, p_mem_info);
		pci_dev_put(p_mem_info->dev);

		kfree(p_mem_info);
		atomic_dec(&client->num_allocs);
	}

	LOG_EXT();
	return err;
}

int esc_mods_device_alloc_pages_2(struct mods_client               *client,
				  struct MODS_DEVICE_ALLOC_PAGES_2 *p)
{
	int err;
	u32 flags = 0;
	struct MODS_ALLOC_PAGES_2 dev_alloc_pages = {0};

	LOG_ENT();

	if (p->contiguous)
		flags |= MODS_ALLOC_CONTIGUOUS;

	if (p->address_bits == 32)
		flags |= MODS_ALLOC_DMA32;

	if (p->attrib == MODS_MEMORY_UNCACHED)
		flags |= MODS_ALLOC_UNCACHED;
	else if (p->attrib == MODS_MEMORY_WRITECOMBINE)
		flags |= MODS_ALLOC_WRITECOMBINE;
	else if (unlikely(p->attrib != MODS_MEMORY_CACHED)) {
		cl_error("invalid cache attrib: %u\n", p->attrib);
		LOG_EXT();
		return -ENOMEM;
	}

	if (p->pci_device.bus > 0xFFU || p->pci_device.device > 0xFFU)
		flags |= MODS_ALLOC_USE_NUMA;
	else
		flags |= MODS_ALLOC_MAP_DEV | MODS_ALLOC_FORCE_NUMA;

	dev_alloc_pages.num_bytes  = p->num_bytes;
	dev_alloc_pages.flags      = flags;
	dev_alloc_pages.numa_node  = MODS_ANY_NUMA_NODE;
	dev_alloc_pages.pci_device = p->pci_device;

	err = esc_mods_alloc_pages_2(client, &dev_alloc_pages);
	if (likely(!err))
		p->memory_handle = dev_alloc_pages.memory_handle;

	LOG_EXT();
	return err;
}

int esc_mods_device_alloc_pages(struct mods_client             *client,
				struct MODS_DEVICE_ALLOC_PAGES *p)
{
	int err;
	u32 flags = 0;
	struct MODS_ALLOC_PAGES_2 dev_alloc_pages = {0};

	LOG_ENT();

	if (p->contiguous)
		flags |= MODS_ALLOC_CONTIGUOUS;

	if (p->address_bits == 32)
		flags |= MODS_ALLOC_DMA32;

	if (p->attrib == MODS_MEMORY_UNCACHED)
		flags |= MODS_ALLOC_UNCACHED;
	else if (p->attrib == MODS_MEMORY_WRITECOMBINE)
		flags |= MODS_ALLOC_WRITECOMBINE;
	else if (unlikely(p->attrib != MODS_MEMORY_CACHED)) {
		cl_error("invalid cache attrib: %u\n", p->attrib);
		LOG_EXT();
		return -ENOMEM;
	}

	if (p->pci_device.bus > 0xFFU || p->pci_device.device > 0xFFU)
		flags |= MODS_ALLOC_USE_NUMA;
	else
		flags |= MODS_ALLOC_MAP_DEV | MODS_ALLOC_FORCE_NUMA;

	dev_alloc_pages.num_bytes           = p->num_bytes;
	dev_alloc_pages.flags               = flags;
	dev_alloc_pages.numa_node           = MODS_ANY_NUMA_NODE;
	dev_alloc_pages.pci_device.domain   = 0;
	dev_alloc_pages.pci_device.bus      = p->pci_device.bus;
	dev_alloc_pages.pci_device.device   = p->pci_device.device;
	dev_alloc_pages.pci_device.function = p->pci_device.function;

	err = esc_mods_alloc_pages_2(client, &dev_alloc_pages);
	if (likely(!err))
		p->memory_handle = dev_alloc_pages.memory_handle;

	LOG_EXT();
	return err;
}

int esc_mods_alloc_pages(struct mods_client *client, struct MODS_ALLOC_PAGES *p)
{
	int err;
	u32 flags = MODS_ALLOC_USE_NUMA;
	struct MODS_ALLOC_PAGES_2 dev_alloc_pages = {0};

	LOG_ENT();

	if (p->contiguous)
		flags |= MODS_ALLOC_CONTIGUOUS;

	if (p->address_bits == 32)
		flags |= MODS_ALLOC_DMA32;

	if (p->attrib == MODS_MEMORY_UNCACHED)
		flags |= MODS_ALLOC_UNCACHED;
	else if (p->attrib == MODS_MEMORY_WRITECOMBINE)
		flags |= MODS_ALLOC_WRITECOMBINE;
	else if (unlikely(p->attrib != MODS_MEMORY_CACHED)) {
		cl_error("invalid cache attrib: %u\n", p->attrib);
		LOG_EXT();
		return -ENOMEM;
	}

	dev_alloc_pages.num_bytes           = p->num_bytes;
	dev_alloc_pages.flags               = flags;
	dev_alloc_pages.numa_node           = MODS_ANY_NUMA_NODE;
	dev_alloc_pages.pci_device.domain   = 0xFFFFU;
	dev_alloc_pages.pci_device.bus      = 0xFFFFU;
	dev_alloc_pages.pci_device.device   = 0xFFFFU;
	dev_alloc_pages.pci_device.function = 0xFFFFU;

	err = esc_mods_alloc_pages_2(client, &dev_alloc_pages);
	if (likely(!err))
		p->memory_handle = dev_alloc_pages.memory_handle;

	LOG_EXT();
	return err;
}

int esc_mods_free_pages(struct mods_client *client, struct MODS_FREE_PAGES *p)
{
	struct MODS_MEM_INFO *p_mem_info;
	int                   err = -EINVAL;

	LOG_ENT();

	p_mem_info = get_mem_handle(client, p->memory_handle);

	if (likely(p_mem_info))
		err = unregister_and_free_alloc(client, p_mem_info);

	LOG_EXT();

	return err;
}

static phys_addr_t get_contig_pa(struct mods_client   *client,
				 struct MODS_MEM_INFO *p_mem_info)
{
	struct scatterlist *sg;
	struct scatterlist *prev_sg = NULL;
	u32                 i;
	bool                contig  = true;

	for_each_sg(p_mem_info->alloc_sg, sg, p_mem_info->num_chunks, i) {
		if ((i > 0) &&
			(sg_phys(prev_sg) + prev_sg->length != sg_phys(sg))) {

			cl_debug(DEBUG_MEM_DETAILED,
				 "merge is non-contiguous because alloc %p chunk %u pa 0x%llx size 0x%x and chunk %u pa 0x%llx\n",
				 p_mem_info,
				 i - 1,
				 (unsigned long long)sg_phys(prev_sg),
				 prev_sg->length,
				 i,
				 (unsigned long long)sg_phys(sg));
			contig = false;
			break;
		}

		prev_sg = sg;
	}

	return contig ? sg_phys(p_mem_info->alloc_sg) : 0;
}

int esc_mods_merge_pages(struct mods_client      *client,
			 struct MODS_MERGE_PAGES *p)
{
	struct MODS_MEM_INFO *p_mem_info;
	int          err        = OK;
	u32          num_chunks = 0;
	u32          alloc_size = 0;
	unsigned int i;
	bool         contig     = true;
	u32          cache_type;

	LOG_ENT();

	if (unlikely(p->num_in_handles < 2 ||
		     p->num_in_handles > MODS_MAX_MERGE_HANDLES)) {
		cl_error("invalid number of input handles: %u\n",
			 p->num_in_handles);
		LOG_EXT();
		return -EINVAL;
	}

	err = mutex_lock_interruptible(&client->mtx);
	if (unlikely(err)) {
		LOG_EXT();
		return err;
	}

	{
		const char   *err_msg = NULL;
		phys_addr_t   prev_pa;
		unsigned long prev_size;

		p_mem_info = get_mem_handle(client, p->in_memory_handles[0]);

		if (unlikely(!validate_mem_handle(client, p_mem_info))) {
			cl_error("handle 0: invalid handle %p\n", p_mem_info);
			err = -EINVAL;
			goto failed;
		}

		WARN_ON(p_mem_info->num_pages == 0);
		if (unlikely(!list_empty(&p_mem_info->dma_map_list) ||
			     sg_dma_address(p_mem_info->sg))) {
			cl_error("handle 0: found dma mappings\n");
			err = -EINVAL;
			goto failed;
		}

		cache_type = p_mem_info->cache_type;
		num_chunks = p_mem_info->num_chunks;
		prev_pa    = get_contig_pa(client, p_mem_info);
		prev_size  = p_mem_info->num_pages << PAGE_SHIFT;

		for (i = 1; i < p->num_in_handles; i++) {
			struct MODS_MEM_INFO *const p_other =
				get_mem_handle(client, p->in_memory_handles[i]);
			phys_addr_t  next_pa;
			unsigned int j;

			if (!validate_mem_handle(client, p_other)) {
				cl_error("handle %u: invalid handle %p\n",
					 i, p);
				err = -EINVAL;
				goto failed;
			}

			for (j = 0; j < i; j++) {
				if (unlikely(p->in_memory_handles[i] ==
					     p->in_memory_handles[j])) {
					err_msg = "duplicate handle";
					break;
				}
			}
			if (err_msg)
				break;

			if (unlikely(p_mem_info->cache_type !=
				     p_other->cache_type)) {
				err_msg = "cache attr mismatch";
				break;
			}

			if (unlikely(p_mem_info->force_numa &&
			    p_mem_info->numa_node != p_other->numa_node)) {
				err_msg = "numa node mismatch";
				break;
			}

			if (unlikely(p_mem_info->dma32 != p_other->dma32)) {
				err_msg = "dma32 mismatch";
				break;
			}

			if (p_mem_info->dev) {
				if (unlikely(p_mem_info->dev !=
					     p_other->dev)) {
					err_msg = "device mismatch";
					break;
				}
			}

			WARN_ON(p_other->num_pages == 0);
			if (unlikely(!list_empty(&p_other->dma_map_list) ||
				     sg_dma_address(p_other->sg))) {
				err_msg = "found dma mappings";
				break;
			}

			num_chunks += p_other->num_chunks;
			next_pa    =  get_contig_pa(client, p_other);

			if (contig && ((prev_pa + prev_size) != next_pa)) {
				contig = false;
				cl_debug(DEBUG_MEM_DETAILED,
					 "merge is non-contiguous because alloc %u %p pa 0x%llx size 0x%lx and alloc %u %p pa 0x%llx\n",
					 i - 1,
					 get_mem_handle(client,
					     p->in_memory_handles[i - 1]),
					 (unsigned long long)prev_pa,
					 prev_size,
					 i,
					 p_other,
					 (unsigned long long)next_pa);
			}

			prev_pa   = next_pa;
			prev_size = p_other->num_pages << PAGE_SHIFT;
		}

		if (unlikely(err_msg)) {
			cl_error("merging handle %u: %s\n", i, err_msg);
			err = -EINVAL;
			goto failed;
		}
	}

	p_mem_info = alloc_mem_info(client, num_chunks, cache_type,
				    &alloc_size);

	if (unlikely(!p_mem_info)) {
		err = -ENOMEM;
		goto failed;
	}

	for (i = 0; i < p->num_in_handles; i++) {
		struct MODS_MEM_INFO *p_other =
			get_mem_handle(client, p->in_memory_handles[i]);
		const u32 other_chunks = p_other->num_chunks;

		cl_debug(DEBUG_MEM_DETAILED, "merge %p (%u) into %p[%u..%u], phys 0x%llx\n",
			 p_other, i, p_mem_info, p_mem_info->num_chunks,
			 p_mem_info->num_chunks + other_chunks - 1,
			 (unsigned long long)sg_phys(p_other->sg));

		list_del(&p_other->list);

		if (i == 0) {
			const size_t copy_size =
				calc_mem_info_size_no_bitmap(other_chunks);

			memcpy(p_mem_info, p_other, copy_size);
			init_mem_info(p_mem_info, num_chunks,
				      p_other->cache_type);
			p_mem_info->num_chunks = other_chunks;
			copy_wc_bitmap(p_mem_info, 0, p_other, other_chunks);

			list_add(&p_mem_info->list, &client->mem_alloc_list);
		} else {
			const u32 num_chunks = p_mem_info->num_chunks;

			memcpy(&p_mem_info->alloc_sg[num_chunks],
			       p_other->alloc_sg,
			       other_chunks * sizeof(struct scatterlist));
			copy_wc_bitmap(p_mem_info, num_chunks,
				       p_other, other_chunks);

			MODS_SG_UNMARK_END(&p_mem_info->alloc_sg[num_chunks - 1]);

			p_mem_info->num_chunks += other_chunks;
			p_mem_info->num_pages  += p_other->num_pages;
		}

		kfree(p_other);
		atomic_dec(&client->num_allocs);
	}

	cl_debug(DEBUG_MEM, "merge alloc %p: %u chunks, %u pages\n",
		 p_mem_info, p_mem_info->num_chunks, p_mem_info->num_pages);

	WARN_ON(num_chunks != p_mem_info->num_chunks);

	if (contig) {
		p_mem_info->sg = &p_mem_info->contig_sg;

		sg_set_page(&p_mem_info->contig_sg,
			    sg_page(p_mem_info->alloc_sg),
			    p_mem_info->num_pages << PAGE_SHIFT,
			    0);
	}

	p->memory_handle = (u64)(size_t)p_mem_info;

failed:
	mutex_unlock(&client->mtx);

	LOG_EXT();

	return err;
}

int esc_mods_set_mem_type(struct mods_client      *client,
			  struct MODS_MEMORY_TYPE *p)
{
	struct MODS_MEM_INFO *p_mem_info;
	u8                    type = MODS_ALLOC_CACHED;
	int                   err;

	LOG_ENT();

	switch (p->type) {
	case MODS_MEMORY_CACHED:
		break;

	case MODS_MEMORY_UNCACHED:
		type = MODS_ALLOC_UNCACHED;
		break;

	case MODS_MEMORY_WRITECOMBINE:
		type = MODS_ALLOC_WRITECOMBINE;
		break;

	default:
		cl_error("unsupported memory type: %u\n", p->type);
		LOG_EXT();
		return -EINVAL;
	}

	err = mutex_lock_interruptible(&client->mtx);
	if (unlikely(err)) {
		LOG_EXT();
		return err;
	}

	p_mem_info = mods_find_alloc(client, p->physical_address);
	if (unlikely(p_mem_info)) {
		cl_error("cannot set mem type on phys addr 0x%llx\n",
			 p->physical_address);
		err = -EINVAL;
	} else {
		client->mem_type.phys_addr = p->physical_address;
		client->mem_type.size      = p->size;
		client->mem_type.type      = type;
	}

	mutex_unlock(&client->mtx);

	LOG_EXT();
	return err;
}

int esc_mods_get_phys_addr(struct mods_client               *client,
			   struct MODS_GET_PHYSICAL_ADDRESS *p)
{
	struct MODS_GET_PHYSICAL_ADDRESS_3 range;
	int err;

	LOG_ENT();

	range.memory_handle = p->memory_handle;
	range.offset        = p->offset;
	memset(&range.pci_device, 0, sizeof(range.pci_device));

	err = get_addr_range(client, &range, NULL);

	if (!err)
		p->physical_address = range.physical_address;

	LOG_EXT();
	return err;
}

int esc_mods_get_phys_addr_2(struct mods_client                 *client,
			     struct MODS_GET_PHYSICAL_ADDRESS_3 *p)
{
	struct MODS_GET_PHYSICAL_ADDRESS_3 range;
	int err;

	LOG_ENT();

	range.memory_handle = p->memory_handle;
	range.offset        = p->offset;
	memset(&range.pci_device, 0, sizeof(range.pci_device));

	err = get_addr_range(client, &range, NULL);

	if (!err)
		p->physical_address = range.physical_address;

	LOG_EXT();
	return err;
}

int esc_mods_get_mapped_phys_addr(struct mods_client               *client,
				  struct MODS_GET_PHYSICAL_ADDRESS *p)
{
	struct MODS_GET_PHYSICAL_ADDRESS_3 range;
	struct MODS_MEM_INFO *p_mem_info;
	int err;

	LOG_ENT();

	p_mem_info = get_mem_handle(client, p->memory_handle);
	if (unlikely(!p_mem_info)) {
		LOG_EXT();
		return -EINVAL;
	}

	range.memory_handle = p->memory_handle;
	range.offset        = p->offset;

	if (p_mem_info->dev) {
		range.pci_device.domain   =
			pci_domain_nr(p_mem_info->dev->bus);
		range.pci_device.bus	   =
			p_mem_info->dev->bus->number;
		range.pci_device.device   =
			PCI_SLOT(p_mem_info->dev->devfn);
		range.pci_device.function =
			PCI_FUNC(p_mem_info->dev->devfn);

		err = get_addr_range(client, &range, &range.pci_device);
	} else {
		memset(&range.pci_device, 0, sizeof(range.pci_device));
		err = get_addr_range(client, &range, NULL);
	}

	if (!err)
		p->physical_address = range.physical_address;

	LOG_EXT();
	return err;
}

int esc_mods_get_mapped_phys_addr_2(struct mods_client                 *client,
				    struct MODS_GET_PHYSICAL_ADDRESS_2 *p)
{
	struct MODS_GET_PHYSICAL_ADDRESS_3 range;
	int err;

	LOG_ENT();

	range.memory_handle = p->memory_handle;
	range.offset        = p->offset;
	range.pci_device    = p->pci_device;

	err = get_addr_range(client, &range, &range.pci_device);

	if (!err)
		p->physical_address = range.physical_address;

	LOG_EXT();
	return err;
}

int esc_mods_get_mapped_phys_addr_3(struct mods_client                 *client,
				    struct MODS_GET_PHYSICAL_ADDRESS_3 *p)
{
	struct MODS_GET_PHYSICAL_ADDRESS_3 range;
	int err;

	LOG_ENT();

	range.memory_handle = p->memory_handle;
	range.offset        = p->offset;
	range.pci_device    = p->pci_device;

	err = get_addr_range(client, &range, &range.pci_device);

	if (!err)
		p->physical_address = range.physical_address;

	LOG_EXT();
	return err;
}

int esc_mods_virtual_to_phys(struct mods_client              *client,
			     struct MODS_VIRTUAL_TO_PHYSICAL *p)
{
	struct MODS_GET_PHYSICAL_ADDRESS_3 range;
	struct list_head                  *head;
	struct list_head                  *iter;
	int                                err;

	LOG_ENT();

	memset(&range, 0, sizeof(range));

	err = mutex_lock_interruptible(&client->mtx);
	if (unlikely(err)) {
		LOG_EXT();
		return err;
	}

	head = &client->mem_map_list;

	list_for_each(iter, head) {
		struct SYS_MAP_MEMORY *p_map_mem;
		u64                    begin, end;

		p_map_mem = list_entry(iter, struct SYS_MAP_MEMORY, list);

		begin = p_map_mem->virtual_addr;
		end   = p_map_mem->virtual_addr + p_map_mem->mapping_length;

		if (p->virtual_address >= begin && p->virtual_address < end) {

			u64 virt_offs = p->virtual_address - begin;

			/* device memory mapping */
			if (!p_map_mem->p_mem_info) {
				p->physical_address = p_map_mem->phys_addr
						      + virt_offs;
				mutex_unlock(&client->mtx);

				cl_debug(DEBUG_MEM_DETAILED,
					 "get phys: map %p virt 0x%llx -> 0x%llx\n",
					 p_map_mem,
					 p->virtual_address,
					 p->physical_address);

				LOG_EXT();
				return OK;
			}

			range.memory_handle =
				(u64)(size_t)p_map_mem->p_mem_info;
			range.offset = virt_offs + p_map_mem->mapping_offs;

			mutex_unlock(&client->mtx);

			err = get_addr_range(client, &range, NULL);
			if (err) {
				LOG_EXT();
				return err;
			}

			p->physical_address = range.physical_address;

			cl_debug(DEBUG_MEM_DETAILED,
				 "get phys: map %p virt 0x%llx -> 0x%llx\n",
				 p_map_mem,
				 p->virtual_address,
				 p->physical_address);

			LOG_EXT();
			return OK;
		}
	}

	mutex_unlock(&client->mtx);

	cl_error("invalid virtual address 0x%llx\n", p->virtual_address);
	LOG_EXT();
	return -EINVAL;
}

int esc_mods_phys_to_virtual(struct mods_client              *client,
			     struct MODS_PHYSICAL_TO_VIRTUAL *p)
{
	struct SYS_MAP_MEMORY *p_map_mem;
	struct list_head      *head;
	struct list_head      *iter;
	u64                    offset;
	u64                    map_offset;
	int                    err;

	LOG_ENT();

	err = mutex_lock_interruptible(&client->mtx);
	if (unlikely(err)) {
		LOG_EXT();
		return err;
	}

	head = &client->mem_map_list;

	list_for_each(iter, head) {
		p_map_mem = list_entry(iter, struct SYS_MAP_MEMORY, list);

		/* device memory mapping */
		if (!p_map_mem->p_mem_info) {
			u64 end = p_map_mem->phys_addr
				+ p_map_mem->mapping_length;
			if (p->physical_address <  p_map_mem->phys_addr ||
			    p->physical_address >= end)
				continue;

			offset = p->physical_address - p_map_mem->phys_addr;
			p->virtual_address = p_map_mem->virtual_addr
					     + offset;
			mutex_unlock(&client->mtx);

			cl_debug(DEBUG_MEM_DETAILED,
				 "get virt: map %p phys 0x%llx -> 0x%llx\n",
				 p_map_mem,
				 p->physical_address,
				 p->virtual_address);

			LOG_EXT();
			return OK;
		}

		/* offset from the beginning of the allocation */
		if (get_alloc_offset(p_map_mem->p_mem_info,
				     p->physical_address,
				     &offset))
			continue;

		/* offset from the beginning of the mapping */
		map_offset = p_map_mem->mapping_offs;

		if ((offset >= map_offset) &&
		    (offset <  map_offset + p_map_mem->mapping_length)) {
			p->virtual_address = p_map_mem->virtual_addr
					   + offset - map_offset;

			mutex_unlock(&client->mtx);
			cl_debug(DEBUG_MEM_DETAILED,
				 "get virt: map %p phys 0x%llx -> 0x%llx\n",
				 p_map_mem,
				 p->physical_address,
				 p->virtual_address);

			LOG_EXT();
			return OK;
		}
	}

	mutex_unlock(&client->mtx);

	cl_error("phys addr 0x%llx is not mapped\n", p->physical_address);
	LOG_EXT();
	return -EINVAL;
}

#if defined(CONFIG_ARM)
int esc_mods_memory_barrier(struct mods_client *client)
{
	/* Full memory barrier on ARMv7 */
	wmb();
	return OK;
}
#endif

#ifdef CONFIG_PCI
int esc_mods_dma_map_memory(struct mods_client         *client,
			    struct MODS_DMA_MAP_MEMORY *p)
{
	struct MODS_MEM_INFO *p_mem_info;
	struct MODS_DMA_MAP  *p_dma_map;
	struct pci_dev       *dev    = NULL;
	int                   err    = -EINVAL;
	bool                  locked = false;

	LOG_ENT();

	p_mem_info = get_mem_handle(client, p->memory_handle);
	if (unlikely(!p_mem_info))
		goto failed;

	err = mutex_lock_interruptible(&client->mtx);
	if (unlikely(err))
		goto failed;
	locked = true;

	if (mods_is_pci_dev(p_mem_info->dev, &p->pci_device)) {
		err = dma_map_to_default_dev(client, p_mem_info);
		goto failed;
	}

	if (mods_is_pci_dev(client->cached_dev, &p->pci_device))
		dev = pci_dev_get(client->cached_dev);
	else {
		mutex_unlock(&client->mtx);
		locked = false;

		err = mods_find_pci_dev(client, &p->pci_device, &dev);
		if (unlikely(err)) {
			if (err == -ENODEV)
				cl_error("dev %04x:%02x:%02x.%x not found\n",
					 p->pci_device.domain,
					 p->pci_device.bus,
					 p->pci_device.device,
					 p->pci_device.function);
			goto failed;
		}

		err = mutex_lock_interruptible(&client->mtx);
		if (unlikely(err))
			goto failed;
		locked = true;
	}

	p_dma_map = find_dma_map(p_mem_info, &p->pci_device);
	if (unlikely(p_dma_map)) {
		cl_debug(DEBUG_MEM_DETAILED,
			 "memory %p already mapped to dev %04x:%02x:%02x.%x\n",
			 p_mem_info,
			 p->pci_device.domain,
			 p->pci_device.bus,
			 p->pci_device.device,
			 p->pci_device.function);
		goto failed;
	}

	err = create_dma_map(client, p_mem_info, dev, &dev->dev);

failed:
	if (locked)
		mutex_unlock(&client->mtx);

	pci_dev_put(dev);

	LOG_EXT();
	return err;
}

int esc_mods_dma_unmap_memory(struct mods_client         *client,
			      struct MODS_DMA_MAP_MEMORY *p)
{
	struct MODS_MEM_INFO *p_mem_info;
	struct pci_dev       *dev = NULL;
	int                   err = -EINVAL;

	LOG_ENT();

	p_mem_info = get_mem_handle(client, p->memory_handle);
	if (unlikely(!p_mem_info))
		goto failed;

	err = mods_find_pci_dev(client, &p->pci_device, &dev);
	if (unlikely(err)) {
		if (err == -ENODEV)
			cl_error("dev %04x:%02x:%02x.%x not found\n",
				 p->pci_device.domain,
				 p->pci_device.bus,
				 p->pci_device.device,
				 p->pci_device.function);
		goto failed;
	}

	err = mutex_lock_interruptible(&client->mtx);
	if (unlikely(err))
		goto failed;

	err = dma_unmap_all(client, p_mem_info, &dev->dev);

	mutex_unlock(&client->mtx);

failed:
	pci_dev_put(dev);

	LOG_EXT();
	return err;
}
#endif /* CONFIG_PCI */

#ifdef MODS_HAS_TEGRA
/* map dma buffer by iommu */
int esc_mods_iommu_dma_map_memory(struct mods_client               *client,
				  struct MODS_IOMMU_DMA_MAP_MEMORY *p)
{
	struct scatterlist   *sg;
	struct MODS_MEM_INFO *p_mem_info;
	char                 *dev_name  = p->dev_name;
	struct mods_smmu_dev *smmu_pdev = NULL;
	struct MODS_DMA_MAP  *p_dma_map;
	dma_addr_t            next_iova = 0;
	u32                   num_chunks;
	u32                   i;
	int                   smmudev_idx;
	int                   err       = -EINVAL;
	bool                  locked    = false;

	LOG_ENT();

	if (!(p->flags & MODS_IOMMU_MAP_CONTIGUOUS))
		cl_error("contiguous flag not set\n");

	p_mem_info = get_mem_handle(client, p->memory_handle);
	if (unlikely(!p_mem_info))
		goto failed;

	if (!list_empty(&p_mem_info->dma_map_list)) {
		cl_error("smmu is already mapped\n");
		goto failed;
	}

	smmudev_idx = get_mods_smmu_device_index(dev_name);
	if (smmudev_idx >= 0)
		smmu_pdev = get_mods_smmu_device(smmudev_idx);
	if (!smmu_pdev || smmudev_idx < 0) {
		cl_error("smmu device %s not found\n", dev_name);
		err = -ENODEV;
		goto failed;
	}

	err = mutex_lock_interruptible(&client->mtx);
	if (unlikely(err))
		goto failed;
	locked = true;

	/* do smmu mapping */
	err = create_dma_map(client, p_mem_info, NULL, smmu_pdev->dev);
	if (err)
		goto failed;

	/* Check if IOVAs are contiguous */
	p_dma_map = list_first_entry(&p_mem_info->dma_map_list,
				     struct MODS_DMA_MAP, list);
	num_chunks = get_num_chunks(p_mem_info);
	for_each_sg(p_dma_map->sg, sg, num_chunks, i) {

		const dma_addr_t iova     = sg_dma_address(sg);
		const dma_addr_t iova_end = iova + sg_dma_len(sg);

		/* Skip checking if IOMMU driver merged it into a single
		 * contiguous chunk.
		 */
		if (iova_end == iova)
			continue;

		if ((i > 0) && (iova != next_iova)) {
			cl_error("sg not contiguous: dma 0x%llx, expected 0x%llx\n",
				 (unsigned long long)sg_dma_address(sg),
				 (unsigned long long)next_iova);

			dma_unmap_and_free(client, p_mem_info, p_dma_map);
			err = -EINVAL;
			goto failed;
		}

		next_iova = iova_end;
	}

	p->physical_address = sg_dma_address(p_dma_map->sg);

failed:
	if (locked)
		mutex_unlock(&client->mtx);

	LOG_EXT();
	return err;
}

/* unmap dma buffer by iommu */
int esc_mods_iommu_dma_unmap_memory(struct mods_client               *client,
				    struct MODS_IOMMU_DMA_MAP_MEMORY *p)
{
	struct MODS_MEM_INFO *p_mem_info;
	struct MODS_DMA_MAP  *p_dma_map;
	int                   err = -EINVAL;

	LOG_ENT();

	p_mem_info = get_mem_handle(client, p->memory_handle);
	if (unlikely(!p_mem_info))
		goto failed;

	if (!list_is_singular(&p_mem_info->dma_map_list)) {
		cl_error("smmu buffer is not mapped, handle=0x%llx\n",
			 (unsigned long long)p_mem_info);
		goto failed;
	}

	err = mutex_lock_interruptible(&client->mtx);
	if (unlikely(err))
		goto failed;

	p_dma_map = list_first_entry(&p_mem_info->dma_map_list,
				     struct MODS_DMA_MAP,
				     list);

	dma_unmap_and_free(client, p_mem_info, p_dma_map);

	mutex_unlock(&client->mtx);

failed:
	LOG_EXT();
	return err;
}
#endif /* MODS_HAS_TEGRA */

int esc_mods_reserve_allocation(struct mods_client             *client,
				struct MODS_RESERVE_ALLOCATION *p)
{
	struct MODS_MEM_INFO        *p_mem_info;
	struct MODS_MEM_INFO        *p_existing_mem_info = NULL;
	struct MODS_MEM_RESERVATION *p_reservation = NULL;
	struct list_head            *head = &client->mem_alloc_list;
	struct list_head            *iter;
	int                         err = -EINVAL;

	LOG_ENT();

	if (!(p->tag) || (p->tag > MODS_MEM_MAX_RESERVATIONS)) {
		cl_error("invalid tag 0x%llx for memory reservations\n",
			 (unsigned long long)p->tag);
		LOG_EXT();
		return -EINVAL;
	}

	/* Get passed mem_info */
	p_mem_info = get_mem_handle(client, p->memory_handle);
	if (unlikely(!p_mem_info)) {
		cl_error("failed to get memory handle\n");
		LOG_EXT();
		return -EINVAL;
	}

	/* Lock mutexes */
	err = mutex_lock_interruptible(&mem_reservation_mtx);
	if (unlikely(err)) {
		LOG_EXT();
		return err;
	}
	err = mutex_lock_interruptible(&client->mtx);
	if (unlikely(err)) {
		mutex_unlock(&mem_reservation_mtx);
		LOG_EXT();
		return err;
	}

	/* Check for existing reservation */
	p_reservation = &mem_reservations[p->tag - 1];
	if (unlikely(p_reservation->p_mem_info)) {
		cl_error("reservation 0x%llX already exists\n",
			 (unsigned long long)p->tag);
		err = -ENOMEM;
		goto failed;
	}

	/* Find existing handle in client and mark as reserved */
	list_for_each(iter, head) {
		p_existing_mem_info = list_entry(iter, struct MODS_MEM_INFO, list);

		if (p_existing_mem_info == p_mem_info)
			break;
		p_existing_mem_info = NULL;
	}
	if (unlikely(!p_existing_mem_info)) {
		cl_error("failed to find mem info requested by reservation\n");
		err = -EINVAL;
		goto failed;
	}
	p_existing_mem_info->reservation_tag = p->tag; /* Set tag to avoid free */

	/* Add memory handle to new reservation */
	p_reservation->p_mem_info = p_existing_mem_info;
	p_reservation->client_id = client->client_id;

failed:
	mutex_unlock(&client->mtx);
	mutex_unlock(&mem_reservation_mtx);
	LOG_EXT();
	return err;
}

int esc_mods_get_reserved_allocation(struct mods_client             *client,
				     struct MODS_RESERVE_ALLOCATION *p)
{
	struct MODS_MEM_RESERVATION *p_reservation = NULL;
	int                         err = -EINVAL;

	LOG_ENT();

	if (!(p->tag) || (p->tag > MODS_MEM_MAX_RESERVATIONS)) {
		cl_error("invalid tag 0x%llx for memory reservations\n",
			 (unsigned long long)p->tag);
		LOG_EXT();
		return -EINVAL;
	}

	err = mutex_lock_interruptible(&mem_reservation_mtx);
	if (unlikely(err)) {
		LOG_EXT();
		return err;
	}

	/* Locate existing reservation */
	p_reservation = &mem_reservations[p->tag - 1];
	if (unlikely(!p_reservation->p_mem_info)) {
		cl_error("no mem reservation for tag 0x%llX\n",
			 (unsigned long long)p->tag);
		p->memory_handle = 0;
		err = -EINVAL;
		goto failed;
	}
	if ((p_reservation->client_id != client->client_id) &&
		(p_reservation->client_id)) {
		cl_error("reservation 0x%llX is claimed by client_id %d\n",
			 (unsigned long long)p->tag, p_reservation->client_id);
		err = -EBUSY;
		p->memory_handle = 0;
		goto failed;
	}

	/* Claim reservation and return handle */
	if (p_reservation->client_id != client->client_id) {
		p_reservation->client_id = client->client_id;
		register_alloc(client, p_reservation->p_mem_info);
		atomic_inc(&client->num_allocs); /* Increment allocations */
		atomic_add((int)p_reservation->p_mem_info->num_pages,
			   &client->num_pages); /* Increment pages */
	}
	p->memory_handle = (u64)(size_t)p_reservation->p_mem_info;

failed:
	mutex_unlock(&mem_reservation_mtx);
	LOG_EXT();
	return err;
}

int esc_mods_release_reserved_allocation(struct mods_client             *client,
					 struct MODS_RESERVE_ALLOCATION *p)
{
	struct MODS_MEM_RESERVATION *p_reservation = NULL;
	int                         err = -EINVAL;

	LOG_ENT();

	if (!(p->tag) || (p->tag > MODS_MEM_MAX_RESERVATIONS)) {
		cl_error("invalid tag 0x%llx for memory reservations\n",
			 (unsigned long long)p->tag);
		LOG_EXT();
		return -EINVAL;
	}

	err = mutex_lock_interruptible(&mem_reservation_mtx);
	if (unlikely(err)) {
		LOG_EXT();
		return err;
	}

	/* Locate existing reservation */
	p_reservation = &mem_reservations[p->tag - 1];
	if (unlikely(!p_reservation->p_mem_info)) {
		cl_error("no mem reservation for tag 0x%llX\n",
			 (unsigned long long)p->tag);
		err = -EINVAL;
		goto failed;
	}
	if (!p_reservation->client_id) {
		cl_error("Reservation with tag 0x%llX not claimed by calling client id\n",
			 (unsigned long long)p->tag);
		err = -EINVAL;
		goto failed;
	}
	if (p_reservation->client_id != client->client_id) {
		cl_error("reservation with tag 0x%llX not claimed by any client\n",
			 (unsigned long long)p->tag);
		err = -EBUSY;
		goto failed;
	}

	if (likely(p_reservation->p_mem_info)) {
		/* Unregister and clear reservation_tag field */
		p_reservation->p_mem_info->reservation_tag = 0;
		memset(p_reservation, 0, sizeof(*p_reservation));
	}

failed:
	mutex_unlock(&mem_reservation_mtx);
	LOG_EXT();
	return err;
}

#ifdef CONFIG_ARM64
static void clear_contiguous_cache(struct mods_client *client,
				   u64                 virt_start,
				   u32                 size)
{
	u64 end = virt_start + size;
	u64 cur;
	u64 d_size;
	static u32 d_line_shift;

	if (!d_line_shift) {
#ifdef CTR_EL0_DminLine_SHIFT
		const u64 ctr_el0 = read_sanitised_ftr_reg(SYS_CTR_EL0);

		d_line_shift =
			cpuid_feature_extract_unsigned_field(ctr_el0, CTR_EL0_DminLine_SHIFT);
#elif KERNEL_VERSION(5, 10, 0) <= MODS_KERNEL_VERSION
		const u64 ctr_el0 = read_sanitised_ftr_reg(SYS_CTR_EL0);

		d_line_shift =
			cpuid_feature_extract_unsigned_field(ctr_el0, CTR_DMINLINE_SHIFT);
#else
		d_line_shift = 4; /* Fallback for kernel 5.9 or older */
#endif
	}

	d_size = (u64)4 << d_line_shift;
	cur = virt_start & ~(d_size - 1);
	do {
		asm volatile("dc civac, %0" : : "r" (cur) : "memory");

		/* Avoid superficial lockups */
		if (!(cur & ((1U << 16) - 1U)))
			cond_resched();
	} while (cur += d_size, cur < end);
	asm volatile("dsb sy" : : : "memory");

	cl_debug(DEBUG_MEM_DETAILED,
		 "flush cache virt 0x%llx size 0x%x\n",
		 virt_start, size);
}

int esc_mods_flush_cpu_cache_range(struct mods_client                *client,
				   struct MODS_FLUSH_CPU_CACHE_RANGE *p)
{
	struct list_head *head;
	struct list_head *iter;
	int               err;

	LOG_ENT();

	if (irqs_disabled() || in_interrupt() ||
	    p->virt_addr_start > p->virt_addr_end) {

		cl_debug(DEBUG_MEM_DETAILED, "cannot flush cache\n");
		LOG_EXT();
		return -EINVAL;
	}

	if (p->flags == MODS_INVALIDATE_CPU_CACHE) {
		cl_debug(DEBUG_MEM_DETAILED, "cannot invalidate cache\n");
		LOG_EXT();
		return -EINVAL;
	}

	err = mutex_lock_interruptible(&client->mtx);
	if (unlikely(err)) {
		LOG_EXT();
		return err;
	}

	head = &client->mem_map_list;
	err  = -EINVAL;

	list_for_each(iter, head) {
		struct SYS_MAP_MEMORY *p_map_mem
			= list_entry(iter, struct SYS_MAP_MEMORY, list);

		const u64 mapped_va   = p_map_mem->virtual_addr;
		const u64 mapped_end  = mapped_va + p_map_mem->mapping_length;
		const u64 flush_start = p->virt_addr_start < mapped_va ? mapped_va
								       : p->virt_addr_start;
		const u64 flush_end   = p->virt_addr_end > mapped_end ? mapped_end
								      : p->virt_addr_end;

		if (flush_start >= flush_end)
			continue;

		clear_contiguous_cache(client, flush_start, flush_end - flush_start);
		err = OK;
	}

	mutex_unlock(&client->mtx);

	if (err)
		cl_error("va range 0x%lx..0x%lx not flushed\n",
			 (unsigned long)p->virt_addr_start,
			 (unsigned long)p->virt_addr_end);

	LOG_EXT();
	return err;
}
#endif /* CONFIG_ARM64 */

/***************************
 * RESERVATION INIT / EXIT *
 ***************************/
void mods_free_mem_reservations(void)
{
	struct mods_client * const client = mods_client_from_id(1);
	int i;

	/* Dummy client used to ensure ensuing functions do not crash */
	memset(client, 0, sizeof(*client));

	/* Clear reserved on claimed reservations and free unclaimed ones */
	for (i = 0; i < MODS_MEM_MAX_RESERVATIONS; i++) {
		struct MODS_MEM_RESERVATION *p_reservation = &mem_reservations[i];

		/* Existing reservation */
		if (p_reservation->p_mem_info) {
			release_chunks(client, p_reservation->p_mem_info);
			pci_dev_put(p_reservation->p_mem_info->dev);
			kfree(p_reservation->p_mem_info);
			memset(p_reservation, 0, sizeof(*p_reservation));
		}
	}
}
