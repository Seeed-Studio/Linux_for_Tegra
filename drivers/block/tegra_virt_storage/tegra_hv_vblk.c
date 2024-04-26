// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h> /* printk() */
#include <linux/pm.h>
#include <linux/slab.h>   /* kmalloc() */
#include <linux/fs.h>   /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/fcntl.h> /* O_ACCMODE */
#include <linux/hdreg.h> /* HDIO_GETGEO */
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <soc/tegra/fuse.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm-generic/bug.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>
#include <linux/version.h>
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
#include <linux/tegra-hsierrrptinj.h>
#endif
#include "tegra_vblk.h"

#define DISCARD_ERASE_SECERASE_MASK	(VS_BLK_DISCARD_OP_F | \
					VS_BLK_SECURE_ERASE_OP_F | \
					VS_BLK_ERASE_OP_F)
#define UFS_IOCTL_MAX_SIZE_SUPPORTED	0x80000
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
#define HSI_SDMMC4_REPORT_ID		0x805EU
#define HSI_ERROR_MAGIC			0xDEADDEAD

static uint32_t total_instance_id;
#endif

static int vblk_major;

static inline uint64_t _arch_counter_get_cntvct(void)
{
	uint64_t cval;

	asm volatile("mrs %0, cntvct_el0" : "=r" (cval));

	return cval;
}

/**
 * vblk_get_req: Get a handle to free vsc request.
 */
static struct vsc_request *vblk_get_req(struct vblk_dev *vblkdev)
{
	struct vsc_request *req = NULL;
	unsigned long bit;

	if (vblkdev->queue_state != VBLK_QUEUE_ACTIVE)
		goto exit;

	bit = find_first_zero_bit(vblkdev->pending_reqs, vblkdev->max_requests);
	if (bit < vblkdev->max_requests) {
		req = &vblkdev->reqs[bit];
		req->vs_req.req_id = bit;
		set_bit(bit, vblkdev->pending_reqs);
		vblkdev->inflight_reqs++;
		mod_timer(&req->timer, jiffies + 30*HZ);
	}

exit:
	return req;
}

static struct vsc_request *vblk_get_req_by_sr_num(struct vblk_dev *vblkdev,
		uint32_t num)
{
	struct vsc_request *req;

	if (num >= vblkdev->max_requests)
		return NULL;

	req = &vblkdev->reqs[num];
	if (test_bit(req->id, vblkdev->pending_reqs) == 0) {
		dev_err(vblkdev->device,
			"sr_num: Request index %d is not active!\n",
			req->id);
		req = NULL;
	}

	/* Assuming serial number is same as index into request array */
	return req;
}

/**
 * vblk_put_req: Free an active vsc request.
 */
static void vblk_put_req(struct vsc_request *req)
{
	struct vblk_dev *vblkdev;

	vblkdev = req->vblkdev;
	if (vblkdev == NULL) {
		pr_err("Request %d does not have valid vblkdev!\n",
				req->id);
		return;
	}

	if (req->id >= vblkdev->max_requests) {
		dev_err(vblkdev->device, "Request Index %d out of range!\n",
				req->id);
		return;
	}

	if (req != &vblkdev->reqs[req->id]) {
		dev_err(vblkdev->device,
			"Request Index %d does not match with the request!\n",
				req->id);
		return;
	}

	if (test_bit(req->id, vblkdev->pending_reqs) == 0) {
		dev_err(vblkdev->device,
			"Request index %d is not active!\n",
			req->id);
	} else {
		clear_bit(req->id, vblkdev->pending_reqs);
		memset(&req->vs_req, 0, sizeof(struct vs_request));
		req->req = NULL;
		memset(&req->iter, 0, sizeof(struct req_iterator));
		vblkdev->inflight_reqs--;

		if ((vblkdev->inflight_reqs == 0) &&
			(vblkdev->queue_state == VBLK_QUEUE_SUSPENDED)) {
			complete(&vblkdev->req_queue_empty);
		}
		del_timer(&req->timer);
	}
}

static int vblk_send_config_cmd(struct vblk_dev *vblkdev)
{
	struct vs_request *vs_req;
	int i = 0;

	/* This while loop exits as long as the remote endpoint cooperates. */
	if (tegra_hv_ivc_channel_notified(vblkdev->ivck) != 0) {
		pr_notice("vblk: send_config wait for ivc channel reset\n");
		while (tegra_hv_ivc_channel_notified(vblkdev->ivck) != 0) {
			if (i++ > IVC_RESET_RETRIES) {
				dev_err(vblkdev->device, "ivc reset timeout\n");
				return -EIO;
			}
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(usecs_to_jiffies(1));
		}
	}
	vs_req = (struct vs_request *)
		tegra_hv_ivc_write_get_next_frame(vblkdev->ivck);
	if (IS_ERR_OR_NULL(vs_req)) {
		dev_err(vblkdev->device, "no empty frame for write\n");
		return -EIO;
	}

	vs_req->type = VS_CONFIGINFO_REQ;

	dev_info(vblkdev->device, "send config cmd to ivc #%d\n",
		vblkdev->ivc_id);

	if (tegra_hv_ivc_write_advance(vblkdev->ivck)) {
		dev_err(vblkdev->device, "ivc write failed\n");
		return -EIO;
	}

	return 0;
}

static int vblk_get_configinfo(struct vblk_dev *vblkdev)
{
	struct vs_request *req;
	int32_t status;

	dev_info(vblkdev->device, "get config data from ivc #%d\n",
		vblkdev->ivc_id);

	req = (struct vs_request *)
		tegra_hv_ivc_read_get_next_frame(vblkdev->ivck);
	if (IS_ERR_OR_NULL(req)) {
		dev_err(vblkdev->device, "no empty frame for read\n");
		return -EIO;
	}

	status = req->status;
	vblkdev->config = req->config_info;

	if (tegra_hv_ivc_read_advance(vblkdev->ivck)) {
		dev_err(vblkdev->device, "ivc read failed\n");
		return -EIO;
	}

	if (status != 0)
		return -EINVAL;

	if (vblkdev->config.type != VS_BLK_DEV) {
		dev_err(vblkdev->device, "Non Blk dev config not supported!\n");
		return -EINVAL;
	}

	if (vblkdev->config.blk_config.num_blks == 0) {
		dev_err(vblkdev->device, "controller init failed\n");
		return -EINVAL;
	}

	return 0;
}

static void req_error_handler(struct vblk_dev *vblkdev, struct request *breq)
{
	dev_err(vblkdev->device,
		"Error for request pos %llx type %llx size %x\n",
		(blk_rq_pos(breq) * (uint64_t)SECTOR_SIZE),
		(uint64_t)req_op(breq),
		blk_rq_bytes(breq));

	blk_mq_end_request(breq, BLK_STS_IOERR);
}

static void handle_non_ioctl_resp(struct vblk_dev *vblkdev,
		struct vsc_request *vsc_req,
		struct vs_blk_response *blk_resp)
{
	struct bio_vec bvec;
	void *buffer;
	size_t size;
	size_t total_size = 0;
	bool invoke_req_err_hand = false;
	struct request *const bio_req = vsc_req->req;
	struct vs_blk_request *const blk_req =
		&(vsc_req->vs_req.blkdev_req.blk_req);

