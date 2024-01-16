// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2024, NVIDIA Corporation.
 */

#include <linux/device.h>
#include <linux/kref.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pid.h>
#include <linux/slab.h>

#include "context.h"
#include "dev.h"

int host1x_memory_context_list_init(struct host1x *host1x)
{
	struct host1x_memory_context_list *cdl = &host1x->context_list;
	struct device_node *node = host1x->dev->of_node;
	struct host1x_hw_memory_context *ctx;
	unsigned int i;
	int err;

	cdl->devs = NULL;
	cdl->len = 0;
	mutex_init(&cdl->lock);

	err = of_property_count_u32_elems(node, "iommu-map");
	if (err < 0)
		return 0;

	cdl->len = err / 4;
	cdl->devs = kcalloc(cdl->len, sizeof(*cdl->devs), GFP_KERNEL);
	if (!cdl->devs)
		return -ENOMEM;

	for (i = 0; i < cdl->len; i++) {
		struct iommu_fwspec *fwspec;

		ctx = &cdl->devs[i];

		ctx->host = host1x;

		device_initialize(&ctx->dev);

		/*
		 * Due to an issue with T194 NVENC, only 38 bits can be used.
		 * Anyway, 256GiB of IOVA ought to be enough for anyone.
		 */
		ctx->dma_mask = DMA_BIT_MASK(38);
		ctx->dev.dma_mask = &ctx->dma_mask;
		ctx->dev.coherent_dma_mask = ctx->dma_mask;
		dev_set_name(&ctx->dev, "host1x-ctx.%d", i);
		ctx->dev.bus = &host1x_context_device_bus_type;
		ctx->dev.parent = host1x->dev;

		dma_set_max_seg_size(&ctx->dev, UINT_MAX);

		err = device_add(&ctx->dev);
		if (err) {
			dev_err(host1x->dev, "could not add context device %d: %d\n", i, err);
			goto del_devices;
		}

		err = of_dma_configure_id(&ctx->dev, node, true, &i);
		if (err) {
			dev_err(host1x->dev, "IOMMU configuration failed for context device %d: %d\n",
				i, err);
			device_del(&ctx->dev);
			goto del_devices;
		}

		fwspec = dev_iommu_fwspec_get(&ctx->dev);
		if (!fwspec || !device_iommu_mapped(&ctx->dev)) {
			dev_err(host1x->dev, "Context device %d has no IOMMU!\n", i);
			device_del(&ctx->dev);
			goto del_devices;
		}

		ctx->stream_id = fwspec->ids[0] & 0xffff;
	}

	return 0;

del_devices:
	while (i--)
		device_del(&cdl->devs[i].dev);

	kfree(cdl->devs);
	cdl->len = 0;

	return err;
}

void host1x_memory_context_list_free(struct host1x_memory_context_list *cdl)
{
	unsigned int i;

	for (i = 0; i < cdl->len; i++)
		device_del(&cdl->devs[i].dev);

	kfree(cdl->devs);
	cdl->len = 0;
}

static struct host1x_hw_memory_context *host1x_memory_context_alloc_hw_locked(struct host1x *host1x,
							  struct device *dev,
							  struct pid *pid)
{
	struct host1x_memory_context_list *cdl = &host1x->context_list;
	struct host1x_hw_memory_context *free = NULL, *can_steal = NULL;
	struct host1x_memory_context *ctx;
	int i;

	if (!cdl->len)
		return ERR_PTR(-EOPNOTSUPP);

	for (i = 0; i < cdl->len; i++) {
		struct host1x_hw_memory_context *cd = &cdl->devs[i];

		if (cd->dev.iommu->iommu_dev != dev->iommu->iommu_dev)
			continue;

		if (cd->owner == pid) {
			refcount_inc(&cd->ref);
			return cd;
		} else if (!cd->owner && !free) {
			free = cd;
		} else if (!cd->active) {
			can_steal = cd;
		}
	}

	if (free)
		goto found;

	/* Steal */

	if (!can_steal) {
		dev_warn(dev, "all context devices are busy\n");
		return ERR_PTR(-EBUSY);
	}

	list_for_each_entry(ctx, &can_steal->owners, entry) {
		struct host1x_context_mapping *mapping;

		ctx->hw = NULL;
		ctx->context_dev = NULL;

		list_for_each_entry(mapping, &ctx->mappings, entry) {
			host1x_bo_unpin(mapping->mapping);
			mapping->mapping = NULL;
		}
	}

	put_pid(can_steal->owner);

	free = can_steal;

found:
	refcount_set(&free->ref, 1);
	free->owner = get_pid(pid);
	INIT_LIST_HEAD(&free->owners);

	return free;
}

static void host1x_memory_context_hw_put(struct host1x_hw_memory_context *cd)
{
	if (refcount_dec_and_test(&cd->ref)) {
		put_pid(cd->owner);
		cd->owner = NULL;
	}
}

