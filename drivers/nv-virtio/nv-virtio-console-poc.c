// SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: GPL-2.0-only

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <soc/tegra/virt/hv-ivc.h>

#include <uapi/linux/virtio_mmio.h>
#include <uapi/linux/virtio_ring.h>

#include <soc/tegra/virt/syscalls.h>

#include <asm/io.h>

/* flag to enable read-write loopback to host userspace */
static bool nvvc_enable_host_user_loopback;
/* flag to enable read-write loopback to android vm */
static bool nvvc_enable_android_loopback = true;
/* flag to enable mempool and ivc */
static bool nvvc_enable_mempool_ivc = true;
/* flag to enable mempool and ivc */
static bool nvvc_received_rxdataavaiable = false;

/* RX FIFO size */
#define NVVC_RX_FIFO_SIZE			(16 * 1024)
/* TX FIFO size */
#define NVVC_TX_FIFO_SIZE			(16 * 1024)
/* RD/WR timeout */
#define NVVC_RD_WR_WAIT_TIMEOUT		(10 * HZ)
/* 32GB - Android IPA Start */
#define NVVC_SHARED_BUF_OFFSET			((32ULL * 1024ULL * 1024ULL * 1024ULL) - 0xc0000000)
#define NVVC_SHARED_BOUNCE_BUF_OFFSET	(8ULL * 1024ULL * 1024ULL * 1024ULL) /* 8GB */
#define NVVC_SHARED_BUF_SIZE		PAGE_SIZE

/*
 * The following defines are from hyper visor.
 * Refer modules/virtio/sidekick/include/virtio-emu.h for details.
 */
#define MMIOSTATEREADY 0xFFFF0001
#define TXDATAAVAIABLE 0xFFFF0002
#define TXDATACONSUMED 0xFFFF0003
#define RXDATAAVAIABLE 0xFFFF0004
#define RXDATACONSUMED 0xFFFF0005

#define MAXVIRTIOQUEUESIZE		4U
#define RECEIVEQUEUENOTIFIERID	0U
#define SENDQUEUENOTIFIERID		1U

#define SENDQUEUEDESCRIPTORPAHIGH		(0 * sizeof(uint64_t))
#define SENDQUEUEDESCRIPTORPALOW		(1 * sizeof(uint64_t))
#define RECEIVEQUEUEDESCRIPTORPAHIGH	(2 * sizeof(uint64_t))
#define RECEIVEQUEUEDESCRIPTORPALOW		(3 * sizeof(uint64_t))
#define SENDQUEUEDRIVERPAHIGH			(4 * sizeof(uint64_t))
#define SENDQUEUEDRIVERPALOW			(5 * sizeof(uint64_t))
#define RECEIVEQUEUEDRIVERPAHIGH		(6 * sizeof(uint64_t))
#define RECEIVEQUEUEDRIVERPALOW			(7 * sizeof(uint64_t))
#define SENDQUEUEDEVICEPAHIGH			(8 * sizeof(uint64_t))
#define SENDQUEUEDEVICEPALOW			(9 * sizeof(uint64_t))
#define RECEIVEQUEUEDEVICEPAHIGH		(10 * sizeof(uint64_t))
#define RECEIVEQUEUEDEVICEPALOW			(11 * sizeof(uint64_t))
#define SENDQUEUEBUFFERADDRESS			(12 * sizeof(uint64_t))
#define RECEIVEQUEUEBUFFERADDRESS		(13 * sizeof(uint64_t))

typedef struct nvvc_dev {
	uint32_t ivc_id;
	uint32_t ivm_id;
	struct tegra_hv_ivc_cookie *ivc;
	struct tegra_hv_ivm_cookie *ivm;
	unsigned char __iomem *hv_virt_base;

	DECLARE_KFIFO(nvvc_rx_fifo, unsigned char, NVVC_RX_FIFO_SIZE);
	DECLARE_KFIFO(nvvc_tx_fifo, unsigned char, NVVC_TX_FIFO_SIZE);
	struct work_struct work;

	wait_queue_head_t read_wait;
	wait_queue_head_t write_wait;

	int maj_num;
	struct class *class;
	struct device *device;
} nvvc_dev_t;

