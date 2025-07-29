// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#ifndef INCLUDE_NVETHERNET_PUBLIC_H
#define INCLUDE_NVETHERNET_PUBLIC_H

#include <linux/netdevice.h>
#include <linux/types.h>

#define COE_ENABLE				1U
#define COE_MACSEC_HDR_OFFSET			42U
#define COE_VLAN_ENABLE				1U
#define COE_MACSEC_HDR_VLAN_DISABLE_OFFSET	38U
#define COE_VLAN_DISABLE			0U
#define COE_MACSEC_SFT_LC1			1024U
#define COE_MACSEC_SFT_LC2			1024U

/* These need to be configured only once during the COE plat driver init */
struct nvether_coe_cfg {
	/* Flag to track if COE is already configured */
	u32 coe_enable;
	/* System wide COE header offset to be used */
	u32 coe_hdr_offset;
	/* System wide config whether COE packets are VLAN tagged */
	u32 vlan_enable;
};

/* These have to be provided for every COE stream added */
struct nvether_per_coe_cfg {
	/* No. of lines in first subframes */
	u32 lc1;
	/* No. of lines in subsequent subframes */
	u32 lc2;
};

/**
 * @brief Configure COE in the macsec controller
 *
 * Configure the COE engine in the macsec controller for the
 * to a DMA channel specified by dma_chan.
 *
 * @param[in] ndev: Network device instance to be used for COE
 * @param[in] ether_coe_config: Struct that defines the system wide COE config.
 *
 * @retval value >=0 on success.
 * @retval "negative value" on failure.
 */
int nvether_coe_config(struct net_device *ndev,
		       struct nvether_coe_cfg *ether_coe_cfg);

/**
 * @brief Configure CoE on a channel
 *
 * @param[in] ndev: Network device to operate on.
 * @param[in] dmachan: Tte dma channel to take ownership of.
 *
 * @retval value >=0 on success.
 * @retval "negative value" on failure.
 */
int nvether_coe_chan_config(struct net_device *ndev, u32 dmachan,
				   struct nvether_per_coe_cfg *p_coe_cfg);

#endif /* INCLUDE_NVETHERNET_PUBLIC_H */
