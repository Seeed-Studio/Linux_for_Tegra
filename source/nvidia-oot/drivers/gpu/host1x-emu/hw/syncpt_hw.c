/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <linux/io.h>

#include "../dev.h"
#include "../syncpt.h"

#define HOST1X_SYNC_SYNCPT(h, x)    ((h) * (x))

/*
 * Write the current syncpoint value back to hw.
 */
static void syncpt_restore(struct host1x_syncpt *sp)
{
    u32 min = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read_min(sp));
    struct host1x *host = sp->host;

    host1x_sync_writel(host, min, HOST1X_SYNC_SYNCPT(host->syncpt_page_size, sp->id));
}

/*
 * Updates the last value read from hardware.
 */
static u32 syncpt_load(struct host1x_syncpt *sp)
{
    u32 old, live;
    struct host1x *host = sp->host;

    /* Loop in case there's a race writing to min_val */
    do {
        old  = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read_min(sp));
        live = host1x_sync_readl(host, HOST1X_SYNC_SYNCPT(host->syncpt_page_size, sp->id));
    } while ((u32)atomic_cmpxchg(&sp->min_val, old, live) != old);

    if (!host1x_syncpt_check_max(sp, live))
        dev_err(host->dev, "%s failed: id=%u, min=%d, max=%d\n",
            __func__, sp->id, HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read_min(sp)),
            HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read_max(sp)));

    return live;
}

/*
 * Write a cpu syncpoint increment to the hardware, without touching
 * the cache.
 */
static int syncpt_cpu_incr(struct host1x_syncpt *sp)
{
    struct host1x *host = sp->host;
    u32 live;

    if (!host1x_syncpt_client_managed(sp) && host1x_syncpt_idle(sp)) {
        dev_err(host->dev, "%s failed: Syncpoint id=%u increment\n",
                            __func__,sp->id);
        return -EINVAL;
    }

    live = host1x_sync_readl(host, HOST1X_SYNC_SYNCPT(host->syncpt_page_size, sp->id));
    host1x_sync_writel(host, (live + 1U),
            HOST1X_SYNC_SYNCPT(host->syncpt_page_size, sp->id));
    wmb();
    return 0;
}

static const struct host1x_syncpt_ops host1x_syncpt_ops = {
    .restore  = syncpt_restore,
    .load     = syncpt_load,
    .cpu_incr = syncpt_cpu_incr,
};
