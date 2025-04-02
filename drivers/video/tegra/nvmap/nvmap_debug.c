// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2011-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * nvmap debug functionalities.
 */

#include <linux/debugfs.h>
#include <linux/pagewalk.h>

#include <nvidia/conftest.h>
#include <linux/mm.h>
#include <linux/nvmap.h>
#include "nvmap_dev.h"
#include "nvmap_alloc.h"
#include "nvmap_handle.h"
#include "nvmap_debug.h"
#include "nvmap_stats.h"
#include "nvmap_dmabuf.h"

static struct debugfs_info *iovmm_debugfs_info;
extern ulong nvmap_init_time;

#define DEBUGFS_OPEN_FOPS_STATIC(name) \
static int nvmap_debug_##name##_open(struct inode *inode, \
					    struct file *file) \
{ \
	return single_open(file, nvmap_debug_##name##_show, \
			    inode->i_private); \
} \
\
static const struct file_operations debug_##name##_fops = { \
	.open = nvmap_debug_##name##_open, \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.release = single_release, \
}

#define DEBUGFS_OPEN_FOPS(name) \
static int nvmap_debug_##name##_open(struct inode *inode, \
					    struct file *file) \
{ \
	return single_open(file, nvmap_debug_##name##_show, \
			    inode->i_private); \
} \
\
const struct file_operations debug_##name##_fops = { \
	.open = nvmap_debug_##name##_open, \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.release = single_release, \
}

#define K(x) (x >> 10)

static void client_stringify(struct nvmap_client *client, struct seq_file *s)
{
	char task_comm[TASK_COMM_LEN];

	if (!client->task) {
		seq_printf(s, "%-18s %18s %8u", client->name, "kernel", 0);
		return;
	}
	get_task_comm(task_comm, client->task);
	seq_printf(s, "%-18s %18s %8u", client->name, task_comm,
		   client->task->pid);
}

static void allocations_stringify(struct nvmap_client *client,
				  struct seq_file *s, u32 heap_type)
{
	struct rb_node *n;
	unsigned int pin_count = 0;
	struct nvmap_device *dev = nvmap_dev;

	nvmap_ref_lock(client);
	mutex_lock(&dev->tags_lock);
	n = rb_first(&client->handle_refs);
	for (; n != NULL; n = rb_next(n)) {
		struct nvmap_handle_ref *ref =
			rb_entry(n, struct nvmap_handle_ref, node);
		struct nvmap_handle *handle = ref->handle;

		if (handle->alloc && handle->heap_type == heap_type) {
			phys_addr_t base = heap_type == NVMAP_HEAP_IOVMM ? 0 :
					   handle->heap_pgalloc ? 0 :
					   (nvmap_get_heap_block_base(handle->carveout));
			size_t size = K(handle->size);
			int i = 0;

next_page:
			if ((heap_type == NVMAP_HEAP_CARVEOUT_VPR) && handle->heap_pgalloc) {
				base = page_to_phys(handle->pgalloc.pages[i++]);
				size = K(PAGE_SIZE);
			}

			seq_printf(s,
				"%-18s %-18s %8llx %10zuK %8x %6u %6u %6u %6u %6u %6u %8pK %6u %s\n"
				, "", "",
				(unsigned long long)base, size,
				handle->userflags,
				atomic_read(&handle->ref),
				atomic_read(&ref->dupes),
				pin_count,
				atomic_read(&handle->kmap_count),
				atomic_read(&handle->umap_count),
				atomic_read(&handle->share_count),
				handle,
				handle->has_hugetlbfs_pages,
				__nvmap_tag_name(dev, handle->userflags >> 16));

			if ((heap_type == NVMAP_HEAP_CARVEOUT_VPR) && handle->heap_pgalloc) {
				i++;
				if (i < (handle->size >> PAGE_SHIFT))
					goto next_page;
			}
		}
	}
	mutex_unlock(&dev->tags_lock);
	nvmap_ref_unlock(client);
}

/* compute the total amount of handle physical memory that is mapped
 * into client's virtual address space. Remember that vmas list is
 * sorted in ascending order of handle offsets.
 * NOTE: This function should be called while holding handle's lock mutex.
 */
