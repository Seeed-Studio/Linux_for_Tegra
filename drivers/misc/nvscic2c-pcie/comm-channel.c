// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#define pr_fmt(fmt)	"nvscic2c-pcie: comm-channel: " fmt

#include <nvidia/conftest.h>

#include <linux/atomic.h>
#include <linux/dma-fence.h>
#include <linux/errno.h>
#include <linux/host1x-next.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <uapi/misc/nvscic2c-pcie-ioctl.h>

#include "comm-channel.h"
#include "common.h"
#include "module.h"
#include "pci-client.h"

#define CACHE_ALIGN		(64)

/* Fifo size */
/*
 * This is wrong, but to have private communication channel functional at the
 * earliest, we allocate large set of frames assuming all the available
 * endpoints can share all possible export descriptors without having to block
 * and wait for channel to become writeable.
 *
 * Despite this huge fifo size, if msg cannot be send, it either means remote
 * is busy processing them quite slow (unlikely) or ill. In such a case, we
 * shall return -EAGAIN for application to retry and application can bail-out
 * after few retries.
 */
#define COMM_CHANNEL_NFRAMES	(1024)
#define COMM_CHANNEL_FRAME_SZ	(64)

/* fifo header.*/
struct header {
	u32 wr_count;
	u32 rd_count;
	u8 pad[CACHE_ALIGN - sizeof(u32) - sizeof(u32)];
} __packed;

/* kthread. */
struct task_t {
	struct task_struct *task;
	wait_queue_head_t waitq;
	struct completion shutdown_compl;
	bool shutdown;
	bool created;
};

/* Notification handling. */
struct syncpt_t {
	u32 id;
	u32 threshold;
	struct host1x_syncpt *sp;

	/* PCIe aperture for writes to peer syncpoint for same comm-channel. */
	struct pci_aper_t peer_mem;

	/* syncpoint physical address for stitching to PCIe BAR backing.*/
	size_t size;
	phys_addr_t phy_addr;

	/* iova mapping of client choice.*/
	void *iova_block_h;
	u64 iova;
	bool mapped_iova;

	/* Lock to protect fences between callback and deinit. */
	struct mutex lock;
	/* Fence to specific Threshold. */
	struct dma_fence *fence;
	struct dma_fence_cb fence_cb;
	/* Work to notify and allocate new fence. */
	struct work_struct work;
	void (*notifier)(void *data);
	void *notifier_data;
	bool fence_release;
};

struct fifo_t {
	/* slot/frames for the comm-channel.*/
	u32 nframes;
	u32 frame_sz;

	/* fifo operations.*/
	struct header *send_hdr;
	struct header *recv_hdr;
	struct header *local_hdr;
	u8 *send;
	u8 *recv;
	u32 wr_pos;
	u32 rd_pos;
	/* serialize send operations.*/
	struct mutex send_lock;

	/* fifo physical pages and stitched to iova of client choice(recv).*/
	struct cpu_buff_t self_mem;
	void *iova_block_h;
	u64 iova;
	bool mapped_iova;

	/* PCIe aperture for writes to peer comm fifo. */
	struct pci_aper_t peer_mem;
};

struct comm_channel_ctx_t {
	/* data. */
	struct fifo_t fifo;

	/* Notification. */
	struct syncpt_t syncpt;

	/* receive message task.*/
	struct task_t r_task;
	atomic_t recv_count;

	/* Callbacks registered for recv messages. */
	struct mutex cb_ops_lock;
	struct callback_ops cb_ops[COMM_MSG_TYPE_MAXIMUM];

	/* pci client handle.*/
	void *pci_client_h;

	/* nvscic2c-pcie DT node reference, used in getting syncpoint shim. */
	struct device_node *of_node;
	struct platform_device *host1x_pdev;
};

static inline bool
can_send(struct fifo_t *fifo, int *ret)
{
	bool send = false;
	u32 peer_toread =
		(fifo->local_hdr->wr_count - fifo->recv_hdr->rd_count);

	if (peer_toread < fifo->nframes) {
		/* space available - can send.*/
		send = true;
		*ret = 0;
	} else if (peer_toread == fifo->nframes) {
		/* full. client can try again (at the moment.)*/
		send = false;
		*ret = -EAGAIN;	// -ENOMEM;
	} else if (peer_toread > fifo->nframes) {
		/* erroneous.*/
		send = false;
		*ret = -EOVERFLOW;
	}

	return send;
}

