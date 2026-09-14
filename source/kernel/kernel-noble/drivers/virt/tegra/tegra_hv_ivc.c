// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/overflow.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#include <soc/tegra/virt/tegra_hv_ivc.h>
#include <soc/tegra/virt/tegra_hv.h>
#include <soc/tegra/ivc.h>

#define ERR(...) pr_err("tegra_hv_ivc: " __VA_ARGS__)
#define INFO(...) pr_info("tegra_hv_ivc: " __VA_ARGS__)
#define DRV_NAME	"tegra_hv_ivc"

struct tegra_hv_data {
	struct tegra_hv_ivc_layout layout;
	const struct ivc_info_page *info;
	int guestid;
	struct tegra_hv_guest_area *guest_ivc_info;
	struct hv_ivc *ivc_devs;
	uint32_t max_qid;
	struct device_node *dev;
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
	const struct tegra_hv_guest_area *givci;
	int			other_guestid;

	const struct tegra_hv_ivc_ops *cookie_ops;
	struct tegra_hv_ivc_cookie cookie;

	/* This lock synchronizes the reserved flag. */
	struct mutex		lock;
	int			reserved;
	int			reserved_by_external;

	char			name[16];
	int			irq;
	uint32_t		queue_index;
};

#define cookie_to_ivc_dev(_cookie) \
	container_of(_cookie, struct hv_ivc, cookie)

static struct tegra_hv_data *tegra_hv_data;

static struct tegra_hv_notify_info ivc_notify;

static struct property interrupts_prop = {
	.name = "interrupts",
};

static struct tegra_hv_data *tegra_hv_get_hvd(void)
{
	if (!tegra_hv_data) {
		INFO("%s: not initialized yet\n", __func__);
		return ERR_PTR(-EPROBE_DEFER);
	} else {
		return tegra_hv_data;
	}
}

static void ivc_raise_irq(struct tegra_ivc *ivc_channel, void *data)
{
	struct hv_ivc *ivc = container_of(ivc_channel, struct hv_ivc, ivc);

	if (WARN_ON(!ivc->cookie.notify_va))
		return;

	*ivc->cookie.notify_va = ivc->qd->raise_irq;
}

