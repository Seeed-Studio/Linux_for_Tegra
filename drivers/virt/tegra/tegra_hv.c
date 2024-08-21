// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/of_irq.h>

#include <soc/tegra/virt/syscalls.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <soc/tegra/virt/tegra_hv.h>
#include <soc/tegra/ivc.h>

#define ERR(...) pr_err("tegra_hv: " __VA_ARGS__)
#define INFO(...) pr_info("tegra_hv: " __VA_ARGS__)
#define DRV_NAME	"tegra_hv"

struct tegra_hv_data;

static struct property interrupts_prop = {
	.name = "interrupts",
};

struct hv_ivc {
	struct tegra_hv_data	*hvd;

	/*
	 * ivc_devs are stored in an id-indexed array; this field indicates
	 * a valid array entry.
	 */
	int			valid;

	/* channel configuration */
	struct tegra_ivc	ivc;
	const struct tegra_hv_queue_data *qd;
	const struct ivc_shared_area *area;
	const struct guest_ivc_info *givci;
	int			other_guestid;

	const struct tegra_hv_ivc_ops *cookie_ops;
	struct tegra_hv_ivc_cookie cookie;

	/* This lock synchronizes the reserved flag. */
	struct mutex		lock;
	int			reserved;

	char			name[16];
	int			irq;
};

#define cookie_to_ivc_dev(_cookie) \
	container_of(_cookie, struct hv_ivc, cookie)

/* Describe all info needed to do IVC to one particular guest */
struct guest_ivc_info {
	uintptr_t shmem;	/* IO remapped shmem */
	size_t length;		/* length of shmem */
};

struct hv_mempool {
	struct tegra_hv_ivm_cookie ivmk;
	const struct ivc_mempool *mpd;
	struct mutex lock;
	int reserved;
};

struct tegra_hv_data {
	const struct ivc_info_page *info;
	int guestid;

	struct guest_ivc_info *guest_ivc_info;

	/* ivc_devs is indexed by queue id */
	struct hv_ivc *ivc_devs;
	uint32_t max_qid;

	/* array with length info->nr_mempools */
	struct hv_mempool *mempools;

	struct class *hv_class;

	struct device_node *dev;
};

/*
 * Global HV state for read-only access by tegra_hv_... APIs
 *
 * This should be accessed only through get_hvd().
 */
static struct tegra_hv_data *tegra_hv_data;

struct ivc_notify_info {
	// Trap based notification
	uintptr_t trap_region_base_va;
	uintptr_t trap_region_base_ipa;
	uintptr_t trap_region_end_ipa;
	uint64_t trap_region_size;
	// MSI based notification
	uintptr_t msi_region_base_va;
	uintptr_t msi_region_base_ipa;
	uintptr_t msi_region_end_ipa;
	uint64_t msi_region_size;
};

static struct ivc_notify_info ivc_notify;

bool is_tegra_hypervisor_mode(void)
{
#ifdef CONFIG_OF
	return of_property_read_bool(of_chosen,
		"nvidia,tegra-hypervisor-mode");
#else
	return false;
#endif
}
EXPORT_SYMBOL(is_tegra_hypervisor_mode);

static void ivc_raise_irq(struct tegra_ivc *ivc_channel, void *data)
{
	struct hv_ivc *ivc = container_of(ivc_channel, struct hv_ivc, ivc);

	if (WARN_ON(!ivc->cookie.notify_va))
		return;

	*ivc->cookie.notify_va = ivc->qd->raise_irq;
}

static struct tegra_hv_data *get_hvd(void)
{
	if (!tegra_hv_data) {
		INFO("%s: not initialized yet\n", __func__);
		return ERR_PTR(-EPROBE_DEFER);
	} else {
		return tegra_hv_data;
	}
}