static nvvc_dev_t *nvvcdev;

static int nvvc_process_rxdataavaiable(void);

static inline uint32_t nvvc_virtio_readl(nvvc_dev_t *nvvcdev, uint32_t reg)
{
	return readl(nvvcdev->hv_virt_base + reg);
}

static inline void nvvc_virtio_writel(nvvc_dev_t *nvvcdev, uint32_t reg, uint32_t val)
{
	writel(val, nvvcdev->hv_virt_base + reg);
}

static inline uint64_t nvvc_virtio_readll(nvvc_dev_t *nvvcdev, uint32_t reg)
{
	return *((uint64_t*)(nvvcdev->hv_virt_base + reg));
}

static inline void nvvc_virtio_writell(nvvc_dev_t *nvvcdev, uint32_t reg, uint64_t val)
{
	*((uint64_t*)(nvvcdev->hv_virt_base + reg)) = val;
}

static int nvvc_open(struct inode *inode, struct file *f)
{
	pr_debug("Open nvvc\n");
	return 0;
}

static int nvvc_release(struct inode *inode, struct file *f)
{
	pr_debug("Release nvvc\n");
	wake_up_interruptible(&nvvcdev->read_wait);
	wake_up_interruptible(&nvvcdev->write_wait);
	return 0;
}

static ssize_t nvvc_read(struct file *f, char *buf, size_t buf_size, loff_t *off)
{
	int read_size = -ENODATA, err;
	unsigned int copied;

	pr_debug("Read nvvc\n");

	if (kfifo_is_empty(&nvvcdev->nvvc_rx_fifo)) {
		err = wait_event_interruptible_timeout(nvvcdev->read_wait,
											!kfifo_is_empty(&nvvcdev->nvvc_rx_fifo),
											NVVC_RD_WR_WAIT_TIMEOUT);
		if ( err == 0 || err == -ERESTARTSYS) {
			return -EINTR;
		}
	}

	if (!kfifo_is_empty(&nvvcdev->nvvc_rx_fifo)) {
		err = kfifo_to_user(&nvvcdev->nvvc_rx_fifo, buf, buf_size, &copied);
		if (err) {
			pr_err("nvvc: less data read (%u)\n", copied);
		}
		read_size = copied;
	} else {
		pr_err("Read nvvc failure: no data\n");
	}
	pr_debug("Read nvvc: read %d bytes\n", read_size);
	return read_size;
}

static ssize_t nvvc_write(struct file *f, const char *buf, size_t buf_size, loff_t * off)
{
	int write_size = -ENOSPC, err;
	unsigned int copied = 0;

	pr_debug("Write nvvc\n");

	if (nvvc_enable_host_user_loopback) {
		if (kfifo_is_full(&nvvcdev->nvvc_rx_fifo)) {
			err = wait_event_interruptible_timeout(nvvcdev->write_wait,
												!kfifo_is_full(&nvvcdev->nvvc_rx_fifo),
												NVVC_RD_WR_WAIT_TIMEOUT);
			if ( err == 0 || err == -ERESTARTSYS) {
				return -EINTR;
			}
		}

		/* write to driver RX FIFO */
		if (!kfifo_is_full(&nvvcdev->nvvc_rx_fifo)) {
			err = kfifo_from_user(&nvvcdev->nvvc_rx_fifo, buf, buf_size, &copied);
			if (err) {
				write_size = err;
			} else {
				write_size = copied;
				pr_debug("nvvc: wrote %u bytes out of %ld bytes\n",
						copied, buf_size);
			}
		}
	} else {
		if (kfifo_is_full(&nvvcdev->nvvc_tx_fifo)) {
			err = wait_event_interruptible_timeout(nvvcdev->write_wait,
												!kfifo_is_full(&nvvcdev->nvvc_tx_fifo),
												NVVC_RD_WR_WAIT_TIMEOUT);
			if ( err == 0 || err == -ERESTARTSYS) {
				return -EINTR;
			}
		}

		/* write to driver TX FIFO */
		if (!kfifo_is_full(&nvvcdev->nvvc_tx_fifo)) {
			err = kfifo_from_user(&nvvcdev->nvvc_tx_fifo, buf, buf_size, &copied);
			if (err) {
				write_size = err;
			} else {
				write_size = copied;
				pr_debug("nvvc: wrote %u bytes out of %ld bytes\n",
						copied, buf_size);
			}
		}
	}

	if (write_size == -ENOSPC)
		pr_debug("Write nvvc failure\n");

#if 0
	/*
	 * TODO: enable processing pending rxdataavaiable after fixing
	 * synchronization issues with kernel and HV events
	 */
	if (write_size > 0 && nvvc_received_rxdataavaiable) {
		err = nvvc_process_rxdataavaiable();
		if (err < 0)
			pr_err("nvvc_write: failed to write to receive virtqueue. err:%d\n", err);
	}
#endif
	return write_size;
}

