// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2025, NVIDIA Corporation.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pid.h>
#include <linux/slab.h>

#include "context.h"
#include "dev.h"

static bool static_context_alloc;
module_param(static_context_alloc, bool, 0644);
MODULE_PARM_DESC(static_context_alloc, "If enabled, memory contexts are allocated immediately on channel open and cannot be relinquished while a channel is open");

static void host1x_memory_context_release(struct device *dev)
{
	/* context device is freed in host1x_memory_context_list_free() */
}

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
	INIT_LIST_HEAD(&cdl->waiters);

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
		dev_set_name(&ctx->dev, "%s.host1x-ctx.%d", dev_name(host1x->dev), i);
		ctx->dev.bus = &host1x_context_device_bus_type;
		ctx->dev.parent = host1x->dev;
		ctx->dev.release = host1x_memory_context_release;

		ctx->dev.dma_parms = &ctx->dma_parms;
		dma_set_max_seg_size(&ctx->dev, UINT_MAX);

		err = device_add(&ctx->dev);
		if (err) {
			dev_err(host1x->dev, "could not add context device %d: %d\n", i, err);
			put_device(&ctx->dev);
			goto unreg_devices;
		}

		err = of_dma_configure_id(&ctx->dev, node, true, &i);
		if (err) {
			dev_err(host1x->dev, "IOMMU configuration failed for context device %d: %d\n",
				i, err);
			device_unregister(&ctx->dev);
			goto unreg_devices;
		}

		fwspec = dev_iommu_fwspec_get(&ctx->dev);
		if (!fwspec || !device_iommu_mapped(&ctx->dev)) {
			dev_err(host1x->dev, "Context device %d has no IOMMU!\n", i);
			device_unregister(&ctx->dev);

			/*
			 * This means that if IOMMU is disabled but context devices
			 * are defined in the device tree, Host1x will fail to probe.
			 * That's probably OK in this time and age.
			 */
			err = -EINVAL;

			goto unreg_devices;
		}

		ctx->stream_id = fwspec->ids[0] & 0xffff;
	}

	return 0;

unreg_devices:
	while (i--)
		device_unregister(&cdl->devs[i].dev);

	kfree(cdl->devs);
	cdl->len = 0;

	return err;
}

void host1x_memory_context_list_free(struct host1x_memory_context_list *cdl)
{
	unsigned int i;

	for (i = 0; i < cdl->len; i++)
		device_unregister(&cdl->devs[i].dev);

	kfree(cdl->devs);
	cdl->len = 0;
}

static bool hw_usable_for_dev(struct host1x_hw_memory_context *hw, struct device *dev)
{
	return hw->dev.iommu->iommu_dev == dev->iommu->iommu_dev;
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

		if (!hw_usable_for_dev(cd, dev))
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

	if (!can_steal || static_context_alloc)
		return ERR_PTR(-EBUSY);

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
	int err;

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

	if (static_context_alloc) {
		err = host1x_memory_context_active(ctx);
		if (err) {
			kfree(ctx);
			return ERR_PTR(err);
		}

		ctx->static_alloc = true;
	}

	return ctx;
}
EXPORT_SYMBOL_GPL(host1x_memory_context_alloc);

struct hw_alloc_waiter {
	struct completion wait; /* Completion to wait for free hw context */
	struct list_head entry;
	struct device *dev;
};

int host1x_memory_context_active(struct host1x_memory_context *ctx)
{
	struct host1x_memory_context_list *cdl = &ctx->host->context_list;
	struct host1x_context_mapping *mapping;
	struct host1x_hw_memory_context *hw;
	struct hw_alloc_waiter waiter;
	bool retrying = false;
	int err = 0;

	mutex_lock(&cdl->lock);

retry:
	if (!ctx->hw) {
		hw = host1x_memory_context_alloc_hw_locked(ctx->host, ctx->dev, ctx->pid);
		if (PTR_ERR(hw) == -EBUSY) {
			/* All contexts busy. Wait for free context. */
			if (static_context_alloc) {
				dev_warn(ctx->dev, "%s: all memory contexts are busy\n", current->comm);
				err = -EBUSY;
				goto unlock;
			}
			if (!retrying)
				dev_warn(ctx->dev, "%s: all memory contexts are busy, waiting\n",
					current->comm);

			init_completion(&waiter.wait);
			waiter.dev = ctx->dev;
			list_add(&waiter.entry, &cdl->waiters);

			mutex_unlock(&cdl->lock);
			err = wait_for_completion_interruptible(&waiter.wait);
			mutex_lock(&cdl->lock);

			list_del(&waiter.entry);
			if (err)
				goto unlock;

			retrying = true;
			goto retry;
		}
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

static void host1x_memory_context_inactive_locked(struct host1x_memory_context *ctx)
{
	struct host1x_memory_context_list *cdl = &ctx->host->context_list;
	struct hw_alloc_waiter *waiter;

	if (--ctx->hw->active == 0) {
		/* Hardware context becomes eligible for stealing */
		list_for_each_entry(waiter, &cdl->waiters, entry) {
			if (!hw_usable_for_dev(ctx->hw, waiter->dev))
				continue;

			complete(&waiter->wait);

			/*
			 * Need to wake up all waiters -- there could be multiple from
			 * the same process that can use the same freed hardware context.
			 */
		}
	}
}

void host1x_memory_context_inactive(struct host1x_memory_context *ctx)
{
	struct host1x_memory_context_list *cdl = &ctx->host->context_list;

	mutex_lock(&cdl->lock);

	host1x_memory_context_inactive_locked(ctx);

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
		if (ctx->static_alloc)
			host1x_memory_context_inactive_locked(ctx);

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
