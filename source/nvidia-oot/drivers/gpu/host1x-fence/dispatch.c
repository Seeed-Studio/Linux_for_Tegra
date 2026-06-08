// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "include/linux/host1x-dispatch_type.h"
#include "include/linux/host1x-dispatch_api.h"
#include <linux/host1x-next.h>
#include <linux/nvhost.h>

bool host1x_wrapper_init(void);

struct host1x_interface_ops  host1x_api_table = {NULL};

static bool host1x_wrapper_init_default_interface(void)
{
	host1x_api_table.host1x_fence_create                = host1x_fence_create;
	host1x_api_table.host1x_fence_extract               = host1x_fence_extract;
	host1x_api_table.host1x_fence_cancel                = host1x_fence_cancel;
	host1x_api_table.host1x_fence_get_node              = host1x_fence_get_node;
	host1x_api_table.host1x_get_dma_mask                = host1x_get_dma_mask;
	host1x_api_table.host1x_syncpt_get                  = host1x_syncpt_get;
	host1x_api_table.host1x_syncpt_get_by_id            = host1x_syncpt_get_by_id;
	host1x_api_table.host1x_syncpt_get_by_id_noref      = host1x_syncpt_get_by_id_noref;
	host1x_api_table.host1x_syncpt_read                 = host1x_syncpt_read;
	host1x_api_table.host1x_syncpt_read_min             = host1x_syncpt_read_min;
	host1x_api_table.host1x_syncpt_read_max             = host1x_syncpt_read_max;
	host1x_api_table.host1x_syncpt_incr                 = host1x_syncpt_incr;
	host1x_api_table.host1x_syncpt_incr_max             = host1x_syncpt_incr_max;
	host1x_api_table.host1x_syncpt_alloc                = host1x_syncpt_alloc;
	host1x_api_table.host1x_syncpt_put                  = host1x_syncpt_put;
	host1x_api_table.host1x_syncpt_id                   = host1x_syncpt_id;
	host1x_api_table.host1x_syncpt_wait_ts              = host1x_syncpt_wait_ts;
	host1x_api_table.host1x_syncpt_wait                 = host1x_syncpt_wait;
	host1x_api_table.host1x_syncpt_get_shim_info        = host1x_syncpt_get_shim_info;
	// Interface for nvhost.h
	host1x_api_table.host1x_writel                      = host1x_writel;
	host1x_api_table.nvhost_get_default_device          = nvhost_get_default_device;
	host1x_api_table.nvhost_get_host1x                  = nvhost_get_host1x;
	host1x_api_table.nvhost_client_device_get_resources = nvhost_client_device_get_resources;
	host1x_api_table.nvhost_client_device_init          = nvhost_client_device_init;
	host1x_api_table.nvhost_client_device_release       = nvhost_client_device_release;
	host1x_api_table.nvhost_get_syncpt_host_managed     = nvhost_get_syncpt_host_managed;
	host1x_api_table.nvhost_get_syncpt_client_managed   = nvhost_get_syncpt_client_managed;
	host1x_api_table.nvhost_get_syncpt_gpu_managed      = nvhost_get_syncpt_gpu_managed;
	host1x_api_table.nvhost_syncpt_put_ref_ext          = nvhost_syncpt_put_ref_ext;
	host1x_api_table.nvhost_syncpt_is_valid_pt_ext      = nvhost_syncpt_is_valid_pt_ext;
	host1x_api_table.nvhost_syncpt_is_expired_ext       = nvhost_syncpt_is_expired_ext;
	host1x_api_table.nvhost_syncpt_set_min_update       = nvhost_syncpt_set_min_update;
	host1x_api_table.nvhost_syncpt_read_ext_check       = nvhost_syncpt_read_ext_check;
	host1x_api_table.nvhost_syncpt_read_maxval          = nvhost_syncpt_read_maxval;
	host1x_api_table.nvhost_syncpt_incr_max_ext         = nvhost_syncpt_incr_max_ext;
	host1x_api_table.nvhost_syncpt_unit_interface_get_byte_offset_ext   = nvhost_syncpt_unit_interface_get_byte_offset_ext;
	host1x_api_table.nvhost_syncpt_unit_interface_get_byte_offset       = nvhost_syncpt_unit_interface_get_byte_offset;
	host1x_api_table.nvhost_syncpt_unit_interface_get_aperture          = nvhost_syncpt_unit_interface_get_aperture;
	host1x_api_table.nvhost_syncpt_unit_interface_init                  = nvhost_syncpt_unit_interface_init;
	host1x_api_table.nvhost_syncpt_unit_interface_deinit                = nvhost_syncpt_unit_interface_deinit;
	host1x_api_table.nvhost_syncpt_address              = nvhost_syncpt_address;
	host1x_api_table.nvhost_intr_register_notifier      = nvhost_intr_register_notifier;
	host1x_api_table.nvhost_module_init                 = nvhost_module_init;
	host1x_api_table.nvhost_module_deinit               = nvhost_module_deinit;
	host1x_api_table.nvhost_module_idle_mult            = nvhost_module_idle_mult;

	return true;
}