static int nvmap_get_client_handle_mss(struct nvmap_client *client,
				struct nvmap_handle *handle, u64 *total)
{
	struct nvmap_vma_list *vma_list = NULL;
	struct vm_area_struct *vma = NULL;
	u64 end_offset = 0, vma_start_offset, vma_size, sum, difference;
	int64_t overlap_size;

	*total = 0;
	list_for_each_entry(vma_list, &handle->vmas, list) {

		if (client->task->pid == vma_list->pid) {
			vma = vma_list->vma;
			vma_size = vma->vm_end - vma->vm_start;

			vma_start_offset = vma->vm_pgoff << PAGE_SHIFT;
			if (check_add_overflow(vma_start_offset, vma_size, &sum))
				return -EOVERFLOW;

			if (end_offset < sum) {
				if (check_add_overflow(*total, vma_size, &sum))
					return -EOVERFLOW;

				*total = sum;

				overlap_size = end_offset - vma_start_offset;
				if (overlap_size > 0) {
					if (check_sub_overflow(*total, (u64)overlap_size,
						&difference))
						return -EOVERFLOW;

					*total = difference;
				}
				if (check_add_overflow(vma_start_offset, vma_size, &sum))
					return -EOVERFLOW;

				end_offset = sum;
			}
		}
	}
	return 0;
}

static int maps_stringify(struct nvmap_client *client,
				struct seq_file *s, u32 heap_type)
{
	struct rb_node *n;
	struct nvmap_vma_list *vma_list = NULL;
	struct vm_area_struct *vma = NULL;
	u64 total_mapped_size, vma_size;
	int err = 0;

	nvmap_ref_lock(client);
	n = rb_first(&client->handle_refs);
	for (; n != NULL; n = rb_next(n)) {
		struct nvmap_handle_ref *ref =
			rb_entry(n, struct nvmap_handle_ref, node);
		struct nvmap_handle *handle = ref->handle;

		if (handle->alloc && handle->heap_type == heap_type) {
			phys_addr_t base = heap_type == NVMAP_HEAP_IOVMM ? 0 :
					   handle->heap_pgalloc ? 0 :
					   (nvmap_get_heap_block_base(handle->carveout));
			size_t size = K(handle->size);
			int i = 0;

next_page:
			if ((heap_type == NVMAP_HEAP_CARVEOUT_VPR) && handle->heap_pgalloc) {
				base = page_to_phys(handle->pgalloc.pages[i++]);
				size = K(PAGE_SIZE);
			}

			seq_printf(s,
				"%-18s %-18s %8llx %10zuK %8x %6u %16pK %12s %12s ",
				"", "",
				(unsigned long long)base, K(handle->size),
				handle->userflags,
				atomic_read(&handle->share_count),
				handle, "", "");

			if ((heap_type == NVMAP_HEAP_CARVEOUT_VPR) && handle->heap_pgalloc) {
				i++;
				if (i < (handle->size >> PAGE_SHIFT))
					goto next_page;
			}

			mutex_lock(&handle->lock);
			err = nvmap_get_client_handle_mss(client, handle,
							&total_mapped_size);
			if (err != 0) {
				mutex_unlock(&handle->lock);
				goto finish;
			}

			seq_printf(s, "%6lluK\n", K(total_mapped_size));

			list_for_each_entry(vma_list, &handle->vmas, list) {

				if (vma_list->pid == client->task->pid) {
					vma = vma_list->vma;
					vma_size = vma->vm_end - vma->vm_start;
					seq_printf(s,
					  "%-18s %-18s %8s %11s %8s %6s %16s %-12lx-%12lx %6lluK\n",
					  "", "", "", "", "", "", "",
					  vma->vm_start, vma->vm_end,
					  K(vma_size));
				}
			}
			mutex_unlock(&handle->lock);
		}
	}

finish:
	nvmap_ref_unlock(client);
	return err;
}

