// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2011-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Interface with nvmap carveouts
 */

#include <linux/debugfs.h>
#include <soc/tegra/fuse-helper.h>
#include <linux/rtmutex.h>
#include <linux/slab.h>
#include <linux/nvmap.h>
#include "nvmap_dev.h"
#include "nvmap_alloc.h"
#include "nvmap_handle.h"
#include "nvmap_alloc_int.h"

bool vpr_cpu_access;

extern struct nvmap_device *nvmap_dev;

extern const struct file_operations debug_clients_fops;
extern const struct file_operations debug_allocations_fops;
extern const struct file_operations debug_all_allocations_fops;
extern const struct file_operations debug_orphan_handles_fops;
extern const struct file_operations debug_maps_fops;
#ifdef NVMAP_CONFIG_DEBUG_MAPS
extern const struct file_operations debug_device_list_fops;
#endif /* NVMAP_CONFIG_DEBUG_MAPS */

int nvmap_create_carveout(const struct nvmap_platform_carveout *co)
{
	int i, err = 0, index_free_from = -1, index_free_to = 0;
	struct nvmap_carveout_node **node_ptr;
	struct nvmap_carveout_node *node;

	mutex_lock(&nvmap_dev->carveout_lock);
	if (!nvmap_dev->heaps) {
		int nr_heaps;

		nvmap_dev->nr_carveouts = 0;
		if (nvmap_dev->plat)
			nr_heaps = nvmap_dev->plat->nr_carveouts + 1;
		else
			nr_heaps = 1;

		nvmap_dev->heaps = kcalloc(nr_heaps, sizeof(struct nvmap_carveout_node *),
					GFP_KERNEL);
		if (!nvmap_dev->heaps) {
			err = -ENOMEM;
			pr_err("couldn't allocate carveout memory\n");
			goto out;
		}

		for (i = 0; i < nr_heaps; i++) {
			nvmap_dev->heaps[i] = kzalloc(sizeof(struct nvmap_carveout_node),
						GFP_KERNEL);
			if (!nvmap_dev->heaps[i]) {
				index_free_from = i - 1;
				index_free_to = 0;
				err = -ENOMEM;
				goto free_mem;
			}
		}

		nvmap_dev->nr_heaps = nr_heaps;
	} else if (nvmap_dev->nr_carveouts >= nvmap_dev->nr_heaps) {
		node_ptr = krealloc(nvmap_dev->heaps,
				sizeof(struct nvmap_carveout_node *) *
				(nvmap_dev->nr_carveouts + 1),
				GFP_KERNEL);
		if (node_ptr == NULL) {
			err = -ENOMEM;
			pr_err("nvmap heap array resize failed\n");
			goto out;
		}

		nvmap_dev->heaps = node_ptr;
		for (i = nvmap_dev->nr_heaps; i < nvmap_dev->nr_carveouts + 1; i++) {
			nvmap_dev->heaps[i] = kzalloc(sizeof(struct nvmap_carveout_node),
						GFP_KERNEL);
			if (!nvmap_dev->heaps[i]) {
				index_free_from = i - 1;
				index_free_to = nvmap_dev->nr_heaps;
				err = -ENOMEM;
				goto free_mem;
			}
		}

		nvmap_dev->nr_heaps = nvmap_dev->nr_carveouts + 1;
	}

	for (i = 0; i < nvmap_dev->nr_heaps; i++)
		if ((co->usage_mask != NVMAP_HEAP_CARVEOUT_IVM &&
			co->usage_mask != NVMAP_HEAP_CARVEOUT_GPU &&
			co->usage_mask != NVMAP_HEAP_CARVEOUT_VPR) &&
			(nvmap_get_heap_bit(nvmap_dev->heaps[i]) & co->usage_mask)) {
			pr_err("carveout %s already exists\n", co->name);
			err = -EEXIST;
			goto out;
		}

	node = nvmap_dev->heaps[nvmap_dev->nr_carveouts];

	node->base = round_up(co->base, PAGE_SIZE);
	node->size = round_down(co->size -
				(node->base - co->base), PAGE_SIZE);
	if (co->size == 0)
		goto out;

	node->carveout = nvmap_heap_create(
			nvmap_dev->dev_user.this_device, co,
			node->base, node->size, node);

	if (!node->carveout) {
		err = -ENOMEM;
		pr_err("couldn't create %s\n", co->name);
		goto out;
	}
	node->index = nvmap_dev->nr_carveouts;
	nvmap_dev->nr_carveouts++;
	node->heap_bit = co->usage_mask;

	if (!IS_ERR_OR_NULL(nvmap_dev->debug_root)) {
		struct dentry *heap_root =
			debugfs_create_dir(co->name, nvmap_dev->debug_root);
		struct debugfs_info *carevout_debugfs_info = node->carveout->carevout_debugfs_info;

		carevout_debugfs_info->heap_bit = node->heap_bit;
		carevout_debugfs_info->numa_id = node->carveout->numa_node_id;

		if (!IS_ERR_OR_NULL(heap_root)) {
			debugfs_create_file("clients", S_IRUGO,
				heap_root,
				(void *)carevout_debugfs_info,
				&debug_clients_fops);
			debugfs_create_file("allocations", S_IRUGO,
				heap_root,
				(void *)carevout_debugfs_info,
				&debug_allocations_fops);
			debugfs_create_file("all_allocations", S_IRUGO,
				heap_root,
				(void *)carevout_debugfs_info,
				&debug_all_allocations_fops);
			debugfs_create_file("orphan_handles", S_IRUGO,
				heap_root,
				(void *)carevout_debugfs_info,
				&debug_orphan_handles_fops);
			debugfs_create_file("maps", S_IRUGO,
				heap_root,
				(void *)carevout_debugfs_info,
				&debug_maps_fops);
			debugfs_create_bool("no_cpu_access", S_IRUGO,
				heap_root, (bool *)&co->no_cpu_access);
#ifdef NVMAP_CONFIG_DEBUG_MAPS
			debugfs_create_file("device_list", S_IRUGO,
				heap_root,
				(void *)carevout_debugfs_info,
				&debug_device_list_fops);
#endif /* NVMAP_CONFIG_DEBUG_MAPS */
			nvmap_heap_debugfs_init(heap_root,
						node->carveout);
			if (!tegra_platform_is_silicon() && node->heap_bit == NVMAP_HEAP_CARVEOUT_VPR)
				debugfs_create_bool("allow_cpu_access", S_IRUGO | S_IWUGO,
					heap_root, (bool *)&vpr_cpu_access);

		}
	}
out:
	mutex_unlock(&nvmap_dev->carveout_lock);
	return err;

free_mem:
	for (i = index_free_from; i >= index_free_to; i--)
		kfree(nvmap_dev->heaps[i]);

	if (index_free_to == 0)
		kfree(nvmap_dev->heaps);

	mutex_unlock(&nvmap_dev->carveout_lock);
	return err;
}

