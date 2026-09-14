// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include "dev.h"
#include "fence.h"
#include "poll.h"
#include <soc/tegra/fuse.h>
#include <soc/tegra/fuse-helper.h>

#ifdef HOST1X_EMU_HRTIMER_FENCE_SCAN
#define HRTIMER_STATE_STOPPED		0U
#define HRTIMER_STATE_SCHEDULED		1U
#define HRTIMER_STATE_RUNNING		2U

static atomic_t hr_polling_active;
struct host1x  *hr_timer_host;
static struct hrtimer emu_hr_timer;

//Timer Callback function. This will be called when timer expires
static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
	unsigned int  bmap_idx;
	unsigned int  sp_id;
	unsigned long irqflags;
	struct host1x_syncpt      *sp;
	struct host1x             *host = hr_timer_host;
	ktime_t ts = ktime_get();
	bool fence_pend = false;

	/* Setting timer state to scanning */
	atomic_set(&hr_polling_active, HRTIMER_STATE_RUNNING);
	for (bmap_idx = 0; bmap_idx < host->sync_bmap_cnt; ++bmap_idx) {
		int           ffs_bit;
		unsigned long syncpt_map_val;

		syncpt_map_val = host->sync_bmap[bmap_idx];
		while ((ffs_bit = fls64(syncpt_map_val)) != 0) {
			unsigned int value;
			struct host1x_syncpt_fence *fence, *tmp;

			sp_id = (ffs_bit - 1) + (bmap_idx * SYNCPT_FENCE_BMAP_BITS);
			sp = &host->syncpt[sp_id];
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

			/* Clear bit in global syncpoint-fence bitmap if fence list empty
			 * else set fence_pend flag if any fence pending
			 */
			if (list_empty(&sp->fences.list) == true)
				clear_bit(ffs_bit-1, &host->sync_bmap[bmap_idx]);
			else
				fence_pend = true;

			spin_unlock_irqrestore(&sp->fences.lock, irqflags);

			/* Clear bit in local bitmap to progress loop for next set-bit */
			clear_bit(ffs_bit-1, &syncpt_map_val);
		}
	}

	if (fence_pend != false)
		atomic_set(&hr_polling_active, HRTIMER_STATE_SCHEDULED);

	if (atomic_cmpxchg(&hr_polling_active,
			HRTIMER_STATE_RUNNING, HRTIMER_STATE_STOPPED) == HRTIMER_STATE_SCHEDULED) {
		hrtimer_forward_now(timer, ktime_set(HRTIMER_TIMEOUT_SEC, host->hr_polling_intrval));
		return HRTIMER_RESTART;
	}
#ifdef HOST1X_EMU_SYNCPT_DEBUG
		pr_info("Host1x-EMU: No Fence-Pending, stoping HRtimer\n");
#endif
	return HRTIMER_NORESTART;
}
#endif

static void host1x_poll_add_fence_to_list(struct host1x_fence_list *list,
					  struct host1x_syncpt_fence *fence)
{
	unsigned int hr_state;
	unsigned int bitmap_indx;
	unsigned int bitmap_bit;
	struct host1x_syncpt_fence *fence_in_list;
	struct host1x_syncpt_pool *pool;

	list_for_each_entry_reverse(fence_in_list, &list->list, list) {
		if ((s32)(fence_in_list->threshold - fence->threshold) <= 0) {
			/* Fence in list is before us, we can insert here */
			list_add(&fence->list, &fence_in_list->list);
			goto sched_polling;
		}
	}

