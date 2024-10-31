// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020-2025 NVIDIA Corporation */

#include <linux/dma-buf.h>
#include <linux/host1x-next.h>
#include <linux/iommu.h>
#include <linux/list.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_utils.h>

#include "drm.h"
#include "uapi.h"

static bool explicit_syncpt_free;
module_param(explicit_syncpt_free, bool, 0644);
MODULE_PARM_DESC(explicit_syncpt_free, "If enabled, syncpoints need to be explicitly freed via IOCTL or they will be left dangling when the fd is closed");

static void tegra_drm_mapping_release(struct kref *ref)
{
	struct tegra_drm_mapping *mapping =
		container_of(ref, struct tegra_drm_mapping, ref);

	if (mapping->ctx_map)
		host1x_memory_context_unmap(mapping->ctx_map);
	else
		host1x_bo_unpin(mapping->bo_map);

	host1x_bo_put(mapping->bo);

	kfree(mapping);
}

void tegra_drm_mapping_put(struct tegra_drm_mapping *mapping)
{
	kref_put(&mapping->ref, tegra_drm_mapping_release);
}

static void tegra_drm_channel_context_close(struct tegra_drm_context *context)
{
	struct tegra_drm_mapping *mapping;
	unsigned long id;

	xa_for_each(&context->mappings, id, mapping)
		tegra_drm_mapping_put(mapping);

	if (context->memory_context)
		host1x_memory_context_put(context->memory_context);

	xa_destroy(&context->mappings);

	host1x_channel_put(context->channel);

	kfree(context);
}

void tegra_drm_uapi_close_file(struct tegra_drm_file *file)
{
	struct tegra_drm_context *context;
	struct host1x_syncpt *sp;
	unsigned long id;

	xa_for_each(&file->contexts, id, context)
		tegra_drm_channel_context_close(context);

	/*
	 * If explicit_syncpt_free is enabled, users must free syncpoints
	 * explicitly or they will be left dangling. This prevents syncpoints
	 * from getting in an unexpected state if e.g. the application crashes.
	 * Obviously only usable on particularly locked down configurations.
	 */
	if (!explicit_syncpt_free) {
		xa_for_each(&file->syncpoints, id, sp)
			host1x_syncpt_put(sp);
	}

	xa_destroy(&file->contexts);
	xa_destroy(&file->syncpoints);
}

static struct tegra_drm_client *tegra_drm_find_client(struct tegra_drm *tegra, u32 class)
{
	struct tegra_drm_client *client;

	list_for_each_entry(client, &tegra->clients, list)
		if (client->base.class == class)
			return client;

	return NULL;
}

int tegra_drm_ioctl_channel_open(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct host1x *host = tegra_drm_to_host1x(drm->dev_private);
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct tegra_drm *tegra = drm->dev_private;
	struct drm_tegra_channel_open *args = data;
	struct tegra_drm_client *client = NULL;
	struct tegra_drm_context *context;
	int err;

	if (args->flags)
		return -EINVAL;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	client = tegra_drm_find_client(tegra, args->host1x_class);
	if (!client) {
		err = -ENODEV;
		goto free;
	}

	if (client->shared_channel) {
		context->channel = host1x_channel_get(client->shared_channel);
	} else {
		context->channel = host1x_channel_request(&client->base);
		if (!context->channel) {
			err = -EBUSY;
			goto free;
		}
	}

	/* Only allocate context if the engine supports context isolation. */
	if (device_iommu_mapped(client->base.dev) && client->ops->can_use_memory_ctx) {
		bool supported;

		err = client->ops->can_use_memory_ctx(client, &supported);
		if (err)
			goto put_channel;

		if (supported)
			context->memory_context = host1x_memory_context_alloc(
				host, client->base.dev, get_task_pid(current, PIDTYPE_TGID));

		if (IS_ERR(context->memory_context)) {
			if (PTR_ERR(context->memory_context) != -EOPNOTSUPP) {
				err = PTR_ERR(context->memory_context);
				goto put_channel;
			} else {
				/*
				 * OK, HW does not support contexts or contexts
				 * are disabled.
				 */
				context->memory_context = NULL;
			}
		}
	}

	err = xa_alloc(&fpriv->contexts, &args->context, context, XA_LIMIT(1, U32_MAX),
		       GFP_KERNEL);
	if (err < 0)
		goto put_memctx;

	context->client = client;
	xa_init_flags(&context->mappings, XA_FLAGS_ALLOC1);

	args->version = client->version;
	args->capabilities = 0;

	if (device_get_dma_attr(client->base.dev) == DEV_DMA_COHERENT)
		args->capabilities |= DRM_TEGRA_CHANNEL_CAP_CACHE_COHERENT;

	return 0;

put_memctx:
	if (context->memory_context)
		host1x_memory_context_put(context->memory_context);
put_channel:
	host1x_channel_put(context->channel);
free:
	kfree(context);

	return err;
}