const struct ivc_info_page *tegra_hv_get_ivc_info(void)
{
	struct tegra_hv_data *hvd = get_hvd();

	if (IS_ERR(hvd))
		return (void *)hvd;
	else
		return tegra_hv_data->info;
}
EXPORT_SYMBOL(tegra_hv_get_ivc_info);

int tegra_hv_get_vmid(void)
{
	struct tegra_hv_data *hvd = get_hvd();

	if (IS_ERR(hvd))
		return -1;
	else
		return hvd->guestid;
}
EXPORT_SYMBOL(tegra_hv_get_vmid);

static void ivc_handle_notification(struct hv_ivc *ivc)
{
	struct tegra_hv_ivc_cookie *ivck = &ivc->cookie;

	/* This function should only be used when callbacks are specified. */
	BUG_ON(!ivc->cookie_ops);

	/* there are data in the queue, callback */
	if (ivc->cookie_ops->rx_rdy && tegra_ivc_can_read(&ivc->ivc))
		ivc->cookie_ops->rx_rdy(ivck);

	/* there is space in the queue to write, callback */
	if (ivc->cookie_ops->tx_rdy && tegra_ivc_can_write(&ivc->ivc))
		ivc->cookie_ops->tx_rdy(ivck);
}

static irqreturn_t ivc_dev_cookie_irq_handler(int irq, void *data)
{
	struct hv_ivc *ivcd = data;

	ivc_handle_notification(ivcd);
	return IRQ_HANDLED;
}

static void ivc_release_irq(struct hv_ivc *ivc)
{
	BUG_ON(!ivc);

	free_irq(ivc->irq, ivc);
}

static int ivc_request_cookie_irq(struct hv_ivc *ivcd)
{
	return request_irq(ivcd->irq, ivc_dev_cookie_irq_handler, 0,
			ivcd->name, ivcd);
}