static inline bool
can_recv(struct fifo_t *fifo, int *ret)
{
	bool recv = false;
	u32 toread = (fifo->recv_hdr->wr_count - fifo->local_hdr->rd_count);

	if (toread == 0) {
		/* no frame available to read.*/
		recv = false;
		*ret = -ENODATA;
	} else if (toread <= fifo->nframes) {
		/* frames available - can read.*/
		recv = true;
		*ret = 0;
	} else if (toread > fifo->nframes) {
		/* erroneous.*/
		recv = false;
		*ret = -EOVERFLOW;
	}

	return recv;
}

static int
send_msg(struct comm_channel_ctx_t *comm_ctx, struct comm_msg *msg)
{
	int ret = 0;
	size_t size = 0;
	void *from = NULL;
	void __iomem *to = NULL;
	struct fifo_t *fifo = NULL;
	struct syncpt_t *syncpt = NULL;
	enum peer_cpu_t peer_cpu = NVCPU_ORIN;

	fifo = &comm_ctx->fifo;
	syncpt = &comm_ctx->syncpt;

	peer_cpu = pci_client_get_peer_cpu(comm_ctx->pci_client_h);
	mutex_lock(&fifo->send_lock);

	/* if no space available, at the moment, client can try again. */
	if (!can_send(fifo, &ret)) {
		mutex_unlock(&fifo->send_lock);
		return ret;
	}

	to = (void __iomem *)(fifo->send + (fifo->wr_pos * fifo->frame_sz));
	from = (void *)(msg);
	size = sizeof(*msg);
	memcpy_toio(to, from, size);

	fifo->local_hdr->wr_count++;
	writel(fifo->local_hdr->wr_count,
	       (void __iomem *)(&fifo->send_hdr->wr_count));

	if (peer_cpu == NVCPU_X86_64) {
	/* comm-channel irq verctor always take from index 0 */
#if defined(PCI_EPC_IRQ_TYPE_ENUM_PRESENT) /* Dropped from Linux 6.8 */
		ret = pci_client_raise_irq(comm_ctx->pci_client_h, PCI_EPC_IRQ_MSI, 0);
#else
		ret = pci_client_raise_irq(comm_ctx->pci_client_h, PCI_IRQ_MSI, 0);
#endif
	} else {
	/* notify peer for each write.*/
		writel(0x1, syncpt->peer_mem.pva);
	}

	fifo->wr_pos = fifo->wr_pos + 1;
	if (fifo->wr_pos >= fifo->nframes)
		fifo->wr_pos = 0;

	mutex_unlock(&fifo->send_lock);

	return ret;
}

int
comm_channel_ctrl_msg_send(void *comm_channel_h, struct comm_msg *msg)
{
	struct comm_channel_ctx_t *comm_ctx =
				(struct comm_channel_ctx_t *)comm_channel_h;

	if (WARN_ON(!comm_ctx || !msg))
		return -EINVAL;

	if (WARN_ON(msg->type != COMM_MSG_TYPE_BOOTSTRAP &&
		    msg->type != COMM_MSG_TYPE_EDMA_RX_DESC_IOVA_RETURN &&
		    msg->type != COMM_MSG_TYPE_SHUTDOWN &&
		    msg->type != COMM_MSG_TYPE_LINK))
		return -EINVAL;

	/*
	 * this is a special ctrl message where the sender: @DRV_MODE_EPC
	 * or @DRV_MODE_EPF shares control information with peer. We do not
	 * check for nvscic2c-pcie link status as these messages can flow
	 * even when nvscic2c-pcie link status is down but possibly with
	 * PCIE physical link between PCIe RP SoC and PCIe EP SoC alive.
	 */

	return send_msg(comm_ctx, msg);
}