	/* Add as first in list */
	list_add(&fence->list, &list->list);

sched_polling:
	bitmap_indx = fence->sp->id / SYNCPT_FENCE_BMAP_BITS;
	bitmap_bit  = fence->sp->id % SYNCPT_FENCE_BMAP_BITS;
	set_bit(bitmap_bit, &fence->sp->host->sync_bmap[bitmap_indx]);
	pool = fence->sp->pool;

#ifdef HOST1X_EMU_KTHREAD_FENCE_SCAN
	atomic_set(&pool->polling_active, 1);
#ifdef HOST1X_EMU_KTHREAD_HIGHPRI_WQ
	queue_delayed_work(pool->host->host1x_wq,
						&pool->pool_work,
						msecs_to_jiffies(pool->host->polling_intrval));
#else
	schedule_delayed_work(&pool->pool_work,
			msecs_to_jiffies(pool->host->polling_intrval));
#endif
#endif /* HOST1X_EMU_KTHREAD_FENCE_SCAN */

#ifdef HOST1X_EMU_HRTIMER_FENCE_SCAN
	hr_state = atomic_xchg(&hr_polling_active, HRTIMER_STATE_SCHEDULED);
	/* CASE 1: if (hr_state == 0)
	 * HRTimer was stoped, need to restart
	 *
	 * CASE 2: if (hr_state == 2)
	 * HRTimer is scanning and sice we have changed the state
	 * to scheduled the HRimer handler will reschedule/restart
	 * itself. No need to start timer here.
	 *
	 * CASE 3: if (hr_state == 1)
	 * HRTimer is reschedule/restart by HRimer handler itself.
	 * No need to start timer here.
	 */
	if (hr_state == HRTIMER_STATE_STOPPED) {
		if (tegra_platform_is_silicon()) {
			ktime_t ktime;

			ktime = ktime_set(HRTIMER_TIMEOUT_SEC, pool->host->hr_polling_intrval);
			hrtimer_start(&emu_hr_timer, ktime, HRTIMER_MODE_REL);
#ifdef HOST1X_EMU_SYNCPT_DEBUG
			pr_info("Host1x-EMU: Starting HRTIMER Polling\n");
#endif
		}
	}
#endif /* HOST1X_EMU_HRTIMER_FENCE_SCAN */
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

#ifdef HOST1X_EMU_KTHREAD_FENCE_SCAN
static void host1x_pool_timeout_handler(struct work_struct *work)
{
	unsigned int              bmap_idx;
	struct delayed_work       *dwork = (struct delayed_work *)work;
	struct host1x_syncpt_pool *pool  = container_of(dwork,
								struct host1x_syncpt_pool, pool_work);
	struct host1x_syncpt      *sp;
	struct host1x             *host = pool->host;
	bool                      fence_pend = false;

	atomic_set(&pool->polling_active, 0);
	for (bmap_idx = 0; bmap_idx < host->sync_bmap_cnt; ++bmap_idx) {
		int ffs_bit;
		/* global-bitmap "&" with pool-mask to check only syncpoints in pool*/
		unsigned long syncpt_map_val = host->sync_bmap[bmap_idx] & pool->sync_bmap[bmap_idx];

		while ((ffs_bit = fls64(syncpt_map_val)) != 0) {
			unsigned int sp_id;

			sp_id = (ffs_bit - 1) + (bmap_idx * SYNCPT_FENCE_BMAP_BITS);
			sp = &host->syncpt[sp_id];
			if (host1x_poll_irq_check_syncpt_fence(sp) == true)
				fence_pend = true;

			/* Clear bit in local bitmap to progress the while-loop*/
			clear_bit((ffs_bit - 1), &syncpt_map_val);
		}
	}

	if ((fence_pend == true) || (atomic_read(&pool->polling_active) != 0)) {
		atomic_set(&pool->polling_active, 1);
		schedule_delayed_work(&pool->pool_work,
				msecs_to_jiffies(host->polling_intrval));
	}
#ifdef HOST1X_EMU_SYNCPT_DEBUG
	else {
		pr_info("Host1x-EMU: No Fence-Pending, stoping KTHREAD\n");
	}
#endif
}
#endif /*HOST1X_EMU_KTHREAD_FENCE_SCAN*/

int host1x_poll_init(struct host1x *host)
{
	unsigned int id;

	for (id = 0; id < host->num_pools; ++id) {
		struct host1x_syncpt_pool *syncpt_pool = &host->pools[id];

		syncpt_pool->host = host;
		spin_lock_init(&syncpt_pool->syncpt_list.lock);
		INIT_LIST_HEAD(&syncpt_pool->syncpt_list.list);

#ifdef HOST1X_EMU_KTHREAD_FENCE_SCAN
		INIT_DELAYED_WORK(&syncpt_pool->pool_work, host1x_pool_timeout_handler);
#endif
	}

	/* Initialize RO-Pool*/
	host->pools[host->ro_pool_id].host = host;
	spin_lock_init(&host->pools[host->ro_pool_id].syncpt_list.lock);
	INIT_LIST_HEAD(&host->pools[host->ro_pool_id].syncpt_list.list);
#ifdef HOST1X_EMU_KTHREAD_FENCE_SCAN
	INIT_DELAYED_WORK(&host->pools[host->ro_pool_id].pool_work,
			host1x_pool_timeout_handler);
#endif

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

bool host1x_poll_irq_check_syncpt_fence(struct host1x_syncpt  *sp)
{
	unsigned int value;
	unsigned long irqflags;
	struct host1x_syncpt_fence *fence, *tmp;
	ktime_t ts = ktime_get();
	bool fence_pend = false;

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

	/* Set fence_pend flag if any fence pending */
	if (list_empty(&sp->fences.list) != true) {
		fence_pend = true;
	} else {
		/* Clear bit in global syncpoint-fence bitmap */
		clear_bit((sp->id % SYNCPT_FENCE_BMAP_BITS),
					&sp->host->sync_bmap[(sp->id / SYNCPT_FENCE_BMAP_BITS)]);
	}
	spin_unlock_irqrestore(&sp->fences.lock, irqflags);

	return fence_pend;
}

void host1x_poll_start(struct host1x *host)
{
#ifdef HOST1X_EMU_KTHREAD_FENCE_SCAN
	int id;

#ifdef HOST1X_EMU_KTHREAD_HIGHPRI_WQ
	/* Create Workqueu pool*/
	if (host->host1x_wq == NULL) {
		host->host1x_wq = alloc_workqueue("host1x-emu-wq",
						WQ_UNBOUND|WQ_HIGHPRI, host->num_pools + 1);
	}
#endif
#endif /*HOST1X_EMU_KTHREAD_FENCE_SCAN*/

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
		atomic_set(&hr_polling_active, HRTIMER_STATE_SCHEDULED);

		hrtimer_start(&emu_hr_timer, ktime, HRTIMER_MODE_REL);
	}
#endif

#ifdef HOST1X_EMU_KTHREAD_FENCE_SCAN
	/*Loop till "host->num_pools + 1" to include Ro-Pool*/
	for (id = 0; id < host->num_pools + 1; ++id) {
		struct host1x_syncpt_pool *syncpt_pool = &host->pools[id];

#ifdef HOST1X_EMU_KTHREAD_HIGHPRI_WQ
		queue_delayed_work(host->host1x_wq,
			&syncpt_pool->pool_work,
			msecs_to_jiffies(host->polling_intrval));
#else
		schedule_delayed_work(&syncpt_pool->pool_work, msecs_to_jiffies(host->polling_intrval));
#endif
		atomic_set(&syncpt_pool->polling_active, 1);
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

	atomic_set(&hr_polling_active, HRTIMER_STATE_STOPPED);
#endif

#ifdef HOST1X_EMU_KTHREAD_FENCE_SCAN
	/*Loop till "host->num_pools + 1" to include Ro-Pool*/
	for (id = 0; id < host->num_pools + 1; ++id) {
		struct host1x_syncpt_pool *syncpt_pool = &host->pools[id];

		//Schedule delayed work immediately
#ifdef HOST1X_EMU_KTHREAD_HIGHPRI_WQ
		queue_delayed_work(host->host1x_wq,
						&syncpt_pool->pool_work, 0);
#else
		schedule_delayed_work(&syncpt_pool->pool_work, 0);
#endif
		//Wait for schedule work to complete
		flush_delayed_work(&syncpt_pool->pool_work);
		//Cancel the work as it reschedule itself
		cancel_delayed_work(&syncpt_pool->pool_work);
		//Set kthread polling state to stoped
		atomic_set(&syncpt_pool->polling_active, 0);
	}
#endif
}