	if (blk_resp->status != 0) {
		invoke_req_err_hand = true;
		goto end;
	}

	if (req_op(bio_req) != REQ_OP_FLUSH) {
		if (blk_req->num_blks !=
		    blk_resp->num_blks) {
			invoke_req_err_hand = true;
			goto end;
		}
	}

	if (req_op(bio_req) == REQ_OP_READ) {
		rq_for_each_segment(bvec, bio_req, vsc_req->iter) {
			size = bvec.bv_len;
			buffer = page_address(bvec.bv_page) +
				bvec.bv_offset;

			if ((total_size + size) >
				(blk_req->num_blks *
				vblkdev->config.blk_config.hardblk_size)) {
				size =
				(blk_req->num_blks *
				vblkdev->config.blk_config.hardblk_size) -
					total_size;
			}

			if (!vblkdev->config.blk_config.use_vm_address) {
				memcpy(buffer,
					vsc_req->mempool_virt +
					total_size,
					size);
			}

			total_size += size;
			if (total_size ==
				(blk_req->num_blks *
				vblkdev->config.blk_config.hardblk_size))
				break;
		}
	}

end:
	if (vblkdev->config.blk_config.use_vm_address) {
		if ((req_op(bio_req) == REQ_OP_READ) ||
			(req_op(bio_req) == REQ_OP_WRITE)) {
			dma_unmap_sg(vblkdev->device,
				vsc_req->sg_lst,
				vsc_req->sg_num_ents,
				DMA_BIDIRECTIONAL);
			devm_kfree(vblkdev->device, vsc_req->sg_lst);
		}
	}

	if (!invoke_req_err_hand) {
			blk_mq_end_request(bio_req, BLK_STS_OK);
	} else {

		req_error_handler(vblkdev, bio_req);
	}
}

/**
 * complete_bio_req: Complete a bio request after server is
 *		done processing the request.
 */
static bool complete_bio_req(struct vblk_dev *vblkdev)
{
	int status = 0;
	struct vsc_request *vsc_req = NULL;
	struct vs_request *vs_req;
	struct vs_request req_resp;
	struct request *bio_req;

	/* First check if ivc read queue is empty */
	if (!tegra_hv_ivc_can_read(vblkdev->ivck))
		goto no_valid_io;

	/* Copy the data and advance to next frame */
	if ((tegra_hv_ivc_read(vblkdev->ivck, &req_resp,
				sizeof(struct vs_request)) <= 0)) {
		dev_err(vblkdev->device,
				"Couldn't increment read frame pointer!\n");
		goto no_valid_io;
	}

	status = req_resp.status;
	if (status != 0) {
		dev_err(vblkdev->device, "IO request error = %d\n",
				status);
	}

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	if (req_resp.req_id != HSI_ERROR_MAGIC) {
#endif
		vsc_req = vblk_get_req_by_sr_num(vblkdev, req_resp.req_id);
		if (vsc_req == NULL) {
			dev_err(vblkdev->device, "serial_number mismatch num %d!\n",
					req_resp.req_id);
			goto complete_bio_exit;
		}
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	} else {
		vblkdev->hsierror_status = req_resp.error_inject_resp.status;
		complete(&vblkdev->hsierror_handle);
		goto complete_bio_exit;
	}
#endif

	bio_req = vsc_req->req;
	vs_req = &vsc_req->vs_req;

	if ((bio_req != NULL) && (status == 0)) {
		if ((vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F)
			&& (req_op(bio_req) == REQ_OP_DRV_IN)) {
			vblk_complete_ioctl_req(vblkdev, vsc_req,
					req_resp.blkdev_resp.
					ioctl_resp.status);
			vblkdev->inflight_ioctl_reqs--;
			blk_mq_end_request(bio_req, BLK_STS_OK);
		}  else if (req_op(bio_req) != REQ_OP_DRV_IN) {
			handle_non_ioctl_resp(vblkdev, vsc_req,
				&(req_resp.blkdev_resp.blk_resp));
		} else {
			dev_info(vblkdev->device, "ioctl(pass through) command not supported\n");
		}

	} else if ((bio_req != NULL) && (status != 0)) {
		if (!(vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F)
			&& req_op(bio_req) == REQ_OP_DRV_IN) {
			dev_info(vblkdev->device, "ioctl(pass through) command not supported\n");
		} else {
			if ((vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F)
				&& (req_op(bio_req) == REQ_OP_DRV_IN))
				vblkdev->inflight_ioctl_reqs--;
			req_error_handler(vblkdev, bio_req);
		}

		if (req_op(bio_req) == REQ_OP_DRV_IN)
			vblkdev->inflight_ioctl_reqs--;
		req_error_handler(vblkdev, bio_req);
	} else {
		dev_err(vblkdev->device,
			"VSC request %d has null bio request!\n",
			vsc_req->id);
		goto bio_null;
	}

bio_null:
	vblk_put_req(vsc_req);

complete_bio_exit:
	return true;

no_valid_io:
	return false;
}

static bool bio_req_sanity_check(struct vblk_dev *vblkdev,
		struct request *bio_req,
		struct vsc_request *vsc_req)
{
	uint64_t start_offset = (blk_rq_pos(bio_req) * (uint64_t)SECTOR_SIZE);
	uint64_t req_bytes = blk_rq_bytes(bio_req);

	if ((start_offset >= vblkdev->size) || (req_bytes > vblkdev->size) ||
		((start_offset + req_bytes) > vblkdev->size))
	{
		dev_err(vblkdev->device,
			"Invalid I/O limit start 0x%llx size 0x%llx > 0x%llx\n",
			start_offset,
			req_bytes, vblkdev->size);
		return false;
	}

	if ((start_offset % vblkdev->config.blk_config.hardblk_size) != 0) {
		dev_err(vblkdev->device, "Unaligned block offset (%lld %d)\n",
			start_offset, vblkdev->config.blk_config.hardblk_size);
		return false;
	}

	if ((req_bytes % vblkdev->config.blk_config.hardblk_size) != 0) {
		dev_err(vblkdev->device, "Unaligned io length (%lld %d)\n",
			req_bytes, vblkdev->config.blk_config.hardblk_size);
		return false;
	}

	/* Check if the req_bytes > mempool_len only if IOVA is enabled and the
	 * operations are not DISCARD and SECURE_ERASE. Mempool is not used
	 * for DISCARD and SECURE_ERASE operations.
	 */
	if ((vblkdev->config.blk_config.use_vm_address == 0) &&
			(req_op(vsc_req->req) != REQ_OP_DISCARD) &&
			(req_op(vsc_req->req) != REQ_OP_SECURE_ERASE) &&
			(req_bytes > (uint64_t)vsc_req->mempool_len)) {
		dev_err(vblkdev->device, "Req bytes %llx greater than %x!\n",
			req_bytes, vsc_req->mempool_len);
		return false;
	}

	return true;
}

static enum blk_cmd_op cleanup_op_supported(struct vblk_dev *vblkdev, uint32_t ops_supported)
{
	enum blk_cmd_op cleanup_op = VS_UNKNOWN_BLK_CMD;