int
comm_channel_msg_send(void *comm_channel_h, struct comm_msg *msg)
{
	enum nvscic2c_pcie_link link = NVSCIC2C_PCIE_LINK_DOWN;
	struct comm_channel_ctx_t *comm_ctx =
				(struct comm_channel_ctx_t *)comm_channel_h;

	if (WARN_ON(!comm_ctx || !msg))
		return -EINVAL;

	if (WARN_ON(msg->type != COMM_MSG_TYPE_REGISTER &&
		    msg->type != COMM_MSG_TYPE_UNREGISTER))
		return -EINVAL;

	link = pci_client_query_link_status(comm_ctx->pci_client_h);

	if (link != NVSCIC2C_PCIE_LINK_UP)
		return -ENOLINK;

	return send_msg(comm_ctx, msg);
}

static int
recv_taskfn(void *arg)
{
	int ret = 0;
	struct comm_channel_ctx_t *comm_ctx = NULL;
	struct comm_msg *msg = NULL;
	struct task_t *task = NULL;
	struct fifo_t *fifo = NULL;
	struct callback_ops *cb_ops = NULL;

	comm_ctx = (struct comm_channel_ctx_t *)(arg);
	task = &comm_ctx->r_task;
	fifo = &comm_ctx->fifo;

	while (!task->shutdown) {
		/* wait for notification from peer or shutdown. */
		wait_event_interruptible(task->waitq,
					 (atomic_read(&comm_ctx->recv_count) ||
					  task->shutdown));
		/* task is exiting.*/
		if (task->shutdown)
			continue;

		/* read all on single notify.*/
		atomic_dec(&comm_ctx->recv_count);
		while (can_recv(fifo, &ret)) {
			msg = (struct comm_msg *)
				(fifo->recv + (fifo->rd_pos * fifo->frame_sz));

			if (msg->type > COMM_MSG_TYPE_INVALID &&
			    msg->type < COMM_MSG_TYPE_MAXIMUM) {
				mutex_lock(&comm_ctx->cb_ops_lock);
				cb_ops = &comm_ctx->cb_ops[msg->type];

				if (cb_ops->callback)
					cb_ops->callback
						((void *)msg, cb_ops->ctx);
				mutex_unlock(&comm_ctx->cb_ops_lock);
			}

			fifo->local_hdr->rd_count++;

			writel(fifo->local_hdr->rd_count,
			       (void __iomem *)(&fifo->send_hdr->rd_count));

			/* do not noifty peer for space availability. */

			fifo->rd_pos = fifo->rd_pos + 1;
			if (fifo->rd_pos >= fifo->nframes)
				fifo->rd_pos = 0;
		}

		/* if nothing (left) to read, go back waiting. */
		continue;
	}

	/* we do not use kthread_stop(), but wait on this.*/
	complete(&task->shutdown_compl);
	return 0;
}

/* Shall be invoked on expiry of syncpoint shim fence/trigger from remote. */
static void
syncpt_callback(void *data)
{
	struct comm_channel_ctx_t *comm_ctx = NULL;

	if (WARN_ON(!data))
		return;

	comm_ctx = (struct comm_channel_ctx_t *)(data);

	/* kick r_task for processing this notification.*/
	atomic_inc(&comm_ctx->recv_count);
	wake_up_interruptible_all(&comm_ctx->r_task.waitq);
}

static void
host1x_cb_func(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct syncpt_t *syncpt = container_of(cb, struct syncpt_t, fence_cb);

	schedule_work(&syncpt->work);
}

static int
allocate_fence(struct syncpt_t *syncpt)
{
	int ret = 0;
	struct dma_fence *fence = NULL;

	fence = host1x_fence_create(syncpt->sp, ++syncpt->threshold, false);
	if (IS_ERR_OR_NULL(fence)) {
		ret = PTR_ERR(fence);
		pr_err("host1x_fence_create failed with: %d\n", ret);
		return ret;
	}

	mutex_lock(&syncpt->lock);
	ret = dma_fence_add_callback(fence, &syncpt->fence_cb, host1x_cb_func);
	if (ret != 0) {
		/* If already expired. */
		if (ret == -ENOENT) {
			ret = 0;
			schedule_work(&syncpt->work);
		}
		goto put_fence;
	}
	syncpt->fence = fence;
	mutex_unlock(&syncpt->lock);

	return ret;

put_fence:
	dma_fence_put(fence);
	mutex_unlock(&syncpt->lock);
	return ret;
}