static __poll_t nvvc_poll(struct file *f, poll_table *pt)
{
	__poll_t poll_status = 0;

	pr_debug("Poll nvvc\n");

	poll_wait(f, &nvvcdev->read_wait, pt);
	poll_wait(f, &nvvcdev->write_wait, pt);

	if (!kfifo_is_empty(&nvvcdev->nvvc_rx_fifo))
		poll_status |= EPOLLIN | EPOLLRDNORM;
	if (!kfifo_is_full(&nvvcdev->nvvc_tx_fifo))
		poll_status |= EPOLLOUT | EPOLLWRNORM;

	return poll_status;
}

static int nvvc_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long ipa_pfn;
	uint64_t ipa, ipa_with_offset;
	int err = 0;
	unsigned char __iomem *base;

	pr_debug("mmap nvvc\n");

	ipa = nvvc_virtio_readll(nvvcdev, SENDQUEUEBUFFERADDRESS);
	pr_debug("nvvc: mmap: ipa:0x%llx\n", ipa);
	ipa_with_offset = NVVC_SHARED_BOUNCE_BUF_OFFSET + ipa;
	pr_debug("nvvc: mmap: ipa_with_offset:0x%llx\n", ipa_with_offset);
	ipa_pfn = ipa_with_offset >> PAGE_SHIFT;

	base =(uint8_t*)memremap(ipa_with_offset, NVVC_SHARED_BUF_SIZE, MEMREMAP_WB);
	if (!base) {
		pr_err("nvvc: ipa memremap failed\n");
		err = -ENOMEM;
		goto err;
	}
	pr_debug("nvvc: mmap: 0x%x, 0x%x, 0x%x, 0x%x\n",
		*(((char*)base) + 0),
		*(((char*)base) + 1),
		*(((char*)base) + 2),
		*(((char*)base) + 3)
		);
	if (remap_pfn_range(vma, vma->vm_start, ipa_pfn,
							NVVC_SHARED_BUF_SIZE, vma->vm_page_prot)) {
		err = -EAGAIN;
		pr_debug("nvvc: mmap: mapping ipa_pfn:0x%lx FAILED\n", ipa_pfn);
	} else {
		err = 0;
		pr_debug("nvvc: mmap: mapping ipa_pfn PASSED\n");
	}
err:
	pr_debug("mmap nvvc. err:%d\n", err);
	return err;
}

static struct file_operations nvvc_fops = {
	.open	= nvvc_open,
	.release	= nvvc_release,
	.read	= nvvc_read,
	.write	= nvvc_write,
	.poll	= nvvc_poll,
	.mmap	 = nvvc_mmap,
};

vring_desc_t *tx_vring_desc = NULL;
vring_avail_t *tx_vring_avail = NULL;
vring_used_t *tx_vring_used = NULL;