bool host1x_wrapper_init(void)
{
	host1x_wrapper_init_default_interface();
	pr_info("Host1x-Fence: Default interface init done\n");
	return true;
}

bool host1x_wrapper_register_interface(struct host1x_interface_ops *host1x_ops)
{

	if (host1x_ops == NULL)
		return false;

	host1x_api_table = *host1x_ops;
	return true;
}
EXPORT_SYMBOL(host1x_wrapper_register_interface);

struct dma_fence* wrap_host1x_fence_create(struct host1x_syncpt *sp,
									  u32  threshold,
									  bool timeout)
{
	return host1x_api_table.host1x_fence_create(sp, threshold, timeout);
}
EXPORT_SYMBOL(wrap_host1x_fence_create);

int wrap_host1x_fence_extract(struct dma_fence *dfence, u32 *id, u32 *threshold)
{
	return host1x_api_table.host1x_fence_extract(dfence, id, threshold);
}
EXPORT_SYMBOL(wrap_host1x_fence_extract);

void wrap_host1x_fence_cancel(struct dma_fence *dfence)
{
	return host1x_api_table.host1x_fence_cancel(dfence);
}
EXPORT_SYMBOL(wrap_host1x_fence_cancel);

int wrap_host1x_fence_get_node(struct dma_fence *fence)
{
	return host1x_api_table.host1x_fence_get_node(fence);
}
EXPORT_SYMBOL(wrap_host1x_fence_get_node);

// Syncpt.c
u64 wrap_host1x_get_dma_mask(struct host1x *host1x)
{
	return host1x_api_table.host1x_get_dma_mask(host1x);
}
EXPORT_SYMBOL(wrap_host1x_get_dma_mask);

struct host1x_syncpt* wrap_host1x_syncpt_get(
								struct host1x_syncpt *sp)
{
	return host1x_api_table.host1x_syncpt_get(sp);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_get);

struct host1x_syncpt* wrap_host1x_syncpt_get_by_id(
								struct host1x *host, unsigned int id)
{
	return host1x_api_table.host1x_syncpt_get_by_id(host, id);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_get_by_id);

struct host1x_syncpt* wrap_host1x_syncpt_get_by_id_noref(struct host1x *host, unsigned int id)
{
	return host1x_api_table.host1x_syncpt_get_by_id_noref(host, id);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_get_by_id_noref);

u32 wrap_host1x_syncpt_read(struct host1x_syncpt *sp)
{
	return host1x_api_table.host1x_syncpt_read(sp);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_read);

u32 wrap_host1x_syncpt_read_min(struct host1x_syncpt *sp)
{
	return host1x_api_table.host1x_syncpt_read_min(sp);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_read_min);

u32 wrap_host1x_syncpt_read_max(struct host1x_syncpt *sp)
{
	return host1x_api_table.host1x_syncpt_read_max(sp);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_read_max);

int wrap_host1x_syncpt_incr(struct host1x_syncpt *sp)
{
	return host1x_api_table.host1x_syncpt_incr(sp);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_incr);

u32 wrap_host1x_syncpt_incr_max(struct host1x_syncpt *sp, u32 incrs)
{
	return host1x_api_table.host1x_syncpt_incr_max(sp, incrs);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_incr_max);

struct host1x_syncpt* wrap_host1x_syncpt_alloc(struct host1x *host,
								unsigned long flags,
								const char *name)
{
	return host1x_api_table.host1x_syncpt_alloc(host, flags, name);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_alloc);

void wrap_host1x_syncpt_put(struct host1x_syncpt *sp)
{
	return host1x_api_table.host1x_syncpt_put(sp);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_put);