	/* Map discard operation if only secure erase ops is supported by VSC */
	if ((ops_supported & DISCARD_ERASE_SECERASE_MASK) == VS_BLK_SECURE_ERASE_OP_F)
		cleanup_op = VS_BLK_SECURE_ERASE;
	else if ((ops_supported & DISCARD_ERASE_SECERASE_MASK) == VS_BLK_ERASE_OP_F)
		cleanup_op = VS_BLK_ERASE;
	else if ((ops_supported & DISCARD_ERASE_SECERASE_MASK) == VS_BLK_DISCARD_OP_F)
		cleanup_op = VS_BLK_DISCARD;
	else
		dev_err(vblkdev->device, "Erase/Discard/SecErase neither is supported");

	return cleanup_op;
}

/**
 * submit_bio_req: Fetch a bio request and submit it to
 * server for processing.
 */
static bool submit_bio_req(struct vblk_dev *vblkdev)
{
	struct vsc_request *vsc_req = NULL;
	struct request *bio_req = NULL;
	struct vs_request *vs_req;
	struct bio_vec bvec;
	size_t size;
	size_t total_size = 0;
	void *buffer;
	struct req_entry *entry = NULL;
	size_t sz;
	uint32_t sg_cnt;
	uint32_t ops_supported = vblkdev->config.blk_config.req_ops_supported;
	dma_addr_t  sg_dma_addr = 0;

	/* Check if ivc queue is full */
	if (!tegra_hv_ivc_can_write(vblkdev->ivck))
		goto bio_exit;

	if (vblkdev->queue == NULL)
		goto bio_exit;

	vsc_req = vblk_get_req(vblkdev);
	if (vsc_req == NULL)
		goto bio_exit;

	spin_lock(&vblkdev->queue_lock);
	if(!list_empty(&vblkdev->req_list)) {
		entry = list_first_entry(&vblkdev->req_list, struct req_entry,
						list_entry);
		if ((vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F) &&
				(req_op(entry->req) == REQ_OP_DRV_IN) &&
				(vblkdev->config.blk_config.use_vm_address) &&
				(vblkdev->inflight_ioctl_reqs >= vblkdev->max_ioctl_requests)) {
			spin_unlock(&vblkdev->queue_lock);
			goto bio_exit;
		}
		list_del(&entry->list_entry);
		bio_req = entry->req;
		kfree(entry);
	}
	spin_unlock(&vblkdev->queue_lock);

	if (bio_req == NULL)
		goto bio_exit;

	if ((vblkdev->config.blk_config.use_vm_address) &&
		((req_op(bio_req) == REQ_OP_READ) ||
		(req_op(bio_req) == REQ_OP_WRITE))) {
		sz = (sizeof(struct scatterlist)
			* bio_req->nr_phys_segments);
		vsc_req->sg_lst =  devm_kzalloc(vblkdev->device, sz,
					GFP_KERNEL);
		if (vsc_req->sg_lst == NULL) {
			dev_err(vblkdev->device,
				"SG mem allocation failed\n");
			goto bio_exit;
		}
		sg_init_table(vsc_req->sg_lst,
			bio_req->nr_phys_segments);
		sg_cnt = blk_rq_map_sg(vblkdev->queue, bio_req,
				vsc_req->sg_lst);
		vsc_req->sg_num_ents = sg_nents(vsc_req->sg_lst);
		if (dma_map_sg(vblkdev->device, vsc_req->sg_lst,
			vsc_req->sg_num_ents, DMA_BIDIRECTIONAL) == 0) {
			dev_err(vblkdev->device, "dma_map_sg failed\n");
			goto bio_exit;
		}
		sg_dma_addr = sg_dma_address(vsc_req->sg_lst);
	}

	vsc_req->req = bio_req;
	vs_req = &vsc_req->vs_req;

	vs_req->type = VS_DATA_REQ;
	if (req_op(bio_req) != REQ_OP_DRV_IN) {
		if (req_op(bio_req) == REQ_OP_READ) {
			vs_req->blkdev_req.req_op = VS_BLK_READ;
		} else if (req_op(bio_req) == REQ_OP_WRITE) {
			vs_req->blkdev_req.req_op = VS_BLK_WRITE;
		} else if (req_op(bio_req) == REQ_OP_FLUSH) {
			vs_req->blkdev_req.req_op = VS_BLK_FLUSH;
		} else if (req_op(bio_req) == REQ_OP_DISCARD) {
			if (vblkdev->config.phys_dev == VSC_DEV_UFS) {
				vs_req->blkdev_req.req_op =
						cleanup_op_supported(vblkdev, ops_supported);
				if (vs_req->blkdev_req.req_op == VS_UNKNOWN_BLK_CMD)
					goto bio_exit;
			} else {
				vs_req->blkdev_req.req_op = VS_BLK_DISCARD;
			}
		} else if (req_op(bio_req) == REQ_OP_SECURE_ERASE) {
			if (vblkdev->config.phys_dev == VSC_DEV_UFS) {
				vs_req->blkdev_req.req_op =
						cleanup_op_supported(vblkdev, ops_supported);
				if (vs_req->blkdev_req.req_op == VS_UNKNOWN_BLK_CMD)
					goto bio_exit;
			} else {
				vs_req->blkdev_req.req_op = VS_BLK_SECURE_ERASE;
			}
		} else {
			dev_err(vblkdev->device,
				"Request direction is not read/write!\n");
			goto bio_exit;
		}

		vsc_req->iter.bio = NULL;
		if (req_op(bio_req) == REQ_OP_FLUSH) {
			vs_req->blkdev_req.blk_req.blk_offset = 0;
			vs_req->blkdev_req.blk_req.num_blks =
				vblkdev->config.blk_config.num_blks;
		} else {
			if (!bio_req_sanity_check(vblkdev, bio_req, vsc_req)) {
				goto bio_exit;
			}

			vs_req->blkdev_req.blk_req.blk_offset = ((blk_rq_pos(bio_req) *
				(uint64_t)SECTOR_SIZE)
				/ vblkdev->config.blk_config.hardblk_size);
			vs_req->blkdev_req.blk_req.num_blks = ((blk_rq_sectors(bio_req) *
				SECTOR_SIZE) /
				vblkdev->config.blk_config.hardblk_size);

			if (!vblkdev->config.blk_config.use_vm_address) {
				vs_req->blkdev_req.blk_req.data_offset =
							vsc_req->mempool_offset;
			} else {
				vs_req->blkdev_req.blk_req.data_offset = 0;
				/* Provide IOVA  as part of request */
				vs_req->blkdev_req.blk_req.iova_addr =
							(uint64_t)sg_dma_addr;
			}
		}

		if (req_op(bio_req) == REQ_OP_WRITE) {
			rq_for_each_segment(bvec, bio_req, vsc_req->iter) {
				size = bvec.bv_len;
				buffer = page_address(bvec.bv_page) +
						bvec.bv_offset;

				if ((total_size + size) >
					(vs_req->blkdev_req.blk_req.num_blks *
					vblkdev->config.blk_config.hardblk_size))
				{
					size = (vs_req->blkdev_req.blk_req.num_blks *
						vblkdev->config.blk_config.hardblk_size) -
						total_size;
				}

				/* memcpy to mempool not needed as VM IOVA is
				 * provided
				 */
				if (!vblkdev->config.blk_config.use_vm_address) {
					memcpy(
					vsc_req->mempool_virt + total_size,
					buffer, size);
				}

				total_size += size;
				if (total_size == (vs_req->blkdev_req.blk_req.num_blks *
					vblkdev->config.blk_config.hardblk_size)) {
					break;
				}
			}
		}
	} else {
		if (vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F
			&& !vblk_prep_ioctl_req(vblkdev,
#if defined(NV_REQUEST_STRUCT_HAS_COMPLETION_DATA_ARG) /* Removed in Linux v6.5 */
			(struct vblk_ioctl_req *)bio_req->completion_data,
#else
			NULL,
#endif
			vsc_req)) {
			vblkdev->inflight_ioctl_reqs++;
		} else if (!(vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F)) {
			dev_info(vblkdev->device, "ioctl(pass through) command not supported\n");
			goto bio_exit;
		} else {
			dev_err(vblkdev->device,
				"Failed to prepare ioctl request!\n");
			goto bio_exit;
		}
	}

	vsc_req->time = _arch_counter_get_cntvct();
	if (!tegra_hv_ivc_write(vblkdev->ivck, vs_req,
				sizeof(struct vs_request))) {
		dev_err(vblkdev->device,
			"Request Id %d IVC write failed!\n",
				vsc_req->id);
		goto bio_exit;
	}

	return true;

bio_exit:
	if (vsc_req != NULL) {
		vblk_put_req(vsc_req);
	}

	if (bio_req != NULL) {
		req_error_handler(vblkdev, bio_req);
		return true;
	}

	return false;
}