int tegra_drm_ioctl_channel_close(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_channel_close *args = data;
	struct tegra_drm_context *context;

	mutex_lock(&fpriv->lock);

	context = xa_load(&fpriv->contexts, args->context);
	if (!context) {
		mutex_unlock(&fpriv->lock);
		return -EINVAL;
	}

	xa_erase(&fpriv->contexts, args->context);

	mutex_unlock(&fpriv->lock);

	tegra_drm_channel_context_close(context);

	return 0;
}

int tegra_drm_ioctl_channel_map(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_channel_map *args = data;
	struct tegra_drm_mapping *mapping;
	struct tegra_drm_context *context;
	enum dma_data_direction direction;
	int err = 0;

	if (args->flags & ~DRM_TEGRA_CHANNEL_MAP_READ_WRITE)
		return -EINVAL;

	mutex_lock(&fpriv->lock);

	context = xa_load(&fpriv->contexts, args->context);
	if (!context) {
		mutex_unlock(&fpriv->lock);
		return -EINVAL;
	}

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		err = -ENOMEM;
		goto unlock;
	}

	kref_init(&mapping->ref);

	mapping->bo = tegra_gem_lookup(file, args->handle);
	if (!mapping->bo) {
		err = -EINVAL;
		goto free;
	}

	switch (args->flags & DRM_TEGRA_CHANNEL_MAP_READ_WRITE) {
	case DRM_TEGRA_CHANNEL_MAP_READ_WRITE:
		direction = DMA_BIDIRECTIONAL;
		break;

	case DRM_TEGRA_CHANNEL_MAP_WRITE:
		direction = DMA_FROM_DEVICE;
		break;

	case DRM_TEGRA_CHANNEL_MAP_READ:
		direction = DMA_TO_DEVICE;
		break;

	default:
		err = -EINVAL;
		goto put_gem;
	}

	if (context->memory_context) {
		mapping->ctx_map = host1x_memory_context_map(
			context->memory_context, mapping->bo, direction);

		if (IS_ERR(mapping->ctx_map)) {
			err = PTR_ERR(mapping->ctx_map);
			goto put_gem;
		}
	} else {
		mapping->bo_map = host1x_bo_pin(context->client->base.dev,
				mapping->bo, direction, NULL);

		if (IS_ERR(mapping->bo_map)) {
			err = PTR_ERR(mapping->bo_map);
			goto put_gem;
		}

		mapping->iova = mapping->bo_map->phys;
		mapping->iova_end = mapping->iova + host1x_to_tegra_bo(mapping->bo)->gem.size;
	}

	err = xa_alloc(&context->mappings, &args->mapping, mapping, XA_LIMIT(1, U32_MAX),
		       GFP_KERNEL);
	if (err < 0)
		goto unpin;

	mutex_unlock(&fpriv->lock);

	return 0;

unpin:
	if (mapping->ctx_map)
		host1x_memory_context_unmap(mapping->ctx_map);
	else
		host1x_bo_unpin(mapping->bo_map);
put_gem:
	host1x_bo_put(mapping->bo);
free:
	kfree(mapping);
unlock:
	mutex_unlock(&fpriv->lock);
	return err;
}

int tegra_drm_ioctl_channel_unmap(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_channel_unmap *args = data;
	struct tegra_drm_mapping *mapping;
	struct tegra_drm_context *context;

	mutex_lock(&fpriv->lock);

	context = xa_load(&fpriv->contexts, args->context);
	if (!context) {
		mutex_unlock(&fpriv->lock);
		return -EINVAL;
	}

	mapping = xa_erase(&context->mappings, args->mapping);

	mutex_unlock(&fpriv->lock);

	if (!mapping)
		return -EINVAL;

	tegra_drm_mapping_put(mapping);
	return 0;
}

