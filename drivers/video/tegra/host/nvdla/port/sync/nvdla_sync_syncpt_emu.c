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
#include <linux/iommu.h>
#include <linux/dma-mapping.h>

/* Local definition of nvhost_syncpt_interface structure */
struct nvhost_syncpt_interface {
	dma_addr_t base;
	size_t size;
	u32 page_size;
};

struct nvdla_sync_device {
	struct platform_device *pdev;
};

struct nvdla_sync_context {
	struct nvdla_sync_device *device;
	uint32_t syncptid;
	dma_addr_t address;
};

static dma_addr_t nvdla_syncpt_address(struct platform_device *pdev, u32 syncpt_id)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvhost_syncpt_interface *syncpt_if = pdata->syncpt_unit_interface;

	return syncpt_if->base + syncpt_if->page_size * syncpt_id;
}

struct nvdla_sync_device *nvdla_sync_device_create_syncpoint(
	struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = NULL;
	struct nvhost_syncpt_interface *syncpt_if = NULL;
	struct nvdla_sync_device *device = NULL;
	phys_addr_t base;
	u32 stride;
	u32 num_syncpts;
	int32_t err;

	if (pdev == NULL)
		goto fail;

	device = (struct nvdla_sync_device *)
			vzalloc(sizeof(struct nvdla_sync_device));
	if (device == NULL) {
		nvdla_dbg_err(pdev, "failed to allocate sync device\n");
		goto fail;
	}

	pdata = platform_get_drvdata(pdev);
	syncpt_if = devm_kzalloc(&pdev->dev, sizeof(*syncpt_if), GFP_KERNEL);
	if (!syncpt_if) {
		err = -ENOMEM;
		goto free_device;
	}

	err = host1x_syncpt_get_shim_info(pdata->host1x, &base, &stride, &num_syncpts);
	if (err) {
		nvdla_dbg_err(pdev, "failed to get syncpt shim info. err=%d\n", err);
		goto free_syncpt_if;
	}

	syncpt_if->base = base;
	syncpt_if->size = stride * num_syncpts;
	syncpt_if->page_size = stride;

	/* If IOMMU is enabled, map it into the device memory */
	if (iommu_get_domain_for_dev(&pdev->dev)) {
		syncpt_if->base = dma_map_resource(&pdev->dev, base,
						syncpt_if->size,
						DMA_BIDIRECTIONAL,
						DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(&pdev->dev, syncpt_if->base)) {
			err = -ENOMEM;
			goto free_syncpt_if;
		}
	}

	pdata->syncpt_unit_interface = syncpt_if;

	dev_info(&pdev->dev,
		"syncpt_unit_base %llx syncpt_unit_size %zx size %x\n",
		base, syncpt_if->size, syncpt_if->page_size);

	device->pdev = pdev;

	return device;

free_syncpt_if:
	devm_kfree(&pdev->dev, syncpt_if);
free_device:
	vfree(device);
fail:
	return NULL;
}

void nvdla_sync_device_destroy(struct nvdla_sync_device *device)
{
	struct nvhost_syncpt_interface *syncpt_if;
	struct nvhost_device_data *pdata;
	struct platform_device *pdev;

	if (device == NULL)
		goto done;

	if (device->pdev == NULL)
		goto free_device;

	pdev = device->pdev;
	pdata = platform_get_drvdata(pdev);
	syncpt_if = pdata->syncpt_unit_interface;

	if (syncpt_if) {
		/* Unmap IOMMU resources if needed */
		if (iommu_get_domain_for_dev(&pdev->dev)) {
			dma_unmap_resource(&pdev->dev, syncpt_if->base, syncpt_if->size,
					DMA_BIDIRECTIONAL, DMA_ATTR_SKIP_CPU_SYNC);
		}

		/* Free the syncpt_if memory */
		devm_kfree(&pdev->dev, syncpt_if);
		pdata->syncpt_unit_interface = NULL;
	}

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
		address = nvdla_syncpt_address(device->pdev, syncptid);

	return address;
}

struct nvdla_sync_context *nvdla_sync_create(struct nvdla_sync_device *device)
{
	struct nvdla_sync_context *context = NULL;
	struct host1x_syncpt *sp = NULL;
	struct nvhost_device_data *pdata = NULL;