static void vblk_request_work(struct work_struct *ws)
{
	struct vblk_dev *vblkdev =
		container_of(ws, struct vblk_dev, work);
	bool req_submitted, req_completed;

	/* Taking ivc lock before performing IVC read/write */
	mutex_lock(&vblkdev->ivc_lock);
	if (tegra_hv_ivc_channel_notified(vblkdev->ivck) != 0) {
		mutex_unlock(&vblkdev->ivc_lock);
		return;
	}

	req_submitted = true;
	req_completed = true;
	while (req_submitted || req_completed) {
		req_completed = complete_bio_req(vblkdev);

		req_submitted = submit_bio_req(vblkdev);
	}
	mutex_unlock(&vblkdev->ivc_lock);
}

/* The simple form of the request function. */
static blk_status_t vblk_request(struct blk_mq_hw_ctx *hctx,
			const struct blk_mq_queue_data *bd)
{
	struct req_entry *entry;
	struct request *req = bd->rq;
	struct vblk_dev *vblkdev = hctx->queue->queuedata;

	blk_mq_start_request(req);

	/* malloc for req list entry */
	entry = kmalloc(sizeof(struct req_entry), GFP_ATOMIC);
	if (entry == NULL) {
		dev_err(vblkdev->device, "Failed to allocate memory\n");
		return BLK_STS_IOERR;
	}

	/* Initialise the entry */
	entry->req = req;
	INIT_LIST_HEAD(&entry->list_entry);

	/* Insert the req to list */
	spin_lock(&vblkdev->queue_lock);
	list_add_tail(&entry->list_entry, &vblkdev->req_list);
	spin_unlock(&vblkdev->queue_lock);

	/* Now invoke the queue to handle data inserted in queue */
	queue_work_on(WORK_CPU_UNBOUND, vblkdev->wq, &vblkdev->work);

	return BLK_STS_OK;
}

/* Open and release */
#if defined(NV_BLOCK_DEVICE_OPERATIONS_OPEN_HAS_GENDISK_ARG) /* Linux v6.5 */
static int vblk_open(struct gendisk *disk, fmode_t mode)
{
	struct vblk_dev *vblkdev = disk->private_data;
#else
static int vblk_open(struct block_device *device, fmode_t mode)
{
	struct vblk_dev *vblkdev = device->bd_disk->private_data;
#endif

	spin_lock(&vblkdev->lock);
	if (!vblkdev->users) {
#if defined(NV_DISK_CHECK_MEDIA_CHANGE_PRESENT) /* Linux v6.5 */
		disk_check_media_change(disk);
#else
		bdev_check_media_change(device);
#endif
	}
	vblkdev->users++;

	spin_unlock(&vblkdev->lock);
	return 0;
}

#if defined(NV_BLOCK_DEVICE_OPERATIONS_RELEASE_HAS_NO_MODE_ARG) /* Linux v6.5 */
static void vblk_release(struct gendisk *disk)
#else
static void vblk_release(struct gendisk *disk, fmode_t mode)
#endif
{
	struct vblk_dev *vblkdev = disk->private_data;

	spin_lock(&vblkdev->lock);

	vblkdev->users--;

	spin_unlock(&vblkdev->lock);
}

static int vblk_getgeo(struct block_device *device, struct hd_geometry *geo)
{
	geo->heads = VS_LOG_HEADS;
	geo->sectors = VS_LOG_SECTS;
	geo->cylinders = get_capacity(device->bd_disk) /
		(geo->heads * geo->sectors);

	return 0;
}

/* The device operations structure. */
static const struct block_device_operations vblk_ops = {
	.owner           = THIS_MODULE,
	.open            = vblk_open,
	.release         = vblk_release,
	.getgeo          = vblk_getgeo,
#if defined(NV_REQUEST_STRUCT_HAS_COMPLETION_DATA_ARG) /* Removed in Linux v6.5 */
	/*
	 * FIXME: ioctl is not supported for Linux v6.5 where the
	 * 'completion_data' member has been removed from the
	 * 'request' structure.
	 */
	.ioctl           = vblk_ioctl
#endif
};

static ssize_t
vblk_phys_dev_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct vblk_dev *vblk = disk->private_data;

	if (vblk->config.phys_dev == VSC_DEV_EMMC)
		return snprintf(buf, 16, "EMMC\n");
	else if (vblk->config.phys_dev == VSC_DEV_UFS)
		return snprintf(buf, 16, "UFS\n");
	else
		return snprintf(buf, 16, "Unknown\n");
}

static ssize_t
vblk_phys_base_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct vblk_dev *vblk = disk->private_data;

	return snprintf(buf, 16, "0x%x\n", vblk->config.phys_base);
}

static ssize_t
vblk_storage_type_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct vblk_dev *vblk = disk->private_data;

	switch (vblk->config.storage_type) {
	case VSC_STORAGE_RPMB:
		return snprintf(buf, 16, "RPMB\n");
	case VSC_STORAGE_BOOT:
		return snprintf(buf, 16, "BOOT\n");
	case VSC_STORAGE_LUN0:
		return snprintf(buf, 16, "LUN0\n");
	case VSC_STORAGE_LUN1:
		return snprintf(buf, 16, "LUN1\n");
	case VSC_STORAGE_LUN2:
		return snprintf(buf, 16, "LUN2\n");
	case VSC_STORAGE_LUN3:
		return snprintf(buf, 16, "LUN3\n");
	case VSC_STORAGE_LUN4:
		return snprintf(buf, 16, "LUN4\n");
	case VSC_STORAGE_LUN5:
		return snprintf(buf, 16, "LUN5\n");
	case VSC_STORAGE_LUN6:
		return snprintf(buf, 16, "LUN6\n");
	case VSC_STORAGE_LUN7:
		return snprintf(buf, 16, "LUN7\n");
	default:
		break;
	}

	return snprintf(buf, 16, "Unknown\n");
}