static int tegra_hv_add_ivc(struct tegra_hv_data *hvd,
		const struct tegra_hv_queue_data *qd, uint32_t index)
{
	struct hv_ivc *ivc;
	int ret;
	int rx_first;
	struct iosys_map rx_map, tx_map;
	uint32_t i;
	struct irq_data *d;
	uint64_t va_offset;

	ivc = &hvd->ivc_devs[qd->id];
	BUG_ON(ivc->valid);
	ivc->valid = 1;
	ivc->hvd = hvd;
	ivc->qd = qd;

	if (qd->peers[0] == hvd->guestid)
		ivc->other_guestid = qd->peers[1];
	else if (qd->peers[1] == hvd->guestid)
		ivc->other_guestid = qd->peers[0];
	else
		BUG();

	/*
	 * Locate the guest_ivc_info representing the remote guest accessed
	 * through this channel.
	 */
	for (i = 0; i < hvd->info->nr_areas; i++) {
		if (hvd->info->areas[i].guest == ivc->other_guestid) {
			ivc->givci = &hvd->guest_ivc_info[i];
			ivc->area = &hvd->info->areas[i];
			break;
		}
	}
	BUG_ON(i == hvd->info->nr_areas);

	BUG_ON(ivc->givci->shmem == 0);

	mutex_init(&ivc->lock);

	if (qd->peers[0] == qd->peers[1]) {
		/*
		 * The queue ids of loopback queues are always consecutive, so
		 * the even-numbered one receives in the first area.
		 */
		rx_first = (qd->id & 1) == 0;
	} else {
		rx_first = hvd->guestid == qd->peers[0];
	}

	BUG_ON(qd->offset >= ivc->givci->length);
	BUG_ON(qd->offset + qd->size * 2 > ivc->givci->length);

	rx_map.is_iomem = false;
	tx_map.is_iomem = false;
	if (rx_first) {
		rx_map.vaddr = (void *)ivc->givci->shmem + qd->offset;
		tx_map.vaddr = (void *)ivc->givci->shmem + qd->offset + qd->size;
	} else {
		tx_map.vaddr = (void *)ivc->givci->shmem + qd->offset;
		rx_map.vaddr = (void *)ivc->givci->shmem + qd->offset + qd->size;
	}

	ret = snprintf(ivc->name, sizeof(ivc->name), "ivc%u", qd->id);
	if (ret < 0)
		return -EINVAL;

	ivc->irq = of_irq_get(hvd->dev, index);
	if (ivc->irq < 0) {
		ERR("Unable to get irq for ivc%u\n", qd->id);
		return ivc->irq;
	}
	d = irq_get_irq_data(ivc->irq);
	if (!d) {
		ERR("Failed to get data for irq %d (ivc%u)\n", ivc->irq,
				qd->id);
		return -ENODEV;
	}

	if (qd->msi_ipa != 0U) {
		if (WARN_ON(ivc_notify.msi_region_size == 0UL))
			return -EINVAL;
		if (WARN_ON(!(qd->msi_ipa >= ivc_notify.msi_region_base_ipa &&
			qd->msi_ipa <= ivc_notify.msi_region_end_ipa))) {
			return -EINVAL;
		}
		va_offset = qd->msi_ipa - ivc_notify.msi_region_base_ipa;
		ivc->cookie.notify_va =
			(uint32_t *)(ivc_notify.msi_region_base_va +
			va_offset);
	} else if (qd->trap_ipa != 0U) {
		if (WARN_ON(ivc_notify.trap_region_size == 0UL))
			return -EINVAL;
		if (WARN_ON(!(qd->trap_ipa >= ivc_notify.trap_region_base_ipa &&
			qd->trap_ipa <= ivc_notify.trap_region_end_ipa))) {
			return -EINVAL;
		}
		va_offset = qd->trap_ipa - ivc_notify.trap_region_base_ipa;
		ivc->cookie.notify_va =
			(uint32_t *)(ivc_notify.trap_region_base_va +
			va_offset);
	} else {
		if (WARN_ON(ivc->cookie.notify_va == NULL))
			return -EINVAL;
	}

	INFO("adding ivc%u: rx_base=%lx tx_base = %lx size=%x irq = %d (%lu)\n",
			qd->id, (uintptr_t)rx_map.vaddr, (uintptr_t)tx_map.vaddr, qd->size, ivc->irq, d->hwirq);
	ret = tegra_ivc_init(&ivc->ivc, NULL, &rx_map, 0, &tx_map, 0, qd->nframes,
			qd->frame_size, ivc_raise_irq, ivc);
	if (ret != 0) {
		ERR("failed to init ivc queue #%u\n", qd->id);
		return  ret;
	}

	/* We may have rebooted, so the channel could be active. */
	ret = tegra_ivc_channel_sync(&ivc->ivc);
	if (ret != 0)
		return  ret;

	INFO("added %s\n", ivc->name);

	return 0;
}

static struct hv_ivc *ivc_device_by_id(struct tegra_hv_data *hvd, uint32_t id)
{
	if (id > hvd->max_qid) {
		return NULL;
	} else {
		struct hv_ivc *ivc = &hvd->ivc_devs[id];

		if (ivc->valid)
			return ivc;
		else
			return NULL;
	}
}

static void tegra_hv_ivc_cleanup(struct tegra_hv_data *hvd)
{
	if (!hvd->ivc_devs)
		return;

	kfree(hvd->ivc_devs);
	hvd->ivc_devs = NULL;
}

static void tegra_hv_cleanup(struct tegra_hv_data *hvd)
{
	/*
	 * Destroying IVC channels in use is not supported. Once it's possible
	 * for IVC channels to be reserved, we no longer clean up.
	 */
	BUG_ON(tegra_hv_data != NULL);

	kfree(hvd->mempools);
	hvd->mempools = NULL;

	tegra_hv_ivc_cleanup(hvd);

	if (hvd->guest_ivc_info) {
		uint32_t i;

		BUG_ON(!hvd->info);
		for (i = 0; i < hvd->info->nr_areas; i++) {
			if (hvd->guest_ivc_info[i].shmem) {
				iounmap((void __iomem *)hvd->guest_ivc_info[i].shmem);
				hvd->guest_ivc_info[i].shmem = 0;
			}
		}

		kfree(hvd->guest_ivc_info);
		hvd->guest_ivc_info = NULL;

		iounmap((void __iomem *)hvd->info);
		hvd->info = NULL;
	}

	if (hvd->hv_class) {
		class_destroy(hvd->hv_class);
		hvd->hv_class = NULL;
	}
}

