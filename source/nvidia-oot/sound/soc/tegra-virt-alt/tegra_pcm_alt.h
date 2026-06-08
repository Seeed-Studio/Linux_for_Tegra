/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __TEGRA_PCM_ALT_H__
#define __TEGRA_PCM_ALT_H__

#define MAX_DMA_REQ_COUNT 2

struct tegra_alt_pcm_dma_params {
	unsigned long addr;
	unsigned long width;
	unsigned long req_sel;
	const char *chan_name;
	size_t buffer_size;
};

int tegra_alt_pcm_platform_register(struct device *dev);
void tegra_alt_pcm_platform_unregister(struct device *dev);

#endif