static
struct nvmap_heap_block *do_nvmap_carveout_alloc(struct nvmap_client *client,
					      struct nvmap_handle *handle,
					      unsigned long type,
					      phys_addr_t *start)
{
	struct nvmap_carveout_node *co_heap;
	struct nvmap_device *dev = nvmap_dev;
	struct nvmap_heap_block *block = NULL;
	int i;

	for (i = 0; i < dev->nr_carveouts; i++) {
		co_heap = dev->heaps[i];

		if (!(co_heap->heap_bit & type))
			continue;

		if (type & NVMAP_HEAP_CARVEOUT_IVM)
			handle->size = ALIGN(handle->size, NVMAP_IVM_ALIGNMENT);

		/*
		 * When NUMA_NO_NODE is specified, iterate all carveouts with same heap_bit
		 * and different numa nid. Else, specific numa nid is specified, then allocate
		 * only from that particular carveout on given numa node.
		 */
		if (handle->numa_id == NUMA_NO_NODE) {
			block = nvmap_heap_alloc(co_heap->carveout, handle, start);
			if (!block)
				continue;
			goto exit;
		} else {
			if (handle->numa_id != co_heap->carveout->numa_node_id)
				continue;
			block = nvmap_heap_alloc(co_heap->carveout, handle, start);
			/* Currently, all IVM carveouts are on numa node 0 and having same
			 * usage_mask, hence if allocation fails from one IVM carveout, it
			 * should try on next carveout.
			 */
			if (!block && (type & NVMAP_HEAP_CARVEOUT_IVM))
				continue;
			goto exit;
		}
	}

exit:
	return block;
}

struct nvmap_heap_block *nvmap_carveout_alloc(struct nvmap_client *client,
					      struct nvmap_handle *handle,
					      unsigned long type,
					      phys_addr_t *start)
{
	return do_nvmap_carveout_alloc(client, handle, type, start);
}
