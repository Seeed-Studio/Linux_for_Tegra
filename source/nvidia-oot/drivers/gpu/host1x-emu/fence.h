/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef HOST1X_EMU_FENCE_H
#define HOST1X_EMU_FENCE_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/dma-fence.h>
#include <linux/timekeeping.h>

struct host1x;
struct host1x_syncpt;

struct host1x_syncpt_fence {
    bool timeout;
    atomic_t signaling;
    u32 threshold;
    struct dma_fence dfence;
    struct host1x_syncpt *sp;
    struct delayed_work timeout_work;

    /**
     * Used for adding into syncpoint fence-list
     */
    struct list_head list;
};

void host1x_fence_signal(struct host1x_syncpt_fence *fence, ktime_t ts);
#endif
