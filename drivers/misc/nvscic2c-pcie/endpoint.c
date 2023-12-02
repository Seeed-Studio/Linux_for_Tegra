// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#define pr_fmt(fmt)	"nvscic2c-pcie: endpoint: " fmt

#include <nvidia/conftest.h>

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/dma-fence.h>
#include <linux/errno.h>
#include <linux/host1x-next.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <uapi/misc/nvscic2c-pcie-ioctl.h>

#include "common.h"
#include "endpoint.h"
#include "module.h"
#include "pci-client.h"
#include "stream-extensions.h"

#define PCIE_STATUS_CHANGE_ACK_TIMEOUT (2000)

/*
 * Masked offsets to return to user, allowing them to mmap
 * different memory segments of endpoints in user-space.
 */
enum mem_mmap_type {
	/* Invalid.*/
	MEM_MMAP_INVALID = 0,
	/* Map Peer PCIe aperture: For Tx across PCIe.*/
	PEER_MEM_MMAP,
	/* Map Self PCIe shared memory: For Rx across PCIe.*/
	SELF_MEM_MMAP,
	/* Map Link memory segment to query link status with Peer.*/
	LINK_MEM_MMAP,
	/* Map eDMA error memory segment to query eDMA xfer errors.*/
	EDMA_ERR_MEM_MMAP,
	/* Maximum. */
	MEM_MAX_MMAP,
};

/* syncpoint handling. */
struct syncpt_t {
	u32 id;
	u32 threshold;
	struct host1x_syncpt *sp;

	/* PCIe aperture for writes to peer syncpoint for same the endpoint. */
	struct pci_aper_t peer_mem;

	/* syncpoint physical address for stritching to PCIe BAR backing.*/
	size_t size;
	phys_addr_t phy_addr;

	/* for mapping above physical pages to iova of client choice.*/
	void *iova_block_h;
	u64 iova;
	bool mapped_iova;

	bool host1x_cb_set;
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

/* private data structure for each endpoint. */
struct endpoint_t {
	/* properties / attributes of this endpoint.*/
	char name[NAME_MAX];

	/* char device management.*/
	u32 minor;
	dev_t dev;
	struct cdev cdev;
	struct device *device;

	/* slot/frames this endpoint is divided into honoring alignment.*/
	u32 nframes;
	u32 frame_sz;

	/* allocated physical memory info for mmap.*/
	struct cpu_buff_t self_mem;

	/* mapping physical pages to iova of client choice.*/
	void *iova_block_h;
	u64 iova;
	bool mapped_iova;

	/* PCIe aperture for writes to peer over pcie. */
	struct pci_aper_t peer_mem;

	/* poll/notifications.*/
	wait_queue_head_t poll_waitq;

	/* syncpoint shim for notifications (rx). */
	struct syncpt_t syncpt;

	/* msi irq to x86 RP */
	u16 msi_irq;

	/*
	 * book-keeping of:
	 *  peer notifications.
	 *  PCIe link event.
	 *  eDMA xfer error event.
	 */
	atomic_t event_count;

	u32 linkevent_id;

	/* propagate events when endpoint was initialized.*/
	atomic_t event_handling;

	/* serialise access to fops.*/
	struct mutex fops_lock;
	atomic_t in_use;
	wait_queue_head_t close_waitq;

	/* when the endpoints are undergoing shutdown.*/
	atomic_t shutdown;

	/* signal eps driver context on ep in use.*/
	atomic_t *eps_in_use;
	wait_queue_head_t *eps_close_waitq;

	/* pci client handle.*/
	void *pci_client_h;

	/* stream extensions.*/
	struct stream_ext_params stream_ext_params;
	void *stream_ext_h;
	struct platform_device *host1x_pdev;
};

/* Overall context for the endpoint sub-module of  nvscic2c-pcie driver.*/
struct endpoint_drv_ctx_t {
	char drv_name[NAME_MAX];

	/* entire char device region allocated for all endpoints.*/
	dev_t char_dev;

	/* every endpoint char device will be registered to this class.*/
	struct class *class;

	/* array of nvscic2c-pcie endpoint logical devices.*/
	u8 nr_endpoint;
	struct endpoint_t *endpoints;

	/* nvscic2c-pcie DT node reference, used in getting syncpoint shim. */
	struct device_node *of_node;

	/* total count of endpoints opened/in-use.*/
	atomic_t eps_in_use;
	wait_queue_head_t eps_close_waitq;
};

/*
 * pci-client would raise this callback only when there is change
 * in PCIe link status(up->down OR down->up).
 */
static void
event_callback(void *event_type, void *ctx);

/* prototype. */
static void
enable_event_handling(struct endpoint_t *endpoint);

/* prototype. */
static int
disable_event_handling(struct endpoint_t *endpoint);

/* prototype. */
static int
ioctl_notify_remote_impl(struct endpoint_t *endpoint);

/* prototype. */
static int
ioctl_get_info_impl(struct endpoint_t *endpoint,
		    struct nvscic2c_pcie_endpoint_info *get_info);

/*
 * open() syscall backing for nvscic2c-pcie endpoint devices.
 *
 * Populate the endpoint_device internal data-structure into fops private data
 * for subsequent calls to other fops handlers.
 */
static int
endpoint_fops_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	enum nvscic2c_pcie_link link = NVSCIC2C_PCIE_LINK_DOWN;
	struct endpoint_t *endpoint =
		container_of(inode->i_cdev, struct endpoint_t, cdev);