static ssize_t vmid_show(const struct class *class,
			 const struct class_attribute *attr, char *buf)
{
	struct tegra_hv_data *hvd = get_hvd();

	BUG_ON(!hvd);
	return snprintf(buf, PAGE_SIZE, "%d\n", hvd->guestid);
}
static CLASS_ATTR_RO(vmid);

static int tegra_hv_setup(struct tegra_hv_data *hvd)
{
	const int intr_property_size = 3;
	uint64_t info_page;
	uint32_t i;
	int ret;
	uint32_t *interrupts_arr;
	uint64_t ivcsize = 0;
	uint64_t mpsize = 0;

	hvd->dev = of_find_compatible_node(NULL, NULL, "nvidia,tegra-hv");
	if (!hvd->dev) {
		ERR("could not find hv node\n");
		return -ENODEV;
	}

	ret = hyp_read_gid(&hvd->guestid);
	if (ret != 0) {
		ERR("Failed to read guest id\n");
		return -ENODEV;
	}

	hvd->hv_class = class_create("tegra_hv");
	if (IS_ERR(hvd->hv_class)) {
		ERR("class_create() failed\n");
		return PTR_ERR(hvd->hv_class);
	}

	ret = class_create_file(hvd->hv_class, &class_attr_vmid);
	if (ret != 0) {
		ERR("failed to create vmid file: %d\n", ret);
		return ret;
	}

	ret = hyp_read_ivc_info(&info_page);
	if (ret != 0) {
		ERR("failed to obtain IVC info page: %d\n", ret);
		return ret;
	}

	hvd->info = (__force struct ivc_info_page *)ioremap_cache(info_page,
			IVC_INFO_PAGE_SIZE);
	if (hvd->info == NULL) {
		ERR("failed to map IVC info page (%llx)\n", info_page);
		return -ENOMEM;
	}

	/*
	 *  Map IVC Trap MMIO Notification region
	 */
	ivc_notify.trap_region_base_ipa = hvd->info->trap_region_base_ipa;
	ivc_notify.trap_region_size = hvd->info->trap_region_size;
	if (ivc_notify.trap_region_size != 0UL) {
		INFO("trap_region_base_ipa %lx: trap_region_size=%llx\n",
			ivc_notify.trap_region_base_ipa,
			ivc_notify.trap_region_size);
		if (WARN_ON(ivc_notify.trap_region_base_ipa == 0UL))
			return -EINVAL;
		if (WARN_ON(ivc_notify.trap_region_base_va != 0UL))
			return -EINVAL;
		ivc_notify.trap_region_end_ipa =
			ivc_notify.trap_region_base_ipa +
			ivc_notify.trap_region_size - 1UL;
		ivc_notify.trap_region_base_va =
			(uintptr_t)ioremap_cache(
				ivc_notify.trap_region_base_ipa,
				ivc_notify.trap_region_size);
		if (ivc_notify.trap_region_base_va == 0UL) {
			ERR("failed to map trap ipa notification page\n");
			return -ENOMEM;
		}
	}

	/*
	 *  Map IVC MSI Notification region
	 */
	ivc_notify.msi_region_base_ipa = hvd->info->msi_region_base_ipa;
	ivc_notify.msi_region_size = hvd->info->msi_region_size;
	if (ivc_notify.msi_region_size != 0UL) {
		INFO("msi_region_base_ipa %lx: msi_region_size=%llx\n",
			ivc_notify.msi_region_base_ipa,
			ivc_notify.msi_region_size);
		if (WARN_ON(ivc_notify.msi_region_base_ipa == 0UL))
			return -EINVAL;
		if (WARN_ON(ivc_notify.msi_region_base_va != 0UL))
			return -EINVAL;
		ivc_notify.msi_region_end_ipa = ivc_notify.msi_region_base_ipa +
				ivc_notify.msi_region_size - 1UL;
		ivc_notify.msi_region_base_va =
			(uintptr_t)ioremap_cache(ivc_notify.msi_region_base_ipa,
			ivc_notify.msi_region_size);
		if (ivc_notify.msi_region_base_va == 0UL) {
			ERR("failed to map msi ipa notification page\n");
			return -ENOMEM;
		}
	}

	hvd->guest_ivc_info = kzalloc(hvd->info->nr_areas *
			sizeof(*hvd->guest_ivc_info), GFP_KERNEL);
	if (hvd->guest_ivc_info == NULL) {
		ERR("failed to allocate %u-entry givci\n",
				hvd->info->nr_areas);
		return -ENOMEM;
	}

	for (i = 0; i < hvd->info->nr_areas; i++) {
		hvd->guest_ivc_info[i].shmem = (uintptr_t)ioremap_cache(
				hvd->info->areas[i].pa,
				hvd->info->areas[i].size);
		if (hvd->guest_ivc_info[i].shmem == 0) {
			ERR("can't map area for guest %u (%llx)\n",
					hvd->info->areas[i].guest,
					hvd->info->areas[i].pa);
			return -ENOMEM;
		}
		hvd->guest_ivc_info[i].length = hvd->info->areas[i].size;
	}

	/* Do not free this, of_add_property does not copy the structure */
	interrupts_arr = kmalloc(hvd->info->nr_queues * sizeof(uint32_t)
			* intr_property_size, GFP_KERNEL);
	if (interrupts_arr == NULL) {
		ERR("failed to allocate array for interrupts property\n");
		return -ENOMEM;
	}

	/*
	 * Determine the largest queue id in order to allocate a queue id-
	 * indexed array and device nodes, and create interrupts property
	 */
	hvd->max_qid = 0;
	for (i = 0; i < hvd->info->nr_queues; i++) {
		const struct tegra_hv_queue_data *qd =
				&ivc_info_queue_array(hvd->info)[i];
		if (qd->id > hvd->max_qid)
			hvd->max_qid = qd->id;
		/* 0 => SPI */
		interrupts_arr[(i * intr_property_size)] = (__force uint32_t)cpu_to_be32(0);
		interrupts_arr[(i * intr_property_size) + 1] =
			(__force uint32_t)cpu_to_be32(qd->irq - 32); /* Id in SPI namespace */
		/* 0x1 == low-to-high edge */
		interrupts_arr[(i * intr_property_size) + 2] = (__force uint32_t)cpu_to_be32(0x1);
	}

	interrupts_prop.length =
		hvd->info->nr_queues * sizeof(uint32_t) * intr_property_size;
	interrupts_prop.value = interrupts_arr;

	if (of_add_property(hvd->dev, &interrupts_prop)) {
		ERR("failed to add interrupts property\n");
		kfree(interrupts_arr);
		return -EACCES;
	}

	hvd->ivc_devs = kzalloc((hvd->max_qid + 1) * sizeof(*hvd->ivc_devs),
			GFP_KERNEL);
	if (hvd->ivc_devs == NULL) {
		ERR("failed to allocate %u-entry ivc_devs array\n",
				hvd->info->nr_queues);
		return -ENOMEM;
	}

	/* instantiate the IVC */
	for (i = 0; i < hvd->info->nr_queues; i++) {
		const struct tegra_hv_queue_data *qd =
				&ivc_info_queue_array(hvd->info)[i];
		ret = tegra_hv_add_ivc(hvd, qd, i);
		if (ret != 0) {
			ERR("failed to add queue #%u\n", qd->id);
			return ret;
		}

		ivcsize += qd->size;
		BUG_ON(ivcsize < qd->size);
	}

	hvd->mempools =
		kzalloc(hvd->info->nr_mempools * sizeof(*hvd->mempools),
								GFP_KERNEL);
	if (hvd->mempools == NULL) {
		ERR("failed to allocate %u-entry mempools array\n",
				hvd->info->nr_mempools);
		return -ENOMEM;
	}

	/* Initialize mempools. */
	for (i = 0; i < hvd->info->nr_mempools; i++) {
		const struct ivc_mempool *mpd =
				&ivc_info_mempool_array(hvd->info)[i];
		struct tegra_hv_ivm_cookie *ivmk = &hvd->mempools[i].ivmk;

		hvd->mempools[i].mpd = mpd;
		mutex_init(&hvd->mempools[i].lock);

		ivmk->ipa = mpd->pa;
		ivmk->size = mpd->size;
		ivmk->peer_vmid = mpd->peer_vmid;

		mpsize += mpd->size;
		BUG_ON(mpsize < mpd->size);

		INFO("added mempool %u: ipa=%llx size=%llx peer=%u\n",
				mpd->id, mpd->pa, mpd->size, mpd->peer_vmid);
	}

	INFO("Memory usage: ivc:0x%llx mempool=0x%llx\n", ivcsize, mpsize);

	return 0;
}

