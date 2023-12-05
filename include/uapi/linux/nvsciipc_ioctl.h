/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __NVSCIIPC_IOCTL_H__
#define __NVSCIIPC_IOCTL_H__

#include <linux/ioctl.h>

#define NVSCIIPC_MAX_EP_NAME	64U
#define NVSCIIPC_MAX_RDMA_NAME	64U
#define NVSCIIPC_MAX_IP_NAME	16U

struct nvsciipc_config_entry {
	/* endpoint name */
	char ep_name[NVSCIIPC_MAX_EP_NAME];
	/* node name for shm/sem */
	char dev_name[NVSCIIPC_MAX_EP_NAME];
	uint32_t backend;       /* backend type */
	uint32_t nframes;       /* frame count */
	uint32_t frame_size;    /* frame size */
	/* ep id    for inter-Proc/Thread
	 * queue id for inter-VM
	 * dev id   for inter-Chip
	 */
	uint32_t id;
	uint64_t vuid;  /* VM-wide unique id */
	char rdma_dev_name[NVSCIIPC_MAX_RDMA_NAME];
	char remote_ip[NVSCIIPC_MAX_IP_NAME];
	uint32_t remote_port;
	uint32_t local_port;
	uint32_t peer_vmid;
	uint32_t noti_type;
};

struct nvsciipc_db {
	int num_eps;
	struct nvsciipc_config_entry **entry;
};

struct nvsciipc_get_vuid {
	char ep_name[NVSCIIPC_MAX_EP_NAME];
	uint64_t vuid;
};

struct nvsciipc_get_db_by_name {
	char ep_name[NVSCIIPC_MAX_EP_NAME];
	struct nvsciipc_config_entry entry;
	uint32_t idx;
};

struct nvsciipc_get_db_by_vuid {
	uint64_t vuid;
	struct nvsciipc_config_entry entry;
	uint32_t idx;
};

/* for userspace level test, debugging purpose only */
struct nvsciipc_validate_auth_token {
	uint32_t auth_token;
	uint64_t local_vuid;
};

/* NvSciIpcTopoId type */
struct nvsciipc_topoid {
	uint32_t socid;
	uint32_t vmid;
};

/* for userspace level test, debugging purpose only */
struct nvsciipc_map_vuid {
	uint64_t vuid;
	struct nvsciipc_topoid peer_topoid;
	uint64_t peer_vuid;
};

/* IOCTL magic number - seen available in ioctl-number.txt*/
#define NVSCIIPC_IOCTL_MAGIC    0xC3

#define NVSCIIPC_IOCTL_SET_DB \
	_IOW(NVSCIIPC_IOCTL_MAGIC, 1, struct nvsciipc_db)

#define NVSCIIPC_IOCTL_GET_VUID \
	_IOWR(NVSCIIPC_IOCTL_MAGIC, 2, struct nvsciipc_get_vuid)

#define NVSCIIPC_IOCTL_GET_DB_BY_NAME \
	_IOWR(NVSCIIPC_IOCTL_MAGIC, 3, struct nvsciipc_get_db_by_name)

#define NVSCIIPC_IOCTL_GET_DB_BY_VUID \
	_IOWR(NVSCIIPC_IOCTL_MAGIC, 4, struct nvsciipc_get_db_by_vuid)

#define NVSCIIPC_IOCTL_GET_DB_SIZE \
	_IOR(NVSCIIPC_IOCTL_MAGIC, 5, uint32_t)

/* debugging purpose only */
#define NVSCIIPC_IOCTL_VALIDATE_AUTH_TOKEN \
	_IOWR(NVSCIIPC_IOCTL_MAGIC, 6, struct nvsciipc_validate_auth_token)

/* debugging purpose only */
#define NVSCIIPC_IOCTL_MAP_VUID \
	_IOWR(NVSCIIPC_IOCTL_MAGIC, 7, struct nvsciipc_map_vuid)

#define NVSCIIPC_IOCTL_GET_VMID \
	_IOWR(NVSCIIPC_IOCTL_MAGIC, 8, uint32_t)

#define NVSCIIPC_IOCTL_NUMBER_MAX 8

#endif /* __NVSCIIPC_IOCTL_H__ */