static int nvvc_map_send_virtqueue(void)
{
	int err = 0;
	uint64_t ipa, ipa_with_offset;
	(void)ipa;(void)ipa_with_offset;

	/* desc */
	pr_debug("nvvc: nvvc_map_send_virtqueue: high:0x%llx highshift:0x%llx\n",
		nvvc_virtio_readll(nvvcdev, SENDQUEUEDESCRIPTORPAHIGH),
		nvvc_virtio_readll(nvvcdev, SENDQUEUEDESCRIPTORPAHIGH) << 32);
	pr_debug("nvvc: nvvc_map_send_virtqueue: low:0x%llx\n",
		nvvc_virtio_readll(nvvcdev, SENDQUEUEDESCRIPTORPALOW));
	ipa = (nvvc_virtio_readll(nvvcdev, SENDQUEUEDESCRIPTORPAHIGH) << 32) |
		nvvc_virtio_readll(nvvcdev, SENDQUEUEDESCRIPTORPALOW);

	pr_debug("nvvc: nvvc_map_send_virtqueue: desc ipa:0x%llx\n", ipa);
	ipa_with_offset = NVVC_SHARED_BUF_OFFSET + ipa;
	pr_debug("nvvc:: ipa_with_offset:0x%llx\n", ipa_with_offset);

	tx_vring_desc =(vring_desc_t*)memremap(ipa_with_offset, sizeof(vring_desc_t), MEMREMAP_WB);
	if (!tx_vring_desc) {
		pr_err("nvvc: tx_vring_desc memremap failed\n");
		err = -ENOMEM;
		goto err;
	}

	/* avail */
	ipa = (nvvc_virtio_readll(nvvcdev, SENDQUEUEDRIVERPAHIGH) << 32) |
		nvvc_virtio_readll(nvvcdev, SENDQUEUEDRIVERPALOW);

	pr_debug("nvvc: nvvc_map_send_virtqueue: avail ipa:0x%llx\n", ipa);
	ipa_with_offset = NVVC_SHARED_BUF_OFFSET + ipa;
	pr_debug("nvvc:: ipa_with_offset:0x%llx\n", ipa_with_offset);

	tx_vring_avail =(vring_avail_t*)memremap(ipa_with_offset, sizeof(vring_avail_t), MEMREMAP_WB);
	if (!tx_vring_avail) {
		pr_err("nvvc: tx_vring_avail memremap failed\n");
		err = -ENOMEM;
		goto err;
	}

	/* used */
	ipa = (nvvc_virtio_readll(nvvcdev, SENDQUEUEDEVICEPAHIGH) << 32) |
		nvvc_virtio_readll(nvvcdev, SENDQUEUEDEVICEPALOW);

	pr_debug("nvvc: nvvc_map_send_virtqueue: used ipa:0x%llx\n", ipa);
	ipa_with_offset = NVVC_SHARED_BUF_OFFSET + ipa;
	pr_debug("nvvc:: ipa_with_offset:0x%llx\n", ipa_with_offset);

	tx_vring_used =(vring_used_t*)memremap(ipa_with_offset, sizeof(vring_used_t), MEMREMAP_WB);
	if (!tx_vring_used) {
		pr_err("nvvc: tx_vring_used memremap failed\n");
		err = -ENOMEM;
		goto err;
	}
	pr_debug("nvvc: tx_vrings memremap SUCCESS\n");
	err = 0;
err:
	return err;
}

static void nvvc_unmap_send_virtqueue(void)
{
	memunmap(tx_vring_desc);
	memunmap(tx_vring_avail);
	memunmap(tx_vring_used);
}

