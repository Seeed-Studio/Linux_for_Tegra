/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef _LINUX_TEGRA_RTCPU_TRACE_H_
#define _LINUX_TEGRA_RTCPU_TRACE_H_

#include <linux/types.h>

struct tegra_rtcpu_trace;
struct camrtc_device_group;

struct tegra_rtcpu_trace *tegra_rtcpu_trace_create(
	struct device *dev,
	struct camrtc_device_group *camera_devices);
int tegra_rtcpu_trace_boot_sync(struct tegra_rtcpu_trace *tracer);
void tegra_rtcpu_trace_flush(struct tegra_rtcpu_trace *tracer);
void tegra_rtcpu_trace_destroy(struct tegra_rtcpu_trace *tracer);
void rtcpu_trace_panic_callback(struct device *dev);
void tegra_rtcpu_trace_set_panic_flag(struct tegra_rtcpu_trace *tracer);

#endif
