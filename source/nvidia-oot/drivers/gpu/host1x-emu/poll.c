// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include "dev.h"
#include "fence.h"
#include "poll.h"
#include <soc/tegra/fuse.h>
#include <soc/tegra/fuse-helper.h>

#ifdef HOST1X_EMU_HRTIMER_FENCE_SCAN
struct host1x  *hr_timer_host;
static struct hrtimer emu_hr_timer;

//Timer Callback function. This will be called when timer expires
static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
	int id;
	unsigned long irqflags;
	struct host1x_syncpt      *sp;
	struct host1x_syncpt      *tmp_spt;
	struct host1x             *host = hr_timer_host;
	ktime_t ts = ktime_get();

	for (id = 0; id < host->num_pools + 1; ++id) {
		struct host1x_syncpt_pool *pool = &host->pools[id];

		list_for_each_entry_safe(sp, tmp_spt, &pool->syncpt_list.list, list) {
			struct host1x_syncpt_fence *fence, *tmp;
			unsigned int value;

			value = host1x_syncpt_load(sp);

			spin_lock_irqsave(&sp->fences.lock, irqflags);
			list_for_each_entry_safe(fence, tmp, &sp->fences.list, list) {
				if (((value - fence->threshold) & 0x80000000U) != 0U) {
					/* Fence is not yet expired, we are done */
					break;
				}

				list_del_init(&fence->list);
				host1x_fence_signal(fence, ts);
			}
			spin_unlock_irqrestore(&sp->fences.lock, irqflags);
		}
	}
	hrtimer_forward_now(timer, ktime_set(HRTIMER_TIMEOUT_SEC, host->hr_polling_intrval));
	return HRTIMER_RESTART;
}
#endif

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

	list_for_each_entry_safe(sp, tmp_spt, &pool->syncpt_list.list, list) {
		host1x_poll_irq_check_syncpt_fence(sp);
	}

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
	list_add_tail(&syncpt->list, &pool->syncpt_list.list);
    }

	return 0;
}

void host1x_poll_irq_check_syncpt_fence(struct host1x_syncpt  *sp)
{
	unsigned int value;
	unsigned long irqflags;
	struct host1x_syncpt_fence *fence, *tmp;
	ktime_t ts = ktime_get();

	value = host1x_syncpt_load(sp);
	spin_lock_irqsave(&sp->fences.lock, irqflags);
	list_for_each_entry_safe(fence, tmp, &sp->fences.list, list) {
		if (((value - fence->threshold) & 0x80000000U) != 0U) {
			/* Fence is not yet expired, we are done */
			break;
		}

		list_del_init(&fence->list);
		host1x_fence_signal(fence, ts);
	}
	spin_unlock_irqrestore(&sp->fences.lock, irqflags);
}

void host1x_poll_start(struct host1x *host)
{
#ifdef HOST1X_EMU_KTHREAD_FENCE_SCAN
	int id;
#endif

#ifdef HOST1X_EMU_HRTIMER_FENCE_SCAN
	if (tegra_platform_is_silicon()) {
		ktime_t ktime;

		hr_timer_host = host;
		ktime = ktime_set(HRTIMER_TIMEOUT_SEC, host->hr_polling_intrval);
#if defined(NV_HRTIMER_SETUP_PRESENT) /* Linux v6.13 */
		hrtimer_setup(&emu_hr_timer, &timer_callback, CLOCK_MONOTONIC,
			      HRTIMER_MODE_REL);
#else
		hrtimer_init(&emu_hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		emu_hr_timer.function = &timer_callback;
#endif
		hrtimer_start(&emu_hr_timer, ktime, HRTIMER_MODE_REL);
	}
#endif

#ifdef HOST1X_EMU_KTHREAD_FENCE_SCAN
	/*Loop till "host->num_pools + 1" to include Ro-Pool*/
	for (id = 0; id < host->num_pools + 1; ++id) {
		struct host1x_syncpt_pool *syncpt_pool = &host->pools[id];

		schedule_delayed_work(&syncpt_pool->pool_work, msecs_to_jiffies(host->polling_intrval));
	}
#endif
}

void host1x_poll_stop(struct host1x *host)
{
#ifdef HOST1X_EMU_KTHREAD_FENCE_SCAN
	int id;
#endif

#ifdef HOST1X_EMU_HRTIMER_FENCE_SCAN
	if (tegra_platform_is_silicon())
		hrtimer_cancel(&emu_hr_timer);
#endif

#ifdef HOST1X_EMU_KTHREAD_FENCE_SCAN
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
#endif
}
