/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <nvidia/conftest.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/limits.h>
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
#include <linux/kthread.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
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
#include <linux/blkdev.h>
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
#include <linux/tegra-hsierrrptinj.h>
#endif
#include "tegra_vblk.h"

#if defined(NV_QUEUE_LIMITS_STRUCT_HAS_FEATURES) \
	&& (NV_IS_EXPORT_SYMBOL_PRESENT_queue_limits_set == 1)
#define NV_BLOCK_USE_QUEUE_LIMITS_SET
#endif

#define DISCARD_ERASE_SECERASE_MASK	(VS_BLK_DISCARD_OP_F | \
					VS_BLK_SECURE_ERASE_OP_F | \
					VS_BLK_ERASE_OP_F)

#define FFU_PASS_THROUGH_CMDS_GID  2089
#define REST_OF_PASS_THROUGH_CMDS_GID 2090
#define VBLK_DEV_BASE_PRIORITY		25U
#define VBLK_DEV_THREAD_NAME_LEN	25U

#define MPIDR_AFFLVL_MASK		0xFFULL
#define MPIDR_AFF1_SHIFT		8U
#define MPIDR_AFF2_SHIFT		16U
#define MAX_NUM_CLUSTERS		3U
#define CPUS_PER_CLUSTER		4U

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

static void print_erase_op_supported(struct device *dev, uint32_t ops)
{
	const char *op;
	if (ops & VS_BLK_DISCARD_OP_F) {
		op = "DISCARD";
	} else if (ops & VS_BLK_SECURE_ERASE_OP_F) {
		op = "SECURE_ERASE";
	} else if (ops & VS_BLK_ERASE_OP_F) {
		op = "ERASE";
	} else {
		op = "Unknown";
	}
	dev_info(dev, "UFS supports erase operation type - %s. So, DISCARD and  SECURE ERASE are mapped to %s",
			op, op);
}

/**
 * vblk_get_req: Get a handle to free vsc request.
 */
static struct vsc_request *vblk_get_req(struct vblk_dev *vblkdev)
{
	struct vsc_request *req = NULL;
	unsigned long bit;
	unsigned long timeout = 30*HZ;
	unsigned long new_jiffies;

	if (vblkdev->queue_state != VBLK_QUEUE_ACTIVE)
		goto exit;

	bit = find_first_zero_bit(vblkdev->pending_reqs, vblkdev->max_requests);
	if (bit < vblkdev->max_requests) {
		req = &vblkdev->reqs[bit];
		req->vs_req.req_id = bit;
		set_bit(bit, vblkdev->pending_reqs);
		vblkdev->inflight_reqs++;

		if (check_add_overflow(jiffies, timeout, &new_jiffies)) {
			/*
			 * with 64-bit jiffies, the timer will not overflow for a very long time.
			 * In case it does, Calculate remaining ticks after wraparound and set the timer
			 *   - ULONG_MAX - jiffies is the remaining ticks after wraparound
			 *   - -1 is to count the wraparound point as one tick.
			 */
			unsigned long remaining = timeout - (ULONG_MAX - jiffies) - 1;
			new_jiffies = remaining;
		}
		mod_timer(&req->timer, new_jiffies);
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
#if defined(NV_TIMER_DELETE_PRESENT) /* Linux v6.15 */
		timer_delete(&req->timer);
#else
		del_timer(&req->timer);
#endif
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
			schedule_timeout(usecs_to_jiffies(IVC_RESET_RETRY_WAIT_15USECS));
		}
	}
	vs_req = (struct vs_request *)
		tegra_hv_ivc_write_get_next_frame(vblkdev->ivck);
	if ((vs_req == NULL) || (IS_ERR(vs_req))) {
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
	uint64_t pos;

	/* Safely multiply using check_mul_overflow */
	if (check_mul_overflow(blk_rq_pos(breq), (uint64_t)SECTOR_SIZE, &pos)) {
		/* Handle overflow - use max possible value */
		pos = U64_MAX;
		dev_err(vblkdev->device, "Position calculation overflow!\n");
	}

	dev_err(vblkdev->device,
			"Error for request pos %llx type %llx size %x\n",
			pos,
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
				vsc_req->dma_direction);
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

static enum dma_data_direction get_dma_data_direction(struct request *bio_req)
{
	if (req_op(bio_req) == REQ_OP_READ)
		return DMA_FROM_DEVICE;
	else if (req_op(bio_req) == REQ_OP_WRITE)
		return DMA_TO_DEVICE;
	else
		return DMA_NONE;
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
#if defined(NV_BLK_RQ_MAP_SG_HAS_NO_QUEUE_ARG) /* Linux v6.15 */
		sg_cnt = blk_rq_map_sg(bio_req, vsc_req->sg_lst);
#else
		sg_cnt = blk_rq_map_sg(vblkdev->queue, bio_req,
				vsc_req->sg_lst);
#endif
		vsc_req->sg_num_ents = sg_nents(vsc_req->sg_lst);
		vsc_req->dma_direction = get_dma_data_direction(bio_req);
		if (dma_map_sg(vblkdev->device, vsc_req->sg_lst,
			vsc_req->sg_num_ents, vsc_req->dma_direction) == 0) {
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
			vblkdev->ioctl_req,
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

/**
 * @defgroup vscd_request_worker LinuxVSCD::Request Worker
 *
 * @ingroup vscd_request_worker
 * @{
 */

/**
 * @brief Worker thread that handles block device requests and completions
 *
 * This worker thread is responsible for:
 * 1. Processing completed block I/O requests from the virtual storage server
 * 2. Submitting new block I/O requests to the virtual storage server
 * 3. Managing the request queue and IVC communication
 *
 * The worker runs in a loop waiting for requests to be queued. When woken up, it:
 * - Acquires the IVC lock to synchronize access to the IVC channel
 * - Processes any completed requests from the server via complete_bio_req()
 * - Submits new pending requests to the server via submit_bio_req()
 * - Continues processing until no more requests are pending
 *
 * @param[in] data Pointer to the vblk_dev device structure
 * @param[out] None
 * @return 0 on success, negative errno on failure
 *
 * @pre
 * - vblk device must be initialized
 * - IVC channel must be established
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Async
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static int vblk_request_worker(void *data)
{
	struct vblk_dev *vblkdev = (struct vblk_dev *)data;
	bool req_submitted, req_completed;
	int ret;

	while (true) {
		ret = wait_for_completion_interruptible(&vblkdev->complete);
		if (ret < 0) {
			continue;
		}

		/* Taking ivc lock before performing IVC read/write */
		mutex_lock(&vblkdev->ivc_lock);
		req_submitted = true;
		req_completed = true;
		while (req_submitted || req_completed) {
			req_completed = complete_bio_req(vblkdev);

			req_submitted = submit_bio_req(vblkdev);
		}
		mutex_unlock(&vblkdev->ivc_lock);
	}

	return 0;
}

/**
 * @brief Block request handler callback for multi-queue block device
 *
 * This function is the main request handler for the virtual block device driver.
 * When the block layer submits I/O requests, this callback:
 * 1. Marks the request as started using blk_mq_start_request()
 * 2. Allocates a new request entry structure
 * 3. Adds the request to the device's pending request list
 * 4. Wakes up the worker thread to process the request
 *
 * The actual I/O processing is done asynchronously by the worker thread, which:
 * - Submits requests to the virtual storage server via IVC
 * - Handles completions and error cases
 * - Manages the request lifecycle
 *
 * @param[in] hctx Block multi-queue hardware context
 * @param[in] bd Block request data containing the request to process
 *
 * @return BLK_STS_OK on success, BLK_STS_IOERR on failure
 *
 * @pre
 * - Block device must be initialized
 * - Request queue must be setup
 * - Worker thread must be running
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Async
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static blk_status_t vblk_request(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	struct req_entry *entry;
	struct request *req = bd->rq;
	struct vblk_dev *vblkdev = hctx->queue->queuedata;

	blk_mq_start_request(req);

	/* malloc for req list entry */
	entry = kzalloc(sizeof(struct req_entry), GFP_ATOMIC);
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

	/* wakeup worker thread */
	complete(&vblkdev->complete);

	return BLK_STS_OK;
}
/** @} */

