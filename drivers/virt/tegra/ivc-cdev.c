/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <soc/tegra/virt/hv-ivc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <soc/tegra/fuse.h>

#include <uapi/linux/tegra-ivc-dev.h>
#include "tegra_hv.h"

#define ERR(...) pr_err("ivc-cdev: " __VA_ARGS__)
#define INFO(...) pr_info("ivc-cdev: " __VA_ARGS__)
#define DBG(...) pr_debug("ivc-cdev: " __VA_ARGS__)

#define INVALID_VMID 0xFFFFFFFF

struct ivc_dev {
	int			minor;
	dev_t			dev;
	struct cdev		cdev;
	struct device		*device;
	char			name[32];

	/* channel configuration */
	struct tegra_hv_ivc_cookie *ivck;
	const struct tegra_hv_queue_data *qd;

	/* File mode */
	wait_queue_head_t	wq;
	/*
	 * Lock for synchronizing access to the IVC channel between the threaded
	 * IRQ handler's notification processing and file ops.
	 */
	struct mutex		file_lock;
	/* Bool to store whether we received any ivc interrupt */
	bool			ivc_intr_rcvd;
};

static dev_t ivc_dev;
static const struct ivc_info_page *s_infop;
/* setup_ivc() set guest id */
static uint32_t s_guestid = INVALID_VMID;


static irqreturn_t ivc_dev_handler(int irq, void *data)
{
	struct ivc_dev *ivcd = data;

	WARN_ON(!ivcd->ivck);

	mutex_lock(&ivcd->file_lock);
	ivcd->ivc_intr_rcvd = true;
	mutex_unlock(&ivcd->file_lock);

	/* simple implementation, just kick all waiters */
	wake_up_interruptible_all(&ivcd->wq);

	return IRQ_HANDLED;
}

static irqreturn_t ivc_threaded_irq_handler(int irq, void *dev_id)
{
	/*
	 * Virtual IRQs are known to be edge-triggered, so no action is needed
	 * to acknowledge them.
	 */
	return IRQ_WAKE_THREAD;
}

static int ivc_dev_open(struct inode *inode, struct file *filp)
{
	struct cdev *cdev = inode->i_cdev;
	struct ivc_dev *ivcd = container_of(cdev, struct ivc_dev, cdev);
	int ret;
	struct tegra_hv_ivc_cookie *ivck;
	struct tegra_ivc *ivcq;

	/*
	 * If we can reserve the corresponding IVC device successfully, then
	 * we have exclusive access to the ivc device.
	 */
	ivck = tegra_hv_ivc_reserve(NULL, ivcd->minor, NULL);
	if (IS_ERR(ivck))
		return PTR_ERR(ivck);

	ivcd->ivck = ivck;
	ivcq = tegra_hv_ivc_convert_cookie(ivck);

	/* request our irq */
	ret = devm_request_threaded_irq(ivcd->device, ivck->irq,
			ivc_threaded_irq_handler, ivc_dev_handler, 0,
			dev_name(ivcd->device), ivcd);
	if (ret < 0) {
		dev_err(ivcd->device, "Failed to request irq %d\n",
				ivck->irq);
		ivcd->ivck = NULL;
		tegra_hv_ivc_unreserve(ivck);
		return ret;
	}

	/* all done */
	filp->private_data = ivcd;

	return 0;
}

static int ivc_dev_release(struct inode *inode, struct file *filp)
{
	struct ivc_dev *ivcd = filp->private_data;
	struct tegra_hv_ivc_cookie *ivck;

	filp->private_data = NULL;

	WARN_ON(!ivcd);

	ivck = ivcd->ivck;

	devm_free_irq(ivcd->device, ivck->irq, ivcd);

	ivcd->ivck = NULL;

	/*
	 * Unreserve after clearing ivck; we no longer have exclusive
	 * access at this point.
	 */
	tegra_hv_ivc_unreserve(ivck);

	return 0;
}

