// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA syncpoint stub implementation
 */

#include "../nvdla_sync.h"

struct nvdla_sync_device *nvdla_sync_device_create_syncpoint(
	struct platform_device *pdev)
{
	(void) pdev;

	return NULL;
}

void nvdla_sync_device_destroy(struct nvdla_sync_device *device)
{
	(void) device;
}

dma_addr_t nvdla_sync_get_address_by_syncptid(
	struct nvdla_sync_device *device,
	uint32_t syncptid)
{
	(void) device;
	(void) syncptid;

	return 0ULL;
}

struct nvdla_sync_context *nvdla_sync_create(struct nvdla_sync_device *device)
{
	(void) device;

	return NULL;
}

void nvdla_sync_destroy(struct nvdla_sync_context *context)
{
	(void) context;
}

dma_addr_t nvdla_sync_get_address(struct nvdla_sync_context *context)
{
	(void) context;

	return 0ULL;
}

uint32_t nvdla_sync_increment_max_value(struct nvdla_sync_context *context,
	uint32_t increment)
{
	(void) context;
	(void) increment;

	return 0U;
}

uint32_t nvdla_sync_get_max_value(struct nvdla_sync_context *context)
{
	(void) context;

	return 0U;
}

int32_t nvdla_sync_wait(struct nvdla_sync_context *context,
	uint32_t threshold,
	uint64_t timeout)
{
	(void) context;
	(void) threshold;
	(void) timeout;

	return -1;
}

int32_t nvdla_sync_signal(struct nvdla_sync_context *context,
	uint32_t signal_value)
{
	(void) context;
	(void) signal_value;

	return -1;
}

void nvdla_sync_print(struct nvdla_sync_context *context)
{
	(void) context;
}

uint32_t nvdla_sync_get_syncptid(struct nvdla_sync_context *context)
{
	(void) context;

	return 0xFFFFFFFFU;
}
