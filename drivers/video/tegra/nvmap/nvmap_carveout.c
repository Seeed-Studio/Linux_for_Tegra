// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Interface with nvmap carveouts
 */

#include <linux/debugfs.h>

#include "nvmap_priv.h"

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
	int i, err = 0;
	struct nvmap_carveout_node *node;

	mutex_lock(&nvmap_dev->carveout_lock);
	if (!nvmap_dev->heaps) {
		int nr_heaps;

		nvmap_dev->nr_carveouts = 0;
		if (nvmap_dev->plat)
			nr_heaps = nvmap_dev->plat->nr_carveouts + 1;
		else
			nr_heaps = 1;
		nvmap_dev->heaps = kzalloc(sizeof(struct nvmap_carveout_node) *
				     nr_heaps, GFP_KERNEL);
		if (!nvmap_dev->heaps) {
			err = -ENOMEM;
			pr_err("couldn't allocate carveout memory\n");
			goto out;
		}
		nvmap_dev->nr_heaps = nr_heaps;
	} else if (nvmap_dev->nr_carveouts >= nvmap_dev->nr_heaps) {
		node = krealloc(nvmap_dev->heaps,
				sizeof(*node) * (nvmap_dev->nr_carveouts + 1),
				GFP_KERNEL);
		if (!node) {
			err = -ENOMEM;
			pr_err("nvmap heap array resize failed\n");
			goto out;
		}
		nvmap_dev->heaps = node;
		nvmap_dev->nr_heaps = nvmap_dev->nr_carveouts + 1;
	}

	for (i = 0; i < nvmap_dev->nr_heaps; i++)
		if ((co->usage_mask != NVMAP_HEAP_CARVEOUT_IVM &&
			co->usage_mask != NVMAP_HEAP_CARVEOUT_GPU) &&
		    (nvmap_dev->heaps[i].heap_bit & co->usage_mask)) {
			pr_err("carveout %s already exists\n", co->name);
			err = -EEXIST;
			goto out;
		}

	node = &nvmap_dev->heaps[nvmap_dev->nr_carveouts];

	node->base = round_up(co->base, PAGE_SIZE);
	node->size = round_down(co->size -
				(node->base - co->base), PAGE_SIZE);
	if (!co->size)
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
		}
	}
out:
	mutex_unlock(&nvmap_dev->carveout_lock);
	return err;
}

#ifdef NVMAP_CONFIG_DEBUG_MAPS
struct nvmap_device_list *nvmap_is_device_present(char *device_name, u32 heap_type)
{
	struct rb_node *node = NULL;
	int i;

	if (heap_type == NVMAP_HEAP_IOVMM) {
		node = nvmap_dev->device_names.rb_node;
	} else {
		for (i = 0; i < nvmap_dev->nr_carveouts; i++) {
			if ((heap_type & nvmap_dev->heaps[i].heap_bit) &&
					nvmap_dev->heaps[i].carveout) {
				node = nvmap_dev->heaps[i].carveout->device_names.rb_node;
				break;
			}
		}
	}
	while (node) {
		struct nvmap_device_list *dl = container_of(node,
						struct nvmap_device_list, node);
		if (strcmp(dl->device_name, device_name) > 0)
			node = node->rb_left;
		else if (strcmp(dl->device_name, device_name) < 0)
			node = node->rb_right;
		else
			return dl;
	}
	return NULL;
}

void nvmap_add_device_name(char *device_name, u64 dma_mask, u32 heap_type)
{
	struct rb_root *root = NULL;
	struct rb_node **new = NULL, *parent = NULL;
	struct nvmap_device_list *dl = NULL;
	int i;

	if (heap_type == NVMAP_HEAP_IOVMM) {
		root = &nvmap_dev->device_names;
	} else {
		for (i = 0; i < nvmap_dev->nr_carveouts; i++) {
			if ((heap_type & nvmap_dev->heaps[i].heap_bit) &&
				nvmap_dev->heaps[i].carveout) {
				root = &nvmap_dev->heaps[i].carveout->device_names;
				break;
			}
		}
	}
	if (root) {
		new = &(root->rb_node);
		while (*new) {
			dl = container_of(*new, struct nvmap_device_list, node);
			parent = *new;
			if (strcmp(dl->device_name, device_name) > 0)
				new = &((*new)->rb_left);
			else if (strcmp(dl->device_name, device_name) < 0)
				new = &((*new)->rb_right);
		}
		dl = kzalloc(sizeof(*dl), GFP_KERNEL);
		if (!dl)
			return;
		dl->device_name = kzalloc(strlen(device_name) + 1, GFP_KERNEL);
		if (!dl->device_name)
			return;
		strcpy(dl->device_name, device_name);
		dl->dma_mask = dma_mask;
		rb_link_node(&dl->node, parent, new);
		rb_insert_color(&dl->node, root);
	}
}

void nvmap_remove_device_name(char *device_name, u32 heap_type)
{
	struct nvmap_device_list *dl = NULL;
	int i;

	dl = nvmap_is_device_present(device_name, heap_type);
	if (dl) {
		if (heap_type == NVMAP_HEAP_IOVMM) {
			rb_erase(&dl->node,
				&nvmap_dev->device_names);
			kfree(dl->device_name);
			kfree(dl);
			return;
		}
		for (i = 0; i < nvmap_dev->nr_carveouts; i++) {
			if ((heap_type & nvmap_dev->heaps[i].heap_bit) &&
				nvmap_dev->heaps[i].carveout) {
				rb_erase(&dl->node,
					&nvmap_dev->heaps[i].carveout->device_names);
				kfree(dl->device_name);
				kfree(dl);
				return;
			}
		}
	}
}
#endif /* NVMAP_CONFIG_DEBUG_MAPS */

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
		co_heap = &dev->heaps[i];

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
