/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __NVCAMERA_LOG_H
#define __NVCAMERA_LOG_H

#include <linux/types.h>

struct platform_device;

void nv_camera_log_isp_submit(struct platform_device *pdev,
		u32 syncpt_id,
		u32 syncpt_thresh,
		u32 channel_id,
		u64 timestamp);

void nv_camera_log_vi_submit(struct platform_device *pdev,
		u32 syncpt_id,
		u32 syncpt_thresh,
		u32 channel_id,
		u64 timestamp);

void nv_camera_log(struct platform_device *pdev,
		u64 timestamp,
		u32 type);

#endif