static void
fence_do_work(struct work_struct *work)
{
	int ret = 0;
	struct syncpt_t *syncpt = container_of(work, struct syncpt_t, work);

	if (syncpt->notifier)
		syncpt->notifier(syncpt->notifier_data);

	mutex_lock(&syncpt->lock);
	/* If deinit triggered, no need to proceed. */
	if (syncpt->fence_release)
		return;

	if (syncpt->fence) {
		dma_fence_put(syncpt->fence);
		syncpt->fence = NULL;
	}
	mutex_unlock(&syncpt->lock);

	ret = allocate_fence(syncpt);
	if (ret != 0) {
		mutex_unlock(&syncpt->lock);
		pr_err("allocate_fence failed with: %d\n", ret);
		return;
	}
}

static int
start_msg_handling(struct comm_channel_ctx_t *comm_ctx)
{
	int ret = 0;
	struct task_t *r_task = &comm_ctx->r_task;
	struct syncpt_t *syncpt = &comm_ctx->syncpt;

	/* start the recv msg processing task.*/
	init_waitqueue_head(&r_task->waitq);
	init_completion(&r_task->shutdown_compl);
	r_task->shutdown = false;
	r_task->task = kthread_run(recv_taskfn, comm_ctx,
				   "comm-channel-recv-task");
	if (IS_ERR_OR_NULL(r_task->task)) {
		pr_err("Failed to create comm channel recv task\n");
		return PTR_ERR(r_task->task);
	}
	r_task->created = true;

	/* enable syncpt notifications handling. */
	mutex_init(&syncpt->lock);
	syncpt->threshold = host1x_syncpt_read(syncpt->sp);
	syncpt->notifier = syncpt_callback;
	syncpt->notifier_data = comm_ctx;
	INIT_WORK(&syncpt->work, fence_do_work);
	syncpt->fence_release = false;

	ret = allocate_fence(syncpt);
	if (ret != 0) {
		pr_err("allocate_fence failed with: %d\n", ret);
		return ret;
	}

	return ret;
}

static int
stop_msg_handling(struct comm_channel_ctx_t *comm_ctx)
{
	int ret = 0;
	struct task_t *r_task = NULL;
	struct syncpt_t *syncpt = NULL;

	if (!comm_ctx)
		return ret;

	syncpt = &comm_ctx->syncpt;
	r_task = &comm_ctx->r_task;

	if (r_task->created) {
		/* Remove dma fence callback. */
		mutex_lock(&syncpt->lock);
		syncpt->fence_release = true;
		if (syncpt->fence) {
			ret = dma_fence_remove_callback(syncpt->fence,
							&syncpt->fence_cb);
			if (ret) {
				/*
				 * If dma_fence_remove_callback() returns true
				 * means callback is removed successfully.
				 * Cancel the fence to drop the refcount.
				 */
				host1x_fence_cancel(syncpt->fence);
			}
			dma_fence_put(syncpt->fence);
			syncpt->fence = NULL;
		}
		mutex_unlock(&syncpt->lock);
		cancel_work_sync(&syncpt->work);
		mutex_destroy(&syncpt->lock);

		/*
		 * initiate stop.
		 * we do not use kthread_stop(), but wait on this.
		 */
		r_task->shutdown = true;
		wake_up_interruptible(&r_task->waitq);
		ret = wait_for_completion_interruptible(&r_task->shutdown_compl);
		if (ret)
			pr_err("Failed to wait for completion\n");

		r_task->created = false;
	}

	return ret;
}

static void
free_syncpoint(struct comm_channel_ctx_t *comm_ctx)
{
	struct syncpt_t *syncpt = NULL;

	if (!comm_ctx)
		return;

	syncpt = &comm_ctx->syncpt;

	if (syncpt->peer_mem.pva) {
		iounmap(syncpt->peer_mem.pva);
		syncpt->peer_mem.pva = NULL;
	}

	if (syncpt->mapped_iova) {
		pci_client_unmap_addr(comm_ctx->pci_client_h,
				      syncpt->iova, syncpt->size);
		syncpt->mapped_iova = false;
	}

	if (syncpt->iova_block_h) {
		pci_client_free_iova(comm_ctx->pci_client_h,
				     &syncpt->iova_block_h);
		syncpt->iova_block_h = NULL;
	}

	if (syncpt->sp) {
		host1x_syncpt_put(syncpt->sp);
		syncpt->sp = NULL;
	}
}

