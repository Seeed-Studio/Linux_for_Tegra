/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2011-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __NVMAP_DEBUG_H
#define __NVMAP_DEBUG_H

#include "nvmap_stats.h"
#if defined(CONFIG_DEBUG_FS)
void nvmap_debug_init(struct dentry **nvmap_debug_root);
void nvmap_debug_free(struct dentry *nvmap_debug_root);
#else
static inline void nvmap_debug_init(struct dentry **nvmap_debug_root)
{
}
static inline void nvmap_debug_free(struct dentry *nvmap_debug_root)
{
}
#endif
struct dentry *nvmap_debug_create_debugfs_handles_by_pid(
				const char *name,
				struct dentry *parent,
				void *data);
void nvmap_debug_remove_debugfs_handles_by_pid(struct dentry *d);

#endif /* __NVMAP_DEBUG_H */