int tegra_drm_ioctl_syncpoint_allocate(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct host1x *host1x = tegra_drm_to_host1x(drm->dev_private);
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_syncpoint_allocate *args = data;
	struct host1x_syncpt *sp;
	int err;

	if (args->id)
		return -EINVAL;

	sp = host1x_syncpt_alloc(host1x, HOST1X_SYNCPT_CLIENT_MANAGED, current->comm);
	if (!sp)
		return -EBUSY;

	args->id = host1x_syncpt_id(sp);

	err = xa_insert(&fpriv->syncpoints, args->id, sp, GFP_KERNEL);
	if (err) {
		host1x_syncpt_put(sp);
		return err;
	}

	return 0;
}

int tegra_drm_ioctl_syncpoint_free(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_syncpoint_allocate *args = data;
	struct host1x_syncpt *sp;

	mutex_lock(&fpriv->lock);
	sp = xa_erase(&fpriv->syncpoints, args->id);
	mutex_unlock(&fpriv->lock);

	if (!sp)
		return -EINVAL;

	host1x_syncpt_put(sp);

	return 0;
}

int tegra_drm_ioctl_syncpoint_wait(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct host1x *host1x = tegra_drm_to_host1x(drm->dev_private);
	struct drm_tegra_syncpoint_wait *args = data;
	signed long timeout_jiffies;
	struct host1x_syncpt *sp;
	ktime_t ts;
	int err;

	if (args->padding != 0)
		return -EINVAL;

	sp = host1x_syncpt_get_by_id_noref(host1x, args->id);
	if (!sp)
		return -EINVAL;

	timeout_jiffies = drm_timeout_abs_to_jiffies(args->timeout_ns);

	err = host1x_syncpt_wait_ts(sp, args->threshold, timeout_jiffies, &args->value, &ts);
	if (err)
		return err;

	args->timestamp = ktime_to_ns(ts);

	return 0;
}

struct tegra_drm_syncpoint_memory_data {
	phys_addr_t base;
	u32 start, length, stride;
	bool readwrite;
	struct host1x *host1x;
};

static struct sg_table *tegra_drm_syncpoint_memory_map_dma_buf(
	struct dma_buf_attachment *attachment, enum dma_data_direction direction)
{
	struct tegra_drm_syncpoint_memory_data *priv = attachment->dmabuf->priv;
	phys_addr_t mem_start = priv->base + priv->stride * priv->start;
	size_t mem_length = priv->stride * priv->length;
	dma_addr_t mem_start_dma;
	struct sg_table *sgt;
	int err;

	if (!priv->readwrite && direction != DMA_TO_DEVICE)
		return ERR_PTR(-EPERM);

	if (!PAGE_ALIGNED(mem_start) || !PAGE_ALIGNED(mem_start + mem_length)) {
		dev_err(attachment->dev, "denied mapping for unaligned syncpoint shim mapping\n");
		return ERR_PTR(-EINVAL);
	}

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	err = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (err)
		goto free_sgt;

	mem_start_dma = dma_map_resource(attachment->dev, mem_start, mem_length, direction,
					 DMA_ATTR_SKIP_CPU_SYNC);
	err = dma_mapping_error(attachment->dev, mem_start_dma);
	if (!mem_start_dma || err)
		goto free_table;

	sg_set_page(sgt->sgl, phys_to_page(mem_start), mem_length, 0);
	sg_dma_address(sgt->sgl) = mem_start_dma;
	sg_dma_len(sgt->sgl) = mem_length;

	return sgt;

free_table:
	sg_free_table(sgt);
free_sgt:
	kfree(sgt);

	return ERR_PTR(err);
}

static void tegra_drm_syncpoint_memory_unmap_dma_buf(
	struct dma_buf_attachment *attachment, struct sg_table *sgt,
	enum dma_data_direction direction)
{
	dma_unmap_resource(attachment->dev, sg_dma_address(sgt->sgl), sg_dma_len(sgt->sgl),
			   direction, DMA_ATTR_SKIP_CPU_SYNC);
	sg_free_table(sgt);
	kfree(sgt);
}

