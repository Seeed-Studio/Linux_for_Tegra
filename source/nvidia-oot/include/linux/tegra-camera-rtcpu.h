/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef _LINUX_TEGRA_CAMERA_RTCPU_H_
#define _LINUX_TEGRA_CAMERA_RTCPU_H_

#include <linux/types.h>

struct device;

int tegra_camrtc_iovm_setup(struct device *dev, dma_addr_t iova);
ssize_t tegra_camrtc_print_version(struct device *dev, char *buf, size_t size);
int tegra_camrtc_reboot(struct device *dev);
int tegra_camrtc_restore(struct device *dev);
bool tegra_camrtc_is_rtcpu_alive(struct device *dev);
void tegra_camrtc_flush_trace(struct device *dev);

bool tegra_camrtc_is_rtcpu_powered(void);

#define TEGRA_CAMRTC_VERSION_LEN 128

int tegra_camrtc_ping(struct device *dev, u32 data, long timeout);
void tegra_camrtc_ivc_ring(struct device *dev, u16 group);

#endif