u32 wrap_host1x_syncpt_id(struct host1x_syncpt *sp)
{
	return host1x_api_table.host1x_syncpt_id(sp);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_id);

int wrap_host1x_syncpt_wait_ts(struct host1x_syncpt *sp,
							u32 thresh, long timeout, u32 *value, ktime_t *ts)
{
	return host1x_api_table.host1x_syncpt_wait_ts(sp, thresh, timeout, value, ts);
}
EXPORT_SYMBOL(wrap_host1x_syncpt_wait_ts);

int wrap_host1x_syncpt_wait(struct host1x_syncpt *sp,
						u32 thresh, long timeout, u32 *value)
{
	if (host1x_api_table.host1x_syncpt_wait != NULL)
		return host1x_api_table.host1x_syncpt_wait(sp, thresh, timeout, value);
	return -ENOSYS;
}
EXPORT_SYMBOL(wrap_host1x_syncpt_wait);

/**
 * Wrapper function for host1x_syncpt_get_shim_info
 */
int wrap_host1x_syncpt_get_shim_info(struct host1x *host,
						 phys_addr_t *base, u32 *stride, u32 *num_syncpts)
{

	return host1x_api_table.host1x_syncpt_get_shim_info(host, base, stride, num_syncpts);
	return -ENOSYS;
}
EXPORT_SYMBOL(wrap_host1x_syncpt_get_shim_info);

// nvhost.c
void wrap_host1x_writel(struct platform_device *pdev, u32 r, u32 v)
{
	return host1x_api_table.host1x_writel(pdev, r, v);
}
EXPORT_SYMBOL(wrap_host1x_writel);

struct platform_device* wrap_nvhost_get_default_device(void)
{
	return host1x_api_table.nvhost_get_default_device();
}
EXPORT_SYMBOL(wrap_nvhost_get_default_device);

struct host1x* wrap_nvhost_get_host1x(struct platform_device *pdev)
{
	return host1x_api_table.nvhost_get_host1x(pdev);
}
EXPORT_SYMBOL(wrap_nvhost_get_host1x);

int wrap_nvhost_client_device_get_resources(struct platform_device *pdev)
{
	return host1x_api_table.nvhost_client_device_get_resources(pdev);
}
EXPORT_SYMBOL(wrap_nvhost_client_device_get_resources);

int wrap_nvhost_client_device_init(struct platform_device *pdev)
{
	return host1x_api_table.nvhost_client_device_init(pdev);
}
EXPORT_SYMBOL(wrap_nvhost_client_device_init);

int wrap_nvhost_client_device_release(struct platform_device *pdev)
{
	return host1x_api_table.nvhost_client_device_release(pdev);
}
EXPORT_SYMBOL(wrap_nvhost_client_device_release);

u32 wrap_nvhost_get_syncpt_host_managed(struct platform_device *pdev,
					u32 param, const char *syncpt_name)
{
	return host1x_api_table.nvhost_get_syncpt_host_managed(pdev, param, syncpt_name);
}
EXPORT_SYMBOL(wrap_nvhost_get_syncpt_host_managed);

u32 wrap_nvhost_get_syncpt_client_managed(struct platform_device *pdev,
					const char *syncpt_name)
{
	return host1x_api_table.nvhost_get_syncpt_client_managed(pdev, syncpt_name);
}
EXPORT_SYMBOL(wrap_nvhost_get_syncpt_client_managed);

u32 wrap_nvhost_get_syncpt_gpu_managed(struct platform_device *pdev,
					const char *syncpt_name)
{
	return host1x_api_table.nvhost_get_syncpt_gpu_managed(pdev, syncpt_name);
}
EXPORT_SYMBOL(wrap_nvhost_get_syncpt_gpu_managed);

void wrap_nvhost_syncpt_put_ref_ext(struct platform_device *pdev, u32 id)
{
	return host1x_api_table.nvhost_syncpt_put_ref_ext(pdev, id);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_put_ref_ext);

bool wrap_nvhost_syncpt_is_valid_pt_ext(struct platform_device *pdev, u32 id)
{
	return host1x_api_table.nvhost_syncpt_is_valid_pt_ext(pdev, id);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_is_valid_pt_ext);