static int nvmap_get_client_mss(struct nvmap_client *client,
				 u64 *total, u32 heap_type, int numa_id)
{
	struct rb_node *n;
	u64 sum;

	*total = 0;
	nvmap_ref_lock(client);
	n = rb_first(&client->handle_refs);
	for (; n != NULL; n = rb_next(n)) {
		struct nvmap_handle_ref *ref =
			rb_entry(n, struct nvmap_handle_ref, node);
		struct nvmap_handle *handle = ref->handle;

		if (handle->alloc && handle->heap_type == heap_type) {
			if (heap_type != NVMAP_HEAP_IOVMM &&
				(nvmap_get_heap_nid(nvmap_block_to_heap(handle->carveout)) !=
				 numa_id))
				continue;
			if (check_add_overflow((u64)handle->size, *total, &sum))
				return -EOVERFLOW;

			*total += handle->size /
				  atomic_read(&handle->share_count);
		}
	}
	nvmap_ref_unlock(client);
	return 0;
}

static int nvmap_page_mapcount(struct page *page)
{

	int mapcount, sum;

	if (check_add_overflow(atomic_read(&page->_mapcount), 1, &sum))
		return -EOVERFLOW;

	mapcount = sum;

	/* Handle page_has_type() pages */
	if (page_has_type(page))
		mapcount = 0;
	if (unlikely(PageCompound(page))) {
#if defined(NV_FOLIO_ENTIRE_MAPCOUNT_PRESENT) /* Linux v5.18 */
		if (check_add_overflow(mapcount, folio_entire_mapcount(page_folio(page)), &sum))
			return -EOVERFLOW;
		mapcount = sum;
#else
		mapcount += compound_mapcount(page);
#endif
	}
	return mapcount;
}

#define PSS_SHIFT 12
static int nvmap_get_total_mss(u64 *pss, u64 *total, u32 heap_type, int numa_id)
{
	int i;
	struct rb_node *n;
	struct nvmap_device *dev = nvmap_dev;

	*total = 0;
	if (pss)
		*pss = 0;
	if (dev == NULL)
		return 0;
	spin_lock(&dev->handle_lock);
	n = rb_first(&dev->handles);
	for (; n != NULL; n = rb_next(n)) {
		struct nvmap_handle *h =
			rb_entry(n, struct nvmap_handle, node);

		if (h == NULL || !h->alloc || h->heap_type != heap_type)
			continue;

		if (heap_type != NVMAP_HEAP_IOVMM &&
			(nvmap_get_heap_nid(nvmap_block_to_heap(h->carveout)) !=
			numa_id))
			continue;

		if (check_add_overflow(*total, (u64)h->size, &*total)) {
			spin_unlock(&dev->handle_lock);
			return -EOVERFLOW;
		}

		if (!pss)
			continue;

		for (i = 0; i < h->size >> PAGE_SHIFT; i++) {
			struct page *page = nvmap_to_page(h->pgalloc.pages[i]);

			if (nvmap_page_mapcount(page) > 0) {
				if (check_add_overflow(*pss, (u64)PAGE_SIZE, &*pss)) {
					spin_unlock(&dev->handle_lock);
					return -EOVERFLOW;
				}

			}
		}
	}
	spin_unlock(&dev->handle_lock);
	return 0;
}

static int nvmap_debug_allocations_show(struct seq_file *s, void *unused)
{
	u64 total;
	struct nvmap_client *client;
	struct debugfs_info *debugfs_information = (struct debugfs_info *)s->private;
	u32 heap_type = nvmap_get_debug_info_heap(debugfs_information);
	int numa_id = nvmap_get_debug_info_nid(debugfs_information);
	int err;

	mutex_lock(&nvmap_dev->clients_lock);
	seq_printf(s, "%-18s %18s %8s %11s\n",
		"CLIENT", "PROCESS", "PID", "SIZE");
	seq_printf(s, "%-18s %18s %8s %11s %8s %6s %6s %6s %6s %6s %6s %8s %6s\n",
			"", "", "BASE", "SIZE", "FLAGS", "REFS",
			"DUPES", "PINS", "KMAPS", "UMAPS", "SHARE", "UID", "FROM_HUGETLBFS");
	list_for_each_entry(client, &nvmap_dev->clients, list) {
		u64 client_total;

		client_stringify(client, s);
		err = nvmap_get_client_mss(client, &client_total, heap_type, numa_id);
		if (err != 0) {
			mutex_unlock(&nvmap_dev->clients_lock);
			return err;
		}
		seq_printf(s, " %10lluK\n", K(client_total));
		allocations_stringify(client, s, heap_type);
		seq_puts(s, "\n");
	}
	mutex_unlock(&nvmap_dev->clients_lock);
	err = nvmap_get_total_mss(NULL, &total, heap_type, numa_id);
	if (err != 0)
		return err;

	seq_printf(s, "%-18s %-18s %8s %10lluK\n", "total", "", "", K(total));
	return 0;
}

