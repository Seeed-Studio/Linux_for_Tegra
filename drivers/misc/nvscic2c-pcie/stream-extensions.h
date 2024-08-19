/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024, NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

/*
 * Internal to gos-nvscic2c module. This file is not supposed to be included
 * by any other external modules.
 */
#ifndef __STREAM_EXTENSIONS_H__
#define __STREAM_EXTENSIONS_H__

#include <linux/types.h>

#include "common.h"

/* forward declaration. */
struct driver_ctx_t;

/* params to instantiate a stream-extension instance.*/
struct stream_ext_params {
	struct node_info_t *local_node;
	struct node_info_t *peer_node;
	u32 ep_id;
	char *ep_name;
	/* Streaming mode per endpoint PCIe aperture mapping limit */
	uint64_t aperture_limit;
	struct platform_device *host1x_pdev;
	enum drv_mode_t drv_mode;
	void *pci_client_h;
	void *comm_channel_h;
	void *vmap_h;
	void *edma_h;
};

int
stream_extension_ioctl(void *stream_ext_h, unsigned int cmd, void *arg);

int
stream_extension_init(struct stream_ext_params *params, void **handle);

void
stream_extension_deinit(void **handle);
#endif //__STREAM_EXTENSIONS_H__
