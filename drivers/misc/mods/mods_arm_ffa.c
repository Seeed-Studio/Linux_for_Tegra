// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved. */

#include "mods_internal.h"
#include <linux/uuid.h>
#include <linux/arm_ffa.h>
#include <linux/errno.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

static const struct ffa_device_id mods_ffa_device_id[] = {
	{ UUID_INIT(0x1f4bfeb9, 0x0f48, 0xdd1e,
		    0x11, 0x9c, 0x2c, 0x86, 0xc9, 0x14, 0x03, 0x22) },
	{}
};

struct mods_ffa_ctx {
	struct ffa_device        *ffa_dev;
#if KERNEL_VERSION(6, 1, 0) <= MODS_KERNEL_VERSION || defined(FFA_PARTITION_AARCH64_EXEC)
	const struct ffa_msg_ops *ffa_ops;
#else
	const struct ffa_dev_ops *ffa_ops;
#endif
};

static DEFINE_MUTEX(mods_ffa_lock);

static struct mods_ffa_ctx mods_ffa_info;

static int ffa_probe(struct ffa_device *ffa_dev)
{
	int ret = 0;

#if KERNEL_VERSION(6, 1, 0) <= MODS_KERNEL_VERSION || defined(FFA_PARTITION_AARCH64_EXEC)
	const struct ffa_msg_ops *ffa_ops = NULL;

	if (ffa_dev->ops)
		ffa_ops = ffa_dev->ops->msg_ops;
#else
	const struct ffa_dev_ops *ffa_ops;

	ffa_ops = ffa_dev_ops_get(ffa_dev);
#endif
	if (!ffa_ops) {
		mods_error_printk("failed \"method\" init: ffa\n");
		return -ENOENT;
	}
	mods_ffa_info.ffa_dev = ffa_dev;
	mods_ffa_info.ffa_ops = ffa_ops;

	mods_debug_printk(DEBUG_TEGRADMA, "mods ffa driver registered\n");

	return ret;
}

static void ffa_remove(struct ffa_device *ffa_dev)
{
	mods_ffa_info.ffa_dev = NULL;
	mods_ffa_info.ffa_ops = NULL;
}

static struct ffa_driver mods_ffa_driver = {
	.name     = "mods_arm_ffa",
	.probe    = ffa_probe,
	.remove   = ffa_remove,
	.id_table = mods_ffa_device_id,
};

int mods_ffa_abi_register(void)
{
	mods_debug_printk(DEBUG_TEGRADMA, "registering MODS FFA driver\n");
	return ffa_register(&mods_ffa_driver);
}

void mods_ffa_abi_unregister(void)
{
	ffa_unregister(&mods_ffa_driver);
}

int esc_mods_arm_ffa_cmd(struct mods_client *client,
			 struct MODS_FFA_PARAMS *p)
{
	int err = -EINVAL;
	struct ffa_send_direct_data data = { 0 };

	// Fill the reg TX command parameters
	data.data0 = p->cmd;
	// 64 bit of the physical address
	data.data1 = p->indata[0];
	// 32 bit of the reg value
	data.data2 = p->indata[1];

	if (!mods_ffa_info.ffa_ops) {
		cl_error("mods ffa cmd error, device not found\n");
		return -ENODEV;
	}

	switch (p->cmd) {
	case MODS_FFA_CMD_READ_REG:
		// Read command
		cl_debug(DEBUG_TEGRADMA, "sending data to SP :read cmd 0x%llx, addr:0x%llx\n",
					  (unsigned long long)data.data0,
					  (unsigned long long)data.data1);
		break;
	case MODS_FFA_CMD_WRITE_REG:
		// Write command
		cl_debug(DEBUG_TEGRADMA, "sending data to SP :write cmd 0x%llx,addr:0x%llx,write_val:0x%llx\n",
					  (unsigned long long)data.data0,
					  (unsigned long long)data.data1,
					  (unsigned long long)data.data2);
		break;
	case MODS_FFA_CMD_READ_VER:
		cl_debug(DEBUG_TEGRADMA, "sending cmd MODS_FFA_CMD_READ_VER to SP\n");
		break;
	case MODS_FFA_CMD_SE_TESTS:
		cl_debug(DEBUG_TEGRADMA, "sending SE_TESTS data to SP :read cmd 0x%llx, alg|engineId:0x%llx\n",
					  (unsigned long long)data.data0,
					  (unsigned long long)data.data1);
		break;
	case MODS_FFA_CMD_SE_KEY_MOVER:
		cl_debug(DEBUG_TEGRADMA, "sending SE_KEY_MOVER data to SP :read cmd 0x%llx, data:0x%llx\n",
					  (unsigned long long)data.data0,
					  (unsigned long long)data.data1);
		break;
	case MODS_FFA_CMD_HSS_TEST:
		cl_debug(DEBUG_TEGRADMA, "sending cmd MODS_FFA_CMD_HSS_TEST to SP\n");
		break;
	case MODS_FFA_CMD_C2C_TEST:
		cl_debug(DEBUG_TEGRADMA, "sending cmd MODS_FFA_CMD_C2C_TEST to SP\n");
		break;
	case MODS_FFA_CMD_MISC:
		cl_debug(DEBUG_TEGRADMA, "sending cmd MODS_FFA_CMD_MISC to SP\n");
		break;
	default:
		cl_error("Unexpected command from SP 0x%llx\n", (unsigned long long)p->cmd);
		return err;
	}

	mutex_lock(&mods_ffa_lock);
	err = mods_ffa_info.ffa_ops->sync_send_receive(mods_ffa_info.ffa_dev, &data);
	mutex_unlock(&mods_ffa_lock);

	switch (p->cmd) {
	case MODS_FFA_CMD_READ_REG:
		// Read command
		cl_debug(DEBUG_TEGRADMA, "received read reg status from SP status:%d,read_val:0x%llx\n",
					  err, (unsigned long long)data.data1);
		p->outdata[0] = data.data1;
		break;
	case MODS_FFA_CMD_WRITE_REG:
		// write command
		cl_debug(DEBUG_TEGRADMA, "received write reg status from SP status: %d\n",
					  err);
		break;
	case MODS_FFA_CMD_READ_VER:
		cl_debug(DEBUG_TEGRADMA, "received version from SP : 0x%llx\n",
					  (unsigned long long)data.data1);
		p->outdata[0] = data.data1;
		break;
	case MODS_FFA_CMD_SE_TESTS:
	case MODS_FFA_CMD_SE_KEY_MOVER:
		p->outdata[0] = data.data1;
		break;
	case MODS_FFA_CMD_HSS_TEST:
	case MODS_FFA_CMD_C2C_TEST:
	case MODS_FFA_CMD_MISC:
		cl_debug(DEBUG_TEGRADMA, "received response from SP: 0x%llx\n",
					  (unsigned long long)data.data1);
		p->outdata[0] = data.data1;
		break;
	}

	if (err) {
		cl_error("unexpected error from SP: %d\n", err);
		return err;
	}
	// data.data0 always holds the error code of the ffa cmd
	if (data.data0) {
		cl_error("error response from SP: %ld\n", (long)data.data0);
		return -EFAULT;
	}
	return OK;
}
