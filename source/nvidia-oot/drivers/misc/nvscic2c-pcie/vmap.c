/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */

#define pr_fmt(fmt)	"nvscic2c-pcie: vmap: " fmt

#include "comm-channel.h"
#include "common.h"
#include "descriptor.h"
#include "module.h"
#include "pci-client.h"
#include "vmap.h"
#include "vmap-internal.h"

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/types.h>

/*
 * *_START must be > 0 to avoid interference with idr_for_each().
 */
#define MEMOBJ_START	(1)
#define SYNCOBJ_START	(1)
#define IMPORTOBJ_START	(1)
#define MEMOBJ_END	(MAX_STREAM_MEMOBJS)
#define SYNCOBJ_END	(MAX_STREAM_SYNCOBJS)
#define IMPORTOBJ_END	(MAX_STREAM_MEMOBJS + MAX_STREAM_SYNCOBJS)

static int
match_dmabuf(int id, void *entry, void *data)
{
	struct memobj_map_ref *map = (struct memobj_map_ref *)entry;

	if (map->pin.dmabuf == (struct dma_buf *)data)
		return id;

	/* 0 shall pick-up next entry.*/
	return 0;
}

static int dev_map_limit_check(uint64_t aperture_limit, uint64_t aperture_inuse, size_t map_size)
{
	int ret = 0;
	bool retval = 0;
	uint64_t total_size = 0;

	retval = AddU64(aperture_inuse, map_size, &total_size);
	if (retval == false) {
		pr_err("AddU64 overflow: aperture_inuse, map_size\n");
		return -EINVAL;
	}
	if (total_size > aperture_limit) {
		ret = -ENOMEM;
		pr_err("per endpoint mapping limit exceeded, aperture_inuse: %lld, map_size: %zu\n",
			aperture_inuse, map_size);
		return ret;
	}

	return ret;
}

static void
dma_mem_get_size_exit(struct memobj_pin_t *pin)
{
	if (!(IS_ERR_OR_NULL(pin->sgt))) {
		dma_buf_unmap_attachment(pin->attach, pin->sgt, pin->dir);
		pin->sgt = NULL;
	}

	if (!(IS_ERR_OR_NULL(pin->attach))) {
		dma_buf_detach(pin->dmabuf, pin->attach);
		pin->attach = NULL;
	}
}

static int dma_mem_get_size(struct vmap_ctx_t *vmap_ctx, struct memobj_pin_t *pin, size_t *map_size)
{
	int ret = 0;
	u32 sg_index = 0;
	struct scatterlist *sg = NULL;
	bool retval = 0;
	uint64_t total_size = 0;

	/*
	 * pin to dummy device (which has smmu disabled) to get scatter-list
	 * of phys addr.
	 */
	pin->attach = dma_buf_attach(pin->dmabuf, &vmap_ctx->dummy_pdev->dev);
	if (IS_ERR_OR_NULL(pin->attach)) {
		ret = (int)PTR_ERR(pin->attach);
		pr_err("client_mngd dma_buf_attach failed\n");
		goto fn_exit;
	}
	pin->sgt = dma_buf_map_attachment(pin->attach, pin->dir);
	if (IS_ERR_OR_NULL(pin->sgt)) {
		ret = (int)PTR_ERR(pin->sgt);
		pr_err("client_mngd dma_buf_attachment failed\n");
		goto fn_exit;
	}

	*map_size = 0;
	for_each_sg(pin->sgt->sgl, sg, pin->sgt->nents, sg_index) {
		retval = AddU64(*map_size, sg->length, &total_size);
		if (retval == false) {
			pr_err("AddU64 overflow: map_size, sg->length\n");
			return -EINVAL;
		}
		*map_size = total_size;
	}

fn_exit:
	dma_mem_get_size_exit(pin);
	return ret;
}

