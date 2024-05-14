/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef __HOST1X_EMU_POLL_H
#define __HOST1X_EMU_POLL_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timekeeping.h>

struct host1x;
struct host1x_syncpt_fence;

struct host1x_fence_list {
    spinlock_t              lock;
    struct list_head        list;
};

struct host1x_syncpt_list {
    spinlock_t              lock;
    struct list_head        list;
};

/**
 * Description: Initialize host1x syncpoint and pool for polling
 * 
 */
int host1x_poll_init(struct host1x *host);

/**
 * Description: Deinitialize host1x syncpoint and pool for polling
 */
void host1x_poll_deinit(struct host1x *host);

/**
 * Description: Enable host1x syncpoint pool polling
 */
void host1x_poll_start(struct host1x *host);

/**
 * Description: Disable host1x syncpoint pool polling
 */
void host1x_poll_stop(struct host1x *host);

/**
 * Description: Polling Thread Handler
 */
void host1x_poll_handle_timeout(struct host1x *host, unsigned int id, ktime_t ts);

/**
 * Description: Add fence to syncpoint object fence-list
 */
void host1x_poll_add_fence_locked(struct host1x *host, struct host1x_syncpt_fence *fence);

/**
 * Description: Remove fence from syncpoint object fence-list
 */
bool host1x_poll_remove_fence(struct host1x *host, struct host1x_syncpt_fence *fence);

#endif
