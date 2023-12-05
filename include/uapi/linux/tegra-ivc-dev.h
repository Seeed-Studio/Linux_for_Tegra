/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __UAPI_TEGRA_IVC_DEV_H
#define __UAPI_TEGRA_IVC_DEV_H

#include <linux/ioctl.h>

#define IVC_INVALID_IPA 0U
#define IVC_TRAP_IPA 1U
#define IVC_MSI_IPA 2U

struct nvipc_ivc_info {
	uint32_t nframes;
	uint32_t frame_size;
	uint32_t queue_offset;
	uint32_t queue_size;
	uint32_t area_size;
	bool     rx_first;
	uint64_t noti_ipa;
	uint16_t noti_irq;
	uint16_t noti_type; /* IVC_TRAP_IPA, IVC_MSI_IPA */
};

/*  IOCTL magic number */
#define NVIPC_IVC_IOCTL_MAGIC 0xAA

/* query ivc info */
#define NVIPC_IVC_IOCTL_GET_INFO \
	_IOWR(NVIPC_IVC_IOCTL_MAGIC, 1, struct nvipc_ivc_info)

/* notify remote */
#define NVIPC_IVC_IOCTL_NOTIFY_REMOTE \
	_IO(NVIPC_IVC_IOCTL_MAGIC, 2)

/* query vmid */
#define NVIPC_IVC_IOCTL_GET_VMID \
	_IOR(NVIPC_IVC_IOCTL_MAGIC, 3, uint32_t)

#define NVIPC_IVC_IOCTL_NUMBER_MAX 3

int ivc_cdev_get_peer_vmid(uint32_t qid, uint32_t *peer_vmid);
int ivc_cdev_get_noti_type(uint32_t qid, uint32_t *noti_type);

#endif
