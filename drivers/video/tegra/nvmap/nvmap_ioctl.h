/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2024, NVIDIA CORPORATION. All rights reserved.
 *
 * ioctl declarations for nvmap
 */

#ifndef __VIDEO_TEGRA_NVMAP_IOCTL_H
#define __VIDEO_TEGRA_NVMAP_IOCTL_H

#include <linux/nvmap.h>

int nvmap_ioctl_pinop(struct file *filp, bool is_pin, void __user *arg,
	bool is32);

int nvmap_ioctl_getid(struct file *filp, void __user *arg);

int nvmap_ioctl_get_ivcid(struct file *filp, void __user *arg);

int nvmap_ioctl_getfd(struct file *filp, void __user *arg);

int nvmap_ioctl_alloc(struct file *filp, void __user *arg);

int nvmap_ioctl_alloc_kind(struct file *filp, void __user *arg);

int nvmap_ioctl_alloc_ivm(struct file *filp, void __user *arg);

int nvmap_ioctl_free(struct file *filp, unsigned long arg);

int nvmap_ioctl_create(struct file *filp, unsigned int cmd, void __user *arg);

int nvmap_ioctl_create_from_va(struct file *filp, void __user *arg);

int nvmap_ioctl_create_from_ivc(struct file *filp, void __user *arg);

int nvmap_ioctl_get_ivc_heap(struct file *filp, void __user *arg);

int nvmap_map_into_caller_ptr(struct file *filp, void __user *arg, bool is32);

int nvmap_ioctl_cache_maint(struct file *filp, void __user *arg, int size);

int nvmap_ioctl_rw_handle(struct file *filp, int is_read, void __user *arg,
	size_t op_size);

int nvmap_ioctl_gup_test(struct file *filp, void __user *arg);

int nvmap_ioctl_set_tag_label(struct file *filp, void __user *arg);

int nvmap_ioctl_get_available_heaps(struct file *filp, void __user *arg);

void kasan_memcpy_toio(void __iomem *to, const void *from,
			size_t count);

int nvmap_ioctl_get_handle_parameters(struct file *filp, void __user *arg);

int nvmap_ioctl_get_sci_ipc_id(struct file *filp, void __user *arg);

int nvmap_ioctl_handle_from_sci_ipc_id(struct file *filp, void __user *arg);

int nvmap_ioctl_query_heap_params(struct file *filp, void __user *arg);

int nvmap_ioctl_query_heap_params_numa(struct file *filp, void __user *arg);

int nvmap_ioctl_dup_handle(struct file *filp, void __user *arg);

int nvmap_ioctl_get_fd_from_list(struct file *filp, void __user *arg);
#endif	/*  __VIDEO_TEGRA_NVMAP_IOCTL_H */