static int ivc_dump(struct hv_ivc *ivc)
{
	INFO("IVC#%d: IRQ=%d(%d) nframes=%d frame_size=%d offset=%d\n",
			ivc->qd->id, ivc->irq, ivc->qd->irq,
			ivc->qd->nframes, ivc->qd->frame_size, ivc->qd->offset);

	return 0;
}

struct tegra_hv_ivc_cookie *tegra_hv_ivc_reserve(struct device_node *dn,
		int id, const struct tegra_hv_ivc_ops *ops)
{
	struct tegra_hv_data *hvd = get_hvd();
	struct hv_ivc *ivc;
	struct tegra_hv_ivc_cookie *ivck;
	int ret;

	if (IS_ERR(hvd))
		return (void *)hvd;

	ivc = ivc_device_by_id(hvd, id);
	if (ivc == NULL)
		return ERR_PTR(-ENODEV);

	mutex_lock(&ivc->lock);
	if (ivc->reserved) {
		ret = -EBUSY;
	} else {
		ivc->reserved = 1;
		ret = 0;
	}
	mutex_unlock(&ivc->lock);

	if (ret != 0)
		return ERR_PTR(ret);

	ivc->cookie_ops = ops;

	ivck = &ivc->cookie;
	ivck->irq = ivc->irq;
	ivck->peer_vmid = ivc->other_guestid;
	ivck->nframes = ivc->qd->nframes;
	ivck->frame_size = ivc->qd->frame_size;

	if (ivc->cookie_ops) {
		ivc_handle_notification(ivc);
		/* request our irq */
		ret = ivc_request_cookie_irq(ivc);
		if (ret) {
			mutex_lock(&ivc->lock);
			BUG_ON(!ivc->reserved);
			ivc->reserved = 0;
			mutex_unlock(&ivc->lock);
			return ERR_PTR(ret);
		}
	}

	/* return pointer to the cookie */
	return ivck;
}
EXPORT_SYMBOL(tegra_hv_ivc_reserve);