static ssize_t
vblk_speed_mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct vblk_dev *vblk = disk->private_data;

	return snprintf(buf, 32, "%s\n", vblk->config.speed_mode);
}

static const struct device_attribute dev_attr_phys_dev_ro =
	__ATTR(phys_dev, 0444,
	       vblk_phys_dev_show, NULL);

static const struct device_attribute dev_attr_phys_base_ro =
	__ATTR(phys_base, 0444,
	       vblk_phys_base_show, NULL);

static const struct device_attribute dev_attr_storage_type_ro =
	__ATTR(storage_type, 0444,
	       vblk_storage_type_show, NULL);

static const struct device_attribute dev_attr_speed_mode_ro =
	__ATTR(speed_mode, 0444,
	       vblk_speed_mode_show, NULL);

static const struct blk_mq_ops vblk_mq_ops = {
	.queue_rq	= vblk_request,
};

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))

/* Error report injection test support is included */
static int vblk_inject_err_fsi(unsigned int inst_id, struct epl_error_report_frame err_rpt_frame,
				void *data)
{
	struct vblk_dev *vblkdev = (struct vblk_dev *)data;
	struct vs_request *vs_req;
	int err = -EFAULT;
	int i = 0;

	/* Sanity check inst_id */
	if (inst_id != vblkdev->instance_id) {
		dev_err(vblkdev->device, "Invalid Input -> Instance ID = 0x%04x\n", inst_id);
		return -EINVAL;
	}

	/* Sanity check reporter_id */
	if (err_rpt_frame.reporter_id != vblkdev->epl_reporter_id) {
		dev_err(vblkdev->device, "Invalid Input -> Reporter ID = 0x%04x\n",
						err_rpt_frame.reporter_id);
		return -EINVAL;
	}

	mutex_lock(&vblkdev->ivc_lock);
	vblkdev->hsierror_status = 0;

	/* This while loop exits as long as the remote endpoint cooperates. */
	if (tegra_hv_ivc_channel_notified(vblkdev->ivck) != 0) {
		pr_notice("vblk: send_config wait for ivc channel reset\n");
		while (tegra_hv_ivc_channel_notified(vblkdev->ivck) != 0) {
			if (i++ > IVC_RESET_RETRIES) {
				dev_err(vblkdev->device, "ivc reset timeout\n");
				mutex_unlock(&vblkdev->ivc_lock);
				return -EIO;
			}
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(usecs_to_jiffies(1));
		}
	}

	while (true) {
		vs_req = (struct vs_request *)
			tegra_hv_ivc_write_get_next_frame(vblkdev->ivck);
		if (vs_req != NULL)
			break;
	}

	vs_req->req_id = HSI_ERROR_MAGIC;
	vs_req->type = VS_ERR_INJECT;
	vs_req->error_inject_req.error_id = err_rpt_frame.error_code;

	if (tegra_hv_ivc_write_advance(vblkdev->ivck)) {
		dev_err(vblkdev->device, "ivc write failed\n");
		mutex_unlock(&vblkdev->ivc_lock);
		return -EIO;
	}

	mutex_unlock(&vblkdev->ivc_lock);

	if (wait_for_completion_timeout(&vblkdev->hsierror_handle, msecs_to_jiffies(1000)) == 0) {
		dev_err(vblkdev->device, "hsi response timeout\n");
		err = -EAGAIN;
		return err;
	}

	err = vblkdev->hsierror_status;
	return err;
}
#endif

