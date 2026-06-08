// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#ifndef __LINUX_HOST1X_H
#define __LINUX_HOST1X_H

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-fence.h>
#include <linux/spinlock.h>
#include <linux/timekeeping.h>
#include <linux/types.h>
#include <linux/symbol-emu.h>

/**
 * TODO: Remove header after pre-silicon verification
 * This header is required till emulation driver verification is done on Tho-r
 * VDK platform. Since we are exporting new kernel symbol, header with declarion
 * of modified symbols is required
 *
 * Changed cannot be added in orignal Host1x driver header files ("linux/nvhost.h"
 * and "host1x-next.h") due to Host1x driver and other dependent drivers. Note in
 * Tho-r verification config both the driver will co-exist.
 *
 * Some data structure are also re-delacred in this header for case where client
 * driver wants to verify over emulation driver.
 */

/*
 * host1x syncpoints
 */
#define HOST1X_SYNCPT_CLIENT_MANAGED    (1 << 0)
#define HOST1X_SYNCPT_HAS_BASE          (1 << 1)
#define HOST1X_SYNCPT_GPU               (1 << 2)

struct host1x;
struct host1x_syncpt;

HOST1X_EMU_EXPORT_DECL(u64, host1x_get_dma_mask(struct host1x *host1x));

HOST1X_EMU_EXPORT_DECL(struct host1x_syncpt*, host1x_syncpt_get_by_id(
                struct host1x *host, u32 id));

HOST1X_EMU_EXPORT_DECL(struct host1x_syncpt*, host1x_syncpt_get_by_id_noref(
                struct host1x *host, u32 id));

HOST1X_EMU_EXPORT_DECL(struct host1x_syncpt*, host1x_syncpt_get(
                struct host1x_syncpt *sp));

HOST1X_EMU_EXPORT_DECL(u32, host1x_syncpt_id(struct host1x_syncpt *sp));

HOST1X_EMU_EXPORT_DECL(u32, host1x_syncpt_read_min(struct host1x_syncpt *sp));

HOST1X_EMU_EXPORT_DECL(u32, host1x_syncpt_read_max(struct host1x_syncpt *sp));

HOST1X_EMU_EXPORT_DECL(u32, host1x_syncpt_read(struct host1x_syncpt *sp));

HOST1X_EMU_EXPORT_DECL(int, host1x_syncpt_incr(struct host1x_syncpt *sp));

HOST1X_EMU_EXPORT_DECL(u32, host1x_syncpt_incr_max(struct host1x_syncpt *sp,
                u32 incrs));

HOST1X_EMU_EXPORT_DECL(void, host1x_syncpt_fence_scan(struct host1x_syncpt *sp));

HOST1X_EMU_EXPORT_DECL(int, host1x_syncpt_wait_ts(struct host1x_syncpt *sp,
                u32 thresh, long timeout, u32 *value, ktime_t *ts));

HOST1X_EMU_EXPORT_DECL(int, host1x_syncpt_wait(struct host1x_syncpt *sp,
                u32 thresh, long timeout, u32 *value));

HOST1X_EMU_EXPORT_DECL(void, host1x_syncpt_put(struct host1x_syncpt *sp));

HOST1X_EMU_EXPORT_DECL(struct host1x_syncpt*, host1x_syncpt_alloc(struct host1x *host,
                      unsigned long flags,
                      const char *name));

HOST1X_EMU_EXPORT_DECL(struct dma_fence*, host1x_fence_create(struct host1x_syncpt *sp, u32 threshold,
                      bool timeout));

HOST1X_EMU_EXPORT_DECL(int, host1x_fence_extract(struct dma_fence *fence, u32 *id, u32 *threshold));

HOST1X_EMU_EXPORT_DECL(int, host1x_fence_get_node(struct dma_fence *fence));

HOST1X_EMU_EXPORT_DECL(void, host1x_fence_cancel(struct dma_fence *fence));

HOST1X_EMU_EXPORT_DECL(int, host1x_syncpt_get_shim_info(struct host1x *host,
				phys_addr_t *base,
				u32 *stride,
				u32 *num_syncpts));

#endif
