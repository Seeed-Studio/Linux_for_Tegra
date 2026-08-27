// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2018-2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
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


#include "nvcamera_log.h"
#define CREATE_TRACE_POINTS
#include "trace/events/camera_common.h"
#include <linux/nvhost.h>
#include <linux/platform_device.h>
#include <uapi/linux/nvhost_events.h>

/*
 * Set to 1 to enable additional kernel API traces
 */
#define NVCAM_ENABLE_EXTRA_TRACES	0


void nv_camera_log_isp_submit(struct platform_device *pdev,
		u32 syncpt_id,
		u32 syncpt_thresh,
		u32 channel_id,
		u64 timestamp)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if (pdata == NULL)
		return;

	/*
	 * Events are meant to be matched with their userspace
	 * analogues. Instead of the PID as (this) thread's ID use the
	 * inherited thread group ID. For the reported TID use this thread's
	 * ID (i.e. PID).
	 */
	trace_isp_task_submit(
		pdata->class,
		channel_id,
		syncpt_id,
		syncpt_thresh,
		current->pid,
		current->tgid
	);
}
EXPORT_SYMBOL_GPL(nv_camera_log_isp_submit);

void nv_camera_log_vi_submit(struct platform_device *pdev,
		u32 syncpt_id,
		u32 syncpt_thresh,
		u32 channel_id,
		u64 timestamp)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if (pdata == NULL)
		return;

	/*
	 * Events are meant to be matched with their userspace
	 * analogues. Instead of the PID as (this) thread's ID use the
	 * inherited thread group ID. For the reported TID use this thread's
	 * ID (i.e. PID).
	 */
	trace_vi_task_submit(
		pdata->class,
		channel_id,
		syncpt_id,
		syncpt_thresh,
		current->pid,
		current->tgid
	);
}
EXPORT_SYMBOL_GPL(nv_camera_log_vi_submit);

#if NVCAM_ENABLE_EXTRA_TRACES

/*
 * Additional camera traces disabled by default
 */
void nv_camera_log(struct platform_device *pdev,
		u64 timestamp,
		u32 type)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if (pdata == NULL)
		return;

	/*
	 * Events are meant to be matched with their userspace
	 * analogues. Instead of the PID as (this) thread's ID use the
	 * inherited thread group ID. For the reported TID use this thread's
	 * ID (i.e. PID).
	 */
	trace_camera_task_log(
		pdata->class,
		type,
		timestamp,
		current->pid,
		current->tgid
	);
}

#else

void nv_camera_log(struct platform_device *pdev,
		u64 timestamp,
		u32 type)
{
}

#endif
EXPORT_SYMBOL_GPL(nv_camera_log);