/* Set up virtual device. */
static void setup_device(struct vblk_dev *vblkdev)
{
	uint32_t max_io_bytes;
	uint32_t req_id;
	uint32_t max_requests;
	uint32_t max_ioctl_requests = 0U;
	struct vsc_request *req;
	int ret;
	struct tegra_hv_ivm_cookie *ivmk;
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	int err;
#endif

	vblkdev->size =
		vblkdev->config.blk_config.num_blks *
			vblkdev->config.blk_config.hardblk_size;

	memset(&vblkdev->tag_set, 0, sizeof(vblkdev->tag_set));
	vblkdev->tag_set.ops = &vblk_mq_ops;
	vblkdev->tag_set.nr_hw_queues = 1;
	vblkdev->tag_set.nr_maps = 1;
	vblkdev->tag_set.queue_depth = 16;
	vblkdev->tag_set.numa_node = NUMA_NO_NODE;
	vblkdev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;

	ret = blk_mq_alloc_tag_set(&vblkdev->tag_set);
	if (ret)
		return;

#if defined(NV_BLK_MQ_ALLOC_QUEUE_PRESENT)
	vblkdev->queue = blk_mq_alloc_queue(&vblkdev->tag_set, NULL, NULL);
#else
	vblkdev->queue = blk_mq_init_queue(&vblkdev->tag_set);
#endif
	if (IS_ERR(vblkdev->queue)) {
		dev_err(vblkdev->device, "failed to init blk queue\n");
		blk_mq_free_tag_set(&vblkdev->tag_set);
		return;
	}

	vblkdev->queue->queuedata = vblkdev;

	blk_queue_logical_block_size(vblkdev->queue,
		vblkdev->config.blk_config.hardblk_size);
	blk_queue_physical_block_size(vblkdev->queue,
		vblkdev->config.blk_config.hardblk_size);

	if (vblkdev->config.blk_config.req_ops_supported & VS_BLK_FLUSH_OP_F) {
		blk_queue_write_cache(vblkdev->queue, true, false);
	}

	if (vblkdev->config.blk_config.max_read_blks_per_io !=
		vblkdev->config.blk_config.max_write_blks_per_io) {
		dev_err(vblkdev->device,
			"Different read/write blks not supported!\n");
		return;
	}

	/* Set the maximum number of requests possible using
	 * server returned information */
	max_io_bytes = (vblkdev->config.blk_config.hardblk_size *
			vblkdev->config.blk_config.max_read_blks_per_io);
	if (max_io_bytes == 0) {
		dev_err(vblkdev->device, "Maximum io bytes value is 0!\n");
		return;
	}

	/* reserve mempool for eMMC device and for ufs device
	 * with pass through support
	 */
	if ((vblkdev->config.blk_config.use_vm_address == 1U
				&& vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F)
				|| vblkdev->config.blk_config.use_vm_address == 0U) {
		if (of_property_read_u32_index(vblkdev->device->of_node, "mempool", 0,
			&(vblkdev->ivm_id))) {
			dev_err(vblkdev->device, "Failed to read mempool property\n");
			return;
		}

		ivmk = tegra_hv_mempool_reserve(vblkdev->ivm_id);
		if (IS_ERR_OR_NULL(ivmk)) {
			dev_err(vblkdev->device, "Failed to reserve IVM channel %d\n",
				vblkdev->ivm_id);
			ivmk = NULL;
			return;
		}
		vblkdev->ivmk = ivmk;

		vblkdev->shared_buffer = devm_memremap(vblkdev->device,
			ivmk->ipa, ivmk->size, MEMREMAP_WB);
		if (IS_ERR_OR_NULL(vblkdev->shared_buffer)) {
			dev_err(vblkdev->device, "Failed to map mempool area %d\n",
				vblkdev->ivm_id);
			tegra_hv_mempool_unreserve(vblkdev->ivmk);
			return;
		}
	}

	/* If IOVA feature is enabled for virt partition, then set max_requests
	 * to number of IVC frames. Since IOCTL's still use mempool, set
	 * max_ioctl_requests based on mempool.
	 */
	if (vblkdev->config.blk_config.use_vm_address == 1U) {
		max_requests = vblkdev->ivck->nframes;
		/* set max_ioctl_requests if pass through is supported */
		if (vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F) {
			max_ioctl_requests = ((vblkdev->ivmk->size) / UFS_IOCTL_MAX_SIZE_SUPPORTED);
			if (max_ioctl_requests > MAX_VSC_REQS)
				max_ioctl_requests = MAX_VSC_REQS;
		}
	} else {
		max_requests = ((vblkdev->ivmk->size) / max_io_bytes);
		max_ioctl_requests = max_requests;
	}

	if (max_requests < MAX_VSC_REQS) {
		/* Warn if the virtual storage device supports
		 * normal read write operations */
		if (vblkdev->config.blk_config.req_ops_supported &
				(VS_BLK_READ_OP_F |
				 VS_BLK_WRITE_OP_F)) {
			dev_warn(vblkdev->device,
				"Setting Max requests to %d, consider "
				"increasing mempool size !\n",
				max_requests);
		}
	} else if (max_requests > MAX_VSC_REQS) {
		max_requests = MAX_VSC_REQS;
		dev_warn(vblkdev->device,
			"Reducing the max requests to %d, consider"
			" supporting more requests for the vblkdev!\n",
			MAX_VSC_REQS);
	}

	/* if the number of ivc frames is lesser than the  maximum requests that
	 * can be supported(calculated based on mempool size above), treat this
	 * as critical error and panic.
	 *
	 *if (num_of_ivc_frames < max_supported_requests)
	 *   PANIC
	 * Ideally, these 2 should be equal for below reasons
	 *   1. Each ivc frame is a request should have a backing data memory
	 *      for transfers. So, number of requests supported by message
	 *      request memory should be <= number of frames in
	 *      IVC queue. The read/write logic depends on this.
	 *   2. If number of requests supported by message request memory is
	 *	more than IVC frame count, then thats a wastage of memory space
	 *      and it introduces a race condition in submit_bio_req().
	 *      The race condition happens when there is only one empty slot in
	 *      IVC write queue and 2 threads enter submit_bio_req(). Both will
	 *      compete for IVC write(After calling ivc_can_write) and one of
	 *      the write will fail. But with vblk_get_req() this race can be
	 *      avoided if num_of_ivc_frames >= max_supported_requests
	 *      holds true.
	 *
	 *  In short, the optimal setting is when both of these are equal
	 */
	if (vblkdev->ivck->nframes < max_requests) {
		/* Error if the virtual storage device supports
		 * read, write and ioctl operations
		 */
		if (vblkdev->config.blk_config.req_ops_supported &
				(VS_BLK_READ_OP_F |
				 VS_BLK_WRITE_OP_F |
				 VS_BLK_IOCTL_OP_F)) {
			panic("hv_vblk: IVC Channel:%u IVC frames %d less than possible max requests %d!\n",
				vblkdev->ivc_id, vblkdev->ivck->nframes,
				max_requests);
			return;
		}
	}

	for (req_id = 0; req_id < max_requests; req_id++){
		req = &vblkdev->reqs[req_id];
		if (vblkdev->config.blk_config.use_vm_address == 0U) {
			req->mempool_virt = (void *)((uintptr_t)vblkdev->shared_buffer +
				(uintptr_t)(req_id * max_io_bytes));
			req->mempool_offset = (req_id * max_io_bytes);
		} else {
			if (vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F) {
				req->mempool_virt = (void *)((uintptr_t)vblkdev->shared_buffer +
				(uintptr_t)((req_id % max_ioctl_requests) * UFS_IOCTL_MAX_SIZE_SUPPORTED));
				req->mempool_offset = (req_id % max_ioctl_requests) * UFS_IOCTL_MAX_SIZE_SUPPORTED;
			}
		}
		req->mempool_len = max_io_bytes;
		req->id = req_id;
		req->vblkdev = vblkdev;
	}

	if (max_requests == 0) {
		dev_err(vblkdev->device,
			"maximum requests set to 0!\n");
		return;
	}

	vblkdev->max_requests = max_requests;
	vblkdev->max_ioctl_requests = max_ioctl_requests;
#if defined(NV_BLK_QUEUE_MAX_HW_SECTORS_PRESENT) /* Removed in Linux v6.10 */
	blk_queue_max_hw_sectors(vblkdev->queue, max_io_bytes / SECTOR_SIZE);
#endif
	blk_queue_flag_set(QUEUE_FLAG_NONROT, vblkdev->queue);

	if ((vblkdev->config.blk_config.req_ops_supported & VS_BLK_SECURE_ERASE_OP_F)
	     || (vblkdev->config.blk_config.req_ops_supported & VS_BLK_ERASE_OP_F))
#if defined(QUEUE_FLAG_SECERASE) /* Removed in Linux 5.19 */
		/*
		 * FIXME: Support for Linux v5.19+ kernels
		 */
		blk_queue_flag_set(QUEUE_FLAG_SECERASE, vblkdev->queue);
#endif

	if ((vblkdev->config.blk_config.req_ops_supported & VS_BLK_DISCARD_OP_F)
	  || (((vblkdev->config.blk_config.req_ops_supported & VS_BLK_SECURE_ERASE_OP_F)
	  || (vblkdev->config.blk_config.req_ops_supported & VS_BLK_ERASE_OP_F))
	  && vblkdev->config.phys_dev == VSC_DEV_UFS)) {
#if defined(QUEUE_FLAG_DISCARD) /* Removed in Linux v5.19 */
		/*
		 * FIXME: Support for Linux v5.19+ kernels
		 */
		blk_queue_flag_set(QUEUE_FLAG_DISCARD, vblkdev->queue);
#endif
		blk_queue_max_discard_sectors(vblkdev->queue,
			vblkdev->config.blk_config.max_erase_blks_per_io);
		vblkdev->queue->limits.discard_granularity =
			vblkdev->config.blk_config.hardblk_size;
	}

	/* And the gendisk structure. */
#if defined(NV_BLK_MQ_ALLOC_DISK_FOR_QUEUE_PRESENT) /* Linux v6.0 */
	vblkdev->gd = blk_mq_alloc_disk_for_queue(vblkdev->queue, NULL);
#elif defined(NV___ALLOC_DISK_NODE_HAS_LKCLASS_ARG) /* Linux v5.15 */
	vblkdev->gd = __alloc_disk_node(vblkdev->queue, NUMA_NO_NODE, NULL);
#else
	vblkdev->gd = __alloc_disk_node(VBLK_MINORS, NUMA_NO_NODE);
#endif
	if (!vblkdev->gd) {
		dev_err(vblkdev->device, "alloc_disk failure\n");
		return;
	}
	vblkdev->gd->major = vblk_major;
	vblkdev->gd->first_minor = vblkdev->devnum * VBLK_MINORS;
	vblkdev->gd->minors = VBLK_MINORS;
	vblkdev->gd->fops = &vblk_ops;
	vblkdev->gd->queue = vblkdev->queue;
	vblkdev->gd->private_data = vblkdev;
#if defined(GENHD_FL_EXT_DEVT) /* Removed in Linux v5.17 */
	vblkdev->gd->flags |= GENHD_FL_EXT_DEVT;
#endif

	/* Don't allow scanning of the device when block
	 * requests are not supported */
	if (!(vblkdev->config.blk_config.req_ops_supported &
				VS_BLK_READ_OP_F)) {
#if defined(GENHD_FL_NO_PART_SCAN) /* Removed in Linux v5.17 */
		vblkdev->gd->flags |= GENHD_FL_NO_PART_SCAN;
#endif
	}

	/* Set disk read-only if config response say so */
	if (!(vblkdev->config.blk_config.req_ops_supported &
				VS_BLK_READ_ONLY_MASK)) {
		dev_info(vblkdev->device, "setting device read-only\n");
		set_disk_ro(vblkdev->gd, 1);
	}

	if (vblkdev->config.storage_type == VSC_STORAGE_RPMB) {
		if (snprintf(vblkdev->gd->disk_name, 32, "vblkrpmb%d",
				vblkdev->devnum) < 0) {
			dev_err(vblkdev->device, "Error while updating disk_name!\n");
			return;
		}
	} else {
		if (snprintf(vblkdev->gd->disk_name, 32, "vblkdev%d",
				vblkdev->devnum) < 0) {
			dev_err(vblkdev->device, "Error while updating disk_name!\n");
			return;
		}
	}

	set_capacity(vblkdev->gd, (vblkdev->size / SECTOR_SIZE));
#if defined(NV_DEVICE_ADD_DISK_HAS_INT_RETURN_TYPE) /* Linux v5.15 */
	if (device_add_disk(vblkdev->device, vblkdev->gd, NULL)) {
		dev_err(vblkdev->device, "Error adding disk!\n");
		return;
	}
#else
	device_add_disk(vblkdev->device, vblkdev->gd, NULL);
#endif

	if (device_create_file(disk_to_dev(vblkdev->gd),
		&dev_attr_phys_dev_ro)) {
		dev_warn(vblkdev->device, "Error adding phys dev file!\n");
		return;
	}

	if (device_create_file(disk_to_dev(vblkdev->gd),
		&dev_attr_phys_base_ro)) {
		dev_warn(vblkdev->device, "Error adding phys base file!\n");
		return;
	}

	if (device_create_file(disk_to_dev(vblkdev->gd),
		&dev_attr_storage_type_ro)) {
		dev_warn(vblkdev->device, "Error adding storage type file!\n");
		return;
	}

	if (device_create_file(disk_to_dev(vblkdev->gd),
		&dev_attr_speed_mode_ro)) {
		dev_warn(vblkdev->device, "Error adding speed_mode file!\n");
		return;
	}


#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	if (vblkdev->config.phys_dev == VSC_DEV_EMMC) {
		vblkdev->epl_id = IP_SDMMC;
		vblkdev->epl_reporter_id = HSI_SDMMC4_REPORT_ID;
		vblkdev->instance_id = total_instance_id++;
	}

	if (vblkdev->epl_id == IP_SDMMC) {
		/* Register error reporting callback */
		err = hsierrrpt_reg_cb(vblkdev->epl_id, vblkdev->instance_id,
							vblk_inject_err_fsi, vblkdev);
		if (err != 0)
			dev_info(vblkdev->device, "Err inj callback registration failed: %d", err);
	}
