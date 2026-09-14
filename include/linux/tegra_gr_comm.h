/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2023, NVIDIA Corporation. All rights reserved.
 *
 * Tegra Graphics Virtualization Communication Framework
 */

#ifndef __TEGRA_GR_COMM_H
#define __TEGRA_GR_COMM_H

#include <linux/device.h>

#define TEGRA_GR_COMM_ID_SELF (0xFF)

int tegra_gr_comm_init(struct device *dev, struct device_node *dn, u32 elems,
		const size_t *queue_sizes, u32 queue_start, u32 num_queues);
void tegra_gr_comm_deinit(u32 queue_start, u32 num_queues);
int tegra_gr_comm_send(u32 peer, u32 index, void *data,
		size_t size);
int tegra_gr_comm_recv(u32 index, void **handle, void **data,
		size_t *size, u32 *sender);
int tegra_gr_comm_sendrecv(u32 peer, u32 index, void **handle,
			void **data, size_t *size);
void tegra_gr_comm_release(void *handle);
u32 tegra_gr_comm_get_server_vmid(void);
void *tegra_gr_comm_oob_get_ptr(u32 peer, u32 index,
				void **ptr, size_t *size);
void tegra_gr_comm_oob_put_ptr(void *handle);
#endif
