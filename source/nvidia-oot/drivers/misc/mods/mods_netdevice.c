// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2015-2023, NVIDIA CORPORATION.  All rights reserved. */

#include "mods_internal.h"
#include <linux/netdevice.h>

int esc_mods_net_force_link(struct mods_client *client,
			    struct MODS_NET_DEVICE_NAME *p)
{
	struct net_device *ndev;

	if (!p ||
	    (strnlen(p->device_name, MAX_NET_DEVICE_NAME_LENGTH) == 0) ||
	    (!memchr(p->device_name, '\0', MAX_NET_DEVICE_NAME_LENGTH))) {
		cl_error("invalid device name\n");
		return -EINVAL;
	}

	for_each_netdev(&init_net, ndev)
		if (!strcmp(ndev->name, p->device_name)) {
			netif_carrier_on(ndev);
			cl_info("carrier forced on: %s\n", p->device_name);
			return OK;
		}

	cl_error("failed to find network device %s\n", p->device_name);
	return -EINVAL;
}