static int nvvc_read_send_virtqueue(void)
{
	int err = -EINVAL;
	uint32_t avail_idx;
	uint32_t desc_idx;
	uint32_t used_idx;
	uint32_t tx_avail_len;
	unsigned char __iomem *iospace;
	uint64_t copied;
	int i;

	if (kfifo_is_full(&nvvcdev->nvvc_rx_fifo)) {
		err = -EBUSY;
		goto err;
	}

	err = nvvc_map_send_virtqueue();
	if (err) {
		goto err;
	}

	pr_debug("nvvc:: tx_vring_desc:%px\n", tx_vring_desc);
	pr_debug("nvvc:: tx_vring_avail:%px\n", tx_vring_avail);
	pr_debug("nvvc:: tx_vring_used:%px\n", tx_vring_used);

	/* get addr from virtio desc + avail */
	avail_idx = (tx_vring_avail->idx - 1U) % MAXVIRTIOQUEUESIZE;
	pr_debug("nvvc: nvvc_read_send_virtqueue: avail_idx:%u\n", avail_idx);

	desc_idx = tx_vring_avail->ring[avail_idx] % MAXVIRTIOQUEUESIZE;
	pr_debug("nvvc: nvvc_read_send_virtqueue: desc_idx:%u\n", desc_idx);

	tx_avail_len = tx_vring_desc[desc_idx].len;
	pr_debug("nvvc: nvvc_read_send_virtqueue: avail_idx:%u\n", tx_avail_len);
	pr_debug("nvvc:: vq buf addr:0x%llx\n", tx_vring_desc[desc_idx].addr);

	/* ioreamp addr */
	iospace = (uint8_t*)memremap(tx_vring_desc[desc_idx].addr +
								NVVC_SHARED_BUF_OFFSET,
								tx_avail_len, MEMREMAP_WB);
	if (!iospace) {
		pr_err("nvvc: nvvc_read_send_virtqueue memremap failed\n");
		err = -ENOMEM;
		goto err_unmap;
	}

	for (i = 0; i < tx_avail_len; i++ ) {
		pr_debug("Tx Data: 0x%x\n", *(((char*)iospace) + i));
	}

	if (nvvc_enable_android_loopback) {
		/* copy to nvvc_tx_fifo */
		copied = kfifo_in(&nvvcdev->nvvc_tx_fifo, iospace, tx_avail_len);
		pr_debug("nvvc: tx data copied : %llu\n", copied);

	} else {
		/* copy to nvvc_rx_fifo */
		copied = kfifo_in(&nvvcdev->nvvc_rx_fifo, iospace, tx_avail_len);
		pr_debug("nvvc: rx data copied : %llu\n", copied);
	}

	/* update virtio used */
	used_idx = tx_vring_used->idx % MAXVIRTIOQUEUESIZE;
	tx_vring_used->ring[used_idx].id = desc_idx;
	tx_vring_used->ring[used_idx].len = copied; /* for data flow control */
	tx_vring_used->flags = 0U;
	tx_vring_used->idx++;

	/* wake up reader */
	if (copied)
		wake_up_interruptible(&nvvcdev->read_wait);

	err = 0;
err_unmap:
	nvvc_unmap_send_virtqueue();
err:
	return err;
}

vring_desc_t *rx_vring_desc = NULL;
vring_avail_t *rx_vring_avail = NULL;
vring_used_t *rx_vring_used = NULL;

static int nvvc_map_receive_virtqueue(void)
{
	int err = 0;
	uint64_t ipa, ipa_with_offset;
	(void)ipa;(void)ipa_with_offset;

	/* desc */
	ipa = (nvvc_virtio_readll(nvvcdev, RECEIVEQUEUEDESCRIPTORPAHIGH) << 32) |
		nvvc_virtio_readll(nvvcdev, RECEIVEQUEUEDESCRIPTORPALOW);

	pr_debug("nvvc: nvvc_map_receive_virtqueue: avail ipa:0x%llx\n", ipa);
	ipa_with_offset = NVVC_SHARED_BUF_OFFSET + ipa;

	rx_vring_desc =(vring_desc_t*)memremap(ipa_with_offset, sizeof(vring_desc_t), MEMREMAP_WB);
	if (!rx_vring_desc) {
		pr_err("nvvc: rx_vring_desc memremap failed\n");
		err = -ENOMEM;
		goto err;
	}

	/* avail */
	ipa = (nvvc_virtio_readll(nvvcdev, RECEIVEQUEUEDRIVERPAHIGH) << 32) |
		nvvc_virtio_readll(nvvcdev, RECEIVEQUEUEDRIVERPALOW);

	pr_debug("nvvc: nvvc_map_receive_virtqueue: avail ipa:0x%llx\n", ipa);
	ipa_with_offset = NVVC_SHARED_BUF_OFFSET + ipa;

	rx_vring_avail =(vring_avail_t*)memremap(ipa_with_offset, sizeof(vring_avail_t), MEMREMAP_WB);
	if (!rx_vring_avail) {
		pr_err("nvvc: rx_vring_avail memremap failed\n");
		err = -ENOMEM;
		goto err;
	}

	/* used */
	ipa = (nvvc_virtio_readll(nvvcdev, RECEIVEQUEUEDEVICEPAHIGH) << 32) |
		nvvc_virtio_readll(nvvcdev, RECEIVEQUEUEDEVICEPALOW);

	pr_debug("nvvc: nvvc_map_receive_virtqueue: used ipa:0x%llx\n", ipa);
	ipa_with_offset = NVVC_SHARED_BUF_OFFSET + ipa;

	rx_vring_used =(vring_used_t*)memremap(ipa_with_offset, sizeof(vring_used_t), MEMREMAP_WB);
	if (!rx_vring_used) {
		pr_err("nvvc: rx_vring_used memremap failed\n");
		err = -ENOMEM;
		goto err;
	}
	pr_debug("nvvc: rx_vrings memremap SUCCESS\n");
	err = 0;
err:
	return err;
}