/* Open and release */
/**
 * @defgroup vscd_open_release LinuxVSCD::Open/Release
 *
 * @ingroup vscd_open_release
 * @{
 */

/**
 * @brief Opens a virtual block device for normal I/O operations
 *
 * This function is called when a block device is opened for I/O operations.
 * It increments the user count and checks for media changes if this is the first user.
 * The media change check ensures the device state is current before allowing access.
 *
 * @param[in] disk Pointer to the gendisk structure representing the block device
 * @param[in] mode File mode flags indicating how the device should be opened
 * @return 0 on success
 *
 * @pre
 * - Device must be initialized
 * - Disk structure must be valid
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

/**
 * @brief Opens a virtual block device for IOCTL operations
 *
 * This function is called when opening the IOCTL-specific device node.
 * It increments the IOCTL user count, checks for media changes if first user,
 * and initializes FFU passthrough command permissions to disabled state.
 *
 * @param[in] disk Pointer to the gendisk structure representing the block device
 * @param[in] mode File mode flags indicating how the device should be opened
 * @return 0 on success
 *
 * @pre
 * - Device must be initialized
 * - Disk structure must be valid
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
#if defined(NV_BLOCK_DEVICE_OPERATIONS_OPEN_HAS_GENDISK_ARG) /* Linux v6.5 */
static int vblk_ioctl_open(struct gendisk *disk, fmode_t mode)
{
	struct vblk_dev *vblkdev = disk->private_data;
#else
static int vblk_ioctl_open(struct block_device *device, fmode_t mode)
{
	struct vblk_dev *vblkdev = device->bd_disk->private_data;
#endif
	spin_lock(&vblkdev->lock);

	if (!vblkdev->ioctl_users) {
#if defined(NV_DISK_CHECK_MEDIA_CHANGE_PRESENT) /* Linux v6.5 */
		disk_check_media_change(disk);
#else
		bdev_check_media_change(device);
#endif
	}
	vblkdev->ioctl_users++;
	vblkdev->allow_ffu_passthrough_cmds = false;

	spin_unlock(&vblkdev->lock);
	return 0;
}

/**
 * @brief Opens a virtual block device for firmware update operations
 *
 * This function is called when opening the FFU-specific device node.
 * It increments the FFU user count, checks for media changes if first user,
 * and initializes FFU passthrough command permissions to disabled state.
 *
 * @param[in] disk Pointer to the gendisk structure representing the block device
 * @param[in] mode File mode flags indicating how the device should be opened
 * @return 0 on success
 *
 * @pre
 * - Device must be initialized
 * - Disk structure must be valid
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
#if defined(NV_BLOCK_DEVICE_OPERATIONS_OPEN_HAS_GENDISK_ARG)
static int vblk_ffu_open(struct gendisk *disk, fmode_t mode)
{
	struct vblk_dev *vblkdev = disk->private_data;
#else
static int vblk_ffu_open(struct block_device *device, fmode_t mode)
{
	struct vblk_dev *vblkdev = device->bd_disk->private_data;
#endif
	spin_lock(&vblkdev->lock);
	if (!vblkdev->ffu_users) {
#if defined(NV_DISK_CHECK_MEDIA_CHANGE_PRESENT)
		disk_check_media_change(disk);
#else
		bdev_check_media_change(device);
#endif
	}
	vblkdev->ffu_users++;
	vblkdev->allow_ffu_passthrough_cmds = false;
	spin_unlock(&vblkdev->lock);
	return 0;
}

/**
 * @brief Releases a virtual block device after IOCTL operations
 *
 * This function is called when closing the IOCTL-specific device node.
 * It safely decrements the IOCTL user count using overflow checking to prevent underflow.
 * The IOCTL interface remains available for other users if count is non-zero.
 *
 * @param[in] disk Pointer to the gendisk structure representing the block device
 * @param[in] mode File mode flags indicating how the device was opened
 *
 * @pre
 * - Device must be initialized
 * - Disk structure must be valid
 * - Device must have been previously opened for IOCTL operations
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
 *   - De-Init: Yes
 */
#if defined(NV_BLOCK_DEVICE_OPERATIONS_RELEASE_HAS_NO_MODE_ARG) /* Linux v6.5 */
static void vblk_ioctl_release(struct gendisk *disk)
#else
static void vblk_ioctl_release(struct gendisk *disk, fmode_t mode)
#endif
{
	struct vblk_dev *vblkdev = disk->private_data;
	short val = 1;

	spin_lock(&vblkdev->lock);

	/* Use check_sub_overflow to safely decrement */
	if (check_sub_overflow(vblkdev->ioctl_users, val, &vblkdev->ioctl_users)) {
		dev_warn(vblkdev->device, "ioctl_users counter underflow prevented\n");
	}

	spin_unlock(&vblkdev->lock);
}

/**
 * @brief Releases a virtual block device after firmware update operations
 *
 * This function is called when closing the FFU-specific device node.
 * It safely decrements the FFU user count using overflow checking to prevent underflow.
 * The FFU interface remains available for other users if count is non-zero.
 *
 * @param[in] disk Pointer to the gendisk structure representing the block device
 * @param[in] mode File mode flags indicating how the device was opened
 *
 * @pre
 * - Device must be initialized
 * - Disk structure must be valid
 * - Device must have been previously opened for FFU operations
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
 *   - De-Init: Yes
 */
#if defined(NV_BLOCK_DEVICE_OPERATIONS_RELEASE_HAS_NO_MODE_ARG)
static void vblk_ffu_release(struct gendisk *disk)
#else
static void vblk_ffu_release(struct gendisk *disk, fmode_t mode)
#endif
{
	struct vblk_dev *vblkdev = disk->private_data;
	short val = 1;

	spin_lock(&vblkdev->lock);
	if (check_sub_overflow(vblkdev->ffu_users, val, &vblkdev->ffu_users))
		dev_warn(vblkdev->device, "ffu_users counter underflow prevented\n");
	spin_unlock(&vblkdev->lock);
}

/**
 * @brief Releases a virtual block device after normal I/O operations
 *
 * This function is called when a block device is closed after I/O operations.
 * It safely decrements the user count using overflow checking to prevent underflow.
 * The device remains operational for other users if the count is non-zero.
 *
 * @param[in] disk Pointer to the gendisk structure representing the block device
 * @param[in] mode File mode flags indicating how the device was opened
 *
 * @pre
 * - Device must be initialized
 * - Disk structure must be valid
 * - Device must have been previously opened
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
 *   - De-Init: Yes
 */
#if defined(NV_BLOCK_DEVICE_OPERATIONS_RELEASE_HAS_NO_MODE_ARG) /* Linux v6.5 */
static void vblk_release(struct gendisk *disk)
#else
static void vblk_release(struct gendisk *disk, fmode_t mode)
#endif
{
	struct vblk_dev *vblkdev = disk->private_data;
	short val = 1;

	spin_lock(&vblkdev->lock);

	/* Use check_sub_overflow to safely decrement */
	if (check_sub_overflow(vblkdev->users, val, &vblkdev->users)) {
		dev_warn(vblkdev->device, "users counter underflow prevented\n");
	}

	spin_unlock(&vblkdev->lock);
}

/**
 * @brief Gets the geometry information for the virtual block device
 *
 * This function returns the logical geometry (heads, sectors, cylinders) for the block device.
 * It uses fixed values for heads and sectors, and calculates cylinders based on device capacity.
 * This information is used by legacy tools that expect disk geometry information.
 *
 * @param[in] device Pointer to the block device structure
 * @param[out] geo Pointer to hd_geometry structure to fill with geometry information
 * @return 0 on success
 *
 * @pre
 * - Device must be initialized
 * - Block device structure must be valid
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
static int vblk_getgeo(struct block_device *device, struct hd_geometry *geo)
{
	geo->heads = VS_LOG_HEADS;
	geo->sectors = VS_LOG_SECTS;
	geo->cylinders = get_capacity(device->bd_disk) /
		(geo->heads * geo->sectors);

	return 0;
}
/** @} */

/* The device operations structure. */
static const struct block_device_operations vblk_ops_no_ioctl = {
	.owner           = THIS_MODULE,
	.open            = vblk_open,
	.release         = vblk_release,
	.getgeo          = vblk_getgeo,
	.ioctl           = vblk_ioctl_not_supported
};

/* The device operations structure. */
static const struct block_device_operations vblk_ops_ioctl = {
	.owner           = THIS_MODULE,
	.open            = vblk_ioctl_open,
	.release         = vblk_ioctl_release,
	.getgeo          = vblk_getgeo,
	.ioctl           = vblk_ioctl
};

/* The device operations structure for FFU operations */
static const struct block_device_operations vblk_ops_ffu = {
	.owner           = THIS_MODULE,
	.open            = vblk_ffu_open,
	.release         = vblk_ffu_release,
	.getgeo          = vblk_getgeo,
	.ioctl           = vblk_ffu_ioctl
};

/**
 * @defgroup vscd_sysfs LinuxVSCD::Sysfs
 *
 * @ingroup vscd_sysfs
 * @{
 */

/**
 * @brief Shows the physical device type (EMMC/UFS) backing this virtual block device
 *
 * This function implements the sysfs show function for the "phys_dev" attribute.
 * It reads the physical device type from the vblk device configuration and returns
 * a string indicating whether the underlying storage device is EMMC or UFS.
 *
 * @param[in] dev Pointer to the device structure
 * @param[in] attr Pointer to the device attribute structure
 * @param[out] buf Buffer to store the attribute value string
 *
 * @return Number of bytes written to buffer on success, negative errno on failure
 *
 * @pre
 * - Device must be initialized
 * - Configuration must be valid
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
static ssize_t vblk_phys_dev_show(struct device *dev,
		struct device_attribute *attr,
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

/**
 * @brief Shows the physical base address of the storage device
 *
 * This function implements the sysfs show function for the "phys_base" attribute.
 * It reads the physical base address from the vblk device configuration and formats
 * it as a hexadecimal string. This represents the base address of the physical
 * storage device in memory.
 *
 * @param[in] dev Pointer to the device structure
 * @param[in] attr Pointer to the device attribute structure
 * @param[out] buf Buffer to store the attribute value string
 *
 * @return Number of bytes written to buffer on success, negative errno on failure
 *
 * @pre
 * - Device must be initialized
 * - Configuration must be valid
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
static ssize_t vblk_phys_base_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct vblk_dev *vblk = disk->private_data;

	return snprintf(buf, 16, "0x%llx\n", vblk->config.phys_base);
}

/**
 * @brief Shows the storage type/partition (RPMB, BOOT, LUNx) of this virtual block device
 *
 * This function implements the sysfs show function for the "storage_type" attribute.
 * It reads the storage type from the vblk device configuration and returns a string
 * indicating the type of storage partition this device represents (e.g. RPMB, BOOT,
 * or one of the logical unit numbers LUN0-LUN7).
 *
 * @param[in] dev Pointer to the device structure
 * @param[in] attr Pointer to the device attribute structure
 * @param[out] buf Buffer to store the attribute value string
 *
 * @return Number of bytes written to buffer on success, negative errno on failure
 *
 * @pre
 * - Device must be initialized
 * - Configuration must be valid
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
static ssize_t vblk_storage_type_show(struct device *dev,
		struct device_attribute *attr,
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

/**
 * @brief Shows the speed mode configuration of the storage device
 *
 * This function implements the sysfs show function for the "speed_mode" attribute.
 * It reads the speed mode string from the vblk device configuration and returns it.
 * The speed mode indicates the operating speed configuration of the underlying
 * physical storage device (e.g. HS400, HS200 for eMMC or HS-G3 for UFS).
 *
 * @param[in] dev Pointer to the device structure
 * @param[in] attr Pointer to the device attribute structure
 * @param[out] buf Buffer to store the attribute value string
 *
 * @return Number of bytes written to buffer on success, negative errno on failure
 *
 * @pre
 * - Device must be initialized
 * - Configuration must be valid
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
static ssize_t vblk_speed_mode_show(struct device *dev,
		struct device_attribute *attr,
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

/** @} */
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
			schedule_timeout(usecs_to_jiffies(IVC_RESET_RETRY_WAIT_15USECS));
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

static int setup_ffu_device(struct vblk_dev *vblkdev)
{
	int ret;

	memset(&vblkdev->ffu_tag_set, 0, sizeof(vblkdev->ffu_tag_set));
	vblkdev->ffu_tag_set.ops = &vblk_mq_ops;
	vblkdev->ffu_tag_set.nr_hw_queues = 1;
	vblkdev->ffu_tag_set.nr_maps = 1;
	vblkdev->ffu_tag_set.queue_depth = 16;
	vblkdev->ffu_tag_set.numa_node = NUMA_NO_NODE;
#if defined(NV_BLK_MQ_F_SHOULD_MERGE)
	vblkdev->ffu_tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
#endif
	ret = blk_mq_alloc_tag_set(&vblkdev->ffu_tag_set);
	if (ret) {
		dev_err(vblkdev->device, "failed to allocate FFU tag set\n");
		return ret;
	}

#if defined(NV_BLK_MQ_ALLOC_QUEUE_PRESENT)
	vblkdev->ffu_queue = blk_mq_alloc_queue(&vblkdev->ffu_tag_set, NULL, NULL);
#else
	vblkdev->ffu_queue = blk_mq_init_queue(&vblkdev->ffu_tag_set);
#endif
	if (IS_ERR(vblkdev->ffu_queue)) {
		dev_err(vblkdev->device, "failed to init FFU blk queue\n");
		blk_mq_free_tag_set(&vblkdev->ffu_tag_set);
		return PTR_ERR(vblkdev->ffu_queue);
	}

	vblkdev->ffu_queue->queuedata = vblkdev;

#if defined(NV_BLOCK_USE_QUEUE_LIMITS_SET)
	struct queue_limits limits;
	limits.physical_block_size = vblkdev->config.blk_config.hardblk_size;
	limits.logical_block_size = vblkdev->config.blk_config.hardblk_size;
	limits.io_min = vblkdev->config.blk_config.hardblk_size;
	limits.io_opt = vblkdev->config.blk_config.hardblk_size;

	if (queue_limits_set(vblkdev->ffu_queue, &limits) != 0) {
		dev_err(vblkdev->device, "failed to set FFU queue limits\n");
		ret = -EINVAL;
		goto cleanup_queue;
	}
#else
	blk_queue_logical_block_size(vblkdev->ffu_queue,
			vblkdev->config.blk_config.hardblk_size);
	blk_queue_physical_block_size(vblkdev->ffu_queue,
			vblkdev->config.blk_config.hardblk_size);
	blk_queue_flag_set(QUEUE_FLAG_NONROT, vblkdev->ffu_queue);
#endif

#if defined(NV_BLK_MQ_ALLOC_DISK_FOR_QUEUE_PRESENT)
	vblkdev->ffu_gd = blk_mq_alloc_disk_for_queue(vblkdev->ffu_queue, NULL);
#elif defined(NV___ALLOC_DISK_NODE_HAS_LKCLASS_ARG)
	vblkdev->ffu_gd = __alloc_disk_node(vblkdev->ffu_queue, NUMA_NO_NODE, NULL);
#else
	vblkdev->ffu_gd = __alloc_disk_node(VBLK_MINORS, NUMA_NO_NODE);
#endif
	if (!vblkdev->ffu_gd) {
		dev_err(vblkdev->device, "alloc_disk failure for FFU\n");
		ret = -ENOMEM;
		goto cleanup_queue;
	}

	vblkdev->ffu_gd->major = vblk_major;
	vblkdev->ffu_gd->first_minor = (vblkdev->devnum * VBLK_MINORS) + (VBLK_MINORS/4);
	vblkdev->ffu_gd->minors = VBLK_MINORS;
	vblkdev->ffu_gd->fops = &vblk_ops_ffu;
	vblkdev->ffu_gd->queue = vblkdev->ffu_queue;
	vblkdev->ffu_gd->private_data = vblkdev;
#if defined(GENHD_FL_EXT_DEVT)
	vblkdev->ffu_gd->flags |= GENHD_FL_EXT_DEVT;
#endif

	if (snprintf(vblkdev->ffu_gd->disk_name, 32, "vblkdev%d.ffu",
				vblkdev->devnum) < 0) {
		dev_err(vblkdev->device, "Error while updating disk_name for FFU!\n");
		ret = -EINVAL;
		goto cleanup_disk;
	}

#if defined(NV_DEVICE_ADD_DISK_HAS_INT_RETURN_TYPE)
	if (device_add_disk(vblkdev->device, vblkdev->ffu_gd, NULL)) {
		dev_err(vblkdev->device, "Error adding FFU disk!\n");
		ret = -EINVAL;
		goto cleanup_disk;
	}
#else
	device_add_disk(vblkdev->device, vblkdev->ffu_gd, NULL);
#endif

	return 0;

cleanup_disk:
	put_disk(vblkdev->ffu_gd);
cleanup_queue:
#if defined(NV_BLK_MQ_DESTROY_QUEUE_PRESENT) /* Linux v6.0 */
	blk_mq_destroy_queue(vblkdev->ffu_queue);
#else
	blk_cleanup_queue(vblkdev->ffu_queue);
#endif
	blk_mq_free_tag_set(&vblkdev->ffu_tag_set);
	return ret;
}

/* Set up ioctl virtual device. */
static int setup_ioctl_device(struct vblk_dev *vblkdev)
{
	int ret;

#if defined(NV_BLOCK_USE_QUEUE_LIMITS_SET)
	struct queue_limits limits;
#endif
	memset(&vblkdev->ioctl_tag_set, 0, sizeof(vblkdev->tag_set));
	vblkdev->ioctl_tag_set.ops = &vblk_mq_ops;
	vblkdev->ioctl_tag_set.nr_hw_queues = 1;
	vblkdev->ioctl_tag_set.nr_maps = 1;
	vblkdev->ioctl_tag_set.queue_depth = 16;
	vblkdev->ioctl_tag_set.numa_node = NUMA_NO_NODE;
#if defined(NV_BLK_MQ_F_SHOULD_MERGE) /* Linux 6.14 */
	vblkdev->ioctl_tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
#endif
	ret = blk_mq_alloc_tag_set(&vblkdev->ioctl_tag_set);
	if (ret) {
		dev_err(vblkdev->device, "failed to allocate tag set\n");
		return ret;
	}

#if defined(NV_BLK_MQ_ALLOC_QUEUE_PRESENT)
	vblkdev->ioctl_queue = blk_mq_alloc_queue(&vblkdev->ioctl_tag_set, NULL, NULL);
#else
	vblkdev->ioctl_queue = blk_mq_init_queue(&vblkdev->ioctl_tag_set);
#endif
	if (IS_ERR(vblkdev->ioctl_queue)) {
		dev_err(vblkdev->device, "failed to init ioctl blk queue\n");
		blk_mq_free_tag_set(&vblkdev->ioctl_tag_set);
		return PTR_ERR(vblkdev->ioctl_queue);
	}

	vblkdev->ioctl_queue->queuedata = vblkdev;

#if defined(NV_BLOCK_USE_QUEUE_LIMITS_SET)
	limits.physical_block_size = vblkdev->config.blk_config.hardblk_size;
	limits.logical_block_size = vblkdev->config.blk_config.hardblk_size;
	limits.io_min = vblkdev->config.blk_config.hardblk_size;
	limits.io_opt = vblkdev->config.blk_config.hardblk_size;

	if (queue_limits_set(vblkdev->ioctl_queue, &limits) != 0) {
		dev_err(vblkdev->device, "failed to set queue limits\n");
		ret = -EINVAL;
		goto cleanup_queue;
	}
#else
	blk_queue_logical_block_size(vblkdev->ioctl_queue,
		vblkdev->config.blk_config.hardblk_size);
	blk_queue_physical_block_size(vblkdev->ioctl_queue,
		vblkdev->config.blk_config.hardblk_size);
	blk_queue_flag_set(QUEUE_FLAG_NONROT, vblkdev->ioctl_queue);
#endif

	/* And the gendisk structure. */
#if defined(NV_BLK_MQ_ALLOC_DISK_FOR_QUEUE_PRESENT) /* Linux v6.0 */
	vblkdev->ioctl_gd = blk_mq_alloc_disk_for_queue(vblkdev->ioctl_queue, NULL);
#elif defined(NV___ALLOC_DISK_NODE_HAS_LKCLASS_ARG) /* Linux v5.15 */
	vblkdev->ioctl_gd = __alloc_disk_node(vblkdev->ioctl_queue, NUMA_NO_NODE, NULL);
#else
	vblkdev->ioctl_gd = __alloc_disk_node(VBLK_MINORS, NUMA_NO_NODE);
#endif
	if (!vblkdev->ioctl_gd) {
		dev_err(vblkdev->device, "alloc_disk failure forl ioctl\n");
		ret = -ENOMEM;
		goto cleanup_queue;
	}
	vblkdev->ioctl_gd->major = vblk_major;
	vblkdev->ioctl_gd->first_minor =
		(vblkdev->devnum * VBLK_MINORS) + (VBLK_MINORS/2);
	vblkdev->ioctl_gd->minors = VBLK_MINORS;
	vblkdev->ioctl_gd->fops = &vblk_ops_ioctl;
	vblkdev->ioctl_gd->queue = vblkdev->ioctl_queue;
	vblkdev->ioctl_gd->private_data = vblkdev;
#if defined(GENHD_FL_EXT_DEVT) /* Removed in Linux v5.17 */
	vblkdev->ioctl_gd->flags |= GENHD_FL_EXT_DEVT;
#endif

	if (snprintf(vblkdev->ioctl_gd->disk_name, 32, "vblkdev%d.ctl",
				vblkdev->devnum) < 0) {
		dev_err(vblkdev->device, "Error while updating disk_name for ioctl!\n");
		ret = -EINVAL;
		goto cleanup_disk;
	}

#if defined(NV_DEVICE_ADD_DISK_HAS_INT_RETURN_TYPE) /* Linux v5.15 */
	if (device_add_disk(vblkdev->device, vblkdev->ioctl_gd, NULL)) {
		dev_err(vblkdev->device, "Error adding ioctl disk!\n");
		ret = -EINVAL;
		goto cleanup_disk;
	}
#else
	device_add_disk(vblkdev->device, vblkdev->ioctl_gd, NULL);
#endif

	return 0;

cleanup_disk:
    put_disk(vblkdev->ioctl_gd);
cleanup_queue:
#if defined(NV_BLK_MQ_DESTROY_QUEUE_PRESENT) /* Linux v6.0 */
		blk_mq_destroy_queue(vblkdev->ioctl_queue);
#else
		blk_cleanup_queue(vblkdev->ioctl_queue);
#endif
    blk_mq_free_tag_set(&vblkdev->ioctl_tag_set);
    return ret;
}

/* Set up virtual device. */
static void setup_device(struct vblk_dev *vblkdev)
{

#if defined(NV_BLOCK_USE_QUEUE_LIMITS_SET)
	struct queue_limits limits;
#endif
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

	/* Calculate total size safely */
	if ((check_mul_overflow(vblkdev->config.blk_config.num_blks,
						  ((uint64_t)vblkdev->config.blk_config.hardblk_size),
						  &vblkdev->size)) == true) {
		dev_err(vblkdev->device, "Size calculation overflow!\n");
		return;  /* Fail device setup */
	}

	memset(&vblkdev->tag_set, 0, sizeof(vblkdev->tag_set));
	vblkdev->tag_set.ops = &vblk_mq_ops;
	vblkdev->tag_set.nr_hw_queues = 1;
	vblkdev->tag_set.nr_maps = 1;
	vblkdev->tag_set.queue_depth = 16;
	vblkdev->tag_set.numa_node = NUMA_NO_NODE;
#if defined(NV_BLK_MQ_F_SHOULD_MERGE) /* Linux 6.14 */
	vblkdev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
#endif
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

#if defined(NV_BLOCK_USE_QUEUE_LIMITS_SET)
	limits.physical_block_size = vblkdev->config.blk_config.hardblk_size;
	limits.logical_block_size = vblkdev->config.blk_config.hardblk_size;
	limits.io_min = vblkdev->config.blk_config.hardblk_size;
	limits.io_opt = vblkdev->config.blk_config.hardblk_size;
#else
	blk_queue_logical_block_size(vblkdev->queue,
		vblkdev->config.blk_config.hardblk_size);
	blk_queue_physical_block_size(vblkdev->queue,
		vblkdev->config.blk_config.hardblk_size);
#endif

	if (vblkdev->config.blk_config.req_ops_supported & VS_BLK_FLUSH_OP_F) {
#if defined(NV_BLOCK_USE_QUEUE_LIMITS_SET)
		limits.features |= (BLK_FEAT_WRITE_CACHE &
				!(BLK_FLAG_WRITE_CACHE_DISABLED) &
				!(BLK_FEAT_FUA));
#else
		blk_queue_write_cache(vblkdev->queue, true, false);
#endif
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

#if defined(NV_BLOCK_USE_QUEUE_LIMITS_SET) && defined(NV_BLK_QUEUE_MAX_HW_SECTORS_PRESENT)
	limits.max_hw_sectors = max_io_bytes / SECTOR_SIZE;
#elif defined(NV_BLK_QUEUE_MAX_HW_SECTORS_PRESENT) /* Removed in Linux v6.10 */
	blk_queue_max_hw_sectors(vblkdev->queue, max_io_bytes / SECTOR_SIZE);
#endif

	if ((vblkdev->config.blk_config.req_ops_supported & VS_BLK_SECURE_ERASE_OP_F)
	     || (vblkdev->config.blk_config.req_ops_supported & VS_BLK_ERASE_OP_F)) {
#if defined(QUEUE_FLAG_SECERASE) /* Removed in Linux 5.19 */
		/*
		 * FIXME: Support for Linux v5.19+ kernels
		 */
		blk_queue_flag_set(QUEUE_FLAG_SECERASE, vblkdev->queue);
#elif defined(NV_BLOCK_USE_QUEUE_LIMITS_SET)
		limits.max_secure_erase_sectors =
			(vblkdev->config.blk_config.max_erase_blks_per_io *
			 vblkdev->config.blk_config.hardblk_size) / SECTOR_SIZE;
#else
		blk_queue_max_secure_erase_sectors(vblkdev->queue,
			((vblkdev->config.blk_config.max_erase_blks_per_io *
			  vblkdev->config.blk_config.hardblk_size) / SECTOR_SIZE));
#endif
	}

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
#if defined(NV_BLOCK_USE_QUEUE_LIMITS_SET)
		/* H/W supports single contiguous range of discard. So set max_discard_segments to 1.
		 *
		 * Ex:
		 * For 4KB block size(vblkdev->config.blk_config.hardblk_size)
		 * and 64MB(vblkdev->config.blk_config.max_erase_blks_per_io) max discard:
		 * Block size = 4KB = 8 sectors
		 * max_discard_sectors = 131072 sectors (64MB)
		 * max_discard_segments = 1 (one contiguous range)
		 *
		 * Segment structure:
		 * [Block 0] [Block 1] ... [Block 16383]  = One segment
		 * |--8 sectors--| ... up to 131072 sectors total
		 */
		limits.max_discard_segments = 1;
		limits.max_discard_sectors =
			((vblkdev->config.blk_config.max_erase_blks_per_io *
			  vblkdev->config.blk_config.hardblk_size) / SECTOR_SIZE);
		limits.discard_granularity = vblkdev->config.blk_config.hardblk_size;
#else
		blk_queue_max_discard_sectors(vblkdev->queue,
			((vblkdev->config.blk_config.max_erase_blks_per_io *
			  vblkdev->config.blk_config.hardblk_size) / SECTOR_SIZE));
		vblkdev->queue->limits.discard_granularity =
			vblkdev->config.blk_config.hardblk_size;
#endif
		print_erase_op_supported(vblkdev->device,
				vblkdev->config.blk_config.req_ops_supported);

	}
#if defined(NV_BLOCK_USE_QUEUE_LIMITS_SET)
	if (queue_limits_set(vblkdev->queue, &limits) != 0) {
		dev_err(vblkdev->device, "failed to set queue limits\n");
		blk_mq_free_tag_set(&vblkdev->tag_set);
		return;
	}
#else
	 blk_queue_flag_set(QUEUE_FLAG_NONROT, vblkdev->queue);
#endif

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
		memset(vblkdev->shared_buffer, 0, ivmk->size);
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
		/* To accomodate 512KB + combo/sg header size in the
		 * mempool add 512 bytes additional for the each mempool
		 * slot.
		 */
		if ((vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F) &&
				(max_io_bytes < EMMC_IOCTL_MAX_SIZE)) {

			max_requests = ((vblkdev->ivmk->size) / EMMC_IOCTL_MAX_SIZE);
		} else {
			max_requests = ((vblkdev->ivmk->size) / max_io_bytes);
		}
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
			if ((vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F) &&
					(max_io_bytes < EMMC_IOCTL_MAX_SIZE)) {
				req->mempool_virt = (void *)((uintptr_t)vblkdev->shared_buffer +
						(uintptr_t)(req_id * EMMC_IOCTL_MAX_SIZE));
				req->mempool_offset = (req_id * EMMC_IOCTL_MAX_SIZE);
				req->mempool_len = EMMC_IOCTL_MAX_SIZE;
			} else {
				req->mempool_virt = (void *)((uintptr_t)vblkdev->shared_buffer +
						(uintptr_t)(req_id * max_io_bytes));
				req->mempool_offset = (req_id * max_io_bytes);
				req->mempool_len = max_io_bytes;
			}
		} else {
			if (vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F) {
				req->mempool_virt = (void *)((uintptr_t)vblkdev->shared_buffer +
				(uintptr_t)((req_id % max_ioctl_requests) * UFS_IOCTL_MAX_SIZE_SUPPORTED));
				req->mempool_offset = (req_id % max_ioctl_requests) * UFS_IOCTL_MAX_SIZE_SUPPORTED;
				req->mempool_len = UFS_IOCTL_MAX_SIZE_SUPPORTED;
			}
		}
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
	vblkdev->gd->fops = &vblk_ops_no_ioctl;
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

	dev_info(vblkdev->device, "device name is %s\n", vblkdev->gd->disk_name);
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
		/* Check for overflow before incrementing */
		if ((check_add_overflow(total_instance_id, 1U, &total_instance_id)) == true) {
			dev_err(vblkdev->device, "Instance ID counter overflow!\n");
			return;  /* Fail device setup */
		}
		vblkdev->instance_id = total_instance_id;
	}

	if (vblkdev->epl_id == IP_SDMMC) {
		/* Register error reporting callback */
		err = hsierrrpt_reg_cb(vblkdev->epl_id, vblkdev->instance_id,
							vblk_inject_err_fsi, vblkdev);
		if (err != 0)
			dev_info(vblkdev->device, "Err inj callback registration failed: %d", err);
	}