static int
allocate_syncpoint(struct comm_channel_ctx_t *comm_ctx)
{
	int ret = 0;
	int prot = 0;
	struct host1x *host1x = NULL;
	struct syncpt_t *syncpt = NULL;
	size_t offsetof = 0x0;

	syncpt = &comm_ctx->syncpt;

	host1x = platform_get_drvdata(comm_ctx->host1x_pdev);
	if (!host1x) {
		pr_err("Host1x handle is null.");
		return -EINVAL;
	}
	syncpt->sp = host1x_syncpt_alloc(host1x, HOST1X_SYNCPT_CLIENT_MANAGED,
					 "nvscic2c-pcie-comm-ch");
	if (IS_ERR_OR_NULL(syncpt->sp)) {
		ret = -ENOMEM;
		pr_err("Failed to reserve comm notify syncpt\n");
		goto err;
	}

	syncpt->id = host1x_syncpt_id(syncpt->sp);
	/* physical address of syncpoint shim. */
	syncpt->phy_addr = get_syncpt_shim_offset(syncpt->id);
	syncpt->size = SP_MAP_SIZE;

	/* reserve iova with the iova manager.*/
	ret = pci_client_alloc_iova(comm_ctx->pci_client_h, syncpt->size,
				    &syncpt->iova, &offsetof,
				    &syncpt->iova_block_h);
	if (ret) {
		pr_err("Err reserving comm syncpt iova region of size: 0x%lx\n",
		       syncpt->size);
		goto err;
	}

	/* map the pages to the reserved iova. */
	prot = (IOMMU_CACHE | IOMMU_READ | IOMMU_WRITE);
	ret = pci_client_map_addr(comm_ctx->pci_client_h, syncpt->iova,
				  syncpt->phy_addr, syncpt->size, prot);
	if (ret) {
		pr_err("Err mapping comm SP physical addr to reserved iova\n");
		goto err;
	}
	syncpt->mapped_iova = true;

	pr_debug("mapped phy:0x%pa[p]+0x%lx to iova:0x%llx\n",
		 &syncpt->phy_addr, syncpt->size, syncpt->iova);

	/*
	 * get peer's aperture offset. Map tx (pcie aper for notif tx.)
	 * for peer's access of comm-syncpt, it is assumed offsets are
	 * same on both SoC.
	 */
	syncpt->peer_mem.size = syncpt->size;
	ret = pci_client_get_peer_aper(comm_ctx->pci_client_h, offsetof,
				       syncpt->peer_mem.size,
				       &syncpt->peer_mem.aper);
	if (ret) {
		pr_err("Failed to get comm peer's syncpt aperture\n");
		goto err;
	}
	syncpt->peer_mem.pva = ioremap(syncpt->peer_mem.aper,
				       syncpt->peer_mem.size);
	if (!syncpt->peer_mem.pva) {
		ret = -ENOMEM;
		pr_err("Failed to ioremap comm peer's syncpt pcie aperture\n");
		goto err;
	}

	return ret;
err:
	free_syncpoint(comm_ctx);
	return ret;
}

static void
free_fifo_memory(struct comm_channel_ctx_t *comm_ctx)
{
	struct fifo_t *fifo = NULL;

	if (!comm_ctx)
		return;

	fifo = &comm_ctx->fifo;

	if (fifo->local_hdr) {
		kfree((void *)fifo->local_hdr);
		fifo->local_hdr = NULL;
	}

	if (fifo->peer_mem.pva) {
		iounmap(fifo->peer_mem.pva);
		fifo->peer_mem.pva = NULL;
	}

	if (fifo->mapped_iova) {
		pci_client_unmap_addr(comm_ctx->pci_client_h,
				      fifo->iova, fifo->self_mem.size);
		fifo->mapped_iova = false;
	}

	if (fifo->iova_block_h) {
		pci_client_free_iova(comm_ctx->pci_client_h,
				     &fifo->iova_block_h);
		fifo->iova_block_h = NULL;
	}

	if (fifo->self_mem.pva) {
		free_pages_exact(fifo->self_mem.pva,
				 fifo->self_mem.size);
		fifo->self_mem.pva = NULL;
	}

	mutex_destroy(&fifo->send_lock);
}