	mutex_lock(&endpoint->fops_lock);

	if (atomic_read(&endpoint->in_use)) {
		/* already in use.*/
		ret = -EBUSY;
		goto err;
	}

	if (atomic_read(&endpoint->shutdown)) {
		/* do not open when module is undergoing shutdown.*/
		ret = -ESHUTDOWN;
		goto err;
	}

	link = pci_client_query_link_status(endpoint->pci_client_h);
	if (link != NVSCIC2C_PCIE_LINK_UP) {
		/* do not open when link is not established.*/
		ret = -ENOLINK;
		goto err;
	}

	/* create stream extension handle.*/
	ret = stream_extension_init(&endpoint->stream_ext_params,
				    &endpoint->stream_ext_h);
	if (ret) {
		pr_err("Failed setting up stream extension handle: (%s)\n",
		       endpoint->name);
		goto err;
	}

	/* start link, data event handling.*/
	enable_event_handling(endpoint);

	atomic_set(&endpoint->in_use, 1);
	filp->private_data = endpoint;

	/*
	 * increment the total opened endpoints in endpoint_drv_ctx_t.
	 * Doesn't need to be guarded in lock, atomic variable.
	 */
	atomic_inc(endpoint->eps_in_use);
err:
	mutex_unlock(&endpoint->fops_lock);
	return ret;
}

/* close() syscall backing for nvscic2c-pcie endpoint devices.*/
static int
endpoint_fops_release(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct endpoint_t *endpoint = filp->private_data;

	if (!endpoint)
		return ret;

	mutex_lock(&endpoint->fops_lock);
	filp->private_data = NULL;
	disable_event_handling(endpoint);
	stream_extension_deinit(&endpoint->stream_ext_h);
	atomic_set(&endpoint->in_use, 0);
	if (atomic_dec_and_test(endpoint->eps_in_use))
		wake_up_interruptible_all(endpoint->eps_close_waitq);
	mutex_unlock(&endpoint->fops_lock);

	return ret;
}

/*
 * mmap() syscall backing for nvscic2c-pcie endpoint device.
 *
 * We support mapping following distinct regions of memory:
 * - Peer's memory for same endpoint(used for Tx),
 * - Self's memory (used for Rx),
 * - pci-client link status memory.
 *
 * We map just one segment of memory in each call based on the information
 * (which memory segment) provided by user-space code.
 */
static int
endpoint_fops_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct endpoint_t *endpoint = filp->private_data;
	u64 mmap_type = vma->vm_pgoff;
	u64 memaddr = 0x0;
	u64 memsize = 0x0;
	int ret = 0;

	if (WARN_ON(!endpoint))
		return -EFAULT;

	if (WARN_ON(!(vma)))
		return -EFAULT;

	mutex_lock(&endpoint->fops_lock);
	if (!atomic_read(&endpoint->in_use)) {
		mutex_unlock(&endpoint->fops_lock);
		return -EBADF;
	}

	switch (mmap_type) {
	case PEER_MEM_MMAP:
		vma->vm_page_prot = pgprot_device(vma->vm_page_prot);
		memaddr = endpoint->peer_mem.aper;
		memsize = endpoint->peer_mem.size;
		break;
	case SELF_MEM_MMAP:
		memaddr = endpoint->self_mem.phys_addr;
		memsize = endpoint->self_mem.size;
		break;
	case LINK_MEM_MMAP:
		if (vma->vm_flags & VM_WRITE) {
			ret = -EPERM;
			pr_err("(%s): LINK_MEM_MMAP called with WRITE prot\n",
			       endpoint->name);
			goto exit;
		}
		ret = pci_client_mmap_link_mem(endpoint->pci_client_h, vma);
		goto exit;
	case EDMA_ERR_MEM_MMAP:
		ret = pci_client_mmap_edma_err_mem(endpoint->pci_client_h,
						   endpoint->minor, vma);
		goto exit;
	default:
		pr_err("(%s): unrecognised mmap type: (%llu)\n",
		       endpoint->name, mmap_type);
		goto exit;
	}

	if ((vma->vm_end - vma->vm_start) != memsize) {
		pr_err("(%s): mmap type: (%llu), memsize mismatch\n",
		       endpoint->name, mmap_type);
		goto exit;
	}

	vma->vm_pgoff  = 0;
#if defined(NV_VM_AREA_STRUCT_HAS_CONST_VM_FLAGS) /* Linux v6.3 */
	vm_flags_set(vma, VM_DONTCOPY);