DEBUGFS_OPEN_FOPS(allocations);

static int nvmap_debug_free_size_show(struct seq_file *s, void *unused)
{
	unsigned long free_mem = 0;

	if (system_heap_free_mem(&free_mem))
		seq_puts(s, "Error while fetching free size of IOVMM memory\n");
	else
		seq_printf(s, "Max allocatable IOVMM memory: %lu bytes\n", free_mem);
	return 0;
}
DEBUGFS_OPEN_FOPS_STATIC(free_size);

#ifdef NVMAP_CONFIG_DEBUG_MAPS
static int nvmap_debug_device_list_show(struct seq_file *s, void *unused)
{
	struct debugfs_info *debugfs_information = (struct debugfs_info *)s->private;
	u32 heap_type = nvmap_get_debug_info_heap(debugfs_information);
	int numa_id = nvmap_get_debug_info_nid(debugfs_information);
	struct rb_node *n = NULL;
	struct nvmap_device_list *dl = NULL;
	int i;

	if (heap_type == NVMAP_HEAP_IOVMM) {
		n = rb_first(&nvmap_dev->device_names);
	} else {
		/* Iterate over all heaps to find the matching heap */
		for (i = 0; i < nvmap_dev->nr_carveouts; i++) {
			if (heap_type & nvmap_get_heap_bit(nvmap_dev->heaps[i])) {
				if (nvmap_get_heap_ptr(nvmap_dev->heaps[i]) && (
					(nvmap_get_heap_nid(nvmap_get_heap_ptr(nvmap_dev->heaps[i]))
					 == numa_id))) {
					n = rb_first(nvmap_heap_get_device_ptr
						((nvmap_get_heap_ptr(nvmap_dev->heaps[i]))));
					break;
				}
			}
		}
	}
	if (n) {
		seq_puts(s, "Device list is\n");
		for (; n != NULL; n = rb_next(n)) {
			dl = rb_entry(n, struct nvmap_device_list, node);
			seq_printf(s, "%s %llu\n", dl->device_name, dl->dma_mask);
		}
	}
	return 0;
}

DEBUGFS_OPEN_FOPS(device_list);
#endif /* NVMAP_CONFIG_DEBUG_MAPS */

static int nvmap_debug_all_allocations_show(struct seq_file *s, void *unused)
{
	struct debugfs_info *debugfs_information = (struct debugfs_info *)s->private;
	u32 heap_type = nvmap_get_debug_info_heap(debugfs_information);
	int numa_id = nvmap_get_debug_info_nid(debugfs_information);
	struct rb_node *n;

	spin_lock(&nvmap_dev->handle_lock);
	seq_printf(s, "%8s %11s %9s %6s %6s %6s %6s %8s %6s\n",
			"BASE", "SIZE", "USERFLAGS", "REFS",
			"KMAPS", "UMAPS", "SHARE", "UID", "FROM_HUGETLBFS");

	/* for each handle */
	n = rb_first(&nvmap_dev->handles);
	for (; n != NULL; n = rb_next(n)) {
		struct nvmap_handle *handle =
			rb_entry(n, struct nvmap_handle, node);
		int i = 0;

		if (handle->alloc && handle->heap_type ==
				nvmap_get_debug_info_heap(debugfs_information)) {
			phys_addr_t base = heap_type == NVMAP_HEAP_IOVMM ? 0 :
					   handle->heap_pgalloc ? 0 :
					   (nvmap_get_heap_block_base(handle->carveout));
			size_t size = K(handle->size);

			if (heap_type != NVMAP_HEAP_IOVMM &&
			    (nvmap_get_heap_nid(nvmap_block_to_heap(handle->carveout)) != numa_id))
				continue;

next_page:
			if ((heap_type == NVMAP_HEAP_CARVEOUT_VPR) && handle->heap_pgalloc) {
				base = page_to_phys(handle->pgalloc.pages[i++]);
				size = K(PAGE_SIZE);
			}

			seq_printf(s,
				"%8llx %10zuK %9x %6u %6u %6u %6u %8p %6u\n",
				(unsigned long long)base, K(handle->size),
				handle->userflags,
				atomic_read(&handle->ref),
				atomic_read(&handle->kmap_count),
				atomic_read(&handle->umap_count),
				atomic_read(&handle->share_count),
				handle,
				handle->has_hugetlbfs_pages);

			if ((heap_type == NVMAP_HEAP_CARVEOUT_VPR) && handle->heap_pgalloc) {
				i++;
				if (i < (handle->size >> PAGE_SHIFT))
					goto next_page;
			}
		}
	}

	spin_unlock(&nvmap_dev->handle_lock);

	return 0;
}