static void nvvc_unmap_receive_virtqueue(void)
{
	memunmap(rx_vring_desc);
	memunmap(rx_vring_avail);
	memunmap(rx_vring_used);
}

static int nvvc_write_receive_virtqueue(void)
{
	int err = -EINVAL;
	uint32_t avail_idx;
	uint32_t desc_idx;
	uint32_t used_idx;
	uint32_t rx_avail_len;
	unsigned char __iomem *iospace;
	uint64_t copied;

	if (kfifo_is_empty(&nvvcdev->nvvc_tx_fifo)) {
		err = -ENODATA;
		pr_debug("no data in nvvc to write to receive virtqueue. err: %d\n", err);
		goto err;
	}

	err = nvvc_map_receive_virtqueue();
	if (err) {
		goto err;
	}

	/* get addr from virtio desc + avail */
	avail_idx = (rx_vring_avail->idx - 1U) % MAXVIRTIOQUEUESIZE;
	desc_idx = rx_vring_avail->ring[avail_idx] % MAXVIRTIOQUEUESIZE;
	rx_avail_len = rx_vring_desc[desc_idx].len;

	/* ioreamp addr */
	iospace = (uint8_t*)memremap(rx_vring_desc[desc_idx].addr +
								NVVC_SHARED_BUF_OFFSET,
								rx_avail_len, MEMREMAP_WB);
	if (!iospace) {
		pr_err("nvvc: nvvc_write_receive_virtqueue memremap failed\n");
		err = -ENOMEM;
		goto err_unmap;
	}

	/* copy from nvvc_tx_fifo */
	copied = kfifo_out(&nvvcdev->nvvc_tx_fifo, iospace, rx_avail_len);
	pr_debug("nvvc: rx data copied : %llu\n", copied);

	/* update virtio used */
	used_idx = rx_vring_used->idx % MAXVIRTIOQUEUESIZE;
	rx_vring_used->ring[used_idx].id = desc_idx;
	rx_vring_used->ring[used_idx].len = copied; /* for data flow control */
	rx_vring_used->flags = 2U;
	rx_vring_used->idx++;

	/* wake up writer */
	if (copied)
		wake_up_interruptible(&nvvcdev->write_wait);

	err = 0;
err_unmap:
	nvvc_unmap_receive_virtqueue();
err:
	return err;
}

static int nvvc_process_rxdataavaiable(void) {
	uint32_t ivc_resp;
	int err;

	err = nvvc_write_receive_virtqueue();
	if (err < 0) {
		pr_debug("write to virtqueue failed: %d\n", err);
	} else {
		/* ivc data consumption */
		if (!tegra_hv_ivc_can_write(nvvcdev->ivc)) {
			err = -EBUSY;
			pr_err("ivc cannot write for rx virtqueue. err: %d\n", err);
		} else {
			ivc_resp = RXDATACONSUMED;
			err = tegra_hv_ivc_write(nvvcdev->ivc, &ivc_resp, sizeof(ivc_resp));
			if (err != sizeof(ivc_resp)) {
				err = -EIO;
				pr_err("ivc write for rx virtqueue failed: %d\n", err);
			}
		}
	}
	return err;
}

