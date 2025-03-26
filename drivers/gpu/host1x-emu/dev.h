// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#ifndef HOST1X_DEV_H
#define HOST1X_DEV_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "poll.h"
#include "syncpt.h"

struct output;
struct dentry;
struct host1x_syncpt;

int host1x_user_init(struct host1x *host);
void host1x_sync_writel(struct host1x *host1x, u32 r, u32 v);
u32 host1x_sync_readl(struct host1x *host1x, u32 r);

struct host1x_syncpt_ops {
    void (*restore)(struct host1x_syncpt *syncpt);
    u32  (*load)(struct host1x_syncpt *syncpt);
    int  (*cpu_incr)(struct host1x_syncpt *syncpt);
};


struct host1x_info {
    u64 dma_mask; 				/* mask of addressable memory */
    unsigned int nb_pts; 		/* host1x: number of syncpoints supported */
    int (*init)(struct host1x *host1x); /* initialize per SoC ops */
};

struct host1x {
    const struct host1x_info *info;

    struct device *dev;
    struct dentry *debugfs;
    struct device_dma_parameters dma_parms;

    /* Charected-Dev*/
    int major;
    int next_minor;
    struct class *host1x_class;
    struct cdev cdev;
    struct device *ctrl;

    /* Resources */
    struct host1x_syncpt *syncpt;
    struct host1x_syncpt_base *bases;

    /* Resources accessible by this VM */
    unsigned int syncpt_end;
    unsigned int syncpt_base;
    unsigned int syncpt_count;
    unsigned int polling_intrval;
#ifdef HOST1X_EMU_HRTIMER_FENCE_SCAN
	unsigned int hr_polling_intrval;
#endif
    bool hv_syncpt_mem;
#ifdef HOST1X_EMU_HYPERVISOR
    void __iomem *syncpt_va_apt;   /* syncpoint apperture mapped in kernel space */
#else
    void *syncpt_va_apt;
#endif
    u64 syncpt_phy_apt;  /* syncpoint page size */
    unsigned int syncpt_page_size; /* syncpoint page size */

    /* Restricted syncpoint pools */
    unsigned int num_pools;
    unsigned int ro_pool_id;
    struct host1x_syncpt_pool *pools;

    /* Resource Ops */
    const struct host1x_syncpt_ops *syncpt_op;

    /* Resources Lock */
    struct mutex syncpt_mutex;
};

static inline void host1x_hw_syncpt_restore(struct host1x *host,
                        struct host1x_syncpt *sp)
{
    host->syncpt_op->restore(sp);
}

static inline u32 host1x_hw_syncpt_load(struct host1x *host,
                    struct host1x_syncpt *sp)
{
    return host->syncpt_op->load(sp);
}

static inline int host1x_hw_syncpt_cpu_incr(struct host1x *host,
                        struct host1x_syncpt *sp)
{
    return host->syncpt_op->cpu_incr(sp);
}
#endif
