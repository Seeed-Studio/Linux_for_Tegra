// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2014-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/atomic.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/platform_device.h>
#include <linux/tegra_nvadsp.h>

#include "dev.h"
#include "hwmailbox.h"

/*
 * Mailbox 0 is for receiving messages
 * from ADSP i.e. CPU <-- ADSP.
 */
static inline u32 recv_hwmbox(struct nvadsp_drv_data *drv)
{
	return drv->chip_data->hwmb.hwmbox0_reg;
}

/*
 * Mailbox 1 is for sending messages
 * to ADSP i.e. CPU --> ADSP
 */
static inline u32 send_hwmbox(struct nvadsp_drv_data *drv)
{
	return drv->chip_data->hwmb.hwmbox1_reg;
}

static u32 hwmb_reg_idx(struct nvadsp_drv_data *drv)
{
	return drv->chip_data->hwmb.reg_idx;
}

u32 hwmbox_readl(struct nvadsp_drv_data *drv, u32 reg)
{
	return readl(drv->base_regs[hwmb_reg_idx(drv)] + reg);
}

void hwmbox_writel(struct nvadsp_drv_data *drv, u32 val, u32 reg)
{
	writel(val, drv->base_regs[hwmb_reg_idx(drv)] + reg);
}


#define PRINT_HWMBOX(d, x) \
	pr_err("%s: 0x%x\n", #x, hwmbox_readl(d, x))

void dump_mailbox_regs(struct nvadsp_drv_data *drv)
{
	pr_err("dumping hwmailbox registers ...\n");
	PRINT_HWMBOX(drv, recv_hwmbox(drv));
	PRINT_HWMBOX(drv, send_hwmbox(drv));
}

static void hwmboxq_init(struct hwmbox_queue *queue)
{
	queue->head = 0;
	queue->tail = 0;
	queue->count = 0;
	init_completion(&queue->comp);
	spin_lock_init(&queue->lock);
	queue->is_hwmbox_busy = false;
	queue->hwmbox_last_msg = 0;
}

/* Must be called with queue lock held in non-interrupt context */
static inline bool
is_hwmboxq_empty(struct hwmbox_queue *queue)
{
	return (queue->count == 0);
}

/* Must be called with queue lock held in non-interrupt context */
static inline bool
is_hwmboxq_full(struct hwmbox_queue *queue)
{
	return (queue->count == HWMBOX_QUEUE_SIZE);
}

/* Must be called with queue lock held in non-interrupt context */
static status_t hwmboxq_enqueue(struct hwmbox_queue *queue,
					    uint32_t data)
{
	int ret = 0;

	if (is_hwmboxq_full(queue)) {
		ret = -EBUSY;
		goto comp;
	}
	queue->array[queue->tail] = data;
	queue->tail = (queue->tail + 1) & HWMBOX_QUEUE_SIZE_MASK;
	queue->count++;

	if (is_hwmboxq_full(queue))
		goto comp;
	else
		goto out;

 comp:
	reinit_completion(&queue->comp);
 out:
	return ret;
}

status_t nvadsp_hwmbox_send_data(struct nvadsp_drv_data *drv,
				uint16_t mid, uint32_t data, uint32_t flags)
{
	struct hwmbox_queue *hwmbox_send_queue = drv->hwmbox_send_queue;
	spinlock_t *lock = &hwmbox_send_queue->lock;
	u32 empty_int_ie = drv->chip_data->hwmb.empty_int_ie;
	unsigned long lockflags;
	int ret = 0;

	if (flags & NVADSP_MBOX_SMSG) {
		data = PREPARE_HWMBOX_SMSG(mid, data);
		pr_debug("nvadsp_mbox_send: data: 0x%x\n", data);
	}

	/* TODO handle LMSG */

	spin_lock_irqsave(lock, lockflags);

	if (!hwmbox_send_queue->is_hwmbox_busy) {
		hwmbox_send_queue->is_hwmbox_busy = true;
		pr_debug("nvadsp_mbox_send: empty mailbox. write to mailbox.\n");
		hwmbox_send_queue->hwmbox_last_msg = data;
		hwmbox_writel(drv, data, send_hwmbox(drv));
		if (empty_int_ie) {
			hwmbox_writel(drv, INT_ENABLE,
					send_hwmbox(drv) + empty_int_ie);
		}
	} else {
		pr_debug("nvadsp_mbox_send: enqueue data\n");
		ret = hwmboxq_enqueue(hwmbox_send_queue, data);
	}
	spin_unlock_irqrestore(lock, lockflags);
	return ret;
}

/* Must be called with queue lock held in non-interrupt context */
static status_t hwmboxq_dequeue(struct hwmbox_queue *queue,
					    uint32_t *data)
{
	int ret = 0;

	if (is_hwmboxq_empty(queue)) {
		ret = -EBUSY;
		goto out;
	}

	if (is_hwmboxq_full(queue))
		complete_all(&queue->comp);

	*data = queue->array[queue->head];
	queue->head = (queue->head + 1) & HWMBOX_QUEUE_SIZE_MASK;
	queue->count--;

 out:
	return ret;
}

static irqreturn_t hwmbox_send_empty_int_handler(int irq, void *devid)
{
	struct platform_device *pdev = devid;
	struct device *dev = &pdev->dev;
	struct nvadsp_drv_data *drv = platform_get_drvdata(pdev);
	struct hwmbox_queue *hwmbox_send_queue = drv->hwmbox_send_queue;
	spinlock_t *lock = &hwmbox_send_queue->lock;
	u32 empty_int_ie = drv->chip_data->hwmb.empty_int_ie;
	unsigned long lockflags;
	uint32_t data, hwmbox_last_msg;
	uint16_t last_mboxid;
	struct nvadsp_mbox *mbox;
	int ret;

	if (!hwmbox_send_queue->is_hwmbox_busy)
		return IRQ_HANDLED;

	spin_lock_irqsave(lock, lockflags);

	data = hwmbox_readl(drv, send_hwmbox(drv));
	if (data != PREPARE_HWMBOX_EMPTY_MSG())
		dev_err(dev, "last mailbox sent failed with 0x%x\n", data);

	hwmbox_last_msg = hwmbox_send_queue->hwmbox_last_msg;
	last_mboxid = HWMBOX_SMSG_MID(hwmbox_last_msg);
	mbox = drv->mboxes[last_mboxid];

	if (mbox) {
		nvadsp_mbox_handler_t ack_handler = mbox->ack_handler;

		if (ack_handler) {
			uint32_t msg = HWMBOX_SMSG_MSG(hwmbox_last_msg);

			ack_handler(msg, mbox->hdata);
		}
	}

	ret = hwmboxq_dequeue(hwmbox_send_queue, &data);
	if (ret == 0) {
		hwmbox_send_queue->hwmbox_last_msg = data;
		hwmbox_writel(drv, data, send_hwmbox(drv));
		dev_dbg(dev, "Writing 0x%x to SEND_HWMBOX\n", data);
	} else {
		hwmbox_send_queue->is_hwmbox_busy = false;
		if (empty_int_ie)
			hwmbox_writel(drv, INT_DISABLE,
					send_hwmbox(drv) + empty_int_ie);
	}
	spin_unlock_irqrestore(lock, lockflags);

	return IRQ_HANDLED;
}

static irqreturn_t hwmbox_recv_full_int_handler(int irq, void *devid)
{
	struct platform_device *pdev = devid;
	struct device *dev = &pdev->dev;
	struct nvadsp_drv_data *drv = platform_get_drvdata(pdev);
	uint32_t data;
	int ret;

	data = hwmbox_readl(drv, recv_hwmbox(drv));
	hwmbox_writel(drv, PREPARE_HWMBOX_EMPTY_MSG(), recv_hwmbox(drv));

	if (IS_HWMBOX_MSG_SMSG(data)) {
		uint16_t mboxid = HWMBOX_SMSG_MID(data);
		struct nvadsp_mbox *mbox = drv->mboxes[mboxid];

		if (!mbox) {
			dev_info(dev,
				 "Failed to get mbox for mboxid: %u\n",
				 mboxid);
			goto out;
		}

		if (mbox->handler) {
			mbox->handler(HWMBOX_SMSG_MSG(data), mbox->hdata);
		} else {
			ret = nvadsp_mboxq_enqueue(&mbox->recv_queue,
						   HWMBOX_SMSG_MSG(data));
			if (ret) {
				dev_info(dev,
					 "Failed to deliver msg 0x%x to"
					 " mbox id %u\n",
					 HWMBOX_SMSG_MSG(data), mboxid);
				goto out;
			}
		}
	} else if (IS_HWMBOX_MSG_LMSG(data)) {
		/* TODO */
	}
 out:
	return IRQ_HANDLED;
}

void nvadsp_free_hwmbox_interrupts(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int recv_virq, send_virq;

	recv_virq = drv->agic_irqs[MBOX_RECV_VIRQ];
	send_virq = drv->agic_irqs[MBOX_SEND_VIRQ];

	devm_free_irq(dev, recv_virq, pdev);
	devm_free_irq(dev, send_virq, pdev);
}

int nvadsp_setup_hwmbox_interrupts(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	u32 empty_int_ie = drv->chip_data->hwmb.empty_int_ie;
	int recv_virq, send_virq;
	int ret;

	if (drv->map_hwmbox_interrupts) {
		ret = drv->map_hwmbox_interrupts(drv);
		if (ret)
			goto err;
	}

	recv_virq = drv->agic_irqs[MBOX_RECV_VIRQ];
	send_virq = drv->agic_irqs[MBOX_SEND_VIRQ];

	ret = devm_request_irq(dev, recv_virq, hwmbox_recv_full_int_handler,
			  IRQF_TRIGGER_RISING, "hwmbox0_recv_full", pdev);
	if (ret)
		goto err;

	if (empty_int_ie)
		hwmbox_writel(drv, INT_DISABLE,
				send_hwmbox(drv) + empty_int_ie);
	ret = devm_request_irq(dev, send_virq, hwmbox_send_empty_int_handler,
			  IRQF_TRIGGER_RISING,
			  "hwmbox1_send_empty", pdev);
	if (ret)
		goto free_interrupts;

	return ret;

 free_interrupts:
	nvadsp_free_hwmbox_interrupts(pdev);
 err:
	return ret;
}

int nvadsp_hwmbox_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int ret = 0;

	drv->hwmbox_send_queue = devm_kzalloc(dev,
				sizeof(struct hwmbox_queue), GFP_KERNEL);
	if (!drv->hwmbox_send_queue) {
		dev_err(dev, "Failed to allocate hwmbox_send_queue\n");
		ret = -ENOMEM;
		goto exit;
	}

	hwmboxq_init(drv->hwmbox_send_queue);

exit:
	return ret;
}