/*
 * Read/Write are not supported on ivc devices as it is now
 * accessed via NvSciIpc library.
 */
static ssize_t ivc_dev_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	return -EPERM;
}

static ssize_t ivc_dev_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *pos)
{
	return -EPERM;
}

static __poll_t ivc_dev_poll(struct file *filp, poll_table *wait)
{
	struct ivc_dev *ivcd = filp->private_data;
	struct tegra_ivc *ivc;
	__poll_t mask = 0;

	WARN_ON(!ivcd);
	ivc = tegra_hv_ivc_convert_cookie(ivcd->ivck);

	poll_wait(filp, &ivcd->wq, wait);

	/* If we have rcvd ivc interrupt, inform the user */
	mutex_lock(&ivcd->file_lock);
	if (ivcd->ivc_intr_rcvd == true) {
		mask |= ((__force __poll_t)POLLIN) | ((__force __poll_t)POLLRDNORM);
		ivcd->ivc_intr_rcvd = false;
	}
	mutex_unlock(&ivcd->file_lock);
	/* no exceptions */

	return mask;
}

static int ivc_dev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct ivc_dev *ivcd = filp->private_data;
	uint64_t map_region_sz;
	uint64_t ivc_area_ipa, ivc_area_size;
	int ret = -EFAULT;

	WARN_ON(!ivcd);

	ret = tegra_hv_ivc_get_info(ivcd->ivck, &ivc_area_ipa, &ivc_area_size);
	if (ret < 0) {
		dev_err(ivcd->device, "%s: get_info failed\n", __func__);
		return ret;
	}

	/* fail if userspace attempts to partially map the mempool */
	map_region_sz = vma->vm_end - vma->vm_start;

	if (((vma->vm_pgoff == 0) && (map_region_sz == ivc_area_size))) {

		if (remap_pfn_range(vma, vma->vm_start,
					(ivc_area_ipa >> PAGE_SHIFT),
					map_region_sz,
					vma->vm_page_prot)) {
			ret = -EAGAIN;
		} else {
			/* success! */
			ret = 0;
		}
#ifdef SUPPORTS_TRAP_MSI_NOTIFICATION
	} else if ((vma->vm_pgoff == (ivc_area_size >> PAGE_SHIFT)) &&
			(map_region_sz <= PAGE_SIZE)) {
		uint64_t noti_ipa = 0;

		if (ivcd->qd->msi_ipa != 0)
			noti_ipa = ivcd->qd->msi_ipa;
		else if (ivcd->qd->trap_ipa != 0)
			noti_ipa = ivcd->qd->trap_ipa;

		if (noti_ipa != 0) {
			if (remap_pfn_range(vma, vma->vm_start,
						noti_ipa >> PAGE_SHIFT,
						map_region_sz,
						vma->vm_page_prot)) {
				ret = -EAGAIN;
			} else {
				/* success! */
				ret = 0;
			}
		}
#endif /* SUPPORTS_TRAP_MSI_NOTIFICATION */
	}

	return ret;
}