DEBUGFS_OPEN_FOPS(all_allocations);

static int nvmap_debug_orphan_handles_show(struct seq_file *s, void *unused)
{
	struct debugfs_info *debugfs_information = (struct debugfs_info *)s->private;
	u32 heap_type = nvmap_get_debug_info_heap(debugfs_information);
	int numa_id = nvmap_get_debug_info_nid(debugfs_information);
	struct rb_node *n;

	spin_lock(&nvmap_dev->handle_lock);
	seq_printf(s, "%8s %11s %9s %6s %6s %6s %8s\n",
			"BASE", "SIZE", "USERFLAGS", "REFS",
			"KMAPS", "UMAPS", "UID");

	/* for each handle */
	n = rb_first(&nvmap_dev->handles);
	for (; n != NULL; n = rb_next(n)) {
		struct nvmap_handle *handle =
			rb_entry(n, struct nvmap_handle, node);
		int i = 0;

		if (handle->alloc && handle->heap_type == heap_type &&
			!atomic_read(&handle->share_count)) {
			phys_addr_t base = heap_type == NVMAP_HEAP_IOVMM ? 0 :
					   handle->heap_pgalloc ? 0 :
					   (nvmap_get_heap_block_base(handle->carveout));
			size_t size = K(handle->size);

			if (heap_type != NVMAP_HEAP_IOVMM &&
				(nvmap_get_heap_nid(nvmap_block_to_heap(handle->carveout)) !=
					numa_id))
				continue;

next_page:
			if ((heap_type == NVMAP_HEAP_CARVEOUT_VPR) && handle->heap_pgalloc) {
				base = page_to_phys(handle->pgalloc.pages[i++]);
				size = K(PAGE_SIZE);
			}

			seq_printf(s,
				"%8llx %10zuK %9x %6u %6u %6u %8p\n",
				(unsigned long long)base, K(handle->size),
				handle->userflags,
				atomic_read(&handle->ref),
				atomic_read(&handle->kmap_count),
				atomic_read(&handle->umap_count),
				handle);

			if ((heap_type == NVMAP_HEAP_CARVEOUT_VPR) && handle->heap_pgalloc) {
				i++;
				if (i < (handle->size >> PAGE_SHIFT))
					goto next_page;
			}
		}
	}

	spin_unlock(&nvmap_dev->handle_lock);

	return 0;
}

DEBUGFS_OPEN_FOPS(orphan_handles);

static int nvmap_debug_maps_show(struct seq_file *s, void *unused)
{
	u64 total;
	struct nvmap_client *client;
	struct debugfs_info *debugfs_information = (struct debugfs_info *)s->private;
	u32 heap_type = nvmap_get_debug_info_heap(debugfs_information);
	int numa_id = nvmap_get_debug_info_nid(debugfs_information);
	int err;

	mutex_lock(&nvmap_dev->clients_lock);
	seq_printf(s, "%-18s %18s %8s %11s\n",
		"CLIENT", "PROCESS", "PID", "SIZE");
	seq_printf(s, "%-18s %18s %8s %11s %8s %6s %9s %21s %18s\n",
		"", "", "BASE", "SIZE", "FLAGS", "SHARE", "UID",
		"MAPS", "MAPSIZE");

	list_for_each_entry(client, &nvmap_dev->clients, list) {
		u64 client_total;

		client_stringify(client, s);
		err = nvmap_get_client_mss(client, &client_total, heap_type, numa_id);
		if (err != 0) {
			mutex_unlock(&nvmap_dev->clients_lock);
			return err;
		}
		seq_printf(s, " %10lluK\n", K(client_total));
		err = maps_stringify(client, s, heap_type);
		if (err != 0) {
			mutex_unlock(&nvmap_dev->clients_lock);
			return -EOVERFLOW;
		}

		seq_puts(s, "\n");
	}
	mutex_unlock(&nvmap_dev->clients_lock);

	err = nvmap_get_total_mss(NULL, &total, heap_type, numa_id);
	if (err != 0)
		return err;

	seq_printf(s, "%-18s %-18s %8s %10lluK\n", "total", "", "", K(total));
	return 0;
}

