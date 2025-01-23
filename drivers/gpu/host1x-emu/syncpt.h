/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef __HOST1X_EMU_SYNCPT_H
#define __HOST1X_EMU_SYNCPT_H

#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/sched.h>
#ifdef CONFIG_TEGRA_HOST1X_EMU_DBG_SYMBL
#include <linux/host1x-emu.h>
#else
#include <linux/host1x-next.h>
#include "include/linux/symbol-emu.h"
#endif

#include "fence.h"
#include "poll.h"

/* Reserved for replacing an expired wait with a NOP */
struct host1x;

struct host1x_syncpt_pool {
    const char *name;
    unsigned int sp_base;
    unsigned int sp_end;
    struct host1x *host;
    struct delayed_work pool_work;
    struct host1x_syncpt_list syncpt_list;
};

struct host1x_syncpt {
    struct kref ref;
    unsigned int id;
    const char *name;
    atomic_t min_val;
    atomic_t max_val;
    bool client_managed;

    struct host1x *host;
    struct host1x_syncpt_pool *pool;
    struct host1x_fence_list fences;
    bool locked;

    /**
     * Used for adding syncpoint to pool->syncpt-list
     */
    struct list_head list;
};

#ifndef CONFIG_TEGRA_HOST1X_EMU_DBG_SYMBL
HOST1X_EMU_EXPORT_DECL(void, host1x_syncpt_fence_scan(struct host1x_syncpt *sp));
#endif /*CONFIG_TEGRA_HOST1X_EMU_DBG_SYMBL*/

/**
 * Description: Initialize sync point array
 */
int host1x_syncpt_init(struct host1x *host);

/**
 * Description: Free sync point array
 */
void host1x_syncpt_deinit(struct host1x *host);

/**
 * Description: Return number of sync point supported.
 */
unsigned int host1x_syncpt_nb_pts(struct host1x *host);

/**
 *  Description: Return true if sync point is client managed.
 */
static inline bool host1x_syncpt_client_managed(struct host1x_syncpt *sp)
{
    return sp->client_managed;
}

/**
 * Description: Check sync point sanity.
 *
 * If max is larger than min, there have too many sync point increments.
 * Client managed sync point are not tracked.
 */
static inline bool host1x_syncpt_check_max(struct host1x_syncpt *sp, u32 real)
{
    u32 max;
    if (sp->client_managed)
        return true;

    max = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read_max(sp));
    return (s32)(max - real) >= 0;
}

/**
 * Description: Returns true if syncpoint min == max, which means that there are
 * no outstanding operations.
 */
static inline bool host1x_syncpt_idle(struct host1x_syncpt *sp)
{
    int min, max;
    smp_rmb();
    min = atomic_read(&sp->min_val);
    max = atomic_read(&sp->max_val);
    return (min == max);
}

/**
 * Description: Load current value from hardware to the shadow register.
 */
u32 host1x_syncpt_load(struct host1x_syncpt *sp);

/**
 * Description: Check if the given syncpoint value has already passed
 */
bool host1x_syncpt_is_expired(struct host1x_syncpt *sp, u32 thresh);

/**
 * Description: Save host1x sync point state into shadow registers.
 */
void host1x_syncpt_save(struct host1x *host);

/**
 * Description: Reset host1x sync point state from shadow registers.
 */
void host1x_syncpt_restore(struct host1x *host);

/**
 * Description: Indicate future operations by incrementing the sync point max.
 */
HOST1X_EMU_EXPORT_DECL(u32, host1x_syncpt_incr_max(struct host1x_syncpt *sp,
                                        u32 incrs));

/**
 * Description: Check if sync point id is valid.
 */
static inline int host1x_syncpt_is_valid(struct host1x_syncpt *sp)
{
    return sp->id < host1x_syncpt_nb_pts(sp->host);
}

/**
 * Description: Set sync as locked.
 */
static inline void host1x_syncpt_set_locked(struct host1x_syncpt *sp)
{
    sp->locked = true;
}
#endif