#endif
}

static void vblk_init_device(struct work_struct *ws)
{
	struct vblk_dev *vblkdev = container_of(ws, struct vblk_dev, init);

	mutex_lock(&vblkdev->ivc_lock);
	/* wait for ivc channel reset to finish */
	if (tegra_hv_ivc_channel_notified(vblkdev->ivck) != 0) {
		mutex_unlock(&vblkdev->ivc_lock);
		return;	/* this will be rescheduled by irq handler */
	}

	if (tegra_hv_ivc_can_read(vblkdev->ivck) && !vblkdev->initialized) {
		if (vblk_get_configinfo(vblkdev)) {
			mutex_unlock(&vblkdev->ivc_lock);
			return;
		}

		mutex_unlock(&vblkdev->ivc_lock);
		vblkdev->initialized = true;
		setup_device(vblkdev);
		return;
	}
	mutex_unlock(&vblkdev->ivc_lock);
}

static irqreturn_t ivc_irq_handler(int irq, void *data)
{
	struct vblk_dev *vblkdev = (struct vblk_dev *)data;

	if (vblkdev->initialized)
		queue_work_on(WORK_CPU_UNBOUND, vblkdev->wq, &vblkdev->work);
	else
		schedule_work(&vblkdev->init);

	return IRQ_HANDLED;
}

static void bio_request_timeout_callback(struct timer_list *timer)
{
	struct vsc_request *req = from_timer(req, timer, timer);

	dev_err(req->vblkdev->device, "Request id %d timed out. curr ctr: %llu sched ctr: %llu\n",
						req->id, _arch_counter_get_cntvct(), req->time);

}

static void tegra_create_timers(struct vblk_dev *vblkdev)
{
	uint32_t i;

	for (i = 0; i < MAX_VSC_REQS; i++)
		timer_setup(&vblkdev->reqs[i].timer, bio_request_timeout_callback, 0);

}