void tegra_hv_ivc_notify(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return;

	ivc = cookie_to_ivc_dev(ivck);
	if (WARN_ON(!ivc->cookie.notify_va))
		return;
	*ivc->cookie.notify_va = ivc->qd->raise_irq;
}
EXPORT_SYMBOL(tegra_hv_ivc_notify);

int tegra_hv_ivc_get_info(struct tegra_hv_ivc_cookie *ivck, uint64_t *pa,
			  uint64_t *size)
{
	struct hv_ivc *ivc;
	int ret;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);

	mutex_lock(&ivc->lock);
	if (ivc->reserved) {
		*pa = ivc->area->pa;
		*size = ivc->area->size;
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	mutex_unlock(&ivc->lock);

	return ret;
}
EXPORT_SYMBOL(tegra_hv_ivc_get_info);

int tegra_hv_ivc_unreserve(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;
	int ret;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);

	mutex_lock(&ivc->lock);
	if (ivc->reserved) {
		if (ivc->cookie_ops)
			ivc_release_irq(ivc);
		ivc->cookie_ops = NULL;
		ivc->reserved = 0;
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	mutex_unlock(&ivc->lock);

	return ret;
}
EXPORT_SYMBOL(tegra_hv_ivc_unreserve);

int tegra_hv_ivc_write(struct tegra_hv_ivc_cookie *ivck, const void *buf,
		int size)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_write(&ivc->ivc, NULL, buf, size);
}
EXPORT_SYMBOL(tegra_hv_ivc_write);