static void tegra_drm_syncpoint_memory_release(struct dma_buf *dma_buf)
{
	struct tegra_drm_syncpoint_memory_data *priv = dma_buf->priv;
	int i;

	if (priv->readwrite) {
		for (i = priv->start; i < priv->start + priv->length; i++) {
			struct host1x_syncpt *sp = host1x_syncpt_get_by_id_noref(priv->host1x, i);

			host1x_syncpt_put(sp);
		}
	}

	kfree(priv);
}

static const struct dma_buf_ops syncpoint_dmabuf_ops = {
	.map_dma_buf = tegra_drm_syncpoint_memory_map_dma_buf,
	.unmap_dma_buf = tegra_drm_syncpoint_memory_unmap_dma_buf,
	.release = tegra_drm_syncpoint_memory_release,
};

int tegra_drm_ioctl_syncpoint_export_memory(struct drm_device *drm, void *data,
					    struct drm_file *file)
{
	struct host1x *host1x = tegra_drm_to_host1x(drm->dev_private);
	struct drm_tegra_syncpoint_export_memory *args = data;
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct tegra_drm_syncpoint_memory_data *priv;
	u32 stride, num_syncpts, end_syncpts_user;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dma_buf;
	phys_addr_t base;
	int err, i;

	if (args->flags & ~DRM_TEGRA_SYNCPOINT_EXPORT_MEMORY_READWRITE)
		return -EINVAL;

	err = host1x_syncpt_get_shim_info(host1x, &base, &stride, &num_syncpts);
	if (err)
		return err;

	if (check_add_overflow(args->start, args->length, &end_syncpts_user))
		return -EINVAL;

	if (end_syncpts_user >= num_syncpts)
		return -EINVAL;

	if (args->length == 0)
		args->length = num_syncpts - end_syncpts_user;

	if (args->flags & DRM_TEGRA_SYNCPOINT_EXPORT_MEMORY_READWRITE) {
		mutex_lock(&fpriv->lock);

		for (i = args->start; i < args->start + args->length; i++) {
			struct host1x_syncpt *sp = xa_load(&fpriv->syncpoints, i);

			if (!sp) {
				mutex_unlock(&fpriv->lock);
				return -EINVAL;
			}

			host1x_syncpt_get(sp);
		}

		mutex_unlock(&fpriv->lock);
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		err = -ENOMEM;
		goto put_syncpts;
	}

	priv->base = base;
	priv->start = args->start;
	priv->length = args->length;
	priv->stride = stride;
	priv->readwrite = (args->flags & DRM_TEGRA_SYNCPOINT_EXPORT_MEMORY_READWRITE);
	priv->host1x = host1x;

	exp_info.ops = &syncpoint_dmabuf_ops;
	exp_info.size = args->length * stride;
	exp_info.flags = O_RDWR;
	exp_info.priv = priv;

	dma_buf = dma_buf_export(&exp_info);
	if (IS_ERR(dma_buf)) {
		err = PTR_ERR(dma_buf);
		goto free_priv;
	}

	args->fd = dma_buf_fd(dma_buf, O_RDWR);
	if (args->fd < 0) {
		err = args->fd;
		goto put_dma_buf;
	}

	args->stride = stride;

	return 0;

put_dma_buf:
	dma_buf_put(dma_buf);

free_priv:
	kfree(priv);

put_syncpts:
	if (args->flags & DRM_TEGRA_SYNCPOINT_EXPORT_MEMORY_READWRITE) {
		for (i = args->start; i < args->start + args->length; i++) {
			struct host1x_syncpt *sp = host1x_syncpt_get_by_id_noref(host1x, i);

			host1x_syncpt_put(sp);
		}
	}

	return err;
}

int tegra_drm_ioctl_syncpoint_increment(struct drm_device *drm, void *data,
					struct drm_file *file)
{
	struct drm_tegra_syncpoint_increment *args = data;
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct host1x_syncpt *sp;
	int err;

	if (args->padding != 0)
		return -EINVAL;

	mutex_lock(&fpriv->lock);
	sp = xa_load(&fpriv->syncpoints, args->id);
	if (!sp) {
		mutex_unlock(&fpriv->lock);
		return -EINVAL;
	}

	err = host1x_syncpt_incr(sp);
	mutex_unlock(&fpriv->lock);

	return err;
}
