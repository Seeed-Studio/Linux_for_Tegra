/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2017-2026 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

/**
 * @file include/media/fusa-capture/capture-isp-channel.h
 *
 * @brief ISP channel character device driver header for the T186/T194 Camera
 * RTCPU platform.
 */

#ifndef __FUSA_CAPTURE_ISP_CHANNEL_H__
#define __FUSA_CAPTURE_ISP_CHANNEL_H__

#include <linux/of_platform.h>

struct isp_channel_drv;

/**
 * @brief ISP fops for Host1x syncpt allocations
 *
 * This fops is a HAL for chip/IP generations, see the respective ISP platform
 * drivers for the implementations.
 */
struct isp_channel_drv_ops {
	/**
	 * @brief Request a syncpt allocation from Host1x.
	 *
	 * @param[in]	pdev		ISP platform_device
	 * @param[in]	name		syncpt name
	 * @param[out]	syncpt_id	assigned syncpt id
	 *
	 * @returns	0 (success), neg. errno (failure)
	 */
	int (*alloc_syncpt)(
		struct platform_device *pdev,
		const char *name,
		uint32_t *syncpt_id);

	/**
	 * @brief Release a syncpt to Host1x.
	 *
	 * @param[in]	pdev	ISP platform_device
	 * @param[in]	id	syncpt id to free
	 */
	void (*release_syncpt)(
		struct platform_device *pdev,
		uint32_t id);

	/**
	 * Fast forward a progres syncpt to Host1x.
	 *
	 * @param[in]	pdev		ISP platform_device
	 * @param[in]	id			syncpt id to fast forward
	 * @param[in]	threshold	value to fast forward to
	 */
	void (*fast_forward_syncpt)(
		struct platform_device *pdev,
		uint32_t id,
		uint32_t threshold);

	/**
	 * @brief Get a syncpt's shim address.
	 *
	 * @param[in]	pdev		ISP platform_device
	 * @param[in]	id		syncpt id
	 * @param[out]	syncpt_addr	syncpt shim address
	 *
	 * @returns	0 (success), neg. errno (failure)
	 */
	int (*get_syncpt_addr)(
		struct platform_device *pdev,
		uint32_t id,
		dma_addr_t *syncpt_addr);
};

/**
 * @brief ISP channel context (character device).
 */
struct tegra_isp_channel {
	struct device *isp_dev; /**< ISP device */
	struct platform_device *ndev; /**< ISP nvhost platform_device */
	struct platform_device *isp_capture_pdev;
		/**< Capture ISP driver platform device */
	struct isp_channel_drv *drv; /**< ISP channel driver context */
	void *priv; /**< ISP channel private context */
	struct isp_capture *capture_data; /**< ISP channel capture context */
	const struct isp_channel_drv_ops *ops; /**< ISP syncpt fops */
	uint32_t pipeline_id; /**< Pipeline ID for logging */
};

/**
 * @brief Create the ISP channels driver contexts, and instantiate
 * channel character device nodes as specified in the device tree.
 *
 * ISP channel nodes appear in the filesystem as:
 * /dev/capture-isp-channel{0..max_isp_channels-1}
 *
 * @param[in]	ndev	ISP platform_device context
 * @param[in]	max_isp_channels	Maximum number of ISP channels
 *
 * @returns	0 (success), neg. errno (failure)
 */
int isp_channel_drv_register(
	struct platform_device *pdev,
	unsigned int max_isp_channels);

/**
 * @brief Destroy the ISP channels driver and all character device nodes.
 *
 * The ISP channels driver and associated channel contexts in memory are freed,
 * rendering the ISP platform driver unusable until re-initialized.
 *
 * @param[in]	dev	ISP device context
 */
void isp_channel_drv_unregister(
	struct device *dev);

/**
 * @brief Register the chip specific syncpt related function table
 *
 * @param[in]	ops	isp_channel_drv_ops fops
 * @returns	0 (success), neg. errno (failure)
 */
int isp_channel_drv_fops_register(
	const struct isp_channel_drv_ops *ops);

int isp_channel_drv_init(void);
void isp_channel_drv_exit(void);
#endif /* __FUSA_CAPTURE_ISP_CHANNEL_H__ */
