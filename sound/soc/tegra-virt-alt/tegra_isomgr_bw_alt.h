/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __TEGRA_ISOMGR_BW_ALT_H__
#define __TEGRA_ISOMGR_BW_ALT_H__

#if defined(CONFIG_TEGRA_ISOMGR)
void tegra_isomgr_adma_register(void);
void tegra_isomgr_adma_unregister(void);
void tegra_isomgr_adma_setbw(struct snd_pcm_substream *substream,
			bool is_running);
void tegra_isomgr_adma_renegotiate(void *p, u32 avail_bw);
#else
static inline void tegra_isomgr_adma_register(void) { return; }
static inline void tegra_isomgr_adma_unregister(void) { return; }
static inline void tegra_isomgr_adma_setbw(struct snd_pcm_substream *substream,
			bool is_running) { return; }
#endif

#endif