/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "dev.h"
#include "fence.h"
#include "poll.h"

static void host1x_poll_add_fence_to_list(struct host1x_fence_list *list,
                      struct host1x_syncpt_fence *fence)
{
    struct host1x_syncpt_fence *fence_in_list;

    list_for_each_entry_reverse(fence_in_list, &list->list, list) {
        if ((s32)(fence_in_list->threshold - fence->threshold) <= 0) {
            /* Fence in list is before us, we can insert here */
            list_add(&fence->list, &fence_in_list->list);
            return;
        }
    }

    /* Add as first in list */
    list_add(&fence->list, &list->list);
}

void host1x_poll_add_fence_locked(struct host1x *host, struct host1x_syncpt_fence *fence)
{
    struct host1x_fence_list *fence_list = &fence->sp->fences;

    INIT_LIST_HEAD(&fence->list);
    host1x_poll_add_fence_to_list(fence_list, fence);
}

bool host1x_poll_remove_fence(struct host1x *host, struct host1x_syncpt_fence *fence)
{
    struct host1x_fence_list *fence_list = &fence->sp->fences;
    unsigned long irqflags;

    spin_lock_irqsave(&fence_list->lock, irqflags);

    if (list_empty(&fence->list)) {
        spin_unlock_irqrestore(&fence_list->lock, irqflags);
        return false;
    }
    list_del_init(&fence->list);

    spin_unlock_irqrestore(&fence_list->lock, irqflags);
    return true;
}

static void host1x_pool_timeout_handler(struct work_struct *work)
{
    struct delayed_work       *dwork = (struct delayed_work *)work;
    struct host1x_syncpt_pool *pool  = container_of(dwork,
                                struct host1x_syncpt_pool, pool_work);
    struct host1x_syncpt      *sp;
    struct host1x_syncpt      *tmp_spt;
    struct host1x             *host = pool->host;
    ktime_t ts = ktime_get();

    spin_lock(&pool->syncpt_list.lock);
    list_for_each_entry_safe(sp, tmp_spt, &pool->syncpt_list.list, list) {
        struct host1x_syncpt_fence *fence, *tmp;
        unsigned int value;

        value = host1x_syncpt_load(sp);

        spin_lock(&sp->fences.lock);
        list_for_each_entry_safe(fence, tmp, &sp->fences.list, list) {
            if (((value - fence->threshold) & 0x80000000U) != 0U) {
                /* Fence is not yet expired, we are done */
                break;
            }

            list_del_init(&fence->list);
            host1x_fence_signal(fence ,ts);
        }
        spin_unlock(&sp->fences.lock);
    }
    spin_unlock(&pool->syncpt_list.lock);

    /**
     * TODO: Optimize pool polling mechanism
     */
    schedule_delayed_work(&pool->pool_work,
            msecs_to_jiffies(host->polling_intrval));
}

int host1x_poll_init(struct host1x *host)
{
    unsigned int id;

    for (id = 0; id < host->num_pools; ++id) {
        struct host1x_syncpt_pool *syncpt_pool = &host->pools[id];

        syncpt_pool->host = host;
        spin_lock_init(&syncpt_pool->syncpt_list.lock);
        INIT_LIST_HEAD(&syncpt_pool->syncpt_list.list);

        INIT_DELAYED_WORK(&syncpt_pool->pool_work, host1x_pool_timeout_handler);
    }

    /* Initialize RO-Pool*/
    host->pools[host->ro_pool_id].host = host;
    spin_lock_init(&host->pools[host->ro_pool_id].syncpt_list.lock);
    INIT_LIST_HEAD(&host->pools[host->ro_pool_id].syncpt_list.list);
    INIT_DELAYED_WORK(&host->pools[host->ro_pool_id].pool_work,
            host1x_pool_timeout_handler);

    for (id = 0; id < host1x_syncpt_nb_pts(host); ++id) {
        struct host1x_syncpt *syncpt = &host->syncpt[id];
        struct host1x_syncpt_pool *pool = syncpt->pool;

        spin_lock_init(&syncpt->fences.lock);
        INIT_LIST_HEAD(&syncpt->fences.list);
        INIT_LIST_HEAD(&syncpt->list);

        /* Add syncpoint to pool list*/
        list_add(&syncpt->list, &pool->syncpt_list.list);
    }

    return 0;
}

void host1x_poll_start(struct host1x *host)
{
    int id;

    /*Loop till "host->num_pools + 1" to include Ro-Pool*/
    for (id = 0; id < host->num_pools + 1; ++id) {
        struct host1x_syncpt_pool *syncpt_pool = &host->pools[id];

        schedule_delayed_work(&syncpt_pool->pool_work, msecs_to_jiffies(host->polling_intrval));
    }
}

void host1x_poll_stop(struct host1x *host)
{
    int id;

    /*Loop till "host->num_pools + 1" to include Ro-Pool*/
    for (id = 0; id < host->num_pools + 1; ++id) {
        struct host1x_syncpt_pool *syncpt_pool = &host->pools[id];

        //Schedule delayed work immediately
        schedule_delayed_work(&syncpt_pool->pool_work, 0);
        //Wait for schedule work to complete
        flush_delayed_work(&syncpt_pool->pool_work);
        //Cancel the work as it reschedule itself
        cancel_delayed_work(&syncpt_pool->pool_work);
    }
}