#else
	vma->vm_flags |= (VM_DONTCOPY); // fork() not supported.
#endif
	ret = remap_pfn_range(vma, vma->vm_start,
			      PFN_DOWN(memaddr),
			      memsize, vma->vm_page_prot);
	if (ret) {
		pr_err("(%s): mmap() failed, mmap type:(%llu)\n",
		       endpoint->name, mmap_type);
	}
exit:
	mutex_unlock(&endpoint->fops_lock);
	return ret;
}

/*
 * poll() syscall backing for nvscic2c-pcie endpoint devices.
 *
 * user-space code shall call poll with FD on read, write and probably exception
 * for endpoint state changes.
 *
 * If we are able to read(), write() or there is a pending state change event
 * to be serviced, we return letting application call get_event(), otherwise
 * kernel f/w will wait for poll_waitq activity to occur.
 */
static __poll_t
endpoint_fops_poll(struct file *filp, poll_table *wait)
{
	__poll_t mask = 0;
	struct endpoint_t *endpoint = filp->private_data;

	if (WARN_ON(!endpoint))
		return (__force __poll_t)POLLNVAL;

	mutex_lock(&endpoint->fops_lock);
	if (!atomic_read(&endpoint->in_use)) {
		mutex_unlock(&endpoint->fops_lock);
		return (__force __poll_t)POLLNVAL;
	}

	/* add all waitq if they are different for read, write & link+state.*/
	poll_wait(filp, &endpoint->poll_waitq, wait);

	/*
	 * wake up read, write (& exception - those who want to use) fd on
	 * getting Link + peer notifications + eDMA xfer error notifications.
	 */
	if (atomic_read(&endpoint->event_count)) {
		atomic_dec(&endpoint->event_count);
		mask = (__force __poll_t)(POLLPRI | POLLIN | POLLOUT);
	}

	mutex_unlock(&endpoint->fops_lock);

	return mask;
}