DEBUGFS_OPEN_FOPS(maps);

static int nvmap_debug_clients_show(struct seq_file *s, void *unused)
{
	u64 total;
	struct nvmap_client *client;
	struct debugfs_info *debugfs_information = (struct debugfs_info *)s->private;
	u32 heap_type = nvmap_get_debug_info_heap(debugfs_information);
	int numa_id = nvmap_get_debug_info_nid(debugfs_information);
	int err;

	mutex_lock(&nvmap_dev->clients_lock);
	seq_printf(s, "%-18s %18s %8s %11s\n",
		"CLIENT", "PROCESS", "PID", "SIZE");
	list_for_each_entry(client, &nvmap_dev->clients, list) {
		u64 client_total;

		client_stringify(client, s);
		err = nvmap_get_client_mss(client, &client_total, heap_type, numa_id);
		if (err != 0) {
			mutex_unlock(&nvmap_dev->clients_lock);
			return err;
		}
		seq_printf(s, " %10lluK\n", K(client_total));
	}
	mutex_unlock(&nvmap_dev->clients_lock);
	err = nvmap_get_total_mss(NULL, &total, heap_type, numa_id);
	if (err != 0)
		return err;

	seq_printf(s, "%-18s %18s %8s %10lluK\n", "total", "", "", K(total));
	return 0;
}

DEBUGFS_OPEN_FOPS(clients);

static const struct file_operations debug_handles_by_pid_fops;

static int nvmap_debug_handles_by_pid_show_client(struct seq_file *s,
		struct nvmap_client *client)
{
	struct rb_node *n;
	int ret = 0;

	nvmap_ref_lock(client);
	n = rb_first(&client->handle_refs);
	for (; n != NULL; n = rb_next(n)) {
		struct nvmap_handle_ref *ref = rb_entry(n,
				struct nvmap_handle_ref, node);
		struct nvmap_handle *handle = ref->handle;
		struct nvmap_debugfs_handles_entry entry;
		u64 total_mapped_size;
		int i = 0;

		if (!handle->alloc)
			continue;

		mutex_lock(&handle->lock);
		ret = nvmap_get_client_handle_mss(client, handle, &total_mapped_size);
		mutex_unlock(&handle->lock);
		if (ret != 0)
			goto finish;

		entry.base = handle->heap_type == NVMAP_HEAP_IOVMM ? 0 :
			     handle->heap_pgalloc ? 0 :
			     (nvmap_get_heap_block_base(handle->carveout));
		entry.size = handle->size;
		entry.flags = handle->userflags;
		entry.share_count = atomic_read(&handle->share_count);
		entry.mapped_size = total_mapped_size;

next_page:
		if ((handle->heap_type == NVMAP_HEAP_CARVEOUT_VPR) && handle->heap_pgalloc) {
			entry.base = page_to_phys(handle->pgalloc.pages[i++]);
			entry.size = K(PAGE_SIZE);
		}

		seq_printf(s, "%llu %12llu %8u %8u %10llu\n", entry.base, entry.size,
				entry.flags, entry.share_count, entry.mapped_size);

		if ((handle->heap_type == NVMAP_HEAP_CARVEOUT_VPR) && handle->heap_pgalloc) {
			i++;
			if (i < (handle->size >> PAGE_SHIFT))
				goto next_page;
		}
	}

finish:
	nvmap_ref_unlock(client);

	return ret;
}

