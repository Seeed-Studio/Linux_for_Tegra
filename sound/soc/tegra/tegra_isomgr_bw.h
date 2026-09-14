/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2016-2023 NVIDIA CORPORATION.  All rights reserved. */

#ifndef __TEGRA_ISOMGR_BW_H__
#define __TEGRA_ISOMGR_BW_H__
void tegra_isomgr_adma_register(struct device *dev);
void tegra_isomgr_adma_unregister(struct device *dev);
void tegra_isomgr_adma_setbw(struct snd_pcm_substream *substream,
			bool is_running);
void tegra_isomgr_adma_renegotiate(void *p, u32 avail_bw);
#endif