#endif

	if (vblkdev->config.blk_config.req_ops_supported & VS_BLK_IOCTL_OP_F) {
		ret = setup_ioctl_device(vblkdev);
		if (ret) {
			dev_err(vblkdev->device, "Failed to setup ioctl device\n");
			return;
		}
		ret = setup_ffu_device(vblkdev);
		if (ret) {
			dev_err(vblkdev->device, "Failed to setup ffu device\n");
			return;
		}
	}
}

static void vblk_init_device(struct work_struct *ws)
{
	struct vblk_dev *vblkdev = container_of(ws, struct vblk_dev, init);
	struct sched_attr attr = {0};
	char vblk_comm[VBLK_DEV_THREAD_NAME_LEN];
	int ret = 0;
	size_t remaining_space;

	mutex_lock(&vblkdev->ivc_lock);
	if (tegra_hv_ivc_can_read(vblkdev->ivck) && !vblkdev->initialized) {
		if (vblk_get_configinfo(vblkdev)) {
			mutex_unlock(&vblkdev->ivc_lock);
			return;
		}

		ret = snprintf(vblk_comm, VBLK_DEV_THREAD_NAME_LEN - 4, "vblkdev%d:%d",
							vblkdev->devnum, vblkdev->config.priority);
		if (ret < 0) {
			dev_err(vblkdev->device, "snprint API failed\n");
			mutex_unlock(&vblkdev->ivc_lock);
			return;
		}

		if (vblkdev->schedulable_vcpu_number != num_possible_cpus()) {

			/* Safely calculate remaining buffer space */
			if (check_sub_overflow((size_t)VBLK_DEV_THREAD_NAME_LEN,
						strlen(vblk_comm), &remaining_space)) {
				dev_err(vblkdev->device,
						"String length calculation overflow\n");
				mutex_unlock(&vblkdev->ivc_lock);
				return;
			}

			/* Safely append to string using snprintf */
			ret = snprintf(vblk_comm + strlen(vblk_comm),
					VBLK_DEV_THREAD_NAME_LEN - strlen(vblk_comm),
					":%u", vblkdev->schedulable_vcpu_number);
			if (ret < 0) {
				dev_err(vblkdev->device, "String append failed\n");
				mutex_unlock(&vblkdev->ivc_lock);
				return;
			}

			/* create partition specific worker thread */
			vblkdev->vblk_kthread = kthread_create_on_cpu(&vblk_request_worker, vblkdev,
						vblkdev->schedulable_vcpu_number, vblk_comm);
		} else {
			/* create partition specific worker thread.
			 * If the conversion is not successful
			 * do not bound kthread to any cpu
			 */
			dev_info(vblkdev->device, "vsc kthread not bound to any cpu\n");
			vblkdev->vblk_kthread = kthread_create(&vblk_request_worker, vblkdev, vblk_comm);
		}
		if (IS_ERR(vblkdev->vblk_kthread)) {
			dev_err(vblkdev->device, "Cannot allocate vblk worker thread\n");
			mutex_unlock(&vblkdev->ivc_lock);
			return;
		}

		/* set thread priority */
		attr.sched_policy = SCHED_RR;
		/*
		 * FIXME: Need to review the priority level set currently <25.
		 */
		attr.sched_priority = VBLK_DEV_BASE_PRIORITY - vblkdev->config.priority;
		WARN_ON_ONCE(sched_setattr_nocheck(vblkdev->vblk_kthread, &attr) != 0);
		init_completion(&vblkdev->complete);
		wake_up_process(vblkdev->vblk_kthread);

		vblkdev->initialized = true;
		mutex_unlock(&vblkdev->ivc_lock);

		setup_device(vblkdev);
		return;
	}
	mutex_unlock(&vblkdev->ivc_lock);
}