/* Need this temporarily to get the change merged. Will be removed later */
#define NVIPC_IVC_IOCTL_GET_INFO_LEGACY 0xC018AA01
#define NVIPC_IVC_IOCTL_NOTIFY_REMOTE_LEGACY 0xC018AA02
static long ivc_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct ivc_dev *ivcd = filp->private_data;
	struct nvipc_ivc_info info;
	uint64_t ivc_area_ipa, ivc_area_size;
	long ret = 0;

	/* validate the cmd */
	if (_IOC_TYPE(cmd) != NVIPC_IVC_IOCTL_MAGIC) {
		dev_err(ivcd->device, "%s: not a ivc ioctl\n", __func__);
		ret = -ENOTTY;
		goto exit;
	}

	if (_IOC_NR(cmd) > NVIPC_IVC_IOCTL_NUMBER_MAX) {
		dev_err(ivcd->device, "%s: wrong ivc ioctl\n", __func__);
		ret = -ENOTTY;
		goto exit;
	}

	if (s_guestid == INVALID_VMID) {
		ERR("VMID is NOT initialized yet");
		ret = -EFAULT;
		goto exit;
	}

	switch (cmd) {
	case NVIPC_IVC_IOCTL_GET_INFO:
	case NVIPC_IVC_IOCTL_GET_INFO_LEGACY:
		ret = tegra_hv_ivc_get_info(ivcd->ivck, &ivc_area_ipa,
				&ivc_area_size);
		if (ret < 0) {
			dev_err(ivcd->device, "%s: get_info failed\n",
				__func__);
			goto exit;
		}

		info.nframes = ivcd->qd->nframes;
		info.frame_size = ivcd->qd->frame_size;
		info.queue_size = ivcd->qd->size;
		info.queue_offset = ivcd->qd->offset;
		info.area_size = ivc_area_size;
#ifdef SUPPORTS_TRAP_MSI_NOTIFICATION
		if (ivcd->qd->msi_ipa != 0) {
			info.noti_ipa = ivcd->qd->msi_ipa;
			info.noti_type = IVC_MSI_IPA;
		} else {
			info.noti_ipa = ivcd->qd->trap_ipa;
			info.noti_type = IVC_TRAP_IPA;
		}

		info.noti_irq = ivcd->qd->raise_irq;
#endif /* SUPPORTS_TRAP_MSI_NOTIFICATION */

		if (ivcd->qd->peers[0] == ivcd->qd->peers[1]) {
			/*
			 * The queue ids of loopback queues are always
			 * consecutive, so the even-numbered one
			 * receives in the first area.
			 */
			info.rx_first = (ivcd->qd->id & 1) == 0;

		} else {
			info.rx_first = (s_guestid == ivcd->qd->peers[0]);
		}

		if (cmd == NVIPC_IVC_IOCTL_GET_INFO) {
			if (copy_to_user((void __user *) arg, &info,
				sizeof(struct nvipc_ivc_info))) {
				ret = -EFAULT;
			}
		} else {
			/* Added temporarily. will be removed */
			if (copy_to_user((void __user *) arg, &info,
				sizeof(struct nvipc_ivc_info) - 16)) {
				ret = -EFAULT;
			}
		}
		break;

	case NVIPC_IVC_IOCTL_NOTIFY_REMOTE:
	case NVIPC_IVC_IOCTL_NOTIFY_REMOTE_LEGACY:
		tegra_hv_ivc_notify(ivcd->ivck);
		break;

	case NVIPC_IVC_IOCTL_GET_VMID:
		if (copy_to_user((void __user *) arg, &s_guestid,
			sizeof(s_guestid))) {
			ret = -EFAULT;
		}
		break;

	default:
		ret = -ENOTTY;
	}

exit:
	return ret;
}

static const struct file_operations ivc_fops = {
	.owner		= THIS_MODULE,
	.open		= ivc_dev_open,
	.release	= ivc_dev_release,
	.llseek		= noop_llseek,
	.read		= ivc_dev_read,
	.write		= ivc_dev_write,
	.mmap		= ivc_dev_mmap,
	.poll		= ivc_dev_poll,
	.unlocked_ioctl = ivc_dev_ioctl,
};

static ssize_t id_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ivc_dev *ivc = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", ivc->qd->id);
}

static ssize_t frame_size_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ivc_dev *ivc = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", ivc->qd->frame_size);
}

static ssize_t nframes_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ivc_dev *ivc = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", ivc->qd->nframes);
}

static ssize_t reserved_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tegra_hv_ivc_cookie *ivck;
	struct ivc_dev *ivc = dev_get_drvdata(dev);
	int reserved;

	ivck = tegra_hv_ivc_reserve(NULL, ivc->minor, NULL);
	if (IS_ERR(ivck))
		reserved = 1;
	else {
		tegra_hv_ivc_unreserve(ivck);
		reserved = 0;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", reserved);
}