static int
memobj_map(struct vmap_ctx_t *vmap_ctx,
	   struct vmap_memobj_map_params *params,
	   struct vmap_obj_attributes *attrib,
	   uint64_t aperture_limit,
	   uint64_t *const aperture_inuse)
{
	int ret = 0;
	s32 id_exist = 0;
	size_t map_size = 0;
	struct memobj_map_ref *map = NULL;
	struct dma_buf *dmabuf = NULL;
	bool retval = 0;
	uint64_t total_size = 0;

	dmabuf = dma_buf_get(params->fd);
	if (IS_ERR_OR_NULL(dmabuf)) {
		pr_err("Failed to get dma_buf for Mem Fd: (%d)\n",
		       params->fd);
		return -EFAULT;
	}

	mutex_lock(&vmap_ctx->mem_idr_lock);

	/* check if the dma_buf is already mapped ? */
	id_exist = idr_for_each(&vmap_ctx->mem_idr, match_dmabuf, &dmabuf);
	if (id_exist > 0)
		map = idr_find(&vmap_ctx->mem_idr, (unsigned long)id_exist);

	if (map) {
		/* already mapped.*/
		/*
		 * requested mapping type != already mapped type.
		 * e.g: mem obj previously mapped with dev mngd and now
		 * as client mngd.
		 */
		if (params->mngd != map->pin.mngd) {
			pr_err("Memobj: Already mapped with another mngd\n");
			ret = -EINVAL;
			goto err;
		}
		/*
		 * add a validation later when rid=sid is enabled, where it
		 * shall be dev_mngd in both case but dev shall be different.
		 */
		kref_get(&map->refcount);
	} else {
		map = kzalloc(sizeof(*map), GFP_KERNEL);
		if (WARN_ON(!map)) {
			ret = -ENOMEM;
			goto err;
		}

		map->vmap_ctx = vmap_ctx;
		kref_init(&map->refcount);
		map->pin.dmabuf = dmabuf;
		map->pin.prot = params->prot;
		map->pin.mngd = params->mngd;
		map->obj_id = idr_alloc(&vmap_ctx->mem_idr, map,
					MEMOBJ_START, MEMOBJ_END,
					GFP_KERNEL);
		if (map->obj_id <= 0) {
			ret = map->obj_id;
			pr_err("Failed to idr alloc for mem obj\n");
			kfree(map);
			goto err;
		}

		if ((map->pin.mngd == VMAP_MNGD_CLIENT) && (aperture_inuse != NULL)) {
			ret = dma_mem_get_size(vmap_ctx, &map->pin, &map_size);
			if (ret != 0) {
				pr_err("Failed in dma buf mem get size\n");
				idr_remove(&vmap_ctx->mem_idr, (unsigned long)map->obj_id);
				kfree(map);
				goto err;
			}
			ret = dev_map_limit_check(aperture_limit, *aperture_inuse, map_size);
			if (ret != 0) {
				pr_err("Failed in aperture limit check\n");
				idr_remove(&vmap_ctx->mem_idr, (unsigned long)map->obj_id);
				kfree(map);
				goto err;
			}
		}

		/* populates map->pin.attrib within.*/
		ret = memobj_pin(vmap_ctx, &map->pin);
		if (ret) {
			pr_err("Failed to pin mem obj fd: (%d)\n", params->fd);
			idr_remove(&vmap_ctx->mem_idr, (unsigned long)map->obj_id);
			kfree(map);
			goto err;
		}
		if ((map->pin.mngd == VMAP_MNGD_CLIENT) && (aperture_inuse != NULL)) {
			retval = AddU64(*aperture_inuse, map->pin.attrib.size, &total_size);
			if (retval == false) {
				pr_err("AddU64 overflow: aperture_inuse, map->pin.attrib.size\n");
				return -EINVAL;
			}
			*aperture_inuse = total_size;
		}
	}

	attrib->type = VMAP_OBJ_TYPE_MEM;
	attrib->id = map->obj_id;
	attrib->iova = map->pin.attrib.iova;
	attrib->size = map->pin.attrib.size;
	attrib->offsetof = map->pin.attrib.offsetof;
err:
	mutex_unlock(&vmap_ctx->mem_idr_lock);
	dma_buf_put(dmabuf); //dma_buf_get()
	return ret;
}

/* must be called with idr lock held.*/
static void
memobj_free(struct kref *kref)
{
	struct memobj_map_ref *map = NULL;

	if (!kref)
		return;

	map = container_of(kref, struct memobj_map_ref, refcount);
	if (map) {
		memobj_unpin(map->vmap_ctx, &map->pin);
		idr_remove(&map->vmap_ctx->mem_idr, (unsigned long)map->obj_id);
		kfree(map);
	}
}