/**
 * @defgroup vscd_irq_timer LinuxVSCD::IRQ/Timer
 *
 * @ingroup vscd_irq_timer
 * @{
 */

/**
 * @brief Interrupt handler for IVC (Inter-VM Communication) events
 *
 * This function handles interrupts from the IVC channel for the virtual block device.
 * It performs two key tasks depending on device initialization state:
 *
 * 1. For initialized devices:
 *    - Wakes up the worker thread by completing the worker completion
 *    - Worker thread then processes any pending IVC messages/requests
 *
 * 2. For uninitialized devices:
 *    - Schedules initialization work on appropriate CPU
 *    - CPU selection based on schedulable_vcpu_number if specified, else uses default CPU
 *    - Initialization includes setting up device config, queues and other resources
 *
 * The handler ensures proper synchronization between interrupt context and worker thread
 * for processing IVC messages.
 *
 * @param[in] irq Interrupt number
 * @param[in] data Pointer to the virtual block device structure (struct vblk_dev)
 * @return IRQ_HANDLED to indicate interrupt was processed
 *
 * @pre
 * - IVC channel must be configured
 * - Device structure must be allocated and basic init done
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: Yes
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Async
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static irqreturn_t ivc_irq_handler(int irq, void *data)
{
	struct vblk_dev *vblkdev = (struct vblk_dev *)data;
	if (vblkdev->initialized) {
		/* wakeup worker thread */
		complete(&vblkdev->complete);
	} else {

		int vcpu = (vblkdev->schedulable_vcpu_number != num_possible_cpus()) ?
				vblkdev->schedulable_vcpu_number : DEFAULT_INIT_VCPU;
		schedule_work_on(vcpu, &vblkdev->init);
	}
	return IRQ_HANDLED;
}

