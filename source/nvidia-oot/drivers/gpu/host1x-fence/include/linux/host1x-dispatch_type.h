/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef __HOST1X_DISPATCH_TYPE_H
#define __HOST1X_DISPATCH_TYPE_H
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/dma-fence.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>

struct host1x;
struct host1x_syncpt;

struct host1x_interface_ops {
//host1x.h Interface
struct dma_fence* (*host1x_fence_create)(struct host1x_syncpt *sp, u32 threshold,bool timeout);
int (*host1x_fence_extract)(struct dma_fence *fence, u32 *id, u32 *threshold);
void (*host1x_fence_cancel)(struct dma_fence *fence);
int (*host1x_fence_get_node)(struct dma_fence *fence);
u64 (*host1x_get_dma_mask)(struct host1x *host1x);
struct host1x_syncpt* (*host1x_syncpt_get)(struct host1x_syncpt *sp);
struct host1x_syncpt* (*host1x_syncpt_get_by_id)(struct host1x *host, u32 id);
struct host1x_syncpt* (*host1x_syncpt_get_by_id_noref)(struct host1x *host, u32 id);
u32 (*host1x_syncpt_read)(struct host1x_syncpt *sp);
u32 (*host1x_syncpt_read_min)(struct host1x_syncpt *sp);
u32 (*host1x_syncpt_read_max)(struct host1x_syncpt *sp);
int (*host1x_syncpt_incr)(struct host1x_syncpt *sp);
u32 (*host1x_syncpt_incr_max)(struct host1x_syncpt *sp, u32 incrs);
struct host1x_syncpt* (*host1x_syncpt_alloc)(struct host1x *host, unsigned long flags, const char *name);
void (*host1x_syncpt_put)(struct host1x_syncpt *sp);
u32 (*host1x_syncpt_id)(struct host1x_syncpt *sp);
int (*host1x_syncpt_wait_ts)(struct host1x_syncpt *sp, u32 thresh, long timeout, u32 *value, ktime_t *ts);
int (*host1x_syncpt_wait)(struct host1x_syncpt *sp, u32 thresh, long timeout,u32 *value);
int (*host1x_syncpt_get_shim_info)(struct host1x *host, phys_addr_t *base,
				u32 *stride, u32 *num_syncpts);

//nvhost.h Interface
void (*host1x_writel)(struct platform_device *pdev, u32 r, u32 v);
struct platform_device* (*nvhost_get_default_device)(void);
struct host1x* (*nvhost_get_host1x)(struct platform_device *pdev);
int (*nvhost_client_device_get_resources)(struct platform_device *pdev);
int (*nvhost_client_device_init)(struct platform_device *pdev);
int (*nvhost_client_device_release)(struct platform_device *pdev);
u32 (*nvhost_get_syncpt_host_managed)(struct platform_device *pdev, u32 param, const char *syncpt_name);
u32 (*nvhost_get_syncpt_client_managed)(struct platform_device *pdev,const char *syncpt_name);
u32 (*nvhost_get_syncpt_gpu_managed)(struct platform_device *pdev,const char *syncpt_name);
void (*nvhost_syncpt_put_ref_ext)(struct platform_device *pdev, u32 id);
bool (*nvhost_syncpt_is_valid_pt_ext)(struct platform_device *pdev, u32 id);
int  (*nvhost_syncpt_is_expired_ext)(struct platform_device *pdev, u32 id, u32 thresh);
void (*nvhost_syncpt_set_min_update)(struct platform_device *pdev, u32 id, u32 val);
int (*nvhost_syncpt_read_ext_check)(struct platform_device *pdev, u32 id, u32 *val);
u32 (*nvhost_syncpt_read_maxval)(struct platform_device *pdev, u32 id);
u32 (*nvhost_syncpt_incr_max_ext)(struct platform_device *pdev, u32 id, u32 incrs);
u32 (*nvhost_syncpt_unit_interface_get_byte_offset_ext)(struct platform_device *pdev,u32 syncpt_id);
u32 (*nvhost_syncpt_unit_interface_get_byte_offset)(u32 syncpt_id);
int (*nvhost_syncpt_unit_interface_get_aperture)(struct platform_device *pdev, u64 *base, size_t *size);
int (*nvhost_syncpt_unit_interface_init)(struct platform_device *pdev);
void (*nvhost_syncpt_unit_interface_deinit)(struct platform_device *pdev);
dma_addr_t (*nvhost_syncpt_address)(struct platform_device *pdev, u32 id);
int (*nvhost_intr_register_notifier)(struct platform_device *pdev,u32 id, u32 thresh,void (*callback)(void *data),void *private_data);
int (*nvhost_module_init)(struct platform_device *pdev);
void (*nvhost_module_deinit)(struct platform_device *pdev);
void (*nvhost_module_reset)(struct platform_device *pdev, bool reboot);
int (*nvhost_module_busy)(struct platform_device *dev);
void (*nvhost_module_idle)(struct platform_device *pdev);
void (*nvhost_module_idle_mult)(struct platform_device *pdev, int refs);
};

bool host1x_wrapper_register_interface(struct host1x_interface_ops *host1x_hw_ops);

#endif /*__HOST1X_DISPATCH_API_H */