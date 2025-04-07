/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h> /* printk() */
#include <linux/fs.h>   /* everything... */
#include <linux/errno.h> /* error codes */
#include <asm-generic/bug.h>
#include <linux/slab.h>   /* kmalloc() */
#include <scsi/scsi.h>
#include <uapi/scsi/ufs/ioctl.h>
#include <scsi/sg.h>
#include <linux/mmc/ioctl.h>
#include "tegra_vblk.h"

int vblk_complete_ioctl_req(struct vblk_dev *vblkdev,
	struct vsc_request *vsc_req, int status)
{
	struct vblk_ioctl_req *ioctl_req = vsc_req->ioctl_req;
	int32_t ret = 0;

	if (ioctl_req == NULL) {
		dev_err(vblkdev->device,
			"Invalid ioctl request for completion!\n");
		ret = -EINVAL;
		goto comp_exit;
	}

	ioctl_req->status = status;
	memcpy(ioctl_req->ioctl_buf, vsc_req->mempool_virt,
			ioctl_req->ioctl_len);
comp_exit:
	return ret;
}

int vblk_prep_ioctl_req(struct vblk_dev *vblkdev,
		struct vblk_ioctl_req *ioctl_req,
		struct vsc_request *vsc_req)
{
	int32_t ret = 0;
	struct vs_request *vs_req;

	if (ioctl_req == NULL) {
		dev_err(vblkdev->device,
			"Invalid ioctl request for preparation!\n");
		return -EINVAL;
	}


	if (ioctl_req->ioctl_len > vsc_req->mempool_len) {
		dev_err(vblkdev->device,
			"Ioctl length %u exceeding mempool length %u!\n", ioctl_req->ioctl_len,
			vsc_req->mempool_len);
		return -EINVAL;
	}

	if (ioctl_req->ioctl_buf == NULL) {
		dev_err(vblkdev->device,
			"Ioctl buffer invalid!\n");
		return -EINVAL;
	}

	vs_req = &vsc_req->vs_req;
	vs_req->blkdev_req.req_op = VS_BLK_IOCTL;
	memcpy(vsc_req->mempool_virt, ioctl_req->ioctl_buf,
			ioctl_req->ioctl_len);
	vs_req->blkdev_req.ioctl_req.ioctl_id = ioctl_req->ioctl_id;
	vs_req->blkdev_req.ioctl_req.data_offset = vsc_req->mempool_offset;
	vs_req->blkdev_req.ioctl_req.ioctl_len = ioctl_req->ioctl_len;

	vsc_req->ioctl_req = ioctl_req;

	return ret;
}

int vblk_submit_ioctl_req(struct vblk_dev *vblkdev,
		unsigned int cmd, void __user *user)
{
	struct vblk_ioctl_req *ioctl_req = NULL;
	struct request *rq;
	int err;

	/*
	 * The caller must have CAP_SYS_RAWIO, and must be calling this on the
	 * whole block device, not on a partition.  This prevents overspray
	 * between sibling partitions.
	 */
	if ((!capable(CAP_SYS_RAWIO))) {
		dev_err(vblkdev->device,
			"Permission denied for passthrough cmds\n");
		return -EPERM;
	}

	ioctl_req = kzalloc(sizeof(struct vblk_ioctl_req), GFP_KERNEL);
	if (!ioctl_req) {
		dev_err(vblkdev->device,
			"failed to alloc memory for ioctl req!\n");
		return -ENOMEM;
	}

	switch (cmd) {
	case SG_IO:
		err = vblk_prep_sg_io(vblkdev, ioctl_req,
			user);
		break;
	case MMC_IOC_MULTI_CMD:
	case MMC_IOC_CMD:
		err = vblk_prep_mmc_multi_ioc(vblkdev, ioctl_req,
			user, cmd);
		break;
	case UFS_IOCTL_COMBO_QUERY:
		err = vblk_prep_ufs_combo_ioc(vblkdev, ioctl_req,
			user, cmd);
		break;
	default:
		dev_err(vblkdev->device, "unsupported command %x!\n", cmd);
		err = -EINVAL;
		goto free_ioctl_req;
	}

	if (err)
		goto free_ioctl_req;

	rq = blk_mq_alloc_request(vblkdev->queue, REQ_OP_DRV_IN, BLK_MQ_REQ_NOWAIT);
	if (IS_ERR_OR_NULL(rq)) {
		dev_err(vblkdev->device,
			"Failed to get handle to a request!\n");
		err = PTR_ERR(rq);
		goto free_ioctl_req;
	}
	vblkdev->ioctl_req = ioctl_req;

#if defined(NV_BLK_EXECUTE_RQ_HAS_NO_GENDISK_ARG) /* Linux v5.17 */
	blk_execute_rq(rq, 0);
#else
	blk_execute_rq(vblkdev->gd, rq, 0);
#endif

	blk_mq_free_request(rq);

	switch (cmd) {
	case SG_IO:
		err = vblk_complete_sg_io(vblkdev, ioctl_req,
			user);
		break;
	case MMC_IOC_MULTI_CMD:
	case MMC_IOC_CMD:
		err = vblk_complete_mmc_multi_ioc(vblkdev, ioctl_req,
			user, cmd);
		break;
	case UFS_IOCTL_COMBO_QUERY:
		err = vblk_complete_ufs_combo_ioc(vblkdev, ioctl_req,
			user, cmd);
		break;
	default:
		dev_err(vblkdev->device, "unsupported command %x!\n", cmd);
		err = -EINVAL;
		goto free_ioctl_req;
	}

free_ioctl_req:
	if (ioctl_req)
		kfree(ioctl_req);

	return err;
}