static int tegra_hv_vblk_probe(struct platform_device *pdev)
{
	static struct device_node *vblk_node;
	struct vblk_dev *vblkdev;
	struct device *dev = &pdev->dev;
	int ret;

	if (!is_tegra_hypervisor_mode()) {
		dev_err(dev, "Hypervisor is not present\n");
		return -ENODEV;
	}

	if (vblk_major == 0) {
		dev_err(dev, "major number is invalid\n");
		return -ENODEV;
	}

	vblk_node = dev->of_node;
	if (vblk_node == NULL) {
		dev_err(dev, "No of_node data\n");
		return -ENODEV;
	}

	dev_info(dev, "allocate drvdata buffer\n");
	vblkdev = devm_kzalloc(dev, sizeof(struct vblk_dev), GFP_KERNEL);
	if (vblkdev == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, vblkdev);
	vblkdev->device = dev;

	/* Get properties of instance and ivc channel id */
	if (of_property_read_u32(vblk_node, "instance", &(vblkdev->devnum))) {
		dev_err(dev, "Failed to read instance property\n");
		ret = -ENODEV;
		goto fail;
	} else {
		if (of_property_read_u32_index(vblk_node, "ivc", 1,
			&(vblkdev->ivc_id))) {
			dev_err(dev, "Failed to read ivc property\n");
			ret = -ENODEV;
			goto fail;
		}
	}

	vblkdev->ivck = tegra_hv_ivc_reserve(NULL, vblkdev->ivc_id, NULL);
	if (IS_ERR_OR_NULL(vblkdev->ivck)) {
		dev_err(dev, "Failed to reserve IVC channel %d\n",
			vblkdev->ivc_id);
		vblkdev->ivck = NULL;
		ret = -ENODEV;
		goto fail;
	}
	tegra_hv_ivc_channel_reset(vblkdev->ivck);
	vblkdev->initialized = false;

	vblkdev->wq = alloc_workqueue("vblk_req_wq%d",
		WQ_UNBOUND | WQ_MEM_RECLAIM,
		1, vblkdev->devnum);
	if (vblkdev->wq == NULL) {
		dev_err(dev, "Failed to allocate workqueue\n");
		ret = -ENOMEM;
		goto free_ivc;
	}

	init_completion(&vblkdev->req_queue_empty);
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	init_completion(&vblkdev->hsierror_handle);
#endif
	vblkdev->queue_state = VBLK_QUEUE_ACTIVE;

	spin_lock_init(&vblkdev->lock);
	spin_lock_init(&vblkdev->queue_lock);
	mutex_init(&vblkdev->ioctl_lock);
	mutex_init(&vblkdev->ivc_lock);

	INIT_WORK(&vblkdev->init, vblk_init_device);
	INIT_WORK(&vblkdev->work, vblk_request_work);
	/* creating and initializing the an internal request list */
	INIT_LIST_HEAD(&vblkdev->req_list);

	/* Create timers for each request going to storage server*/
	tegra_create_timers(vblkdev);

	if (devm_request_irq(vblkdev->device, vblkdev->ivck->irq,
		ivc_irq_handler, 0, "vblk", vblkdev)) {
		dev_err(dev, "Failed to request irq %d\n", vblkdev->ivck->irq);
		ret = -EINVAL;
		goto free_wq;
	}

	mutex_lock(&vblkdev->ivc_lock);
	if (vblk_send_config_cmd(vblkdev)) {
		dev_err(dev, "Failed to send config cmd\n");
		ret = -EACCES;
		mutex_unlock(&vblkdev->ivc_lock);
		goto free_wq;
	}
	mutex_unlock(&vblkdev->ivc_lock);

	return 0;

free_wq:
	destroy_workqueue(vblkdev->wq);

free_ivc:
	tegra_hv_ivc_unreserve(vblkdev->ivck);

fail:
	return ret;
}

static int tegra_hv_vblk_remove(struct platform_device *pdev)
{
	struct vblk_dev *vblkdev = platform_get_drvdata(pdev);

	if (vblkdev->gd) {
		del_gendisk(vblkdev->gd);
		put_disk(vblkdev->gd);
	}

	if (vblkdev->queue)
#if defined(NV_BLK_MQ_DESTROY_QUEUE_PRESENT) /* Linux v6.0 */
		blk_mq_destroy_queue(vblkdev->queue);
#else
		blk_cleanup_queue(vblkdev->queue);
#endif

	destroy_workqueue(vblkdev->wq);
	tegra_hv_ivc_unreserve(vblkdev->ivck);

	if ((vblkdev->config.blk_config.use_vm_address == 1U
				&& vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F)
				|| vblkdev->config.blk_config.use_vm_address == 0U) {
		tegra_hv_mempool_unreserve(vblkdev->ivmk);
	}

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	if (vblkdev->epl_id == IP_SDMMC)
		hsierrrpt_dereg_cb(vblkdev->epl_id, vblkdev->instance_id);
#endif

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tegra_hv_vblk_suspend(struct device *dev)
{
	struct vblk_dev *vblkdev = dev_get_drvdata(dev);
	unsigned long flags;

	if (vblkdev->queue) {
		spin_lock_irqsave(&vblkdev->queue->queue_lock, flags);
		blk_mq_stop_hw_queues(vblkdev->queue);
		spin_unlock_irqrestore(&vblkdev->queue->queue_lock, flags);

		spin_lock(&vblkdev->lock);
		vblkdev->queue_state = VBLK_QUEUE_SUSPENDED;

		/* Mark the queue as empty if inflight requests are 0 */
		if (vblkdev->inflight_reqs == 0)
			complete(&vblkdev->req_queue_empty);
		spin_unlock(&vblkdev->lock);

		wait_for_completion(&vblkdev->req_queue_empty);
		disable_irq(vblkdev->ivck->irq);

		flush_workqueue(vblkdev->wq);
	}

	return 0;
}

static int tegra_hv_vblk_resume(struct device *dev)
{
	struct vblk_dev *vblkdev = dev_get_drvdata(dev);
	unsigned long flags;

	if (vblkdev->queue) {
		spin_lock(&vblkdev->lock);
		vblkdev->queue_state = VBLK_QUEUE_ACTIVE;
		reinit_completion(&vblkdev->req_queue_empty);
		spin_unlock(&vblkdev->lock);

		enable_irq(vblkdev->ivck->irq);

		spin_lock_irqsave(&vblkdev->queue->queue_lock, flags);
		blk_mq_start_hw_queues(vblkdev->queue);
		spin_unlock_irqrestore(&vblkdev->queue->queue_lock, flags);

		queue_work_on(WORK_CPU_UNBOUND, vblkdev->wq, &vblkdev->work);
	}

	return 0;
}

static const struct dev_pm_ops tegra_hv_vblk_pm_ops = {
	.suspend = tegra_hv_vblk_suspend,
	.resume = tegra_hv_vblk_resume,
};
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_OF
static struct of_device_id tegra_hv_vblk_match[] = {
	{ .compatible = "nvidia,tegra-hv-storage", },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_hv_vblk_match);
#endif /* CONFIG_OF */

static struct platform_driver tegra_hv_vblk_driver = {
	.probe	= tegra_hv_vblk_probe,
	.remove	= tegra_hv_vblk_remove,
	.driver	= {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tegra_hv_vblk_match),
#ifdef CONFIG_PM_SLEEP
		.pm = &tegra_hv_vblk_pm_ops,
#endif
	},
};

static int __init tegra_hv_vblk_driver_init(void)
{
	vblk_major = 0;
	vblk_major = register_blkdev(vblk_major, "vblk");
	if (vblk_major <= 0) {
		pr_err("vblk: unable to get major number\n");
		return -ENODEV;
	}

	return platform_driver_register(&tegra_hv_vblk_driver);
}
module_init(tegra_hv_vblk_driver_init);

static void __exit tegra_hv_vblk_driver_exit(void)
{
	unregister_blkdev(vblk_major, "vblk");
	platform_driver_unregister(&tegra_hv_vblk_driver);
}
module_exit(tegra_hv_vblk_driver_exit);

MODULE_AUTHOR("Dilan Lee <dilee@nvidia.com>");
MODULE_DESCRIPTION("Virtual storage device over Tegra Hypervisor IVC channel");
MODULE_LICENSE("GPL v2");