static int nvmap_debug_handles_by_pid_show(struct seq_file *s, void *unused)
{
	struct nvmap_pid_data *p = s->private;
	struct nvmap_client *client;
	struct nvmap_debugfs_handles_header header;
	int ret = 0;

	header.version = 1;
	seq_printf(s, "%s: %u\n", "header.version", header.version);
	seq_printf(s, "%s %8s %8s %12s %8s\n", "base",
		"size", "flags", "share_count", "mapped_size");
	mutex_lock(&nvmap_dev->clients_lock);

	list_for_each_entry(client, &nvmap_dev->clients, list) {
		if (nvmap_client_pid(client) != p->pid)
			continue;

		ret = nvmap_debug_handles_by_pid_show_client(s, client);
		if (ret < 0)
			break;
	}

	mutex_unlock(&nvmap_dev->clients_lock);
	return ret;
}

DEBUGFS_OPEN_FOPS_STATIC(handles_by_pid);

struct dentry *nvmap_debug_create_debugfs_handles_by_pid(
				const char *name,
				struct dentry *parent,
				void *data)
{
	return debugfs_create_file(name, 0444,
			parent, data,
			&debug_handles_by_pid_fops);
}

void nvmap_debug_remove_debugfs_handles_by_pid(struct dentry *d)
{
	debugfs_remove(d);
}

#ifdef NVMAP_CONFIG_PROCRANK
struct procrank_stats {
	struct vm_area_struct *vma;
	u64 pss;
};

static int procrank_pte_entry(pte_t *pte, unsigned long addr, unsigned long end,
		struct mm_walk *walk)
{
	struct procrank_stats *mss = walk->private;
	struct vm_area_struct *vma = mss->vma;
	struct page *page = NULL;
	int mapcount;

	if (pte_present(*pte))
		page = vm_normal_page(vma, addr, *pte);
	else if (is_swap_pte(*pte)) {
		swp_entry_t swpent = pte_to_swp_entry(*pte);

		if (is_migration_entry(swpent))
			page = migration_entry_to_page(swpent);
	}

	if (!page)
		return 0;

	mapcount = nvmap_page_mapcount(page);
	if (mapcount >= 2)
		mss->pss += (PAGE_SIZE << PSS_SHIFT) / mapcount;
	else
		mss->pss += (PAGE_SIZE << PSS_SHIFT);

	return 0;
}

#ifndef PTRACE_MODE_READ_FSCREDS
#define PTRACE_MODE_READ_FSCREDS PTRACE_MODE_READ
#endif

static void nvmap_iovmm_get_client_mss(struct nvmap_client *client, u64 *pss,
				   u64 *total)
{
	struct mm_walk_ops wk_ops = {
		.pte_entry = procrank_pte_entry,
	};
	struct rb_node *n;
	struct nvmap_vma_list *tmp;
	struct procrank_stats mss;
	struct mm_walk procrank_walk = {
		.ops = &wk_ops,
		.private = &mss,
	};
	struct mm_struct *mm;

	memset(&mss, 0, sizeof(mss));
	*pss = *total = 0;

	mm = mm_access(client->task,
			PTRACE_MODE_READ_FSCREDS);
	if (!mm || IS_ERR(mm))
		return;

	nvmap_acquire_mmap_read_lock(mm);
	procrank_walk.mm = mm;

	nvmap_ref_lock(client);
	n = rb_first(&client->handle_refs);
	for (; n != NULL; n = rb_next(n)) {
		struct nvmap_handle_ref *ref =
			rb_entry(n, struct nvmap_handle_ref, node);
		struct nvmap_handle *h = ref->handle;

		if (!h || !h->alloc || !h->heap_pgalloc)
			continue;

		mutex_lock(&h->lock);
		list_for_each_entry(tmp, &h->vmas, list) {
			if (client->task->pid == tmp->pid) {
				mss.vma = tmp->vma;
					walk_page_range(procrank_walk.mm,
						tmp->vma->vm_start,
						tmp->vma->vm_end,
						procrank_walk.ops,
						procrank_walk.private);
			}
		}
		mutex_unlock(&h->lock);
		*total += h->size / atomic_read(&h->share_count);
	}

	nvmap_release_mmap_read_lock(mm);
	mmput(mm);
	*pss = (mss.pss >> PSS_SHIFT);
	nvmap_ref_unlock(client);
}