static ssize_t peer_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ivc_dev *ivc = dev_get_drvdata(dev);

	if (s_guestid == INVALID_VMID) {
		ERR("VMID is NOT initialized yet");
		return 0;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", (ivc->qd->peers[0] == s_guestid)
			? ivc->qd->peers[1] : ivc->qd->peers[0]);
}

static DEVICE_ATTR_RO(id);
static DEVICE_ATTR_RO(frame_size);
static DEVICE_ATTR_RO(nframes);
static DEVICE_ATTR_RO(reserved);
static DEVICE_ATTR_RO(peer);

static struct attribute *ivc_attrs[] = {
	&dev_attr_id.attr,
	&dev_attr_frame_size.attr,
	&dev_attr_nframes.attr,
	&dev_attr_peer.attr,
	&dev_attr_reserved.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ivc);

static dev_t ivc_dev;
static uint32_t max_qid;
static struct ivc_dev *ivc_dev_array;
static struct class *ivc_class;

static int __init add_ivc(int i)
{
	const struct tegra_hv_queue_data *qd = &ivc_info_queue_array(s_infop)[i];
	struct ivc_dev *ivc = &ivc_dev_array[i];
	int ret;

	ivc->minor = qd->id;
	ivc->dev = MKDEV(MAJOR(ivc_dev), qd->id);
	ivc->qd = qd;

	cdev_init(&ivc->cdev, &ivc_fops);
	ret = snprintf(ivc->name, sizeof(ivc->name) - 1, "ivc%d", qd->id);
	if (ret < 0) {
		ERR("snprintf() failed\n");
		return ret;
	}

	ret = cdev_add(&ivc->cdev, ivc->dev, 1);
	if (ret != 0) {
		ERR("cdev_add() failed\n");
		return ret;
	}

	mutex_init(&ivc->file_lock);
	init_waitqueue_head(&ivc->wq);

	/* parent is this hvd dev */
	ivc->device = device_create(ivc_class, NULL, ivc->dev, ivc,
			ivc->name);
	if (IS_ERR(ivc->device)) {
		ERR("device_create() failed for %s\n", ivc->name);
		return PTR_ERR(ivc->device);
	}
	/* point to ivc */
	dev_set_drvdata(ivc->device, ivc);

	return 0;
}

static int __init setup_ivc(void)
{
	uint32_t i;
	int32_t id;
	int result;

	if (s_infop == NULL) {
		ERR("ivc info is not initialized");
		return -EFAULT;
	}

	max_qid = 0;
	for (i = 0; i < s_infop->nr_queues; i++) {
		const struct tegra_hv_queue_data *qd =
				&ivc_info_queue_array(s_infop)[i];
		if (qd->id > max_qid)
			max_qid = qd->id;
	}

	/* allocate the whole chardev range */
	result = alloc_chrdev_region(&ivc_dev, 0, max_qid, "ivc");
	if (result < 0) {
		ERR("alloc_chrdev_region() failed\n");
		return result;
	}

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	ivc_class = class_create("ivc");
#else
	ivc_class = class_create(THIS_MODULE, "ivc");
#endif
	if (IS_ERR(ivc_class)) {
		ERR("failed to create ivc class: %ld\n", PTR_ERR(ivc_class));
		return PTR_ERR(ivc_class);
	}
	ivc_class->dev_groups = ivc_groups;

	ivc_dev_array = kcalloc(s_infop->nr_queues, sizeof(*ivc_dev_array),
			GFP_KERNEL);
	if (!ivc_dev_array) {
		ERR("failed to allocate ivc_dev_array");
		return -ENOMEM;
	}

	/*
	 * Make a second pass through the queues to instantiate the char devs
	 * corresponding to existent queues.
	 */
	for (i = 0; i < s_infop->nr_queues; i++) {
		result = add_ivc(i);
		if (result != 0)
			return result;
	}

	id = tegra_hv_get_vmid();
	if (id < 0) {
		ERR("failed to get VMID");
		return -EFAULT;
	}
	s_guestid = (uint32_t)id;

	INFO("guest ID: %d\n", s_guestid);

	return 0;
}