static int
memobj_unmap(struct vmap_ctx_t *vmap_ctx, s32 obj_id)
{
	struct memobj_map_ref *map = NULL;

	mutex_lock(&vmap_ctx->mem_idr_lock);
	map = idr_find(&vmap_ctx->mem_idr, (unsigned long)obj_id);
	if (!map) {
		mutex_unlock(&vmap_ctx->mem_idr_lock);
		return -EBADF;
	}

	kref_put(&map->refcount, memobj_free);
	mutex_unlock(&vmap_ctx->mem_idr_lock);

	return 0;
}

static int
memobj_putref(struct vmap_ctx_t *vmap_ctx, s32 obj_id)
{
	return memobj_unmap(vmap_ctx, obj_id);
}

static int
memobj_getref(struct vmap_ctx_t *vmap_ctx, s32 obj_id)
{
	struct memobj_map_ref *map = NULL;

	mutex_lock(&vmap_ctx->mem_idr_lock);
	map = idr_find(&vmap_ctx->mem_idr, (unsigned long)obj_id);
	if (WARN_ON(!map)) {
		mutex_unlock(&vmap_ctx->mem_idr_lock);
		return -EBADF;
	}

	kref_get(&map->refcount);
	mutex_unlock(&vmap_ctx->mem_idr_lock);

	return 0;
}

static int
match_syncpt_id(int id, void *entry, void *data)
{
	struct syncobj_map_ref *map = (struct syncobj_map_ref *)entry;

	if (map->pin.syncpt_id == *((u32 *)data))
		return id;

	/* 0 shall pick-up next entry.*/
	return 0;
}

static int
syncobj_map(struct vmap_ctx_t *vmap_ctx,
	    struct vmap_syncobj_map_params *params,
	    struct vmap_obj_attributes *attrib,
	    uint64_t aperture_limit,
	    uint64_t *const aperture_inuse)
{
	int ret = 0;
	s32 id_exist = 0;
	u32 syncpt_id = 0;
	struct syncobj_map_ref *map = NULL;
	bool retval = 0;
	uint64_t total_size = 0;

	syncpt_id = params->id;
	mutex_lock(&vmap_ctx->sync_idr_lock);

	/* check if the syncpt is already mapped ? */
	id_exist = idr_for_each(&vmap_ctx->sync_idr, match_syncpt_id,
				&syncpt_id);
	if (id_exist > 0)
		map = idr_find(&vmap_ctx->sync_idr, (unsigned long)id_exist);

	if (map) {
		/* mapping again a SYNC obj(local or remote) is not permitted.*/
		ret = -EPERM;
		goto err;
	} else {
		map = kzalloc(sizeof(*map), GFP_KERNEL);
		if (WARN_ON(!map)) {
			ret = -ENOMEM;
			goto err;
		}

		map->vmap_ctx = vmap_ctx;
		kref_init(&map->refcount);
		map->obj_id = idr_alloc(&vmap_ctx->sync_idr, map,
					SYNCOBJ_START, SYNCOBJ_END,
					GFP_KERNEL);
		if (map->obj_id <= 0) {
			ret = map->obj_id;
			pr_err("Failed to idr alloc for sync obj\n");
			kfree(map);
			goto err;
		}

		if ((params->mngd == VMAP_MNGD_CLIENT) && (aperture_inuse != NULL)) {
			ret = dev_map_limit_check(aperture_limit, *aperture_inuse, SP_MAP_SIZE);
			if (ret != 0) {
				pr_err("Failed in aperture limit check\n");
				idr_remove(&vmap_ctx->sync_idr, (unsigned long)map->obj_id);
				kfree(map);
				goto err;
			}
		}

		/* local syncobjs do not need to be pinned to pcie iova.*/
		map->pin.fd = params->fd;
		map->pin.syncpt_id = syncpt_id;
		map->pin.pin_reqd = params->pin_reqd;
		map->pin.prot = params->prot;
		map->pin.mngd = params->mngd;
		ret = syncobj_pin(vmap_ctx, &map->pin);
		if (ret) {
			pr_err("Failed to pin sync obj Id: (%d)\n",
			       syncpt_id);
			idr_remove(&vmap_ctx->sync_idr, (unsigned long)map->obj_id);
			kfree(map);
			goto err;
		}
		if ((params->mngd == VMAP_MNGD_CLIENT) && (aperture_inuse != NULL)) {
			retval = AddU64(*aperture_inuse, map->pin.attrib.size, &total_size);
			if (retval == false) {
				pr_err("AddU64 overflow: aperture_inuse, map->pin.attrib.size\n");
				return -EINVAL;
			}
			*aperture_inuse = total_size;
		}

		attrib->type = VMAP_OBJ_TYPE_SYNC;
		attrib->id = map->obj_id;
		attrib->iova = map->pin.attrib.iova;
		attrib->size = map->pin.attrib.size;
		attrib->offsetof = map->pin.attrib.offsetof;
		attrib->syncpt_id = map->pin.attrib.syncpt_id;
	}
err:
	mutex_unlock(&vmap_ctx->sync_idr_lock);

	return ret;
}