/**
 * @brief Timer callback function to handle block I/O request timeouts
 *
 * This function is called when a block I/O request timer expires, indicating that
 * the request has taken longer than expected to complete. It handles timeout
 * conditions by:
 * 1. Converting the timer structure back to the request structure using container_of
 * 2. Logging an error message with:
 *    - The request ID that timed out
 *    - Current system counter value
 *    - Original request schedule time
 * This helps identify stuck or slow I/O requests for debugging purposes.
 *
 * @param[in] timer Pointer to the timer_list structure that expired
 *
 * @pre
 * - Timer must be initialized and started for a valid vsc_request
 * - vblkdev device structure must be valid
 * - Request must be in-flight when timer expires
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: Yes
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Async
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static void bio_request_timeout_callback(struct timer_list *timer)
{
#if defined(timer_container_of) /* Linux v6.16 */
	struct vsc_request *req = timer_container_of(req, timer, timer);
#else
	struct vsc_request *req = from_timer(req, timer, timer);
#endif

	dev_err(req->vblkdev->device, "Request id %d timed out. curr ctr: %llu sched ctr: %llu\n",
						req->id, _arch_counter_get_cntvct(), req->time);

}

/** @} */

static void vblk_request_config(struct work_struct *ws)
{
	struct vblk_dev *vblkdev = container_of(ws, struct vblk_dev, rq_cfg);

	vblkdev->ivck = tegra_hv_ivc_reserve(NULL, vblkdev->ivc_id, NULL);
	if (IS_ERR_OR_NULL(vblkdev->ivck)) {
		dev_err(vblkdev->device, "Failed to reserve IVC channel %d\n",
			vblkdev->ivc_id);
		vblkdev->ivck = NULL;
		return;
	}

	if (devm_request_irq(vblkdev->device, vblkdev->ivck->irq,
		ivc_irq_handler, 0, "vblk", vblkdev)) {
		dev_err(vblkdev->device, "Failed to request irq %d\n", vblkdev->ivck->irq);
		goto free_ivc;
	}

	tegra_hv_ivc_channel_reset(vblkdev->ivck);

	mutex_lock(&vblkdev->ivc_lock);
	if (vblk_send_config_cmd(vblkdev)) {
		dev_err(vblkdev->device, "Failed to send config cmd\n");
		mutex_unlock(&vblkdev->ivc_lock);
		goto free_ivc;
	}
	mutex_unlock(&vblkdev->ivc_lock);

	return;

free_ivc:
	tegra_hv_ivc_unreserve(vblkdev->ivck);
}