static void nvvc_work(struct work_struct *work)
{
	struct nvvc_dev *nvvcdev = container_of(work, struct nvvc_dev, work);
	uint32_t ivc_cmd, ivc_resp;
	int err = 0;

	if (tegra_hv_ivc_channel_notified(nvvcdev->ivc) != 0) {
		pr_err("nvvc_work: ivc channel not notified\n");
		return;
	}

	while (tegra_hv_ivc_can_read(nvvcdev->ivc)) {
		err = tegra_hv_ivc_read(nvvcdev->ivc, &ivc_cmd, sizeof(ivc_cmd));
		if (err < 0) {
			pr_err("ivc read of interrupt failed: %d\n", err);
			return;
		}

		switch (ivc_cmd) {
		case TXDATAAVAIABLE:
			err = nvvc_read_send_virtqueue();
			if (err < 0) {
				pr_err("ivc read from virtqueue failed: %d\n", err);
			} else {
				/* ivc data consumption */
				if (!tegra_hv_ivc_can_write(nvvcdev->ivc)) {
					err = -EBUSY;
					pr_err("ivc cannot write for virtqueue. err: %d\n", err);
				} else {
					ivc_resp = TXDATACONSUMED;
					err = tegra_hv_ivc_write(nvvcdev->ivc, &ivc_resp, sizeof(ivc_resp));
					if (err != sizeof(ivc_resp)) {
						err = -EIO;
						pr_err("ivc write for virtqueue failed: %d\n", err);
					}
				}
			}
			break;
		case RXDATAAVAIABLE:
			/* set flag so that next nvvc_write start writing data */
			nvvc_received_rxdataavaiable = true;

			err = nvvc_process_rxdataavaiable();
			if (err < 0)
				pr_debug("nvvc_process_rxdataavaiable failed: err %d\n", err);
			break;
		default:
			pr_err("ivc read invalid command: 0x%x\n", ivc_cmd);
			break;
		}
	}
}

static irqreturn_t nvvc_ivc_irq_handler(int irq, void *data)
{
	struct nvvc_dev *nvvcdev = (struct nvvc_dev *)data;

	if (tegra_hv_ivc_channel_notified(nvvcdev->ivc) != 0) {
		pr_err("ivc channel not usable\n");
		return IRQ_HANDLED;
	}

#if 1
	schedule_work(&nvvcdev->work);
#endif
	return IRQ_HANDLED;
}

static int nvvc_hv_init(nvvc_dev_t *nvvcdev)
{
	int err = 0;

	if (!nvvc_enable_mempool_ivc) {
		pr_err("nvvc hv init SKIPPED\n");
		return 0;
	}

	pr_debug("nvvc hv init\n");

	nvvcdev->ivc_id = 454;
	nvvcdev->ivm_id = 98;

	/* mempool */
	nvvcdev->ivm = tegra_hv_mempool_reserve(nvvcdev->ivm_id);
	if (IS_ERR_OR_NULL(nvvcdev->ivm)) {
		pr_err("nvvc: No mempool found\n");
		err = -ENOMEM;
		goto err;
	}

	nvvcdev->hv_virt_base =
		(uint8_t*)memremap(nvvcdev->ivm->ipa, nvvcdev->ivm->size, MEMREMAP_WB);
	if (!nvvcdev->hv_virt_base) {
		pr_err("nvvc: Remap mempool failed\n");
		err = -ENOMEM;
		goto free_ivm;
	}

	/* ivc */
	nvvcdev->ivc = tegra_hv_ivc_reserve(NULL, nvvcdev->ivc_id, NULL);
	if (IS_ERR_OR_NULL(nvvcdev->ivc)) {
		pr_err("nvvc: No IVC channel %d\n", nvvcdev->ivc_id);
		nvvcdev->ivc = NULL;
		err = -ENODEV;
		goto unmap_ivm;
	}

	if (request_irq(nvvcdev->ivc->irq, nvvc_ivc_irq_handler, 0,
						"nvvc", nvvcdev)) {
		pr_err("nvvc: Failed to request irq %d\n", nvvcdev->ivc->irq);
		err = -EINVAL;
		goto free_ivc;
	}

	tegra_hv_ivc_channel_reset(nvvcdev->ivc);

	pr_debug("nvvc hv init SUCCESS\n");
	return 0;

free_ivc:
	tegra_hv_ivc_unreserve(nvvcdev->ivc);
unmap_ivm:
	iounmap(nvvcdev->hv_virt_base);
free_ivm:
	tegra_hv_mempool_unreserve(nvvcdev->ivm);
err:
	return err;
}