/* must be called with idr lock held.*/
static void
syncobj_free(struct kref *kref)
{
	struct syncobj_map_ref *map = NULL;

	if (!kref)
		return;

	map = container_of(kref, struct syncobj_map_ref, refcount);
	if (map) {
		syncobj_unpin(map->vmap_ctx, &map->pin);
		idr_remove(&map->vmap_ctx->sync_idr, (unsigned long)map->obj_id);
		kfree(map);
	}
}

static int
syncobj_unmap(struct vmap_ctx_t *vmap_ctx, s32 obj_id)
{
	struct syncobj_map_ref *map = NULL;

	mutex_lock(&vmap_ctx->sync_idr_lock);
	map = idr_find(&vmap_ctx->sync_idr, (unsigned long)obj_id);
	if (!map) {
		mutex_unlock(&vmap_ctx->sync_idr_lock);
		return -EBADF;
	}

	kref_put(&map->refcount, syncobj_free);
	mutex_unlock(&vmap_ctx->sync_idr_lock);

	return 0;
}

static int
syncobj_putref(struct vmap_ctx_t *vmap_ctx, s32 obj_id)
{
	return syncobj_unmap(vmap_ctx, obj_id);
}

static int
syncobj_getref(struct vmap_ctx_t *vmap_ctx, s32 obj_id)
{
	struct memobj_map_ref *map = NULL;

	if (!vmap_ctx)
		return -EINVAL;

	mutex_lock(&vmap_ctx->sync_idr_lock);
	map = idr_find(&vmap_ctx->sync_idr, (unsigned long)obj_id);
	if (WARN_ON(!map)) {
		mutex_unlock(&vmap_ctx->sync_idr_lock);
		return -EBADF;
	}

	kref_get(&map->refcount);
	mutex_unlock(&vmap_ctx->sync_idr_lock);

	return 0;
}

static int
match_export_desc(int id, void *entry, void *data)
{
	struct importobj_map_ref *map = (struct importobj_map_ref *)entry;

	if (map->reg.export_desc == *((u64 *)data))
		return id;

	/* 0 shall pick-up next entry.*/
	return 0;
}

static int
importobj_map(struct vmap_ctx_t *vmap_ctx,
	      struct vmap_importobj_map_params *params,
	      struct vmap_obj_attributes *attrib)
{
	int ret = 0;
	s32 id_exist = 0;
	struct importobj_map_ref *map = NULL;

	mutex_lock(&vmap_ctx->import_idr_lock);

	/* check if we have export descriptor from remote already ? */
	id_exist = idr_for_each(&vmap_ctx->import_idr, match_export_desc,
				&params->export_desc);
	if (id_exist > 0)
		map = idr_find(&vmap_ctx->import_idr, (unsigned long)id_exist);

