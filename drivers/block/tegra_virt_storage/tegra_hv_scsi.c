/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h>   /* kmalloc() */
#include <linux/fs.h>   /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/fcntl.h> /* O_ACCMODE */
#include <asm/uaccess.h>
#include <asm-generic/bug.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include "tegra_vblk.h"

#define UFS_REQUEST_SENS_DATA_LEN 18U
#define SCSI_WRITE_BUFFER_CMD_CODE     0x3B
#define SCSI_MODE_FFU_VAL            0xE

int vblk_prep_sg_io(struct vblk_dev *vblkdev,
		struct vblk_ioctl_req *ioctl_req,
		void __user *user)
{
	int err = 0;
	sg_io_hdr_t *hp = NULL;
	uint32_t header_len = sizeof(sg_io_hdr_t);
	struct vblk_sg_io_hdr *vblk_hp;
	uint32_t vblk_sg_header_len = sizeof(struct vblk_sg_io_hdr);
	uint32_t cmnd_offset;
	void *cmnd;
	uint32_t sbp_offset;
	void *sbp;
	uint32_t data_buf_offset;
	uint32_t data_buf_offset_aligned;
	void *data_buf;
	uint32_t data_buf_size_aligned;
	uint32_t ioctl_len;
	void *ioctl_buf = NULL;
	uint32_t max_sb_len;
	uint8_t *cdb;
	uint32_t alignment_add;
	uint32_t temp_sum;

	hp = kzalloc(header_len, GFP_KERNEL);
	if (hp == NULL) {
		return -ENOMEM;
	}

	if (copy_from_user(hp, user, header_len)) {
		err = -EFAULT;
		goto free_hp;
	}

	if ((!hp->cmdp) || (hp->cmd_len < 6) ||
		(hp->cmd_len > VBLK_SG_MAX_CMD_LEN)) {
		err = -EMSGSIZE;
		goto free_hp;
	}

	cmnd_offset = vblk_sg_header_len;

	sbp_offset = (cmnd_offset + hp->cmd_len);
	if (sbp_offset < cmnd_offset) {
		err = - EMSGSIZE;
		goto free_hp;
	}
	if (hp->mx_sb_len == 0)
		max_sb_len = UFS_REQUEST_SENS_DATA_LEN;
	else
		max_sb_len = hp->mx_sb_len;

	data_buf_offset = (sbp_offset + max_sb_len);

	if (data_buf_offset < sbp_offset) {
		err = -EMSGSIZE;
		goto free_hp;
	}

	/* Check for subtraction overflow */
	if (check_sub_overflow(vblkdev->config.blk_config.hardblk_size, 1U, &alignment_add)) {
		/* True means overflow would occur */
		err = -EINVAL;
		goto free_hp;
	}

	/* Check for overflow in alignment calculation */
	if (check_add_overflow(data_buf_offset, alignment_add, &temp_sum)) {
		/* True means overflow would occur */
		err = -EMSGSIZE;
		goto free_hp;
	}

	/* Safe to perform alignment as no overflow detected */
	data_buf_offset_aligned = ALIGN(data_buf_offset,
			vblkdev->config.blk_config.hardblk_size);

	/* Verify alignment result */
	if (data_buf_offset_aligned < data_buf_offset) {
		err = -EMSGSIZE;
		goto free_hp;
	}

	/* Check for overflow in alignment calculation */
	if (check_add_overflow(hp->dxfer_len, alignment_add, &temp_sum)) {
		/* True means overflow would occur */
		err = -EMSGSIZE;
		goto free_hp;
	}

	data_buf_size_aligned = ALIGN(hp->dxfer_len,
			vblkdev->config.blk_config.hardblk_size);
	if (data_buf_size_aligned < hp->dxfer_len) {
		err = -EMSGSIZE;
		goto free_hp;
	}

	/* Some user space applications set the dxfer_direction to 0 when
	 * the dxfer_len is set to 0. The native SCSI driver (sg.c) handles this
	 * by checking for both dxfer_direction and dxfer_len.
	 * To be compatible with the native SCSI driver, set the dxfer_direction
	 * to SG_DXFER_NONE when dxfer_len is <= 0.
	 */
	if (hp->dxfer_len <= 0) {
		hp->dxfer_direction = SG_DXFER_NONE;
	}

	ioctl_len = data_buf_offset_aligned + data_buf_size_aligned;
	if (ioctl_len < data_buf_offset_aligned) {
		err = -EMSGSIZE;
		goto free_hp;
	}

	ioctl_buf = kzalloc(ioctl_len, GFP_KERNEL);
	if (ioctl_buf == NULL) {
		err = -ENOMEM;
		goto free_hp;
	}

	vblk_hp = (struct vblk_sg_io_hdr *)(ioctl_buf);
	sbp = (ioctl_buf + sbp_offset);
	cmnd = (ioctl_buf + cmnd_offset);
	if (copy_from_user(cmnd, hp->cmdp, hp->cmd_len)) {
		err = -EFAULT;
		goto free_ioctl_buf;
	}

	cdb = (uint8_t *)cmnd;
	if ((cdb[0] == SCSI_WRITE_BUFFER_CMD_CODE) &&
			(cdb[1] == SCSI_MODE_FFU_VAL) &&
			!vblkdev->allow_ffu_passthrough_cmds) {
		dev_err(vblkdev->device,
			"FFU permission not present\n");
		err = -EPERM;
		goto free_ioctl_buf;
	}

	data_buf = (ioctl_buf + data_buf_offset_aligned);

	switch (hp->dxfer_direction) {
	case SG_DXFER_NONE:
		vblk_hp->data_direction = SCSI_DATA_NONE;
		break;
	case SG_DXFER_TO_DEV:
		vblk_hp->data_direction = SCSI_TO_DEVICE;
		break;
	case SG_DXFER_FROM_DEV:
		vblk_hp->data_direction = SCSI_FROM_DEVICE;
		break;
	case SG_DXFER_TO_FROM_DEV:
		vblk_hp->data_direction = SCSI_BIDIRECTIONAL;
		break;
	default:
		err = -EBADMSG;
		goto free_ioctl_buf;
	}

	if ((vblk_hp->data_direction == SCSI_TO_DEVICE) ||
		(vblk_hp->data_direction == SCSI_BIDIRECTIONAL)) {
		if (copy_from_user(data_buf, hp->dxferp, hp->dxfer_len)) {
			err = -EFAULT;
			goto free_ioctl_buf;
		}
	}

	vblk_hp->cmd_len = hp->cmd_len;
	vblk_hp->mx_sb_len = max_sb_len;
	/* This is actual data len on which storage server needs to act */
	vblk_hp->dxfer_len = hp->dxfer_len;
	/* This is the data buffer len, data length is strictly dependent on the
	 * IOCTL being executed. data_buffer length is atleast cache aligned to
	 * make sure that cache operations can be done successfully without
	 * corruption.
	 * Since Block size is 4K, if it is aligned to blocksize, it will
	 * indirectly align to cache line.
	 */
	vblk_hp->dxfer_buf_len = data_buf_size_aligned;
	vblk_hp->xfer_arg_offset = data_buf_offset_aligned;
	vblk_hp->cmdp_arg_offset = cmnd_offset;
	vblk_hp->sbp_arg_offset = sbp_offset;
	ioctl_req->ioctl_id = VBLK_SG_IO_ID;
	ioctl_req->ioctl_buf = ioctl_buf;
	ioctl_req->ioctl_len = ioctl_len;

free_ioctl_buf:
	if (err && ioctl_buf)
		kfree(ioctl_buf);

free_hp:
	if (hp)
		kfree(hp);

	return err;
}