static int
allocate_fifo_memory(struct comm_channel_ctx_t *comm_ctx)
{
	int ret = 0;
	int prot = 0;
	size_t offsetof = 0x0;
	struct fifo_t *fifo = &comm_ctx->fifo;

	mutex_init(&fifo->send_lock);

	/* memory size includes frames and header.*/
	fifo->nframes = COMM_CHANNEL_NFRAMES;
	fifo->frame_sz = COMM_CHANNEL_FRAME_SZ;
	fifo->self_mem.size = (fifo->nframes * fifo->frame_sz);
	fifo->self_mem.size += sizeof(struct header);
	fifo->self_mem.size = ALIGN(fifo->self_mem.size, PAGE_SIZE);
	fifo->self_mem.pva = alloc_pages_exact(fifo->self_mem.size,
					       (GFP_KERNEL | __GFP_ZERO));
	if (!fifo->self_mem.pva) {
		pr_err("Error allocating fifo contiguous pages: (%lu)\n",
		       (fifo->self_mem.size >> PAGE_SHIFT));
		return -ENOMEM;
	}
	fifo->self_mem.phys_addr =
				page_to_phys(virt_to_page(fifo->self_mem.pva));

	/* reserve iova for stitching comm channel memory for peer access.*/
	ret = pci_client_alloc_iova(comm_ctx->pci_client_h, fifo->self_mem.size,
				    &fifo->iova, &offsetof,
				    &fifo->iova_block_h);
	if (ret) {
		pr_err("Failed reserving fifo iova region of size: 0x%lx\n",
		       fifo->self_mem.size);
		goto err;
	}

	/* map the pages to the reserved iova.*/
	prot = (IOMMU_CACHE | IOMMU_READ | IOMMU_WRITE);
	ret = pci_client_map_addr(comm_ctx->pci_client_h, fifo->iova,
				  fifo->self_mem.phys_addr, fifo->self_mem.size,
				  prot);
	if (ret) {
		pr_err("Failed to map comm fifo pages to reserved iova\n");
		goto err;
	}
	fifo->mapped_iova = true;

	pr_debug("comm fifo mapped page:0x%pa[p]+0x%lx to iova:0x%llx\n",
		 &fifo->self_mem.phys_addr, fifo->self_mem.size, fifo->iova);

	/*
	 * for peer's access of comm-fifo, it is assumed offsets are
	 * same on both SoC.
	 */
	fifo->peer_mem.size = fifo->self_mem.size;
	ret = pci_client_get_peer_aper(comm_ctx->pci_client_h, offsetof,
				       fifo->peer_mem.size,
				       &fifo->peer_mem.aper);
	if (ret) {
		pr_err("Failed to get comm peer's fifo aperture\n");
		goto err;
	}
	fifo->peer_mem.pva = ioremap(fifo->peer_mem.aper, fifo->peer_mem.size);
	if (!fifo->peer_mem.pva) {
		ret = -ENOMEM;
		pr_err("Failed to ioremap peer's comm fifo aperture\n");
		goto err;
	}

	/* allocate local header.*/
	fifo->local_hdr = kzalloc(sizeof(*fifo->local_hdr), GFP_KERNEL);
	if (WARN_ON(!fifo->local_hdr)) {
		ret = -ENOMEM;
		goto err;
	}

	fifo->recv_hdr = (struct header *)(fifo->self_mem.pva);
	fifo->send_hdr = (__force struct header *)(fifo->peer_mem.pva);
	fifo->recv = ((u8 *)fifo->recv_hdr + sizeof(struct header));
	fifo->send = ((u8 *)fifo->send_hdr + sizeof(struct header));

	return ret;

err:
	free_fifo_memory(comm_ctx);
	return ret;
}

