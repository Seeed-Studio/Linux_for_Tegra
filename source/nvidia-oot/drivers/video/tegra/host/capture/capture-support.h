/* SPDX-License-Identifier: GPL-2.0-only
 *
 * SPDX-FileCopyrightText: Copyright (c) 2017-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Capture support for syncpoint management
 */

#ifndef _CAPTURE_SUPPORT_H_
#define _CAPTURE_SUPPORT_H_

#include <linux/types.h>
#include <linux/platform_device.h>

int capture_alloc_syncpt(struct platform_device *pdev,
			const char *name,
			uint32_t *syncpt_id);

void capture_release_syncpt(struct platform_device *pdev, uint32_t id);

int capture_get_syncpt_addr(struct platform_device *pdev,
			uint32_t id,
			dma_addr_t *syncpt_addr);

#endif /* _CAPTURE_SUPPORT_H_ */
