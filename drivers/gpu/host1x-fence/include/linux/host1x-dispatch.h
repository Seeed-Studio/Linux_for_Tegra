/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef __HOST1X_DISPATCH_H
#define __HOST1X_DISPATCH_H
#include <linux/host1x-dispatch_api.h>

// fence.c
#define  host1x_fence_create				wrap_host1x_fence_create
#define  host1x_fence_extract				wrap_host1x_fence_extract
#define  host1x_fence_cancel				wrap_host1x_fence_cancel
#define  host1x_syncpt_fence_scan			wrap_host1x_syncpt_fence_scan
// Syncpt.c
#define  host1x_get_dma_mask				wrap_host1x_get_dma_mask
#define  host1x_syncpt_get					wrap_host1x_syncpt_get
#define  host1x_syncpt_get_by_id			wrap_host1x_syncpt_get_by_id
#define  host1x_syncpt_get_by_id_noref		wrap_host1x_syncpt_get_by_id_noref
#define  host1x_syncpt_read					wrap_host1x_syncpt_read
#define  host1x_syncpt_read_min				wrap_host1x_syncpt_read_min
#define  host1x_syncpt_read_max				wrap_host1x_syncpt_read_max
#define  host1x_syncpt_incr					wrap_host1x_syncpt_incr
#define  host1x_syncpt_incr_max				wrap_host1x_syncpt_incr_max
#define  host1x_syncpt_alloc				wrap_host1x_syncpt_alloc
#define  host1x_syncpt_put					wrap_host1x_syncpt_put
#define  host1x_syncpt_id					wrap_host1x_syncpt_id
#define  host1x_syncpt_wait_ts				wrap_host1x_syncpt_wait_ts
#define  host1x_syncpt_wait					wrap_host1x_syncpt_wait
#define  host1x_fence_get_node			    wrap_host1x_fence_get_node
#define  host1x_syncpt_get_shim_info		wrap_host1x_syncpt_get_shim_info
//nvhost.h

#define  host1x_writel										wrap_host1x_writel
#define  nvhost_get_default_device							wrap_nvhost_get_default_device
#define  nvhost_get_host1x									wrap_nvhost_get_host1x
#define  nvhost_client_device_get_resources					wrap_nvhost_client_device_get_resources
#define  nvhost_client_device_init							wrap_nvhost_client_device_init
#define  nvhost_client_device_release						wrap_nvhost_client_device_release
#define  nvhost_get_syncpt_host_managed						wrap_nvhost_get_syncpt_host_managed
#define  nvhost_get_syncpt_client_managed					wrap_nvhost_get_syncpt_client_managed
#define  nvhost_get_syncpt_gpu_managed						wrap_nvhost_get_syncpt_gpu_managed
#define  nvhost_syncpt_put_ref_ext							wrap_nvhost_syncpt_put_ref_ext
#define  nvhost_syncpt_is_valid_pt_ext						wrap_nvhost_syncpt_is_valid_pt_ext
#define  nvhost_syncpt_is_expired_ext						wrap_nvhost_syncpt_is_expired_ext
#define  nvhost_syncpt_set_min_update						wrap_nvhost_syncpt_set_min_update
#define  nvhost_syncpt_read_ext_check						wrap_nvhost_syncpt_read_ext_check
#define  nvhost_syncpt_read_maxval							wrap_nvhost_syncpt_read_maxval
#define  nvhost_syncpt_incr_max_ext							wrap_nvhost_syncpt_incr_max_ext
#define  nvhost_syncpt_unit_interface_get_byte_offset_ext	wrap_nvhost_syncpt_unit_interface_get_byte_offset_ext
#define  nvhost_syncpt_unit_interface_get_byte_offset		wrap_nvhost_syncpt_unit_interface_get_byte_offset
#define  nvhost_syncpt_unit_interface_get_aperture			wrap_nvhost_syncpt_unit_interface_get_aperture
#define  nvhost_syncpt_unit_interface_init					wrap_nvhost_syncpt_unit_interface_init
#define  nvhost_syncpt_unit_interface_deinit				wrap_nvhost_syncpt_unit_interface_deinit
#define  nvhost_syncpt_address								wrap_nvhost_syncpt_address
#define  nvhost_intr_register_notifier						wrap_nvhost_intr_register_notifier
#define  nvhost_module_init									wrap_nvhost_module_init
#define  nvhost_module_deinit								wrap_nvhost_module_deinit
#define  nvhost_module_reset								wrap_nvhost_module_reset
#define  nvhost_module_busy									wrap_nvhost_module_busy
#define  nvhost_module_idle									wrap_nvhost_module_idle
#define  nvhost_module_idle_mult							wrap_nvhost_module_idle_mult

#endif /*__HOST1X_DISPATCH_H */