	if (!map) {
		ret = -EAGAIN;
		pr_debug("Failed to find descriptor: (%llu), try again\n",
			 params->export_desc);
		goto err;
	} else {
		/* importing beyond the export from remote is not permitted.*/
		if (map->reg.nr_import == map->reg.nr_export) {
			ret = -EPERM;
			goto err;
		}
		map->reg.nr_import++;

		attrib->type = VMAP_OBJ_TYPE_IMPORT;
		attrib->id = map->obj_id;
		attrib->iova = map->reg.attrib.iova;
		attrib->size = map->reg.attrib.size;
		attrib->offsetof = map->reg.attrib.offsetof;
	}
err:
	mutex_unlock(&vmap_ctx->import_idr_lock);

	return ret;
}

/* must be called with idr lock held.*/
static void
importobj_free(struct kref *kref)
{
	struct importobj_map_ref *map = NULL;

	if (!kref)
		return;

	map = container_of(kref, struct importobj_map_ref, refcount);
	if (map) {
		idr_remove(&map->vmap_ctx->import_idr, (unsigned long)map->obj_id);
		kfree(map);
	}
}

static int
importobj_unmap(struct vmap_ctx_t *vmap_ctx, s32 obj_id)
{
	struct importobj_map_ref *map = NULL;
	struct comm_msg msg = {0};

	mutex_lock(&vmap_ctx->import_idr_lock);

	map = idr_find(&vmap_ctx->import_idr, (unsigned long)obj_id);
	if (!map) {
		mutex_unlock(&vmap_ctx->import_idr_lock);
		return -EINVAL;
	}
	if (WARN_ON(!map->reg.nr_import)) {
		pr_err("Import ObjId: (%d) wasn't imported yet\n", obj_id);
		mutex_unlock(&vmap_ctx->import_idr_lock);
		return -EINVAL;
	}

	/*
	 * Each import corresponds to an export, if we unmap an imported
	 * object, it's exported instance is also refcounted. Remote must
	 * export again for it to be imported on local SoC again.
	 */
	msg.type = COMM_MSG_TYPE_UNREGISTER;
	msg.u.unreg.export_desc = map->reg.export_desc;
	msg.u.unreg.iova = map->reg.attrib.iova;
	msg.u.unreg.size = map->reg.attrib.size;
	msg.u.unreg.offsetof = map->reg.attrib.offsetof;
	comm_channel_msg_send(vmap_ctx->comm_channel_h, &msg);

	kref_put(&map->refcount, importobj_free);
	mutex_unlock(&vmap_ctx->import_idr_lock);

	return 0;
}

static int
importobj_putref(struct vmap_ctx_t *vmap_ctx, s32 obj_id)
{
	return importobj_unmap(vmap_ctx, obj_id);
}

static int
importobj_getref(struct vmap_ctx_t *vmap_ctx, s32 obj_id)
{
	struct memobj_map_ref *map = NULL;

	mutex_lock(&vmap_ctx->import_idr_lock);
	map = idr_find(&vmap_ctx->import_idr, (unsigned long)obj_id);
	if (WARN_ON(!map)) {
		mutex_unlock(&vmap_ctx->import_idr_lock);
		return -EBADF;
	}

	kref_get(&map->refcount);
	mutex_unlock(&vmap_ctx->import_idr_lock);

	return 0;
}