int tegra_hv_ivc_write_user(struct tegra_hv_ivc_cookie *ivck, const void __user *buf,
		int size)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_write(&ivc->ivc, buf, NULL, size);
}
EXPORT_SYMBOL(tegra_hv_ivc_write_user);

int tegra_hv_ivc_read(struct tegra_hv_ivc_cookie *ivck, void *buf, int size)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_read(&ivc->ivc, NULL, buf, size);
}
EXPORT_SYMBOL(tegra_hv_ivc_read);

int tegra_hv_ivc_read_user(struct tegra_hv_ivc_cookie *ivck, void __user *buf, int size)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_read(&ivc->ivc, buf, NULL, size);
}
EXPORT_SYMBOL(tegra_hv_ivc_read_user);

int tegra_hv_ivc_read_peek(struct tegra_hv_ivc_cookie *ivck, void *buf,
			   int off, int count)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_read_peek(&ivc->ivc, NULL, buf, off, count);
}
EXPORT_SYMBOL(tegra_hv_ivc_read_peek);

int tegra_hv_ivc_can_read(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_can_read(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_can_read);

int tegra_hv_ivc_can_write(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_can_write(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_can_write);

int tegra_hv_ivc_tx_empty(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_empty(&ivc->ivc, &ivc->ivc.tx.map);
}
EXPORT_SYMBOL(tegra_hv_ivc_tx_empty);

uint32_t tegra_hv_ivc_tx_frames_available(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_frames_available(&ivc->ivc, &ivc->ivc.tx.map);
}
EXPORT_SYMBOL(tegra_hv_ivc_tx_frames_available);

int tegra_hv_ivc_dump(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return ivc_dump(ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_dump);

void *tegra_hv_ivc_read_get_next_frame(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);
	struct iosys_map map;
	int err;

	err = tegra_ivc_read_get_next_frame(&ivc->ivc, &map);
	if (err)
		return ERR_PTR(err);
	return map.vaddr;
}
EXPORT_SYMBOL(tegra_hv_ivc_read_get_next_frame);

void *tegra_hv_ivc_write_get_next_frame(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);
	struct iosys_map map;
	int err;

	err = tegra_ivc_write_get_next_frame(&ivc->ivc, &map);
	if (err)
		return ERR_PTR(err);

	return map.vaddr;
}
EXPORT_SYMBOL(tegra_hv_ivc_write_get_next_frame);

int tegra_hv_ivc_write_advance(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_write_advance(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_write_advance);

int tegra_hv_ivc_read_advance(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_read_advance(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_read_advance);

struct tegra_ivc *tegra_hv_ivc_convert_cookie(struct tegra_hv_ivc_cookie *ivck)
{
	return &cookie_to_ivc_dev(ivck)->ivc;
}
EXPORT_SYMBOL(tegra_hv_ivc_convert_cookie);

struct tegra_hv_ivm_cookie *tegra_hv_mempool_reserve(unsigned int id)
{
	uint32_t i;
	struct hv_mempool *mempool;
	int reserved;

	if (!tegra_hv_data)
		return ERR_PTR(-EPROBE_DEFER);

	/* Locate a mempool with matching id. */
	for (i = 0; i < tegra_hv_data->info->nr_mempools; i++) {
		mempool = &tegra_hv_data->mempools[i];
		if (mempool->mpd->id == id)
			break;
	}

	if (i == tegra_hv_data->info->nr_mempools)
		return ERR_PTR(-ENODEV);

	mutex_lock(&mempool->lock);
	reserved = mempool->reserved;
	mempool->reserved = 1;
	mutex_unlock(&mempool->lock);

	return reserved ? ERR_PTR(-EBUSY) : &mempool->ivmk;
}
EXPORT_SYMBOL(tegra_hv_mempool_reserve);

int tegra_hv_mempool_unreserve(struct tegra_hv_ivm_cookie *ivmk)
{
	int reserved;
	struct hv_mempool *mempool = container_of(ivmk, struct hv_mempool,
			ivmk);

	mutex_lock(&mempool->lock);
	reserved = mempool->reserved;
	mempool->reserved = 0;
	mutex_unlock(&mempool->lock);

	return reserved ? 0 : -EINVAL;
}
EXPORT_SYMBOL(tegra_hv_mempool_unreserve);

int tegra_hv_ivc_channel_notified(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	return tegra_ivc_notified(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_channel_notified);

void tegra_hv_ivc_channel_reset(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc = cookie_to_ivc_dev(ivck);

	if (ivc->cookie_ops) {
		ERR("reset unsupported with callbacks");
		BUG();
	}

	tegra_ivc_reset(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_channel_reset);

static int tegra_hv_probe(struct platform_device *pdev)
{
	struct tegra_hv_data *hvd;
	int ret;

	if (!is_tegra_hypervisor_mode())
		return -ENODEV;

	hvd = kzalloc(sizeof(*hvd), GFP_KERNEL);
	if (!hvd) {
		ERR("failed to allocate hvd\n");
		return -ENOMEM;
	}

	ret = tegra_hv_setup(hvd);
	if (ret != 0) {
		tegra_hv_cleanup(hvd);
		kfree(hvd);
		return ret;
	}

	/*
	 * Ensure that all contents of hvd are visible before they are visible
	 * to other threads.
	 */
	smp_wmb();

	BUG_ON(tegra_hv_data);
	tegra_hv_data = hvd;
	INFO("tegra_hv driver probed successfully\n");

	return 0;
}

static int tegra_hv_remove(struct platform_device *pdev)
{
	if (!is_tegra_hypervisor_mode())
		return 0;

	tegra_hv_cleanup(tegra_hv_data);
	kfree(tegra_hv_data);
	tegra_hv_data = NULL;

	INFO("tegra_hv driver removed successfully\n");

	return 0;
}

static const struct of_device_id tegra_hv_match[] = {
	{ .compatible = "nvidia,tegra-hv", },
	{},
};

static struct platform_driver tegra_hv_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tegra_hv_match),
	},
	.probe = tegra_hv_probe,
	.remove = tegra_hv_remove,
};

static int __init tegra_hv_init(void)
{
	int ret;

	ret = platform_driver_register(&tegra_hv_driver);
	if (ret)
		pr_err("Error: tegra_hv driver registration failed\n");

	return ret;
}

static void __exit tegra_hv_exit(void)
{
	platform_driver_unregister(&tegra_hv_driver);
}

postcore_initcall(tegra_hv_init);
module_exit(tegra_hv_exit);

MODULE_AUTHOR("Manish Bhardwaj <mbhardwaj@nvidia.com>");
MODULE_DESCRIPTION("Hypervisor Driver");
MODULE_LICENSE("GPL v2");