static int tegra_hv_add_ivc(struct tegra_hv_data *hvd,
		const struct tegra_hv_queue_data *qd, uint32_t index)
{
	struct hv_ivc *ivc;
	int ret;
	int rx_first;
	struct iosys_map rx_map, tx_map;
	uint32_t i;
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

	ivc->queue_index = index;

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

/*
 * Safe validation for tainted data from ioremap_cache
 * Returns validated ivc_info_page pointer or NULL on failure
 * Fixes tainted_data_downcast and parm_assign violations
 */
static struct ivc_info_page *validate_ivc_info_page(void __iomem *mapped_mem,
						    size_t size)
{
	struct ivc_info_page *info;
	uint32_t nr_queues;

	if (!mapped_mem || size < sizeof(struct ivc_info_page)) {
		ERR("validate_ivc_info_page: Invalid parameters\n");
		return NULL;
	}

	/* Temporary access to validate header fields - still tainted at this point */
	info = (struct ivc_info_page *)mapped_mem;

	/* Read and validate critical fields that determine memory layout */
	nr_queues = info->nr_queues;

	/* Validate nr_queues against known safe bounds */
	if (nr_queues == 0U || nr_queues > PCT_MAX_NUM_IVC_QUEUES) {
		ERR("validate_ivc_info_page: Invalid nr_queues: %u\n", nr_queues);
		return NULL;
	}

	/* Additional validation for other critical fields */
	if (info->trap_region_size > 0 && info->trap_region_base_ipa == 0) {
		ERR("validate_ivc_info_page: Invalid trap region configuration\n");
		return NULL;
	}

	if (info->msi_region_size > 0 && info->msi_region_base_ipa == 0) {
		ERR("validate_ivc_info_page: Invalid MSI region configuration\n");
		return NULL;
	}

	return info;
}

int tegra_hv_ivc_layout_init(struct tegra_hv_ivc_layout *layout,
			     int (*read_ivc_info)(uint64_t *ivc_info_page_pa))
{
	uint64_t info_page;
	void __iomem *mapped_mem;
	uint32_t i;
	int ret;

	if (!layout || !read_ivc_info)
		return -EINVAL;

	memset(layout, 0, sizeof(*layout));

	ret = read_ivc_info(&info_page);
	if (ret != 0)
		return ret;

	mapped_mem = ioremap_cache(info_page, IVC_INFO_PAGE_SIZE);
	if (mapped_mem == NULL) {
		ERR("failed to map IVC info page (%llx)\n", info_page);
		return -ENOMEM;
	}

	/* Validate tainted data before assignment */
	layout->info = validate_ivc_info_page(mapped_mem, IVC_INFO_PAGE_SIZE);
	if (layout->info == NULL) {
		ERR("IVC info page validation failed\n");
		iounmap(mapped_mem);
		return -EINVAL;
	}

	/*
	 *  Map IVC Trap MMIO Notification region
	 */
	layout->notify.trap_region_base_ipa =
		layout->info->trap_region_base_ipa;
	layout->notify.trap_region_size = layout->info->trap_region_size;
	if (layout->notify.trap_region_size != 0UL) {
		if (WARN_ON(layout->notify.trap_region_base_ipa == 0UL)) {
			ret = -EINVAL;
			goto err;
		}
		if (WARN_ON(layout->notify.trap_region_base_va != 0UL)) {
			ret = -EINVAL;
			goto err;
		}
		layout->notify.trap_region_end_ipa =
			layout->notify.trap_region_base_ipa +
			layout->notify.trap_region_size - 1UL;
		layout->notify.trap_region_base_va =
			(uintptr_t)ioremap_cache(
				layout->notify.trap_region_base_ipa,
				layout->notify.trap_region_size);
		if (layout->notify.trap_region_base_va == 0UL) {
			ERR("failed to map trap ipa notification page\n");
			ret = -ENOMEM;
			goto err;
		}
	}

	/*
	 *  Map IVC MSI Notification region
	 */
	layout->notify.msi_region_base_ipa = layout->info->msi_region_base_ipa;
	layout->notify.msi_region_size = layout->info->msi_region_size;
	if (layout->notify.msi_region_size != 0UL) {
		if (WARN_ON(layout->notify.msi_region_base_ipa == 0UL)) {
			ret = -EINVAL;
			goto err;
		}
		if (WARN_ON(layout->notify.msi_region_base_va != 0UL)) {
			ret = -EINVAL;
			goto err;
		}
		layout->notify.msi_region_end_ipa =
			layout->notify.msi_region_base_ipa +
			layout->notify.msi_region_size - 1UL;
		layout->notify.msi_region_base_va =
			(uintptr_t)ioremap_cache(
				layout->notify.msi_region_base_ipa,
				layout->notify.msi_region_size);
		if (layout->notify.msi_region_base_va == 0UL) {
			ERR("failed to map msi ipa notification page\n");
			ret = -ENOMEM;
			goto err;
		}
	}

	layout->guest_ivc_info = kzalloc(layout->info->nr_areas *
			sizeof(*layout->guest_ivc_info), GFP_KERNEL);
	if (layout->guest_ivc_info == NULL) {
		ERR("failed to allocate %u-entry givci\n",
				layout->info->nr_areas);
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < layout->info->nr_areas; i++) {
		layout->guest_ivc_info[i].shmem = (uintptr_t)ioremap_cache(
				layout->info->areas[i].pa,
				layout->info->areas[i].size);
		if (layout->guest_ivc_info[i].shmem == 0) {
			ERR("can't map area for guest %u (%llx)\n",
					layout->info->areas[i].guest,
					layout->info->areas[i].pa);
			ret = -ENOMEM;
			goto err;
		}
		layout->guest_ivc_info[i].length =
			layout->info->areas[i].size;
	}

	return 0;
err:
	tegra_hv_ivc_layout_release(layout);

	return ret;
}
EXPORT_SYMBOL(tegra_hv_ivc_layout_init);

void tegra_hv_ivc_layout_release(struct tegra_hv_ivc_layout *layout)
{
	uint32_t i;

	if (!layout)
		return;

	if (layout->guest_ivc_info && layout->info) {
		for (i = 0; i < layout->info->nr_areas; i++) {
			if (layout->guest_ivc_info[i].shmem)
				iounmap((void __iomem *)layout->guest_ivc_info[i].shmem);
		}
	}

	kfree(layout->guest_ivc_info);
	layout->guest_ivc_info = NULL;

	if (layout->notify.trap_region_base_va) {
		iounmap((void __iomem *)layout->notify.trap_region_base_va);
		layout->notify.trap_region_base_va = 0UL;
		layout->notify.trap_region_base_ipa = 0;
		layout->notify.trap_region_end_ipa = 0;
		layout->notify.trap_region_size = 0;
	}

	if (layout->notify.msi_region_base_va) {
		iounmap((void __iomem *)layout->notify.msi_region_base_va);
		layout->notify.msi_region_base_va = 0UL;
		layout->notify.msi_region_base_ipa = 0;
		layout->notify.msi_region_end_ipa = 0;
		layout->notify.msi_region_size = 0;
	}

	if (layout->info) {
		iounmap((void __iomem *)layout->info);
		layout->info = NULL;
	}
}
EXPORT_SYMBOL(tegra_hv_ivc_layout_release);

uint32_t tegra_hv_get_max_qid(const struct ivc_info_page *info)
{
	uint32_t max_qid = 0;
	uint32_t i;

	for (i = 0; i < info->nr_queues; i++) {
		const struct tegra_hv_queue_data *qd =
				&ivc_info_queue_array(info)[i];

		if (qd->id > max_qid)
			max_qid = qd->id;
	}

	return max_qid;
}
EXPORT_SYMBOL(tegra_hv_get_max_qid);

int tegra_hv_add_ivc_interrupts(struct device_node *dev,
				const struct ivc_info_page *info,
				struct property *prop)
{
	struct device_node *p;
	uint32_t intr_property_size;
	uint32_t *interrupts_arr;
	uint32_t i, result;

	p = of_irq_find_parent(dev);
	if (!p) {
		ERR("Get interrupt parent failed\n");
		return -ENODEV;
	}

	if (of_property_read_u32(p, "#interrupt-cells", &intr_property_size)) {
		ERR("Get interrupt-cells failed\n");
		return -ENODEV;
	}

	if (check_mul_overflow(info->nr_queues, intr_property_size, &result)) {
		ERR("%s: operation got overflown.\n", __func__);
		return -EAGAIN;
	}

	/*
	 * Allocate array for interrupts property.
	 * Note: Do not free this, of_add_property does not copy the structure.
	 */
	interrupts_arr = kzalloc(result * sizeof(uint32_t), GFP_KERNEL);
	if (!interrupts_arr) {
		ERR("failed to allocate array for interrupts property\n");
		return -ENOMEM;
	}

	for (i = 0; i < info->nr_queues; i++) {
		const struct tegra_hv_queue_data *qd =
				&ivc_info_queue_array(info)[i];

		/* 0 => SPI */
		interrupts_arr[(i * intr_property_size)] =
			(__force uint32_t)cpu_to_be32(0);
		/* IRQ id in SPI namespace */
		interrupts_arr[(i * intr_property_size) + 1] =
			(__force uint32_t)cpu_to_be32(qd->irq - 32);
		/* 0x1 == low-to-high edge */
		interrupts_arr[(i * intr_property_size) + 2] =
			(__force uint32_t)cpu_to_be32(0x1);
		/* The 4th cell is a phandle to a node describing a set of CPUs this
		 * interrupt is affine to. The interrupt must be a PPI, and the node
		 * pointed must be a subnode of the "ppi-partitions" subnode. For
		 * interrupt types other than PPI or PPIs that are not partitioned,
		 * this cell must be zero. See the "ppi-partitions" node description below.
		 */
		if (intr_property_size > 3)
			interrupts_arr[(i * intr_property_size) + 3] =
				(__force uint32_t)cpu_to_be32(0);
	}

	prop->name = "interrupts";
	prop->length = info->nr_queues * sizeof(uint32_t) * intr_property_size;
	prop->value = interrupts_arr;

	if (of_add_property(dev, prop)) {
		ERR("failed to add interrupts property\n");
		kfree(interrupts_arr);
		return -EACCES;
	}

	return 0;
}
EXPORT_SYMBOL(tegra_hv_add_ivc_interrupts);

static int tegra_hv_ivc_setup(struct tegra_hv_data *hvd)
{
	uint32_t i;
	uint64_t ivcsize = 0;
	int ret;

	hvd->dev = of_find_compatible_node(NULL, NULL, "nvidia,tegra_hv_ivc");
	if (!hvd->dev) {
		ERR("could not find hv node\n");
		return -ENODEV;
	}

	ret = hyp_read_gid(&hvd->guestid);
	if (ret != 0) {
		ERR("Failed to read guest id\n");
		return -ENODEV;
	}

	if (hvd->guestid >= NGUESTS_MAX) {
		ERR("Failed to read valid guest id\n");
		return -EINVAL;
	}

	ret = tegra_hv_ivc_layout_init(&hvd->layout, hyp_read_ivc_info);
	if (ret != 0) {
		ERR("failed to obtain IVC info page: %d\n", ret);
		return ret;
	}

	hvd->info = hvd->layout.info;
	hvd->guest_ivc_info = hvd->layout.guest_ivc_info;
	ivc_notify = hvd->layout.notify;

	hvd->max_qid = tegra_hv_get_max_qid(hvd->info);

	ret = tegra_hv_add_ivc_interrupts(hvd->dev, hvd->info, &interrupts_prop);
	if (ret != 0) {
		ERR("failed to add interrupts property\n");
		return ret;
	}

	hvd->ivc_devs = kzalloc((hvd->max_qid + 1) * sizeof(*hvd->ivc_devs),
			GFP_KERNEL);
	if (hvd->ivc_devs == NULL) {
		ERR("failed to allocate %u-entry ivc_devs array\n",
				hvd->info->nr_queues);
		return -ENOMEM;
	}

	/* instantiate the IVC */
	ivcsize = 0;
	for (i = 0; i < hvd->info->nr_queues; i++) {
		const struct tegra_hv_queue_data *qd =
				&ivc_info_queue_array(hvd->info)[i];

		/* Validate qd->id before using as offset */
		if (qd->id > hvd->max_qid) {
			ERR("Invalid queue ID %u > max_qid %u\n",
			    qd->id, hvd->max_qid);
			return -EINVAL;
		}
		ret = tegra_hv_add_ivc(hvd, qd, i);
		if (ret != 0) {
			ERR("failed to add queue #%u\n", qd->id);
			return ret;
		}

		ivcsize += qd->size;
		BUG_ON(ivcsize < qd->size);
	}

#ifndef CONFIG_KERNEL_BUILD_WITH_PROD_DEFCONFIG
	INFO("Memory usage: ivc:0x%llx\n", ivcsize);
#endif

	return 0;
}

static struct hv_ivc *tegra_hv_ivc_device_by_id(struct tegra_hv_data *hvd, uint32_t id)
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

	tegra_hv_ivc_layout_release(&hvd->layout);
	hvd->guest_ivc_info = NULL;
	hvd->info = NULL;
	memset(&ivc_notify, 0, sizeof(ivc_notify));
}