static void tegra_create_timers(struct vblk_dev *vblkdev)
{
	uint32_t i;

	for (i = 0; i < MAX_VSC_REQS; i++)
		timer_setup(&vblkdev->reqs[i].timer, bio_request_timeout_callback, 0);

}

/**
 * @defgroup vscd_probe_remove LinuxVSCD::Probe/Remove
 *
 * @ingroup vscd_probe_remove
 * @{
 */

/**
 * @brief Probe function to initialize and setup virtual block device driver
 *
 * This function initializes a virtual block device that provides virtualized storage access
 * in a hypervisor environment. It performs the following key steps:
 * 1. Allocates and initializes the virtual block device structure
 * 2. Sets up IVC (Inter-VM Communication) channel for storage commands
 * 3. Configures the block device parameters like size, operations etc.
 * 4. Creates block device nodes:
 *    - Main block device for regular I/O
 *    - IOCTL device node for control operations
 *    - FFU (Firmware Field Update) device node
 * 5. Initializes request queues and work queues
 * 6. Sets up device attributes
 *
 * @param[in] dev Platform device pointer from device tree
 * @return 0 on success, negative errno on failure
 *
 * @pre Platform must be running in virtualized environment with hypervisor
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
static int tegra_hv_vblk_probe(struct platform_device *pdev)
{
	static struct device_node *vblk_node;
	struct device_node *schedulable_vcpu_number_node;
	struct vblk_dev *vblkdev;
	struct device *dev = &pdev->dev;
	uint8_t is_cpu_bound = true;
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
	schedulable_vcpu_number_node = of_find_node_by_name(NULL, "virt-storage-request-submit-cpu-mapping");
	/* read lcpu_affinity from dts */
	if (schedulable_vcpu_number_node == NULL) {
		dev_err(dev, "%s: virt-storage-request-submit-cpu-mapping DT not found\n",
				__func__);
		is_cpu_bound = false;
	} else if (of_property_read_u32(schedulable_vcpu_number_node, "lcpu2tovcpu", &(vblkdev->schedulable_vcpu_number)) != 0) {
		dev_err(dev, "%s: lcpu2tovcpu affinity is not found\n", __func__);
		is_cpu_bound = false;
	}
	if (vblkdev->schedulable_vcpu_number >= num_online_cpus()) {
		dev_err(dev, "%s: cpu affinity (%d) > online cpus (%d)\n", __func__, vblkdev->schedulable_vcpu_number, num_online_cpus());
		is_cpu_bound = false;
	}
	if (false == is_cpu_bound) {
		dev_err(dev, "%s: WARN: CPU is unbound\n", __func__);
		vblkdev->schedulable_vcpu_number = num_possible_cpus();
	}

	vblkdev->initialized = false;

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
	INIT_WORK(&vblkdev->rq_cfg, vblk_request_config);

	/* creating and initializing the an internal request list */
	INIT_LIST_HEAD(&vblkdev->req_list);

	/* Create timers for each request going to storage server*/
	tegra_create_timers(vblkdev);
	schedule_work_on(((is_cpu_bound == false) ? DEFAULT_INIT_VCPU : vblkdev->schedulable_vcpu_number),
			&vblkdev->rq_cfg);

	return 0;