int vblk_complete_sg_io(struct vblk_dev *vblkdev,
		struct vblk_ioctl_req *ioctl_req,
		void __user *user)
{
	sg_io_hdr_t *hp = NULL;
	uint32_t header_len = sizeof(sg_io_hdr_t);
	struct vblk_sg_io_hdr *vblk_hp;
	void *sbp;
	void *data_buf;
	int err = 0;

	if (ioctl_req->status) {
		err = ioctl_req->status;
		if (ioctl_req->ioctl_buf)
			kfree(ioctl_req->ioctl_buf);
		goto exit;
	}

	/* Validate ioctl buffer */
	if (!ioctl_req->ioctl_buf ||
			ioctl_req->ioctl_len < sizeof(struct vblk_sg_io_hdr)) {
		err = -EINVAL;
		goto exit;
	}

	hp = kzalloc(header_len, GFP_KERNEL);
	if (hp == NULL) {
		return -ENOMEM;
	}

	if (copy_from_user(hp, user, header_len)) {
		err = -EFAULT;
		goto free_hp;
	}

	vblk_hp = (struct vblk_sg_io_hdr *)(ioctl_req->ioctl_buf);
	hp->status = 0xff & vblk_hp->status;
	hp->masked_status = status_byte(vblk_hp->status);
	hp->host_status = host_byte(vblk_hp->status);
	hp->driver_status = driver_byte(vblk_hp->status);
	if (hp->mx_sb_len != 0U)
		hp->sb_len_wr = vblk_hp->sb_len_wr;
	else
		hp->sb_len_wr = 0U;

	/* TODO: Handle the residual length */
	hp->resid = 0;

	sbp = (ioctl_req->ioctl_buf + vblk_hp->sbp_arg_offset);
	if ((hp->sb_len_wr != 0) && (hp->sbp != NULL)) {
		if (copy_to_user(hp->sbp, sbp, hp->sb_len_wr)) {
			err = -EFAULT;
			goto free_hp;
		}
	}

	data_buf = (ioctl_req->ioctl_buf + vblk_hp->xfer_arg_offset);

	if ((vblk_hp->data_direction == SCSI_FROM_DEVICE) ||
		(vblk_hp->data_direction == SCSI_BIDIRECTIONAL)) {
		if (copy_to_user(hp->dxferp, data_buf, vblk_hp->dxfer_len)) {
			err = -EFAULT;
			goto free_hp;
		}
	}

	if (copy_to_user(user, hp, header_len)) {
		err = -EFAULT;
		goto free_hp;
	}

free_hp:
	if (ioctl_req->ioctl_buf)
		kfree(ioctl_req->ioctl_buf);

	if (hp)
		kfree(hp);
exit:
	return err;
}