static int ivc_dump(struct hv_ivc *ivc)
{
	INFO("IVC#%d: IRQ=%d(%d) nframes=%d frame_size=%d offset=%d\n",
			ivc->qd->id, ivc->irq, ivc->qd->irq,
			ivc->qd->nframes, ivc->qd->frame_size, ivc->qd->offset);

	return 0;
}

static void tegra_hv_ivc_handle_notification(struct hv_ivc *ivc)
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

	tegra_hv_ivc_handle_notification(ivcd);
	return IRQ_HANDLED;
}

static void tegra_hv_ivc_release_irq(struct hv_ivc *ivc)
{
	BUG_ON(!ivc);

	free_irq(ivc->irq, ivc);
}

static int tegra_hv_ivc_request_cookie_irq(struct hv_ivc *ivcd)
{
	return request_irq(ivcd->irq, ivc_dev_cookie_irq_handler, 0,
			ivcd->name, ivcd);
}

struct tegra_hv_ivc_cookie *tegra_hv_ivc_reserve(struct device_node *dn,
		int id, const struct tegra_hv_ivc_ops *ops)
{
	struct tegra_hv_data *hvd = tegra_hv_get_hvd();
	struct hv_ivc *ivc;
	struct tegra_hv_ivc_cookie *ivck;
	int ret;

	if (IS_ERR(hvd))
		return (void *)hvd;

	ivc = tegra_hv_ivc_device_by_id(hvd, id);
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

	/* Get IRQ from device tree on first reserve */
	if (ivc->irq == 0) {
		ret = of_irq_get(hvd->dev, ivc->queue_index);
		if (ret <= 0) {
			ERR("Unable to get irq for ivc%u\n", ivc->qd->id);
			mutex_lock(&ivc->lock);
			ivc->reserved = 0;
			mutex_unlock(&ivc->lock);
			return ERR_PTR(ret);
		}
		ivc->irq = ret;
	}

	ivc->cookie_ops = ops;

	ivck = &ivc->cookie;
	ivck->irq = ivc->irq;
	ivck->peer_vmid = ivc->other_guestid;
	ivck->nframes = ivc->qd->nframes;
	ivck->frame_size = ivc->qd->frame_size;

	if (ivc->cookie_ops) {
		tegra_hv_ivc_handle_notification(ivc);
		/* request our irq */
		ret = tegra_hv_ivc_request_cookie_irq(ivc);
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
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return;
	}

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
			tegra_hv_ivc_release_irq(ivc);
		ivc->cookie_ops = NULL;
		ivc->reserved = 0;
		tegra_ivc_clean_queue_data(&ivc->ivc);
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	mutex_unlock(&ivc->lock);

	return ret;
}
EXPORT_SYMBOL(tegra_hv_ivc_unreserve);

