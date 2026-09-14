/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2018-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __VIDEO_TEGRA_NVMAP_STATS_H
#define __VIDEO_TEGRA_NVMAP_STATS_H

enum nvmap_stats_t {
	NS_ALLOC = 0,
	NS_RELEASE,
	NS_UALLOC,
	NS_URELEASE,
	NS_KALLOC,
	NS_KRELEASE,
	NS_CFLUSH_RQ,
	NS_CFLUSH_DONE,
	NS_UCFLUSH_RQ,
	NS_UCFLUSH_DONE,
	NS_KCFLUSH_RQ,
	NS_KCFLUSH_DONE,
	NS_TOTAL,
	NS_NUM,
};

struct nvmap_stats {
	atomic64_t stats[NS_NUM];
	atomic64_t collect;
};

extern struct nvmap_stats nvmap_stats;

void nvmap_stats_init(struct dentry *nvmap_debug_root);
void nvmap_stats_inc(enum nvmap_stats_t, size_t size);
void nvmap_stats_dec(enum nvmap_stats_t, size_t size);
u64 nvmap_stats_read(enum nvmap_stats_t);
#endif /* __VIDEO_TEGRA_NVMAP_STATS_H */