struct host1x_memory_context *host1x_memory_context_alloc(
	struct host1x *host1x, struct device *dev, struct pid *pid)
{
	struct host1x_memory_context_list *cdl = &host1x->context_list;
	struct host1x_memory_context *ctx;

	if (!cdl->len)
		return ERR_PTR(-EOPNOTSUPP);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->host = host1x;
	ctx->dev = dev;
	ctx->pid = get_pid(pid);

	refcount_set(&ctx->ref, 1);
	INIT_LIST_HEAD(&ctx->mappings);

	return ctx;
}
EXPORT_SYMBOL_GPL(host1x_memory_context_alloc);

int host1x_memory_context_active(struct host1x_memory_context *ctx)
{
	struct host1x_memory_context_list *cdl = &ctx->host->context_list;
	struct host1x_context_mapping *mapping;
	struct host1x_hw_memory_context *hw;
	int err = 0;

	mutex_lock(&cdl->lock);

	if (!ctx->hw) {
		hw = host1x_memory_context_alloc_hw_locked(ctx->host, ctx->dev, ctx->pid);
		if (IS_ERR(hw)) {
			err = PTR_ERR(hw);
			goto unlock;
		}

		ctx->hw = hw;
		ctx->context_dev = &hw->dev;
		list_add(&ctx->entry, &hw->owners);

		list_for_each_entry(mapping, &ctx->mappings, entry) {
			mapping->mapping = host1x_bo_pin(
				&hw->dev, mapping->bo, mapping->direction, NULL);
			if (IS_ERR(mapping->mapping)) {
				err = PTR_ERR(mapping->mapping);
				mapping->mapping = NULL;
				goto unpin;
			}
		}
	}

	ctx->hw->active++;

	mutex_unlock(&cdl->lock);

	return 0;

unpin:
	list_for_each_entry(mapping, &ctx->mappings, entry) {
		if (mapping->mapping)
			host1x_bo_unpin(mapping->mapping);
	}

	host1x_memory_context_hw_put(ctx->hw);
	list_del(&ctx->entry);
	ctx->hw = NULL;
unlock:
	mutex_unlock(&cdl->lock);
	return err;
}
EXPORT_SYMBOL_GPL(host1x_memory_context_active);

struct host1x_context_mapping *host1x_memory_context_map(
	struct host1x_memory_context *ctx, struct host1x_bo *bo, enum dma_data_direction direction)
{
	struct host1x_memory_context_list *cdl = &ctx->host->context_list;
	struct host1x_context_mapping *m;
	struct host1x_bo_mapping *bo_m;

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return ERR_PTR(-ENOMEM);

	m->host = ctx->host;
	m->bo = bo;
	m->direction = direction;

	mutex_lock(&cdl->lock);

	if (ctx->hw) {
		bo_m = host1x_bo_pin(&ctx->hw->dev, bo, direction, NULL);
		if (IS_ERR(bo_m)) {
			mutex_unlock(&cdl->lock);
			kfree(m);

			return ERR_CAST(bo_m);
		}

		m->mapping = bo_m;
	}

	list_add(&m->entry, &ctx->mappings);

	mutex_unlock(&cdl->lock);

	return m;
}
EXPORT_SYMBOL_GPL(host1x_memory_context_map);

void host1x_memory_context_unmap(struct host1x_context_mapping *m)
{
	struct host1x_memory_context_list *cdl = &m->host->context_list;

	mutex_lock(&cdl->lock);

	list_del(&m->entry);

	mutex_unlock(&cdl->lock);

	if (m->mapping)
		host1x_bo_unpin(m->mapping);

	kfree(m);
}
EXPORT_SYMBOL_GPL(host1x_memory_context_unmap);

void host1x_memory_context_inactive(struct host1x_memory_context *ctx)
{
	struct host1x_memory_context_list *cdl = &ctx->host->context_list;

	mutex_lock(&cdl->lock);

	ctx->hw->active--;

	mutex_unlock(&cdl->lock);
}
EXPORT_SYMBOL_GPL(host1x_memory_context_inactive);

void host1x_memory_context_get(struct host1x_memory_context *ctx)
{
	refcount_inc(&ctx->ref);
}
EXPORT_SYMBOL_GPL(host1x_memory_context_get);

void host1x_memory_context_put(struct host1x_memory_context *ctx)
{
	struct host1x_memory_context_list *cdl = &ctx->host->context_list;

	if (refcount_dec_and_mutex_lock(&ctx->ref, &cdl->lock)) {
		if (ctx->hw) {
			list_del(&ctx->entry);

			host1x_memory_context_hw_put(ctx->hw);
			ctx->hw = NULL;

			WARN_ON(!list_empty(&ctx->mappings));
		}

		put_pid(ctx->pid);
		mutex_unlock(&cdl->lock);
		kfree(ctx);
	}
}
EXPORT_SYMBOL_GPL(host1x_memory_context_put);
