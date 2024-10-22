// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2011-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/xarray.h>
#include <linux/dma-buf.h>
#include <linux/nvmap.h>
#include "nvmap_dev.h"
#include "nvmap_alloc.h"
#include "nvmap_handle.h"

/*
 * Initialize xarray mapping
 */
void nvmap_id_array_init(struct xarray *id_arr)
{
	xa_init_flags(id_arr, XA_FLAGS_ALLOC1);
}

/*
 * Remove id to dma_buf mapping
 */
void nvmap_id_array_exit(struct xarray *id_arr)
{
	xa_destroy(id_arr);
}

/*
 * Create mapping between the id(NvRmMemHandle) and dma_buf
 */
int nvmap_id_array_id_alloc(struct xarray *id_arr, u32 *id, struct dma_buf *dmabuf)
{
	static u32 xa_start = U32_MAX / 2;
	int e;

	if (!id_arr || !dmabuf)
		return -EINVAL;

alloc_from_start:
	e = xa_alloc(id_arr, id, dmabuf,
		       XA_LIMIT(xa_start, U32_MAX), GFP_KERNEL);

	xa_lock(id_arr);
	if (!e) {
		if (*id == U32_MAX)
			xa_start = U32_MAX / 2;
		else
			xa_start = *id + 1;
	} else if (e == -EBUSY && xa_start != U32_MAX / 2) {
		/*
		 * xa_alloc returns -EBUSY if there are no free entries.
		 * In this case we will give one more try by resetting xa_start.
		 */
		xa_start = U32_MAX / 2;
		xa_unlock(id_arr);
		goto alloc_from_start;
	}
	xa_unlock(id_arr);

	return e;
}

/*
 * Clear mapping between the id(NvRmMemHandle) and dma_buf
 */
struct dma_buf *nvmap_id_array_id_release(struct xarray *id_arr, u32 id)
{
	if (!id_arr || !id)
		return NULL;

	return xa_erase(id_arr, id);
}

/*
 * Return dma_buf from the id.
 */
struct dma_buf *nvmap_id_array_get_dmabuf_from_id(struct xarray *id_arr, u32 id)
{
	struct dma_buf *dmabuf;

	dmabuf = xa_load(id_arr, id);
	if (!IS_ERR_OR_NULL(dmabuf))
		get_dma_buf(dmabuf);

	return dmabuf;
}