int
comm_channel_init(struct driver_ctx_t *drv_ctx, void **comm_channel_h)
{
	int ret = 0;
	struct comm_channel_ctx_t *comm_ctx = NULL;

	if (WARN_ON(sizeof(struct comm_msg) > COMM_CHANNEL_FRAME_SZ))
		return -EINVAL;

	/* should not be an already instantiated. */
	if (WARN_ON(!drv_ctx || !comm_channel_h || *comm_channel_h))
		return -EINVAL;

	/* start by allocating the comm ctx.*/
	comm_ctx = kzalloc(sizeof(*comm_ctx), GFP_KERNEL);
	if (WARN_ON(!comm_ctx))
		return -ENOMEM;
	mutex_init(&comm_ctx->cb_ops_lock);
	atomic_set(&comm_ctx->recv_count, 0);

	comm_ctx->pci_client_h = drv_ctx->pci_client_h;
	comm_ctx->of_node = drv_ctx->drv_param.of_node;
	comm_ctx->host1x_pdev = drv_ctx->drv_param.host1x_pdev;

	/*
	 * allocate fifo area, make it visible to peer. Assume same aperture
	 * for peer access too.
	 */
	ret = allocate_fifo_memory(comm_ctx);
	if (ret)
		goto err;

	/*
	 * allocate notification for comm-channel, Assume same aperture
	 * for peer access too.
	 */
	ret = allocate_syncpoint(comm_ctx);
	if (ret)
		goto err;

	/* we can now wait for notifications/messages to be received.*/
	ret = start_msg_handling(comm_ctx);
	if (ret)
		goto err;

	*comm_channel_h = comm_ctx;
	return ret;
err:
	comm_channel_deinit((void **)&comm_ctx);
	return ret;
}

void
comm_channel_deinit(void **comm_channel_h)
{
	struct comm_channel_ctx_t *comm_ctx =
				(struct comm_channel_ctx_t *)(*comm_channel_h);
	if (!comm_ctx)
		return;

	stop_msg_handling(comm_ctx);
	free_syncpoint(comm_ctx);
	free_fifo_memory(comm_ctx);
	mutex_destroy(&comm_ctx->cb_ops_lock);
	kfree(comm_ctx);

	*comm_channel_h = NULL;
}

int
comm_channel_register_msg_cb(void *comm_channel_h, enum comm_msg_type type,
			     struct callback_ops *ops)
{
	int ret = 0;
	struct callback_ops *cb_ops = NULL;
	struct comm_channel_ctx_t *comm_ctx =
				(struct comm_channel_ctx_t *)comm_channel_h;

	if (WARN_ON(!comm_ctx || !ops || !ops->callback))
		return -EINVAL;

	if (WARN_ON(type <= COMM_MSG_TYPE_INVALID ||
		    type >= COMM_MSG_TYPE_MAXIMUM))
		return -EINVAL;

	mutex_lock(&comm_ctx->cb_ops_lock);

	cb_ops = &comm_ctx->cb_ops[type];
	if (cb_ops->callback) {
		pr_err("Callback for msg type: (%u) is already taken\n", type);
		ret = -EBUSY;
	} else {
		cb_ops->callback = ops->callback;
		cb_ops->ctx = ops->ctx;
	}

	mutex_unlock(&comm_ctx->cb_ops_lock);
	return ret;
}

int
comm_channel_unregister_msg_cb(void *comm_channel_h, enum comm_msg_type type)
{
	int ret = 0;
	struct callback_ops *cb_ops = NULL;
	struct comm_channel_ctx_t *comm_ctx =
				(struct comm_channel_ctx_t *)comm_channel_h;

	if (WARN_ON(!comm_ctx))
		return -EINVAL;

	if (WARN_ON(type <= COMM_MSG_TYPE_INVALID ||
		    type >= COMM_MSG_TYPE_MAXIMUM))
		return -EINVAL;

	mutex_lock(&comm_ctx->cb_ops_lock);
	cb_ops = &comm_ctx->cb_ops[type];
	cb_ops->callback = NULL;
	cb_ops->ctx = NULL;
	mutex_unlock(&comm_ctx->cb_ops_lock);

	return ret;
}