int
vmap_obj_map(void *vmap_h, struct vmap_obj_map_params *params,
	     struct vmap_obj_attributes *attrib, uint64_t aperture_limit,
	     uint64_t *const aperture_inuse)
{
	int ret = 0;
	struct vmap_ctx_t *vmap_ctx = (struct vmap_ctx_t *)vmap_h;

	if (WARN_ON(!vmap_ctx || !params || !attrib))
		return -EINVAL;

	switch (params->type) {
	case VMAP_OBJ_TYPE_MEM:
		ret = memobj_map(vmap_ctx,
				 &params->u.memobj,
				 attrib,
				 aperture_limit,
				 aperture_inuse);
		break;
	case VMAP_OBJ_TYPE_SYNC:
		ret = syncobj_map(vmap_ctx,
				  &params->u.syncobj,
				  attrib,
				  aperture_limit,
				  aperture_inuse);
		break;
	case VMAP_OBJ_TYPE_IMPORT:
		ret = importobj_map(vmap_ctx, &params->u.importobj, attrib);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

int
vmap_obj_unmap(void *vmap_h, enum vmap_obj_type type, s32 obj_id)
{
	int ret = 0;
	struct vmap_ctx_t *vmap_ctx = (struct vmap_ctx_t *)vmap_h;

	if (WARN_ON(!vmap_ctx))
		return -EINVAL;

	switch (type) {
	case VMAP_OBJ_TYPE_MEM:
		ret = memobj_unmap(vmap_ctx, obj_id);
		break;
	case VMAP_OBJ_TYPE_SYNC:
		ret = syncobj_unmap(vmap_ctx, obj_id);
		break;
	case VMAP_OBJ_TYPE_IMPORT:
		ret = importobj_unmap(vmap_ctx, obj_id);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

int
vmap_obj_getref(void *vmap_h, enum vmap_obj_type type, s32 obj_id)
{
	int ret = 0;
	struct vmap_ctx_t *vmap_ctx = (struct vmap_ctx_t *)vmap_h;

	if (WARN_ON(!vmap_ctx))
		return -EINVAL;

	switch (type) {
	case VMAP_OBJ_TYPE_MEM:
		ret = memobj_getref(vmap_ctx, obj_id);
		break;
	case VMAP_OBJ_TYPE_SYNC:
		ret = syncobj_getref(vmap_ctx, obj_id);
		break;
	case VMAP_OBJ_TYPE_IMPORT:
		ret = importobj_getref(vmap_ctx, obj_id);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

int
vmap_obj_putref(void *vmap_h, enum vmap_obj_type type, s32 obj_id)
{
	int ret = 0;
	struct vmap_ctx_t *vmap_ctx = (struct vmap_ctx_t *)vmap_h;

	if (WARN_ON(!vmap_ctx))
		return -EINVAL;

	switch (type) {
	case VMAP_OBJ_TYPE_MEM:
		ret = memobj_putref(vmap_ctx, obj_id);
		break;
	case VMAP_OBJ_TYPE_SYNC:
		ret = syncobj_putref(vmap_ctx, obj_id);
		break;
	case VMAP_OBJ_TYPE_IMPORT:
		ret = importobj_putref(vmap_ctx, obj_id);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static void
vmap_importobj_unregister(void *data, void *ctx)
{
	struct vmap_ctx_t *vmap_ctx = (struct vmap_ctx_t *)ctx;
	struct comm_msg *msg = (struct comm_msg *)data;
	union descriptor_t desc;

	WARN_ON(!vmap_ctx);
	WARN_ON(!msg);
	WARN_ON(msg->type != COMM_MSG_TYPE_UNREGISTER);

	desc.value = msg->u.unreg.export_desc;
	pr_debug("Unregister Desc: (%llu)\n", desc.value);
	if (desc.bit.handle_type == STREAM_OBJ_TYPE_MEM)
		vmap_obj_putref(vmap_ctx, VMAP_OBJ_TYPE_MEM,
				(s32)desc.bit.handle_id);
	else
		vmap_obj_putref(vmap_ctx, VMAP_OBJ_TYPE_SYNC,
				(s32)desc.bit.handle_id);
}

static void
vmap_importobj_register(void *data, void *ctx)
{
	struct vmap_ctx_t *vmap_ctx = (struct vmap_ctx_t *)ctx;
	struct comm_msg *msg = (struct comm_msg *)data;
	struct importobj_map_ref *map = NULL;
	s32 id_exist = 0;

	WARN_ON(!vmap_ctx);
	WARN_ON(!msg);
	WARN_ON(msg->type != COMM_MSG_TYPE_REGISTER);

	mutex_lock(&vmap_ctx->import_idr_lock);

	/* check if we have export descriptor from remote already ? */
	id_exist = idr_for_each(&vmap_ctx->import_idr, match_export_desc,
				&msg->u.reg.export_desc);
	if (id_exist > 0)
		map = idr_find(&vmap_ctx->import_idr, (unsigned long)id_exist);

	if (map) {
		if (msg->u.reg.iova != map->reg.attrib.iova) {
			pr_err("attrib:iova doesn't match for export desc\n");
			goto err;
		}
		if (msg->u.reg.size != map->reg.attrib.size) {
			pr_err("attrib:size doesn't match for export desc\n");
			goto err;
		}
		if (msg->u.reg.offsetof != map->reg.attrib.offsetof) {
			pr_err("attrib:offsetof doesn't match for export desc\n");
			goto err;
		}
		map->reg.nr_export++;
		kref_get(&map->refcount);
		pr_debug("Registered descriptor again: (%llu)\n",
			 map->reg.export_desc);
	} else {
		/* map for the first time.*/
		map = kzalloc(sizeof(*map), GFP_KERNEL);
		if (WARN_ON(!map))
			goto err;

		map->vmap_ctx = vmap_ctx;
		kref_init(&map->refcount);
		map->reg.nr_export = 1;
		map->reg.export_desc = msg->u.reg.export_desc;
		map->reg.attrib.iova = msg->u.reg.iova;
		map->reg.attrib.size = msg->u.reg.size;
		map->reg.attrib.offsetof = msg->u.reg.offsetof;
		map->obj_id = idr_alloc(&vmap_ctx->import_idr, map,
					IMPORTOBJ_START, IMPORTOBJ_END,
					GFP_KERNEL);
		if (map->obj_id <= 0) {
			pr_err("Failed to idr alloc for import obj\n");
			kfree(map);
			goto err;
		}
		pr_debug("Registered descriptor: (%llu)\n", map->reg.export_desc);
	}
err:
	mutex_unlock(&vmap_ctx->import_idr_lock);
}

/* Entry point for the virtual mapping sub-module/abstraction. */
int
vmap_init(struct driver_ctx_t *drv_ctx, void **vmap_h)
{
	int ret = 0;
	struct callback_ops cb_ops = {0};
	struct vmap_ctx_t *vmap_ctx = NULL;

	/* should not be an already instantiated vmap context. */
	if (WARN_ON(!drv_ctx || !vmap_h || *vmap_h))
		return -EINVAL;

	vmap_ctx = kzalloc(sizeof(*vmap_ctx), GFP_KERNEL);
	if (WARN_ON(!vmap_ctx))
		return -ENOMEM;

	vmap_ctx->host1x_pdev = drv_ctx->drv_param.host1x_pdev;
	vmap_ctx->comm_channel_h = drv_ctx->comm_channel_h;
	vmap_ctx->pci_client_h = drv_ctx->pci_client_h;
	vmap_ctx->chip_id = drv_ctx->chip_id;
	idr_init(&vmap_ctx->mem_idr);
	idr_init(&vmap_ctx->sync_idr);
	idr_init(&vmap_ctx->import_idr);
	mutex_init(&vmap_ctx->mem_idr_lock);
	mutex_init(&vmap_ctx->sync_idr_lock);
	mutex_init(&vmap_ctx->import_idr_lock);

	vmap_ctx->dummy_pdev = platform_device_alloc(drv_ctx->drv_name, -1);
	if (!vmap_ctx->dummy_pdev) {
		ret = -ENOMEM;
		pr_err("Failed to allocate dummy platform device\n");
		goto err;
	}
	ret = platform_device_add(vmap_ctx->dummy_pdev);
	if (ret) {
		platform_device_put(vmap_ctx->dummy_pdev);
		pr_err("Failed to add the dummy platform device\n");
		goto err;
	}
	if (vmap_ctx->chip_id == TEGRA234)
		ret = dma_set_mask(&vmap_ctx->dummy_pdev->dev, DMA_BIT_MASK(39));
	else
		ret = dma_set_mask(&vmap_ctx->dummy_pdev->dev, DMA_BIT_MASK(40));
	if (ret) {
		platform_device_del(vmap_ctx->dummy_pdev);
		platform_device_put(vmap_ctx->dummy_pdev);
		pr_err("Failed to set mask for dummy platform device\n");
		goto err;
	}
	vmap_ctx->dummy_pdev_init = true;

	/* comm-channel for registering and unregistering import objects.*/
	cb_ops.callback = vmap_importobj_register;
	cb_ops.ctx = (void *)vmap_ctx;
	ret = comm_channel_register_msg_cb(vmap_ctx->comm_channel_h,
					   COMM_MSG_TYPE_REGISTER, &cb_ops);
	if (ret) {
		pr_err("Failed to add callback with for Register msg\n");
		goto err;
	}

	cb_ops.callback = vmap_importobj_unregister;
	cb_ops.ctx = (void *)vmap_ctx;
	ret = comm_channel_register_msg_cb(vmap_ctx->comm_channel_h,
					   COMM_MSG_TYPE_UNREGISTER, &cb_ops);
	if (ret) {
		pr_err("Failed to add callback with for Unregister msg\n");
		goto err;
	}

	*vmap_h = vmap_ctx;
	return ret;
err:
	vmap_deinit((void **)&vmap_ctx);
	return ret;
}

/* Exit point only. */
static int
memobj_release(s32 obj_id, void *ptr, void *data)
{
	struct memobj_map_ref *map = (struct memobj_map_ref *)(ptr);
	struct vmap_ctx_t *vmap_ctx = (struct vmap_ctx_t *)(data);

	/* release irrespective of reference counts. */
	if (map) {
		memobj_unpin(vmap_ctx, &map->pin);
		kfree(map);
	}

	return 0;
}

/* Exit point only. */
static int
syncobj_release(s32 obj_id, void *ptr, void *data)
{
	struct syncobj_map_ref *map = (struct syncobj_map_ref *)(ptr);
	struct vmap_ctx_t *vmap_ctx = (struct vmap_ctx_t *)(data);

	/* release irrespective of reference counts. */
	if (map) {
		syncobj_unpin(vmap_ctx, &map->pin);
		kfree(map);
	}

	return 0;
}

/* Exit point only. */
static int
importobj_release(s32 obj_id, void *ptr, void *data)
{
	struct importobj_map_ref *map = (struct importobj_map_ref *)(ptr);
	struct vmap_ctx_t *vmap_ctx = (struct vmap_ctx_t *)(data);
	struct comm_msg msg = {0};

	if (map) {
		msg.type = COMM_MSG_TYPE_UNREGISTER;
		msg.u.unreg.export_desc = map->reg.export_desc;
		msg.u.unreg.iova = map->reg.attrib.iova;
		msg.u.unreg.size = map->reg.attrib.size;
		msg.u.unreg.offsetof = map->reg.attrib.offsetof;
		comm_channel_msg_send(vmap_ctx->comm_channel_h, &msg);

		kfree(map);
	}

	return 0;
}

/* Exit point for nvscic2c-pcie vmap sub-module/abstraction. */
void
vmap_deinit(void **vmap_h)
{
	struct vmap_ctx_t *vmap_ctx = (struct vmap_ctx_t *)(*vmap_h);

	if (!vmap_ctx)
		return;

	comm_channel_unregister_msg_cb(vmap_ctx->comm_channel_h,
				       COMM_MSG_TYPE_REGISTER);
	comm_channel_unregister_msg_cb(vmap_ctx->comm_channel_h,
				       COMM_MSG_TYPE_UNREGISTER);
	/*
	 * free all the allocations still idr allocated IDR.
	 *
	 * ideally, this should not be the case, however in scenario:
	 * application went away and the remote missed to free the imported
	 * target handle, then during module unload (PCIe link shall be down)
	 * we shall free all the pinned + yet to be unpinned handles.
	 */
	mutex_lock(&vmap_ctx->mem_idr_lock);
	idr_for_each(&vmap_ctx->mem_idr, memobj_release, vmap_ctx);
	idr_destroy(&vmap_ctx->mem_idr);
	mutex_unlock(&vmap_ctx->mem_idr_lock);

	mutex_lock(&vmap_ctx->sync_idr_lock);
	idr_for_each(&vmap_ctx->sync_idr, syncobj_release, vmap_ctx);
	idr_destroy(&vmap_ctx->sync_idr);
	mutex_unlock(&vmap_ctx->sync_idr_lock);

	mutex_lock(&vmap_ctx->import_idr_lock);
	idr_for_each(&vmap_ctx->import_idr, importobj_release, vmap_ctx);
	idr_destroy(&vmap_ctx->import_idr);
	mutex_unlock(&vmap_ctx->import_idr_lock);

	if (vmap_ctx->dummy_pdev_init) {
		platform_device_unregister(vmap_ctx->dummy_pdev);
		vmap_ctx->dummy_pdev_init = false;
	}

	kfree(vmap_ctx);
	*vmap_h = NULL;
}