static void cleanup_ivc(void)
{
	uint32_t i;

	if (ivc_dev_array && (s_infop != NULL)) {
		for (i = 0; i < s_infop->nr_queues; i++) {
			struct ivc_dev *ivc = &ivc_dev_array[i];

			if (ivc->device) {
				cdev_del(&ivc->cdev);
				device_del(ivc->device);
			}
		}
		kfree(ivc_dev_array);
		ivc_dev_array = NULL;
	}

	if (!IS_ERR_OR_NULL(ivc_class)) {
		class_destroy(ivc_class);
		ivc_class = NULL;
	}

	if (ivc_dev) {
		unregister_chrdev_region(ivc_dev, max_qid);
		ivc_dev = 0;
	}
}

static int __init ivc_init(void)
{
	int result;

	if (is_tegra_hypervisor_mode() == false) {
		INFO("hypervisor not present\n");
		/*retunring success in case of native kernel otherwise
		  systemd-modules-load service will failed.*/
		return 0;
	}

	s_infop = tegra_hv_get_ivc_info();
	if (IS_ERR(s_infop))
		return -ENODEV;

	result = setup_ivc();
	if (result != 0)
		cleanup_ivc();

	return result;
}

static void __exit ivc_exit(void)
{
	if (is_tegra_hypervisor_mode() == false) {
		INFO("hypervisor not present\n");
		return;
	}

	cleanup_ivc();
}

int ivc_cdev_get_peer_vmid(uint32_t qid, uint32_t *peer_vmid)
{
	uint32_t i;
	int32_t ret = -ENOENT;

	if ((s_infop == NULL) || (s_guestid == INVALID_VMID)) {
		ERR("ivc info or VMID is NOT initialized yet");
		ret = -EFAULT;
		goto exit;
	}

	for (i = 0; i < s_infop->nr_queues; i++) {
		struct ivc_dev *ivc = &ivc_dev_array[i];

		if (ivc->qd->id == qid) {
			if (ivc->qd->peers[0] == s_guestid)
				*peer_vmid = ivc->qd->peers[1];
			else
				*peer_vmid = ivc->qd->peers[0];
			ret = 0;
			DBG("found qid %d: peer_vmid=%d\n", qid, *peer_vmid);
			break;
		}
	}

	if (ret != 0)
		INFO("qid %d not found\n", qid);

exit:
	return ret;
}
EXPORT_SYMBOL(ivc_cdev_get_peer_vmid);

int ivc_cdev_get_noti_type(uint32_t qid, uint32_t *noti_type)
{
	uint32_t i;
	int32_t ret = -ENOENT;

	if ((s_infop == NULL) || (s_guestid == INVALID_VMID)) {
		ERR("ivc info or VMID is NOT initialized yet");
		ret = -EFAULT;
		goto exit;
	}

	for (i = 0; i < s_infop->nr_queues; i++) {
		struct ivc_dev *ivc = &ivc_dev_array[i];

		if (ivc->qd->id == qid) {
			if (ivc->qd->msi_ipa != 0)
				*noti_type = IVC_MSI_IPA;
			else
				*noti_type = IVC_TRAP_IPA;
			ret = 0;
			DBG("found qid %d: noti_type=%d\n", qid, *noti_type);
			break;
		}
	}

	if (ret != 0)
		INFO("qid %d not found\n", qid);

exit:
	return ret;
}
EXPORT_SYMBOL(ivc_cdev_get_noti_type);

module_init(ivc_init);
module_exit(ivc_exit);

MODULE_LICENSE("GPL");