bool tegra_hv_ivc_reserve_id(uint32_t id)
{
	struct tegra_hv_data *hvd = tegra_hv_get_hvd();
	struct hv_ivc *ivc;
	bool reserved = false;

	if (IS_ERR(hvd))
		return false;

	ivc = tegra_hv_ivc_device_by_id(hvd, id);
	if (ivc == NULL)
		return false;

	mutex_lock(&ivc->lock);
	if (!ivc->reserved) {
		ivc->reserved = 1;
		ivc->reserved_by_external = 1;
		reserved = true;
	}
	mutex_unlock(&ivc->lock);

	return reserved;
}
EXPORT_SYMBOL(tegra_hv_ivc_reserve_id);

void tegra_hv_ivc_unreserve_id(uint32_t id)
{
	struct tegra_hv_data *hvd = tegra_hv_get_hvd();
	struct hv_ivc *ivc;

	if (IS_ERR(hvd))
		return;

	ivc = tegra_hv_ivc_device_by_id(hvd, id);
	if (ivc == NULL)
		return;

	mutex_lock(&ivc->lock);
	if (ivc->reserved_by_external) {
		ivc->reserved = 0;
		ivc->reserved_by_external = 0;
	}
	mutex_unlock(&ivc->lock);
}
EXPORT_SYMBOL(tegra_hv_ivc_unreserve_id);

