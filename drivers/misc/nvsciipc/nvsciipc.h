/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2023, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef __NVSCIIPC_KERNEL_H__
#define __NVSCIIPC_KERNEL_H__

#include <linux/nvscierror.h>
#include <linux/nvsciipc_interface.h>
#include <uapi/linux/nvsciipc_ioctl.h>

#define ERR(...) pr_err("nvsciipc: " __VA_ARGS__)
#define INFO(...) pr_info("nvsciipc: " __VA_ARGS__)
#define DBG(...) pr_debug("nvsciipc: " __VA_ARGS__)

#define MODULE_NAME			 "nvsciipc"
#define MAX_NAME_SIZE		   64

#define NVSCIIPC_BACKEND_ITC		0U
#define NVSCIIPC_BACKEND_IPC		1U
#define NVSCIIPC_BACKEND_IVC		2U
#define NVSCIIPC_BACKEND_C2C_PCIE   3U
#define NVSCIIPC_BACKEND_C2C_NPM	4U
#define NVSCIIPC_BACKEND_UNKNOWN	0xFFFFFFFFU

struct nvsciipc {
	struct device *dev;

	dev_t dev_t;
	struct class *nvsciipc_class;
	struct cdev cdev;
	struct device *device;
	char device_name[MAX_NAME_SIZE];

	int num_eps;
	struct nvsciipc_config_entry **db;
	volatile bool set_db_f;
};

struct vuid_bitfield_64 {
	uint64_t index    : 16;
	uint64_t type     : 4;
	uint64_t vmid     : 8;
	uint64_t socid    : 28;
	uint64_t reserved : 8;
};

union nvsciipc_vuid_64 {
	uint64_t value;
	struct vuid_bitfield_64 bit;
};

/***********************************************************************/
/********************* Functions declaration ***************************/
/***********************************************************************/

static void nvsciipc_cleanup(struct nvsciipc *ctx);

static int nvsciipc_dev_open(struct inode *inode, struct file *filp);
static int nvsciipc_dev_release(struct inode *inode, struct file *filp);
static long nvsciipc_dev_ioctl(struct file *filp, unsigned int cmd,
			unsigned long arg);
static int nvsciipc_ioctl_get_vuid(struct nvsciipc *ctx, unsigned int cmd,
			unsigned long arg);
static int nvsciipc_ioctl_set_db(struct nvsciipc *ctx, unsigned int cmd,
			unsigned long arg);

#endif /* __NVSCIIPC_KERNEL_H__ */