fail:
	return ret;
}

/**
 * @brief Remove function to cleanup and remove virtual block device driver
 *
 * This function performs cleanup when the virtual block device is removed.
 * Key cleanup steps include:
 * 1. Waits for pending requests to complete
 * 2. Removes block device nodes (main, ioctl and ffu)
 * 3. Cleans up request queues
 * 4. Frees IVC channels and shared memory
 * 5. Frees device structures and resources
 *
 * @param[in] dev Platform device pointer for the device being removed
 * @return 0 on success, negative errno on failure
 *
 * @pre Device must have been previously probed successfully
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: No
 *   - De-Init: Yes
 */
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
	if (vblkdev->ioctl_queue)
#if defined(NV_BLK_MQ_DESTROY_QUEUE_PRESENT) /* Linux v6.0 */
		blk_mq_destroy_queue(vblkdev->ioctl_queue);
#else
		blk_cleanup_queue(vblkdev->ioctl_queue);
#endif

	if (vblkdev->ffu_queue)
#if defined(NV_BLK_MQ_DESTROY_QUEUE_PRESENT) /* Linux v6.0 */
		blk_mq_destroy_queue(vblkdev->ffu_queue);
#else
		blk_cleanup_queue(vblkdev->ffu_queue);
#endif

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

/** @} */

