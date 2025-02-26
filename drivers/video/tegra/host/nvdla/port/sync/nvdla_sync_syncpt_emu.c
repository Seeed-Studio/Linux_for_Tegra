// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA syncpoint emulator wrapper implementation
 */

#include "../nvdla_sync.h"
#include "../nvdla_host_wrapper.h"

#include "../../nvdla_debug.h"
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

struct nvdla_sync_device {
	struct platform_device *pdev;
};

struct nvdla_sync_context {
	struct nvdla_sync_device *device;
	uint32_t syncptid;
	dma_addr_t address;
};

struct nvdla_sync_device *nvdla_sync_device_create_syncpoint(
	struct platform_device *pdev)
{
	int32_t err;
	struct nvdla_sync_device *device = NULL;

	if (pdev == NULL)
		goto fail;

	device = (struct nvdla_sync_device *)
			vzalloc(sizeof(struct nvdla_sync_device));
	if (device == NULL) {
		nvdla_dbg_err(pdev, "failed to allocate sync device\n");
		goto fail;
	}

	err = nvhost_syncpt_unit_interface_init(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to init syncpt interface. err=%d\n",
			err);
		goto free_device;
	}

	device->pdev = pdev;

	return device;

free_device:
	vfree(device);
fail:
	return NULL;
}

void nvdla_sync_device_destroy(struct nvdla_sync_device *device)
{
	if (device == NULL)
		goto done;

	if (device->pdev == NULL)
		goto free_device;

	nvhost_syncpt_unit_interface_deinit(device->pdev);

free_device:
	device->pdev = NULL;
	vfree(device);
done:
	return;
}

dma_addr_t nvdla_sync_get_address_by_syncptid(
	struct nvdla_sync_device *device,
	uint32_t syncptid)
{
	dma_addr_t address = 0ULL;

	if (device != NULL)
		address = nvhost_syncpt_address(device->pdev, syncptid);

	return address;
}

struct nvdla_sync_context *nvdla_sync_create(struct nvdla_sync_device *device)
{
	struct nvdla_sync_context *context = NULL;

	if ((device == NULL) || (device->pdev == NULL))
		goto fail;

	context = (struct nvdla_sync_context *)
			(vzalloc(sizeof(struct nvdla_sync_context)));
	if (context == NULL) {
		nvdla_dbg_err(device->pdev,
			"Failure to allocate sync context\n");
		goto fail;
	}

	context->syncptid = nvhost_get_syncpt_host_managed(device->pdev, 0U, NULL);

	context->address = nvhost_syncpt_address(device->pdev, context->syncptid);
	context->device = device;

	return context;

fail:
	return NULL;
}

void nvdla_sync_destroy(struct nvdla_sync_context *context)
{
	if (context == NULL)
		goto done;

	if ((context->device == NULL) || (context->device->pdev == NULL))
		goto free_context;

	/* Release the syncpoint ID */
	nvhost_syncpt_put_ref_ext(context->device->pdev, context->syncptid);

free_context:
	context->device = NULL;
	vfree(context);
done:
	return;
}

dma_addr_t nvdla_sync_get_address(struct nvdla_sync_context *context)
{
	dma_addr_t address = 0ULL;

	if (context != NULL)
		address = context->address;

	return address;
}

uint32_t nvdla_sync_increment_max_value(struct nvdla_sync_context *context,
	uint32_t increment)
{
	uint32_t maxval = 0U;

	if ((context == NULL) || (context->device == NULL))
		goto fail;

	maxval = nvhost_syncpt_incr_max_ext(context->device->pdev,
					context->syncptid,
					increment);

fail:
	return maxval;
}

uint32_t nvdla_sync_get_max_value(struct nvdla_sync_context *context)
{
	int32_t maxval = 0U;

	if ((context == NULL) || (context->device == NULL))
		goto fail;

	maxval = nvhost_syncpt_read_maxval(context->device->pdev,
					context->syncptid);

fail:
	return maxval;
}

int32_t nvdla_sync_wait(struct nvdla_sync_context *context,
	uint32_t threshold,
	uint64_t timeout)
{
	int32_t err = 0;
	int wait_complete;
	struct nvdla_sync_device *device;

	if ((context == NULL) || (context->device == NULL)) {
		err = -EINVAL;
		goto fail;
	}

	device = context->device;
	if (timeout == 0ULL) {
		wait_complete = nvhost_syncpt_is_expired_ext(device->pdev,
				context->syncptid,
				threshold);
		if (wait_complete == 0) {
			nvdla_dbg_err(device->pdev,
				"Wait on sp[%u] for threshold[%u] timedout\n",
				context->syncptid, threshold);
			err = -ETIMEDOUT;
			goto fail;
		}
	} else {
		nvdla_dbg_err(device->pdev,
			"Non-zero timeout[%llu] wait is not supported.\n",
			timeout);
		err = -EINVAL;
		goto fail;
	}

fail:
	return err;
}

int32_t nvdla_sync_signal(struct nvdla_sync_context *context,
	uint32_t signal_value)
{
	int err = 0;

	if ((context == NULL) || (context->device == NULL)) {
		err = -EINVAL;
		goto fail;
	}

	nvhost_syncpt_set_min_update(context->device->pdev,
			context->syncptid,
			signal_value);

fail:
	return err;
}

void nvdla_sync_print(struct nvdla_sync_context *context)
{
	struct platform_device *pdev;

	if (((context == NULL) || (context->device == NULL)) ||
			(context->device->pdev == NULL))
		goto done;

	pdev = context->device->pdev;
	nvdla_dbg_info(pdev, "syncptid[%u]\n", context->syncptid);

done:
	return;
}

uint32_t nvdla_sync_get_syncptid(struct nvdla_sync_context *context)
{
	uint32_t syncptid = 0xFFFFFFFFU;

	if (context != NULL)
		syncptid = context->syncptid;

	return syncptid;
}
