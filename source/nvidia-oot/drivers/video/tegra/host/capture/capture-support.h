/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Capture support for syncpoint and GoS management
 *
 * Copyright (c) 2017-2022, NVIDIA Corporation.  All rights reserved.
 */

#ifndef _CAPTURE_SUPPORT_H_
#define _CAPTURE_SUPPORT_H_

#include <linux/types.h>
#include <linux/platform_device.h>

int capture_alloc_syncpt(struct platform_device *pdev,
			const char *name,
			uint32_t *syncpt_id);

void capture_release_syncpt(struct platform_device *pdev, uint32_t id);

void capture_get_gos_table(struct platform_device *pdev,
			int *gos_count,
			const dma_addr_t **gos_table);

int capture_get_syncpt_gos_backing(struct platform_device *pdev,
			uint32_t id,
			dma_addr_t *syncpt_addr,
			uint32_t *gos_index,
			uint32_t *gos_offset);

#endif /* _CAPTURE_SUPPORT_H_ */