int tegra_hv_ivc_write(struct tegra_hv_ivc_cookie *ivck, const void *buf,
		int size)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return -EPERM;
	}

	return tegra_ivc_write(&ivc->ivc, NULL, buf, size);
}
EXPORT_SYMBOL(tegra_hv_ivc_write);

int tegra_hv_ivc_write_user(struct tegra_hv_ivc_cookie *ivck, const void __user *buf,
		int size)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return -EPERM;
	}

	return tegra_ivc_write(&ivc->ivc, buf, NULL, size);
}
EXPORT_SYMBOL(tegra_hv_ivc_write_user);

int tegra_hv_ivc_read(struct tegra_hv_ivc_cookie *ivck, void *buf, int size)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return -EPERM;
	}

	return tegra_ivc_read(&ivc->ivc, NULL, buf, size);
}
EXPORT_SYMBOL(tegra_hv_ivc_read);

int tegra_hv_ivc_read_user(struct tegra_hv_ivc_cookie *ivck, void __user *buf, int size)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return -EPERM;
	}

	return tegra_ivc_read(&ivc->ivc, buf, NULL, size);
}
EXPORT_SYMBOL(tegra_hv_ivc_read_user);

int tegra_hv_ivc_can_read(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return -EPERM;
	}

	return tegra_ivc_can_read(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_can_read);

int tegra_hv_ivc_can_write(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return -EPERM;
	}

	return tegra_ivc_can_write(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_can_write);

int tegra_hv_ivc_tx_empty(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return -EPERM;
	}

	return tegra_ivc_empty(&ivc->ivc, &ivc->ivc.tx.map);
}
EXPORT_SYMBOL(tegra_hv_ivc_tx_empty);