int wrap_nvhost_syncpt_is_expired_ext(struct platform_device *pdev, u32 id,
					u32 thresh)
{
	return host1x_api_table.nvhost_syncpt_is_expired_ext(pdev, id, thresh);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_is_expired_ext);

void wrap_nvhost_syncpt_set_min_update(struct platform_device *pdev, u32 id, u32 val)
{
	return host1x_api_table.nvhost_syncpt_set_min_update(pdev, id, val);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_set_min_update);

int wrap_nvhost_syncpt_read_ext_check(struct platform_device *pdev, u32 id, u32 *val)
{
	return host1x_api_table.nvhost_syncpt_read_ext_check(pdev, id, val);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_read_ext_check);

u32 wrap_nvhost_syncpt_read_maxval(struct platform_device *pdev, u32 id)
{
	return host1x_api_table.nvhost_syncpt_read_maxval(pdev, id);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_read_maxval);

u32 wrap_nvhost_syncpt_incr_max_ext(struct platform_device *pdev, u32 id, u32 incrs)
{
	return host1x_api_table.nvhost_syncpt_incr_max_ext(pdev, id, incrs);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_incr_max_ext);

u32 wrap_nvhost_syncpt_unit_interface_get_byte_offset_ext(struct platform_device *pdev,
							u32 syncpt_id)
{
	return host1x_api_table.nvhost_syncpt_unit_interface_get_byte_offset_ext(pdev, syncpt_id);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_unit_interface_get_byte_offset_ext);

u32 wrap_nvhost_syncpt_unit_interface_get_byte_offset(u32 syncpt_id)
{
	return host1x_api_table.nvhost_syncpt_unit_interface_get_byte_offset(syncpt_id);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_unit_interface_get_byte_offset);

int wrap_nvhost_syncpt_unit_interface_get_aperture(struct platform_device *pdev,
						u64 *base, size_t *size)
{
	return host1x_api_table.nvhost_syncpt_unit_interface_get_aperture(pdev, base, size);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_unit_interface_get_aperture);

int wrap_nvhost_syncpt_unit_interface_init(struct platform_device *pdev)
{
	return host1x_api_table.nvhost_syncpt_unit_interface_init(pdev);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_unit_interface_init);

void wrap_nvhost_syncpt_unit_interface_deinit(struct platform_device *pdev)
{
	return host1x_api_table.nvhost_syncpt_unit_interface_deinit(pdev);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_unit_interface_deinit);

dma_addr_t wrap_nvhost_syncpt_address(struct platform_device *pdev, u32 id)
{
	return host1x_api_table.nvhost_syncpt_address(pdev, id);
}
EXPORT_SYMBOL(wrap_nvhost_syncpt_address);

int wrap_nvhost_intr_register_notifier(struct platform_device *pdev,
					u32 id, u32 thresh,
					void (*callback)(void *data),
					void *private_data)
{
	return host1x_api_table.nvhost_intr_register_notifier(pdev,
									id, thresh,
									callback,
									private_data);
}
EXPORT_SYMBOL(wrap_nvhost_intr_register_notifier);

int wrap_nvhost_module_init(struct platform_device *pdev)
{
	return host1x_api_table.nvhost_module_init(pdev);
}
EXPORT_SYMBOL(wrap_nvhost_module_init);

void wrap_nvhost_module_deinit(struct platform_device *pdev)
{
	return host1x_api_table.nvhost_module_deinit(pdev);
}
EXPORT_SYMBOL(wrap_nvhost_module_deinit);

void wrap_nvhost_module_reset(struct platform_device *pdev, bool reboot)
{
	return host1x_api_table.nvhost_module_reset(pdev, reboot);
}
EXPORT_SYMBOL(wrap_nvhost_module_reset);

int wrap_nvhost_module_busy(struct platform_device *pdev)
{
	return host1x_api_table.nvhost_module_busy(pdev);
}
EXPORT_SYMBOL(wrap_nvhost_module_busy);

inline void wrap_nvhost_module_idle(struct platform_device *pdev)
{
	return host1x_api_table.nvhost_module_idle(pdev);
}
EXPORT_SYMBOL(wrap_nvhost_module_idle);

void wrap_nvhost_module_idle_mult(struct platform_device *pdev, int refs)
{
	return host1x_api_table.nvhost_module_idle_mult(pdev, refs);
}
EXPORT_SYMBOL(wrap_nvhost_module_idle_mult);