/* ioctl() syscall backing for nvscic2c-pcie endpoint device. */
#define MAX_IOCTL_ARG_SIZE (sizeof(union nvscic2c_pcie_ioctl_arg_max_size))
static long
endpoint_fops_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	u8 buf[MAX_IOCTL_ARG_SIZE] __aligned(sizeof(u64)) = {0};
	struct endpoint_t *endpoint = filp->private_data;

	if (WARN_ON(!endpoint))
		return -EFAULT;

	if (WARN_ON(_IOC_TYPE(cmd) != NVSCIC2C_PCIE_IOCTL_MAGIC ||
		    _IOC_NR(cmd) == 0 ||
		    _IOC_NR(cmd) > NVSCIC2C_PCIE_IOCTL_NUMBER_MAX) ||
		    _IOC_SIZE(cmd) > MAX_IOCTL_ARG_SIZE)
		return -ENOTTY;

	/* copy the cmd if it was meant from user->kernel. */
	(void)memset(buf, 0, sizeof(buf));
	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (copy_from_user(buf, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	mutex_lock(&endpoint->fops_lock);
	if (!atomic_read(&endpoint->in_use)) {
		mutex_unlock(&endpoint->fops_lock);
		return -EBADF;
	}
	switch (cmd) {
	case NVSCIC2C_PCIE_IOCTL_GET_INFO:
		ret = ioctl_get_info_impl
			(endpoint, (struct nvscic2c_pcie_endpoint_info *)buf);
		break;
	case NVSCIC2C_PCIE_IOCTL_NOTIFY_REMOTE:
		ret = ioctl_notify_remote_impl(endpoint);
		break;
	default:
		ret = stream_extension_ioctl(endpoint->stream_ext_h, cmd, buf);
		break;
	}
	mutex_unlock(&endpoint->fops_lock);

	/* copy the cmd result back to user if it was kernel->user: get_info.*/
	if (ret == 0 && (_IOC_DIR(cmd) & _IOC_READ))
		ret = copy_to_user((void __user *)arg, buf, _IOC_SIZE(cmd));
	return ret;
}

/*
 * All important endpoint dev node properites required for user-space
 * to map the channel memory and work without going to LKM for data
 * xfer are exported in this ioctl implementation.
 *
 * Because we export different memory for a single nvscic2c-pcie endpoint,
 * export the memory regions as masked offsets.
 */
static int
ioctl_get_info_impl(struct endpoint_t *endpoint,
		    struct nvscic2c_pcie_endpoint_info *get_info)
{
	if (endpoint->peer_mem.size > U32_MAX ||
	    endpoint->self_mem.size > U32_MAX)
		return -EINVAL;

	get_info->nframes         = endpoint->nframes;
	get_info->frame_size      = endpoint->frame_sz;
	get_info->peer.offset     = (PEER_MEM_MMAP << PAGE_SHIFT);
	get_info->peer.size       = endpoint->peer_mem.size;
	get_info->self.offset     = (SELF_MEM_MMAP << PAGE_SHIFT);
	get_info->self.size       = endpoint->self_mem.size;
	get_info->link.offset     = (LINK_MEM_MMAP << PAGE_SHIFT);
	get_info->link.size       = PAGE_ALIGN(sizeof(enum nvscic2c_pcie_link));
	get_info->edma_err.offset = (EDMA_ERR_MEM_MMAP << PAGE_SHIFT);
	get_info->edma_err.size   = PAGE_ALIGN(sizeof(u32));

	return 0;
}

/*
 * implement NVSCIC2C_PCIE_IOCTL_NOTIFY_REMOTE ioctl call.
 */
static int
ioctl_notify_remote_impl(struct endpoint_t *endpoint)
{
	int ret = 0;
	enum nvscic2c_pcie_link link = NVSCIC2C_PCIE_LINK_DOWN;
	struct syncpt_t *syncpt = &endpoint->syncpt;
	enum peer_cpu_t peer_cpu = NVCPU_ORIN;

	link = pci_client_query_link_status(endpoint->pci_client_h);
	peer_cpu =  pci_client_get_peer_cpu(endpoint->pci_client_h);

	if (link != NVSCIC2C_PCIE_LINK_UP)
		return -ENOLINK;

	if (peer_cpu == NVCPU_X86_64) {
		ret = pci_client_raise_irq(endpoint->pci_client_h, PCI_EPC_IRQ_MSI,
					   endpoint->msi_irq);
	} else {
		/*
		 * Ordering between message/data and host1x syncpoints is not
		 * enforced strictly. Out of the possible WARs, implement dummy
		 * PCIe Read before any syncpoint notifications towards peer.
		 *
		 * For any writes from UMD which require notification, issuing a
		 * dummy PCIe read here shall suffice for all cases where UMD writes
		 * data and requires notification via syncpoint.
		 */
		(void)readl(syncpt->peer_mem.pva);

		/*
		 * increment peer's syncpoint. Write of any 4-byte value
		 * increments remote's syncpoint shim by 1.
		 */
		writel(0x1, syncpt->peer_mem.pva);
	}

	return ret;
}

static void
enable_event_handling(struct endpoint_t *endpoint)
{
	/*
	 * propagate link and state change events that occur after the device
	 * is opened and not the stale ones.
	 */
	atomic_set(&endpoint->event_count, 0);
	atomic_set(&endpoint->event_handling, 1);
}

static int
disable_event_handling(struct endpoint_t *endpoint)
{
	int ret = 0;

	if (!endpoint)
		return ret;

	atomic_set(&endpoint->event_handling, 0);
	atomic_set(&endpoint->event_count, 0);

	return ret;
}

static void
event_callback(void *data, void *ctx)
{
	struct endpoint_t *endpoint = NULL;

	if (!ctx) {
		pr_err("Spurious link event callback\n");
		return;
	}

	endpoint = (struct endpoint_t *)(ctx);

	/* notify only if the endpoint was opened.*/
	if (atomic_read(&endpoint->event_handling)) {
		atomic_inc(&endpoint->event_count);
		wake_up_interruptible_all(&endpoint->poll_waitq);
	}
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
	if (IS_ERR(fence)) {
		ret = PTR_ERR(fence);
		pr_err("host1x_fence_create failed with: %d\n", ret);
		return ret;
	}

	mutex_lock(&syncpt->lock);
	ret = dma_fence_add_callback(fence, &syncpt->fence_cb, host1x_cb_func);
	if (ret != 0) {
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

/*
 * Callback registered with Syncpoint shim, shall be invoked
 * on expiry of syncpoint shim fence/trigger from remote.
 */
static void
syncpt_callback(void *data)
{
	/* Skip args ceck, trusting host1x. */

	event_callback(NULL, data);
}

/*
 * unpin/unmap and free the syncpoints allocated.
 */
static void
free_syncpoint(struct endpoint_drv_ctx_t *eps_ctx,
	       struct endpoint_t *endpoint)
{
	int ret = 0;
	struct syncpt_t *syncpt = NULL;

	if (!eps_ctx || !endpoint)
		return;

	syncpt = &endpoint->syncpt;

	if (syncpt->host1x_cb_set) {
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
	}
	syncpt->host1x_cb_set = false;

	if (syncpt->peer_mem.pva) {
		iounmap(syncpt->peer_mem.pva);
		syncpt->peer_mem.pva = NULL;
	}

	if (syncpt->mapped_iova) {
		pci_client_unmap_addr(endpoint->pci_client_h,
				      syncpt->iova, syncpt->size);
		syncpt->mapped_iova = false;
	}

	if (syncpt->iova_block_h) {
		pci_client_free_iova(endpoint->pci_client_h,
				     &syncpt->iova_block_h);
		syncpt->iova_block_h = NULL;
	}

	if (syncpt->sp) {
		host1x_syncpt_put(syncpt->sp);
		syncpt->sp = NULL;
	}
}

/* Allocate syncpoint shim for the endpoint. Subsequently, map/pin
 * them to PCIe BAR backing.
 */
static int
allocate_syncpoint(struct endpoint_drv_ctx_t *eps_ctx,
		   struct endpoint_t *endpoint)
{
	int ret = 0;
	int prot = 0;
	struct host1x *host1x = NULL;
	struct syncpt_t *syncpt = NULL;
	size_t offsetof = 0x0;

	syncpt = &endpoint->syncpt;

	host1x = platform_get_drvdata(endpoint->host1x_pdev);
	if (!host1x) {
		pr_err("Could not get host1x handle from host1x_pdev.\n");
		return -EINVAL;
	}

	syncpt->sp = host1x_syncpt_alloc(host1x, HOST1X_SYNCPT_CLIENT_MANAGED,
					 endpoint->name);
	if (IS_ERR_OR_NULL(syncpt->sp)) {
		pr_err("(%s): Failed to reserve syncpt\n", endpoint->name);
		return -ENOMEM;
	}

	syncpt->id = host1x_syncpt_id(syncpt->sp);
	/* physical address of syncpoint shim. */
	syncpt->phy_addr = get_syncpt_shim_offset(syncpt->id);
	syncpt->size = SP_MAP_SIZE;

	/* reserve iova with the iova manager.*/
	ret = pci_client_alloc_iova(endpoint->pci_client_h, syncpt->size,
				    &syncpt->iova, &offsetof,
				    &syncpt->iova_block_h);
	if (ret) {
		pr_err("(%s): Err reserving iova region of size(SP): (%lu)\n",
		       endpoint->name, syncpt->size);
		goto err;
	}

	/* map the pages to the reserved iova.*/
	prot = (IOMMU_CACHE | IOMMU_READ | IOMMU_WRITE);
	ret = pci_client_map_addr(endpoint->pci_client_h, syncpt->iova,
				  syncpt->phy_addr, syncpt->size, prot);
	if (ret) {
		pr_err("(%s): Failed to map SP physical addr to reserved iova\n",
		       endpoint->name);
		goto err;
	}
	syncpt->mapped_iova = true;

	pr_debug("(%s): mapped phy:0x%pa[p]+0x%lx to iova:0x%llx\n",
		 endpoint->name, &syncpt->phy_addr, syncpt->size, syncpt->iova);

	/* get peer's aperture offset. Map tx (pcie aper for notif tx.)*/
	syncpt->peer_mem.size = syncpt->size;
	ret = pci_client_get_peer_aper(endpoint->pci_client_h, offsetof,
				       syncpt->peer_mem.size,
				       &syncpt->peer_mem.aper);
	if (ret) {
		pr_err("Failed to get comm peer's syncpt pcie aperture\n");
		goto err;
	}

	syncpt->peer_mem.pva = ioremap(syncpt->peer_mem.aper,
				       syncpt->peer_mem.size);
	if (!syncpt->peer_mem.pva) {
		ret = -ENOMEM;
		pr_err("(%s): Failed to ioremap peer's syncpt pcie aperture\n",
		       endpoint->name);
		goto err;
	}

	syncpt->threshold = host1x_syncpt_read(syncpt->sp);

	/* enable syncpt notifications handling from peer.*/
	mutex_init(&syncpt->lock);
	syncpt->notifier = syncpt_callback;
	syncpt->notifier_data = (void *)endpoint;
	INIT_WORK(&syncpt->work, fence_do_work);
	syncpt->host1x_cb_set = true;
	syncpt->fence_release = false;

	ret = allocate_fence(syncpt);
	if (ret != 0) {
		pr_err("allocate_fence failed with: %d\n", ret);
		goto err;
	}

	return ret;
err:
	free_syncpoint(eps_ctx, endpoint);
	return ret;
}

/* unmap the memory from PCIe BAR iova and free the allocated physical pages. */
static void
free_memory(struct endpoint_drv_ctx_t *eps_ctx, struct endpoint_t *endpoint)
{
	if (!eps_ctx || !endpoint)
		return;

	if (endpoint->mapped_iova) {
		pci_client_unmap_addr(endpoint->pci_client_h,
				      endpoint->iova, endpoint->self_mem.size);
		endpoint->mapped_iova = false;
	}

	if (endpoint->iova_block_h) {
		pci_client_free_iova(endpoint->pci_client_h,
				     &endpoint->iova_block_h);
		endpoint->iova_block_h = NULL;
	}

	if (endpoint->self_mem.pva) {
		free_pages_exact(endpoint->self_mem.pva,
				 endpoint->self_mem.size);
		endpoint->self_mem.pva = NULL;
	}
}

/*
 * allocate coniguous physical memory for endpoint. This shall be mapped
 * to PCIe BAR iova.
 */
static int
allocate_memory(struct endpoint_drv_ctx_t *eps_ctx, struct endpoint_t *ep)
{
	int ret = 0;
	int prot = 0;
	size_t offsetof = 0x0;

	/*
	 * memory size includes space for frames(aligned to PAGE_SIZE) plus
	 * one additional PAGE for frames header (managed/used by user-space).
	 */
	ep->self_mem.size = (ep->nframes * ep->frame_sz);
	ep->self_mem.size = ALIGN(ep->self_mem.size, PAGE_SIZE);
	ep->self_mem.size += PAGE_SIZE;
	ep->self_mem.pva = alloc_pages_exact(ep->self_mem.size,
					     (GFP_KERNEL | __GFP_ZERO));
	if (!ep->self_mem.pva) {
		ret = -ENOMEM;
		pr_err("(%s): Error allocating: (%lu) contiguous pages\n",
		       ep->name, (ep->self_mem.size >> PAGE_SHIFT));
		goto err;
	}
	ep->self_mem.phys_addr = page_to_phys(virt_to_page(ep->self_mem.pva));
	pr_debug("(%s): physical page allocated at:(0x%pa[p]+0x%lx)\n",
		 ep->name, &ep->self_mem.phys_addr, ep->self_mem.size);

	/* reserve iova with the iova manager.*/
	ret = pci_client_alloc_iova(ep->pci_client_h, ep->self_mem.size,
				    &ep->iova, &offsetof, &ep->iova_block_h);
	if (ret) {
		pr_err("(%s): Failed to reserve iova region of size: 0x%lx\n",
		       ep->name, ep->self_mem.size);
		goto err;
	}

	/* map the pages to the reserved iova.*/
	prot = (IOMMU_CACHE | IOMMU_READ | IOMMU_WRITE);
	ret = pci_client_map_addr(ep->pci_client_h, ep->iova,
				  ep->self_mem.phys_addr, ep->self_mem.size,
				  prot);
	if (ret) {
		pr_err("(%s): Failed to map physical page to reserved iova\n",
		       ep->name);
		goto err;
	}
	ep->mapped_iova = true;

	pr_debug("(%s): mapped page:0x%pa[p]+0x%lx to iova:0x%llx\n", ep->name,
		 &ep->self_mem.phys_addr, ep->self_mem.size, ep->iova);

	/* get peer's aperture offset. Used in mmaping tx mem.*/
	ep->peer_mem.size = ep->self_mem.size;
	ret = pci_client_get_peer_aper(ep->pci_client_h, offsetof,
				       ep->peer_mem.size, &ep->peer_mem.aper);
	if (ret) {
		pr_err("Failed to get peer's endpoint pcie aperture\n");
		goto err;
	}

	return ret;
err:
	free_memory(eps_ctx, ep);
	return ret;
}

/*
 * Set of per-endpoint char device file operations. Do not support:
 * read() and write() on nvscic2c-pcie endpoint descriptors.
 */
static const struct file_operations endpoint_fops = {
	.owner          = THIS_MODULE,
	.open           = endpoint_fops_open,
	.release        = endpoint_fops_release,
	.mmap           = endpoint_fops_mmap,
	.unlocked_ioctl = endpoint_fops_ioctl,
	.poll           = endpoint_fops_poll,
	.llseek         = noop_llseek,
};

/* Clean up the endpoint devices. */
static int
remove_endpoint_device(struct endpoint_drv_ctx_t *eps_ctx,
		       struct endpoint_t *endpoint)
{
	int ret = 0;

	if (!eps_ctx || !endpoint)
		return ret;

	pci_client_unregister_for_link_event(endpoint->pci_client_h,
					     endpoint->linkevent_id);
	free_syncpoint(eps_ctx, endpoint);
	free_memory(eps_ctx, endpoint);
	if (endpoint->device) {
		device_destroy(eps_ctx->class, endpoint->dev);
		cdev_del(&endpoint->cdev);
		endpoint->device = NULL;
	}
	mutex_destroy(&endpoint->fops_lock);
	return ret;
}

/* Create the nvscic2c-pcie endpoint devices for the user-space to:
 * - Map the endpoints Self and Peer area.
 * - send notifications to remote/peer.
 * - receive notifications from peer.
 */
static int
create_endpoint_device(struct endpoint_drv_ctx_t *eps_ctx,
		       struct endpoint_t *endpoint)
{
	int ret = 0;
	struct callback_ops ops = {0};

	/* initialise the endpoint internals.*/
	mutex_init(&endpoint->fops_lock);
	atomic_set(&endpoint->in_use, 0);
	atomic_set(&endpoint->shutdown, 0);
	init_waitqueue_head(&endpoint->poll_waitq);
	init_waitqueue_head(&endpoint->close_waitq);

	/* create the nvscic2c endpoint char device.*/
	endpoint->dev = MKDEV(MAJOR(eps_ctx->char_dev), endpoint->minor);
	cdev_init(&endpoint->cdev, &endpoint_fops);
	endpoint->cdev.owner = THIS_MODULE;
	ret = cdev_add(&endpoint->cdev, endpoint->dev, 1);
	if (ret != 0) {
		pr_err("(%s): cdev_add() failed\n", endpoint->name);
		goto err;
	}
	/* parent is this hvd dev */
	endpoint->device = device_create(eps_ctx->class, NULL,
					 endpoint->dev, endpoint,
					 endpoint->name);
	if (IS_ERR(endpoint->device)) {
		cdev_del(&endpoint->cdev);
		ret = PTR_ERR(endpoint->device);
		pr_err("(%s): device_create() failed\n", endpoint->name);
		goto err;
	}
	dev_set_drvdata(endpoint->device, endpoint);

	/* allocate physical pages for the endpoint PCIe BAR (rx) area.*/
	ret = allocate_memory(eps_ctx, endpoint);
	if (ret) {
		pr_err("(%s): Failed to allocate physical pages\n",
		       endpoint->name);
		goto err;
	}

	/* allocate syncpoint for notification.*/
	ret = allocate_syncpoint(eps_ctx, endpoint);
	if (ret) {
		pr_err("(%s): Failed to allocate syncpt shim for notifications\n",
		       endpoint->name);
		goto err;
	}

	/* Register for link events.*/
	ops.callback = &(event_callback);
	ops.ctx = (void *)(endpoint);
	ret = pci_client_register_for_link_event(endpoint->pci_client_h, &ops,
						 &endpoint->linkevent_id);
	if (ret) {
		pr_err("(%s): Failed to register for PCIe link events\n",
		       endpoint->name);
		goto err;
	}

	/* all okay.*/
	return ret;
err:
	remove_endpoint_device(eps_ctx, endpoint);
	return ret;
}

/*
 * Entry point for the endpoint(s) char device sub-module/abstraction.
 *
 * On successful return (0), devices would have been created and ready to
 * accept ioctls from user-space application.
 */
int
endpoints_setup(struct driver_ctx_t *drv_ctx, void **endpoints_h)
{
	u32 i = 0;
	int ret = 0;
	struct endpoint_t *endpoint = NULL;
	struct endpoint_prop_t *ep_prop = NULL;
	struct endpoint_drv_ctx_t *eps_ctx = NULL;
	struct stream_ext_params *stream_ext_params = NULL;

	/* this cannot be initialized again.*/
	if (WARN_ON(!drv_ctx || !endpoints_h || *endpoints_h))
		return -EINVAL;

	if (WARN_ON(drv_ctx->drv_param.nr_endpoint == 0 ||
		    drv_ctx->drv_param.nr_endpoint > MAX_ENDPOINTS))
		return -EINVAL;

	if (WARN_ON(strlen(drv_ctx->drv_name) > (NAME_MAX - 1)))
		return -EINVAL;

	/* start by allocating the endpoint driver (global for all eps) ctx.*/
	eps_ctx = kzalloc(sizeof(*eps_ctx), GFP_KERNEL);
	if (WARN_ON(!eps_ctx))
		return -ENOMEM;

	eps_ctx->nr_endpoint = drv_ctx->drv_param.nr_endpoint;
	eps_ctx->of_node = drv_ctx->drv_param.of_node;
	strcpy(eps_ctx->drv_name, drv_ctx->drv_name);
	init_waitqueue_head(&eps_ctx->eps_close_waitq);

	/* allocate the whole chardev range */
	ret = alloc_chrdev_region(&eps_ctx->char_dev, 0,
				  eps_ctx->nr_endpoint, eps_ctx->drv_name);
	if (ret < 0)
		goto err;

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	eps_ctx->class = class_create(eps_ctx->drv_name);
#else
	eps_ctx->class = class_create(THIS_MODULE, eps_ctx->drv_name);
#endif
	if (IS_ERR_OR_NULL(eps_ctx->class)) {
		ret = PTR_ERR(eps_ctx->class);
		goto err;
	}

	/* allocate char devices context for supported endpoints.*/
	eps_ctx->endpoints = kzalloc((eps_ctx->nr_endpoint *
				      sizeof(*eps_ctx->endpoints)),
				     GFP_KERNEL);
	if (WARN_ON(!eps_ctx->endpoints)) {
		ret = -ENOMEM;
		goto err;
	}

	/* create char devices for each endpoint.*/
	for (i = 0; i < eps_ctx->nr_endpoint; i++) {
		endpoint = &eps_ctx->endpoints[i];
		ep_prop = &drv_ctx->drv_param.endpoint_props[i];
		stream_ext_params = &endpoint->stream_ext_params;

		/* copy the parameters from nvscic2c-pcie driver ctx.*/
		strcpy(endpoint->name, ep_prop->name);
		endpoint->minor = ep_prop->id;
		endpoint->nframes = ep_prop->nframes;
		endpoint->frame_sz = ep_prop->frame_sz;
		endpoint->pci_client_h = drv_ctx->pci_client_h;
		endpoint->eps_in_use = &eps_ctx->eps_in_use;
		endpoint->eps_close_waitq = &eps_ctx->eps_close_waitq;
		endpoint->host1x_pdev = drv_ctx->drv_param.host1x_pdev;
		/* set index of the msi-x interruper vector
		 * where the first one is reserved for comm-channel
		 */
		endpoint->msi_irq = i + 1;
		stream_ext_params->local_node = &drv_ctx->drv_param.local_node;
		stream_ext_params->peer_node = &drv_ctx->drv_param.peer_node;
		stream_ext_params->host1x_pdev = drv_ctx->drv_param.host1x_pdev;
		stream_ext_params->pci_client_h = drv_ctx->pci_client_h;
		stream_ext_params->comm_channel_h = drv_ctx->comm_channel_h;
		stream_ext_params->vmap_h = drv_ctx->vmap_h;
		stream_ext_params->edma_h = drv_ctx->edma_h;
		stream_ext_params->ep_id = ep_prop->id;
		stream_ext_params->ep_name = endpoint->name;
		stream_ext_params->drv_mode = drv_ctx->drv_mode;

		/* create nvscic2c-pcie endpoint device.*/
		ret = create_endpoint_device(eps_ctx, endpoint);
		if (ret)
			goto err;
	}

	*endpoints_h = eps_ctx;
	return ret;
err:
	endpoints_release((void **)&eps_ctx);
	return ret;
}

/* Exit point for nvscic2c-pcie endpoints: Wait for all endpoints to close.*/
#define MAX_WAITFOR_CLOSE_TIMEOUT_MSEC	(5000)
int
endpoints_waitfor_close(void *endpoints_h)
{
	u32 i = 0;
	int ret = 0;
	long timeout = 0;
	struct endpoint_drv_ctx_t *eps_ctx =
				 (struct endpoint_drv_ctx_t *)endpoints_h;

	if (!eps_ctx || !eps_ctx->endpoints)
		return ret;

	/*
	 * Signal all endpoints about exit/shutdown. This also doesn't
	 * allow them to be opened again unless reinitialized.
	 */
	for (i = 0; i < eps_ctx->nr_endpoint; i++) {
		struct endpoint_t *endpoint = &eps_ctx->endpoints[i];

		atomic_set(&endpoint->shutdown, 1);

		/* allow fops_open() or fops_release() to complete.*/
		mutex_lock(&endpoint->fops_lock);
		mutex_unlock(&endpoint->fops_lock);
	}

	/* wait for endpoints to be closed. */
	while (timeout == 0) {
		timeout = wait_event_interruptible_timeout
				(eps_ctx->eps_close_waitq,
				 !(atomic_read(&eps_ctx->eps_in_use)),
				 msecs_to_jiffies(MAX_WAITFOR_CLOSE_TIMEOUT_MSEC));

		for (i = 0; i < eps_ctx->nr_endpoint; i++) {
			struct endpoint_t *endpoint = &eps_ctx->endpoints[i];

			if (atomic_read(&endpoint->in_use)) {
				if (timeout == -ERESTARTSYS) {
					ret = timeout;
					pr_err("(%s): Wait for endpoint:(%s) close - Interrupted\n",
					       eps_ctx->drv_name, endpoint->name);
				} else if (timeout == 0) {
					pr_err("(%s): Still waiting for endpoint:(%s) to close\n",
					       eps_ctx->drv_name, endpoint->name);
				} else {
					/* erroneous case - should not happen.*/
					ret = -EFAULT;
					pr_err("(%s): Error: Endpoint: (%s) is still open\n",
					       eps_ctx->drv_name, endpoint->name);
				}
			}
		}
	}

	return ret;
}

/* exit point for nvscic2c-pcie endpoints char device sub-module/abstraction.*/
int
endpoints_release(void **endpoints_h)
{
	u32 i = 0;
	int ret = 0;
	struct endpoint_drv_ctx_t *eps_ctx =
				 (struct endpoint_drv_ctx_t *)(*endpoints_h);
	if (!eps_ctx)
		return ret;

	/* all endpoints must be closed.*/
	if (atomic_read(&eps_ctx->eps_in_use))
		pr_err("(%s): Unexpected. Endpoint(s) are still in-use.\n",
		       eps_ctx->drv_name);

	/* remove all the endpoints char devices.*/
	if (eps_ctx->endpoints) {
		for (i = 0; i < eps_ctx->nr_endpoint; i++) {
			struct endpoint_t *endpoint =
						 &eps_ctx->endpoints[i];
			remove_endpoint_device(eps_ctx, endpoint);
		}
		kfree(eps_ctx->endpoints);
		eps_ctx->endpoints = NULL;
	}

	if (eps_ctx->class) {
		class_destroy(eps_ctx->class);
		eps_ctx->class = NULL;
	}

	if (eps_ctx->char_dev) {
		unregister_chrdev_region(eps_ctx->char_dev,
					 eps_ctx->nr_endpoint);
		eps_ctx->char_dev = 0;
	}

	kfree(eps_ctx);
	*endpoints_h = NULL;

	return ret;
}