#ifndef CONFIG_KERNEL_BUILD_WITH_PROD_DEFCONFIG
uint32_t tegra_hv_ivc_tx_frames_available(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return 0;
	}

	return tegra_ivc_frames_available(&ivc->ivc, &ivc->ivc.tx.map);
}
EXPORT_SYMBOL(tegra_hv_ivc_tx_frames_available);

int tegra_hv_ivc_dump(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return -EPERM;
	}

	return ivc_dump(ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_dump);
#endif

void *tegra_hv_ivc_read_get_next_frame(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;
	struct iosys_map map;
	int err;

	if (ivck == NULL)
		return ERR_PTR(-EINVAL);

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return ERR_PTR(-EPERM);
	}

	err = tegra_ivc_read_get_next_frame(&ivc->ivc, &map);
	if (err)
		return ERR_PTR(err);
	return map.vaddr;
}
EXPORT_SYMBOL(tegra_hv_ivc_read_get_next_frame);

void *tegra_hv_ivc_write_get_next_frame(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;
	struct iosys_map map;
	int err;

	if (ivck == NULL)
		return ERR_PTR(-EINVAL);

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return ERR_PTR(-EPERM);
	}

	err = tegra_ivc_write_get_next_frame(&ivc->ivc, &map);
	if (err)
		return ERR_PTR(err);

	return map.vaddr;
}
EXPORT_SYMBOL(tegra_hv_ivc_write_get_next_frame);

int tegra_hv_ivc_write_advance(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return -EPERM;
	}

	return tegra_ivc_write_advance(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_write_advance);

int tegra_hv_ivc_read_advance(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return -EPERM;
	}

	return tegra_ivc_read_advance(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_read_advance);

int tegra_hv_ivc_channel_notified(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return -EINVAL;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return -EPERM;
	}

	return tegra_ivc_notified(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_channel_notified);

void tegra_hv_ivc_channel_reset(struct tegra_hv_ivc_cookie *ivck)
{
	struct hv_ivc *ivc;

	if (ivck == NULL)
		return;

	ivc = cookie_to_ivc_dev(ivck);
	if (!ivc->reserved) {
		pr_err("%s: Error ivc queue is not reserved\n", __func__);
		return;
	}

	if (ivc->cookie_ops) {
		ERR("reset unsupported with callbacks");
		BUG();
	}

	tegra_ivc_reset(&ivc->ivc);
}
EXPORT_SYMBOL(tegra_hv_ivc_channel_reset);

static int tegra_hv_ivc_probe(struct platform_device *pdev)
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

	ret = tegra_hv_ivc_setup(hvd);
	if (ret != 0) {
		tegra_hv_ivc_cleanup(hvd);
		kfree(hvd);
		return ret;
	}

	BUG_ON(tegra_hv_data);
	tegra_hv_data = hvd;

	INFO("Tegra Hypervisor IVC driver probed\n");
	return 0;
}

static int tegra_hv_ivc_remove(struct platform_device *pdev)
{
	if (!is_tegra_hypervisor_mode())
		return 0;

	tegra_hv_ivc_cleanup(tegra_hv_data);
	kfree(tegra_hv_data);
	tegra_hv_data = NULL;

	INFO("Tegra Hypervisor IVC driver removed\n");
	return 0;
}

static const struct of_device_id tegra_hv_ivc_match[] = {
	{ .compatible = "nvidia,tegra_hv_ivc", },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_hv_ivc_match);

static struct platform_driver tegra_hv_ivc_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tegra_hv_ivc_match),
	},
	.probe = tegra_hv_ivc_probe,
	.remove = tegra_hv_ivc_remove,
};

static int __init tegra_hv_ivc_init(void)
{
	int ret;

	ret = platform_driver_register(&tegra_hv_ivc_driver);
	if (ret)
		ERR("platform driver registration failed: %d\n", ret);

	return ret;
}

static void __exit tegra_hv_ivc_exit(void)
{
	platform_driver_unregister(&tegra_hv_ivc_driver);
}

postcore_initcall(tegra_hv_ivc_init);
module_exit(tegra_hv_ivc_exit);

MODULE_AUTHOR("Manish Bhardwaj <mbhardwaj@nvidia.com>");
MODULE_DESCRIPTION("Tegra Hypervisor IVC Driver");
MODULE_LICENSE("GPL v2");