static int nvmap_debug_iovmm_procrank_show(struct seq_file *s, void *unused)
{
	u64 pss, total;
	struct nvmap_client *client;
	struct nvmap_device *dev = s->private;
	u64 total_memory, total_pss;

	mutex_lock(&dev->clients_lock);
	seq_printf(s, "%-18s %18s %8s %11s %11s\n",
		"CLIENT", "PROCESS", "PID", "PSS", "SIZE");
	list_for_each_entry(client, &dev->clients, list) {
		client_stringify(client, s);
		nvmap_iovmm_get_client_mss(client, &pss, &total);
		seq_printf(s, " %10lluK %10lluK\n", K(pss), K(total));
	}
	mutex_unlock(&dev->clients_lock);

	err = nvmap_get_total_mss(&total_pss, &total_memory, NVMAP_HEAP_IOVMM, NUMA_NO_NODE);
	if (err != 0)
		return err;

	seq_printf(s, "%-18s %18s %8s %10lluK %10lluK\n",
		"total", "", "", K(total_pss), K(total_memory));
	return 0;
}

DEBUGFS_OPEN_FOPS(iovmm_procrank);
#endif /* NVMAP_CONFIG_PROCRANK */

static void nvmap_iovmm_debugfs_init(struct dentry *nvmap_debug_root)
{
	iovmm_debugfs_info = nvmap_create_debugfs_info();
	if (iovmm_debugfs_info != NULL) {
		struct dentry *iovmm_root =
			debugfs_create_dir("iovmm", nvmap_debug_root);

		nvmap_set_debugfs_heap(iovmm_debugfs_info, NVMAP_HEAP_IOVMM);
		nvmap_set_debugfs_numa(iovmm_debugfs_info, NUMA_NO_NODE);

		if (!IS_ERR_OR_NULL(iovmm_root)) {
			debugfs_create_file("clients", 0444, iovmm_root,
				(void *)iovmm_debugfs_info,
				&debug_clients_fops);
			debugfs_create_file("allocations", 0444, iovmm_root,
				(void *)iovmm_debugfs_info,
				&debug_allocations_fops);
			debugfs_create_file("all_allocations", 0444,
				iovmm_root, (void *)iovmm_debugfs_info,
				&debug_all_allocations_fops);
			debugfs_create_file("orphan_handles", 0444,
				iovmm_root, (void *)iovmm_debugfs_info,
				&debug_orphan_handles_fops);
			debugfs_create_file("maps", 0444, iovmm_root,
				(void *)iovmm_debugfs_info,
				&debug_maps_fops);
			debugfs_create_file("free_size", 0444, iovmm_root,
				(void *)iovmm_debugfs_info,
				&debug_free_size_fops);
#ifdef NVMAP_CONFIG_DEBUG_MAPS
			debugfs_create_file("device_list", 0444, iovmm_root,
				(void *)iovmm_debugfs_info,
				&debug_device_list_fops);
#endif

#ifdef NVMAP_CONFIG_PROCRANK
			debugfs_create_file("procrank", 0444, iovmm_root,
				nvmap_dev, &debug_iovmm_procrank_fops);
#endif
		}
	}
}

static void nvmap_iovmm_debugfs_free(void)
{
	nvmap_free_debugfs_info(iovmm_debugfs_info);
}

void nvmap_debug_init(struct dentry **nvmap_debug_root)
{
	u32 max_handle = nvmap_handle_get_max_handle_count();

	*nvmap_debug_root = debugfs_create_dir("nvmap", NULL);
	if (IS_ERR_OR_NULL(*nvmap_debug_root)) {
		pr_err("nvmap_debug: couldn't create debugfs\n");
		nvmap_debug_root = NULL;
		return;
	}

	debugfs_create_u32("max_handle_count", 0444,
				*nvmap_debug_root, &max_handle);

	nvmap_dev->handles_by_pid = debugfs_create_dir("handles_by_pid",
				*nvmap_debug_root);

	debugfs_create_ulong("nvmap_init_time", 0644,
				*nvmap_debug_root, &nvmap_init_time);

	nvmap_iovmm_debugfs_init(*nvmap_debug_root);

#ifdef NVMAP_CONFIG_PAGE_POOLS
	nvmap_page_pool_debugfs_init(*nvmap_debug_root);
#endif

	nvmap_stats_init(*nvmap_debug_root);
}

void nvmap_debug_free(struct dentry *nvmap_debug_root)
{
	if (nvmap_debug_root == NULL)
		return;

	nvmap_iovmm_debugfs_free();
	debugfs_remove_recursive(nvmap_debug_root);
}