	if ((device == NULL) || (device->pdev == NULL))
		goto fail;

	context = (struct nvdla_sync_context *)
			(vzalloc(sizeof(struct nvdla_sync_context)));
	if (context == NULL) {
		nvdla_dbg_err(device->pdev,
			"Failure to allocate sync context\n");
		goto fail;
	}

	pdata = platform_get_drvdata(device->pdev);
	sp = host1x_syncpt_alloc(pdata->host1x,
				0U, /* Not client managed */
				dev_name(&device->pdev->dev));
	if (!sp) {
		nvdla_dbg_err(device->pdev, "Failed to allocate syncpoint\n");
		goto free_context;
	}

	context->syncptid = host1x_syncpt_id(sp);
	if (context->syncptid == 0) {
		nvdla_dbg_err(device->pdev, "Failed to get syncpoint ID\n");
		host1x_syncpt_put(sp); /* Release the allocated syncpoint */
		goto free_context;
	}

	context->address = nvdla_syncpt_address(device->pdev, context->syncptid);
	context->device = device;

	return context;

free_context:
	vfree(context);
fail:
	return NULL;
}

void nvdla_sync_destroy(struct nvdla_sync_context *context)
{
	struct host1x_syncpt *sp = NULL;
	struct nvhost_device_data *pdata;

	if (context == NULL)
		goto done;

	if ((context->device == NULL) || (context->device->pdev == NULL))
		goto free_context;

	pdata = platform_get_drvdata(context->device->pdev);
	sp = host1x_syncpt_get_by_id_noref(pdata->host1x, context->syncptid);
	if (WARN_ON(!sp))
		goto free_context;

	host1x_syncpt_put(sp);

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
	struct nvhost_device_data *pdata;
	struct host1x_syncpt *sp;

	if ((context == NULL) || (context->device == NULL))
		goto fail;

	pdata = platform_get_drvdata(context->device->pdev);
	sp = host1x_syncpt_get_by_id_noref(pdata->host1x, context->syncptid);
	if (WARN_ON(!sp))
		goto fail;

	maxval = host1x_syncpt_incr_max(sp, increment);

fail:
	return maxval;
}

uint32_t nvdla_sync_get_max_value(struct nvdla_sync_context *context)
{
	int32_t maxval = 0U;
	struct nvhost_device_data *pdata;
	struct host1x_syncpt *sp;

	if ((context == NULL) || (context->device == NULL))
		goto fail;

	pdata = platform_get_drvdata(context->device->pdev);
	sp = host1x_syncpt_get_by_id_noref(pdata->host1x, context->syncptid);
	if (WARN_ON(!sp))
		goto fail;

	maxval = host1x_syncpt_read_max(sp);

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
	struct nvhost_device_data *pdata;
	struct host1x_syncpt *sp;

	if ((context == NULL) || (context->device == NULL)) {
		err = -EINVAL;
		goto fail;
	}

	device = context->device;
	if (timeout == 0ULL) {
		pdata = platform_get_drvdata(device->pdev);
		sp = host1x_syncpt_get_by_id_noref(pdata->host1x, context->syncptid);
		if (WARN_ON(!sp)) {
			err = -EINVAL;
			goto fail;
		}

		wait_complete = (host1x_syncpt_wait(sp, threshold, 0, NULL) == 0);
		if (!wait_complete) {
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
	struct nvhost_device_data *pdata;
	struct host1x_syncpt *sp;
	uint32_t cur;

	if ((context == NULL) || (context->device == NULL)) {
		err = -EINVAL;
		goto fail;
	}

	pdata = platform_get_drvdata(context->device->pdev);
	sp = host1x_syncpt_get_by_id_noref(pdata->host1x, context->syncptid);
	if (WARN_ON(!sp)) {
		err = -EINVAL;
		goto fail;
	}

	cur = host1x_syncpt_read(sp);
	while (cur++ != signal_value)
		host1x_syncpt_incr(sp);

	/* Read back to ensure the value has been updated */
	host1x_syncpt_read(sp);

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