/* The ioctl() implementation */
static int vblk_common_ioctl(struct vblk_dev *vblkdev, fmode_t mode,
		unsigned int cmd, unsigned long arg)
{
	int ret;

	switch (cmd) {
	case MMC_IOC_MULTI_CMD:
	case MMC_IOC_CMD:
	case SG_IO:
	case UFS_IOCTL_COMBO_QUERY:
		ret = vblk_submit_ioctl_req(vblkdev, cmd,
			(void __user *)arg);
		break;
	default:  /* unknown command */
		ret = -ENOTTY;
		break;
	}

	return ret;
}

/**
 * @defgroup vscd_ioctl LinuxVSCD::IOCTL
 *
 * @ingroup vscd_ioctl
 * @{
 */
/**
 * @brief Handles IOCTL commands for firmware update operations
 *
 * This function processes IOCTL commands specifically for firmware updates.
 * It temporarily enables FFU passthrough command permissions, processes the IOCTL,
 * then disables FFU permissions again. This provides controlled access to firmware
 * update capabilities through a dedicated interface.
 *
 * @param[in] bdev Pointer to the block device structure
 * @param[in] mode File mode flags
 * @param[in] cmd IOCTL command code
 * @param[in] arg Command-specific argument
 * @return 0 on success, negative errno on failure
 *
 * @pre
 * - Device must be initialized
 * - Block device must be opened through FFU interface
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
int vblk_ffu_ioctl(struct block_device *bdev, fmode_t mode,
    unsigned int cmd, unsigned long arg)
{
	struct vblk_dev *vblkdev = bdev->bd_disk->private_data;
	int ret;

	mutex_lock(&vblkdev->ioctl_lock);
	vblkdev->allow_ffu_passthrough_cmds = true;
	ret = vblk_common_ioctl(vblkdev, mode, cmd, arg);
	vblkdev->allow_ffu_passthrough_cmds = false;
	mutex_unlock(&vblkdev->ioctl_lock);

	return ret;
}

/**
 * @brief Handles IOCTL commands for normal block device operations
 *
 * This function processes IOCTL commands for the block device, supporting:
 * - MMC IOC commands (single and multi)
 * - SCSI generic (SG_IO) commands
 * - UFS combo query commands
 * The function validates permissions and delegates to specific handlers.
 *
 * @param[in] bdev Pointer to the block device structure
 * @param[in] mode File mode flags
 * @param[in] cmd IOCTL command code
 * @param[in] arg Command-specific argument
 * @return 0 on success, negative errno on failure
 *
 * @pre
 * - Device must be initialized
 * - Block device must be opened
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
int vblk_ioctl(struct block_device *bdev, fmode_t mode,
		unsigned int cmd, unsigned long arg)
{
	int ret;
	struct vblk_dev *vblkdev = bdev->bd_disk->private_data;

	mutex_lock(&vblkdev->ioctl_lock);
	ret = vblk_common_ioctl(vblkdev, mode, cmd, arg);
	mutex_unlock(&vblkdev->ioctl_lock);

	return ret;
}

/**
 * @brief Handles IOCTL commands for non-control device nodes
 *
 * This function rejects IOCTL commands that are only supported on the control device node.
 * It returns -ENOTTY for MMC, SCSI, and UFS commands when called on regular block device nodes.
 * This enforces access control by requiring privileged operations to use the control node.
 *
 * @param[in] bdev Pointer to the block device structure
 * @param[in] mode File mode flags
 * @param[in] cmd IOCTL command code
 * @param[in] arg Command-specific argument
 * @return -ENOTTY to indicate command is not supported
 *
 * @pre
 * - Device must be initialized
 * - Block device must be opened
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
int vblk_ioctl_not_supported(struct block_device *bdev, fmode_t mode,
	unsigned int cmd, unsigned long arg)
{
	int ret;
	struct vblk_dev *vblkdev = bdev->bd_disk->private_data;

	switch (cmd) {
	case MMC_IOC_MULTI_CMD:
	case MMC_IOC_CMD:
	case SG_IO:
	case UFS_IOCTL_COMBO_QUERY:
		dev_err(vblkdev->device,
			"IOCTL is not supported on non-ctl device node %u %lu\n",
			cmd, arg);
		ret = -ENOTTY;
		break;
	default:  /* unknown command */
		ret = -ENOTTY;
		break;
	}

	return ret;
}
/** @} */
