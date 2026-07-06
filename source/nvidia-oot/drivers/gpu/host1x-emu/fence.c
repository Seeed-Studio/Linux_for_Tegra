// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sync_file.h>
#include <linux/dma-fence.h>
#include <soc/tegra/fuse.h>
#include <soc/tegra/fuse-helper.h>
#include "dev.h"
#include "fence.h"
#include "poll.h"
#include "syncpt.h"

static const char *host1x_syncpt_fence_get_driver_name(struct dma_fence *f)
{
    return "host1x-emu";
}

static const char *host1x_syncpt_fence_get_timeline_name(struct dma_fence *f)
{
    return "syncpoint";
}

static struct host1x_syncpt_fence *to_host1x_fence(struct dma_fence *f)
{
    return container_of(f, struct host1x_syncpt_fence, dfence);
}

static bool host1x_syncpt_fence_signaled(struct dma_fence *f)
{
    struct host1x_syncpt_fence *sf = to_host1x_fence(f);

    return host1x_syncpt_is_expired(sf->sp, sf->threshold) || f->error;
}

static bool host1x_syncpt_fence_enable_signaling(struct dma_fence *f)
{
    struct host1x_syncpt_fence *sf = to_host1x_fence(f);

    if (host1x_syncpt_is_expired(sf->sp, sf->threshold))
        return false;

    /* Reference for poll path. */
    dma_fence_get(f);

    /*
     * The dma_fence framework requires the fence driver to keep a
     * reference to any fences for which 'enable_signaling' has been
     * called (and that have not been signalled).
     *
     * We cannot currently always guarantee that all fences get signalled
     * or cancelled. As such, for such situations, set up a timeout, so
     * that long-lasting fences will get reaped eventually.
     */
    if (sf->timeout) {
        /* Reference for timeout path. */
        dma_fence_get(f);
        schedule_delayed_work(&sf->timeout_work, msecs_to_jiffies(30000));
    }

    host1x_poll_add_fence_locked(sf->sp->host, sf);

    return true;
}

static void host1x_syncpt_fence_release(struct dma_fence *fence)
{
	struct host1x_syncpt_fence *sf = to_host1x_fence(fence);

	kfree(sf);
}

const struct dma_fence_ops host1x_syncpt_fence_ops = {
	.get_driver_name   = host1x_syncpt_fence_get_driver_name,
	.get_timeline_name = host1x_syncpt_fence_get_timeline_name,
	.enable_signaling  = host1x_syncpt_fence_enable_signaling,
	.signaled          = host1x_syncpt_fence_signaled,
	.release           = host1x_syncpt_fence_release,
};

static void host1x_fence_timeout_handler(struct work_struct *work)
{
    struct delayed_work *dwork    = (struct delayed_work *)work;
    struct host1x_syncpt_fence *sf =
        container_of(dwork, struct host1x_syncpt_fence, timeout_work);

    if (atomic_xchg(&sf->signaling, 1)) {
        /* Already on poll path, drop timeout path reference if any. */
        if (sf->timeout)
            dma_fence_put(&sf->dfence);
        return;
    }

    if (host1x_poll_remove_fence(sf->sp->host, sf)) {
        /*
         * Managed to remove fence from queue, so it's safe to drop
         * the poll path's reference.
         */
        dma_fence_put(&sf->dfence);
    }
    dma_fence_set_error(&sf->dfence, -ETIMEDOUT);
    dma_fence_signal(&sf->dfence);

    /* Drop timeout path reference if any. */
    if (sf->timeout)
        dma_fence_put(&sf->dfence);
}

void host1x_fence_signal(struct host1x_syncpt_fence *sf, ktime_t ts)
{
    if (atomic_xchg(&sf->signaling, 1)) {
        /*
         * Already on timeout path, but we removed the fence before
         * timeout path could, so drop poll path reference.
         */
        dma_fence_put(&sf->dfence);
        return;
    }

    if (sf->timeout && cancel_delayed_work(&sf->timeout_work)) {
        /*
         * We know that the timeout path will not be entered.
         * Safe to drop the timeout path's reference now.
         */
        dma_fence_put(&sf->dfence);
    }

    dma_fence_signal_timestamp_locked(&sf->dfence, ts);

    /*Drop poll path reference*/
    dma_fence_put(&sf->dfence);
}

HOST1X_EMU_EXPORT_DECL(struct dma_fence*, host1x_fence_create(struct host1x_syncpt *sp,
                                      u32  threshold,
                                      bool timeout))
{
    struct host1x_syncpt_fence *fence;

    if (!tegra_platform_is_silicon()) {
        dev_info_once(sp->host->dev, "fence timeout disabled due to pre-silicon platform\n");
        timeout = false;
    }

    /* Allocate memory for Host1x-Fence*/
    fence = kzalloc(sizeof(*fence), GFP_KERNEL);
    if (!fence) {
        return ERR_PTR(-ENOMEM);
    }
    fence->sp        = sp;
    fence->threshold = threshold;
    fence->timeout   = timeout;

    dma_fence_init(&fence->dfence,
                   &host1x_syncpt_fence_ops,
                   &sp->fences.lock,
                   dma_fence_context_alloc(1),
                   0);
    INIT_DELAYED_WORK(&fence->timeout_work, host1x_fence_timeout_handler);
    return &fence->dfence;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_fence_create);

HOST1X_EMU_EXPORT_DECL(int, host1x_fence_extract(struct dma_fence *dfence, u32 *id, u32 *threshold))
{
    struct host1x_syncpt_fence *sf;

    if (dfence->ops != &host1x_syncpt_fence_ops)
        return -EINVAL;

    sf = to_host1x_fence(dfence);

    *id = sf->sp->id;
    *threshold = sf->threshold;

    return 0;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_fence_extract);

HOST1X_EMU_EXPORT_DECL(int, host1x_fence_get_node(struct dma_fence *dfence))
{
	struct host1x_syncpt_fence *sf;
	int node;

	if (dfence->ops != &host1x_syncpt_fence_ops)
		return -EINVAL;

	sf = to_host1x_fence(dfence);
	node = dev_to_node(sf->sp->host->dev);
	return node == NUMA_NO_NODE ? 0 : node;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_fence_get_node);

HOST1X_EMU_EXPORT_DECL(void, host1x_fence_cancel(struct dma_fence *dfence))
{
    struct host1x_syncpt_fence *sf = to_host1x_fence(dfence);

    schedule_delayed_work(&sf->timeout_work, 0);
    flush_delayed_work(&sf->timeout_work);
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_fence_cancel);

HOST1X_EMU_EXPORT_DECL(void, host1x_syncpt_fence_scan(struct host1x_syncpt *sp))
{
	host1x_poll_irq_check_syncpt_fence(sp);
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_fence_scan);