/**
 * @defgroup vscd_suspend_resume LinuxVSCD::Suspend/Resume
 *
 * @ingroup vscd_suspend_resume
 * @{
 */

#ifdef CONFIG_PM_SLEEP
/**
 * @brief Suspends the virtual block device driver
 *
 * This function handles the suspend operation for the virtual block device by:
 * 1. Stopping hardware request queues for both regular I/O and IOCTL operations
 * 2. Setting queue state to suspended
 * 3. Waiting for any inflight requests to complete
 * 4. Disabling IVC interrupts once queue is empty
 *
 * The function ensures clean suspension by:
 * - Using spinlocks to safely stop queues
 * - Tracking inflight requests via completion mechanism
 * - Properly handling both regular and IOCTL queues
 * - Disabling interrupts only after all requests complete
 *
 * @param[in] dev Pointer to device structure
 * @return 0 on success, negative errno on failure
 *
 * @pre
 * - Device must be initialized
 * - Driver must be in active state
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
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
	}

	if (vblkdev->ioctl_queue) {
		spin_lock_irqsave(&vblkdev->ioctl_queue->queue_lock, flags);
		blk_mq_stop_hw_queues(vblkdev->ioctl_queue);
		spin_unlock_irqrestore(&vblkdev->ioctl_queue->queue_lock, flags);
	}

	return 0;
}

/**
 * @brief Resumes the virtual block device driver
 *
 * This function handles the resume operation for the virtual block device by:
 * 1. Setting queue state back to active
 * 2. Reinitializing completion tracking
 * 3. Re-enabling IVC interrupts
 * 4. Restarting hardware request queues for both regular I/O and IOCTL operations
 * 5. Waking up worker thread to process any pending requests
 *
 * The function ensures clean resume by:
 * - Using spinlocks to safely restart queues
 * - Properly handling both regular and IOCTL queues
 * - Re-enabling interrupts before processing requests
 * - Signaling worker thread to check for pending work
 *
 * @param[in] dev Pointer to device structure
 * @return 0 on success, negative errno on failure
 *
 * @pre
 * - Device must be initialized
 * - Driver must be in suspended state
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
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

		/* wakeup worker thread */
		complete(&vblkdev->complete);
	}

	if (vblkdev->ioctl_queue) {
		spin_lock_irqsave(&vblkdev->ioctl_queue->queue_lock, flags);
		blk_mq_start_hw_queues(vblkdev->ioctl_queue);
		spin_unlock_irqrestore(&vblkdev->ioctl_queue->queue_lock, flags);
	}

	return 0;
}

/** @} */

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

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_hv_vblk_remove_wrapper(struct platform_device *pdev)
{
	tegra_hv_vblk_remove(pdev);
}
#else
static int tegra_hv_vblk_remove_wrapper(struct platform_device *pdev)
{
	return tegra_hv_vblk_remove(pdev);
}
#endif

static struct platform_driver tegra_hv_vblk_driver = {
	.probe	= tegra_hv_vblk_probe,
	.remove	= tegra_hv_vblk_remove_wrapper,
	.driver	= {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tegra_hv_vblk_match),
#ifdef CONFIG_PM_SLEEP
		.pm = &tegra_hv_vblk_pm_ops,
#endif
	},
};

/**
 * @defgroup vscd_module_init_exit LinuxVSCD::Module Init/Exit
 *
 * @ingroup vscd_module_init_exit
 * @{
 */

/**
 * @brief Initialize the Tegra Hypervisor Virtual Block Device driver
 *
 * This function initializes the virtual block device driver that enables storage
 * virtualization in Tegra Hypervisor environments. It performs the following:
 * - Registers the block device driver with the kernel
 * - Allocates major number for block devices
 * - Initializes the virtual block device framework
 * - Sets up IVC (Inter-VM Communication) channels for storage operations
 * - Creates sysfs entries for device attributes
 *
 * @return 0 on success, negative errno on failure
 *
 * @pre None
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
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

/**
 * @brief Cleanup and remove the Tegra Hypervisor Virtual Block Device driver
 *
 * This function performs cleanup when the virtual block device driver is removed.
 * It handles:
 * - Unregistering the block device driver
 * - Freeing allocated major number
 * - Cleaning up IVC channels
 * - Removing sysfs entries
 * - Freeing allocated resources
 *
 * @return None
 *
 * @pre Driver must have been initialized via tegra_hv_vblk_driver_init()
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: No
 *   - De-Init: Yes
 */
static void __exit tegra_hv_vblk_driver_exit(void)
{
	unregister_blkdev(vblk_major, "vblk");
	platform_driver_unregister(&tegra_hv_vblk_driver);
}
module_exit(tegra_hv_vblk_driver_exit);
/**
 * @}
 */

MODULE_AUTHOR("Dilan Lee <dilee@nvidia.com>");
MODULE_DESCRIPTION("Virtual storage device over Tegra Hypervisor IVC channel");
MODULE_LICENSE("GPL v2");