static void nvvc_hv_deinit(nvvc_dev_t *nvvcdev)
{
	if (!nvvc_enable_mempool_ivc)
		return;

	free_irq(nvvcdev->ivc->irq, nvvcdev);
	tegra_hv_ivc_unreserve(nvvcdev->ivc);

	iounmap(nvvcdev->hv_virt_base);
	tegra_hv_mempool_unreserve(nvvcdev->ivm);
}

static int nvvc_register_dev(nvvc_dev_t *nvvcdev)
{
	int err;

	pr_debug("Register nvvc\n");

	nvvcdev->maj_num = register_chrdev(0, "vconsole", &nvvc_fops);
	if (nvvcdev->maj_num < 0) {
		pr_err("register_chrdev failed\n");
		err = nvvcdev->maj_num;
		goto err;
	}

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	nvvcdev->class = class_create("nvvc");
#else
	nvvcdev->class = class_create(THIS_MODULE, "nvvc");
#endif
	if (IS_ERR(nvvcdev->class)) {
		pr_err("class_create failed\n");
		err = PTR_ERR(nvvcdev->class);
		goto err_class;
	}

	nvvcdev->device = device_create(nvvcdev->class, NULL,
									MKDEV(nvvcdev->maj_num, 0),
									NULL, "vconsole");
	if (IS_ERR(nvvcdev->device)) {
		pr_err("device_create failed\n");
		err = PTR_ERR(nvvcdev->device);
		goto err_device;
	}
	pr_debug("Register nvvc SUCCESS\n");
	return 0;

err_device:
	class_destroy(nvvcdev->class);
err_class:
	unregister_chrdev(nvvcdev->maj_num, "vconsole");
err:
	pr_err("Register nvvc FAILURE. Error=%d\n", err);
	return err;
}

static void nvvc_unregister_dev(nvvc_dev_t *nvvcdev)
{
	pr_debug("Un-register nvvc\n");
	device_destroy(nvvcdev->class, MKDEV(nvvcdev->maj_num, 0));
	class_unregister(nvvcdev->class);
	class_destroy(nvvcdev->class);
	unregister_chrdev(nvvcdev->maj_num, "vconsole");
}

static int __init nvvc_init(void)
{
	int err = 0;

	pr_debug("Init nvvc\n");

	nvvcdev = kzalloc(sizeof(nvvc_dev_t), GFP_KERNEL);
	if (nvvcdev == NULL) {
		err = -ENOMEM;
		goto err;
	}

	INIT_KFIFO(nvvcdev->nvvc_rx_fifo);
	INIT_KFIFO(nvvcdev->nvvc_tx_fifo);
	INIT_WORK(&nvvcdev->work, nvvc_work);

	init_waitqueue_head(&nvvcdev->read_wait);
	init_waitqueue_head(&nvvcdev->write_wait);

	err = nvvc_hv_init(nvvcdev);
	if (err) {
		goto err;
	}

	err = nvvc_register_dev(nvvcdev);
	if (err) {
		goto err;
	}

	pr_debug("Init nvvc SUCCESS\n");

err:
	return err;
}

static void __exit nvvc_exit(void)
{
	pr_debug("Exit nvvc\n");
	nvvc_unregister_dev(nvvcdev);
	nvvc_hv_deinit(nvvcdev);
	kfree(nvvcdev);
	pr_debug("Exit nvvc SUCCESS\n");
}

module_init(nvvc_init);
module_exit(nvvc_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nitin Kumbhar");
MODULE_VERSION("0.1");
