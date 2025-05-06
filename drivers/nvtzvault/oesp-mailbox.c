// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "oesp-mailbox.h"
#include "nvtzvault-common.h"
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/memory.h>

#define MBOX_FORMAT_FLAG 	(0x1+(0x0<<8)+('P'<<16)+('S'<<24))


/* Mailbox Request register offsets */
#define REQ_OPCODE_OFFSET		0x800U
#define REQ_FORMAT_FLAG_OFFSET	0x804U
#define DRIVER_ID_OFFSET	0x808U
#define IOVA_LOW_OFFSET		0x80CU
#define IOVA_HIGH_OFFSET	0x810U
#define MSG_SIZE_OFFSET		0x814U

/* Mailbox Response register offsets */
#define RESP_OPCODE_OFFSET	0x1000U
#define RESP_FORMAT_FLAG_OFFSET	0x1004U
#define RESP_STATUS_OFFSET		0x1008U

/* Mailbox register offset */
#define EXT_CTRL_OFFSET		0x4U
#define PSC_CTRL_REG_OFFSET	0x8U

/* Mailbox register bitfields */
#define MBOX_IN_VALID		0x1U
#define LIC_INTR_EN		0x100U
#define MBOX_OUT_DONE		0x10U
#define MBOX_OUT_VALID		0x1U

#define MAX_MAILBOX		8U
#define MBOX_TIMEOUT_MS		(300U * 1000U)	/* Set to 5 minutes, required for Secure Storage */

static DECLARE_COMPLETION(mbox_completion);

struct mbox_req {
	u32 task_opcode;
	u32 format_flag;
	u32 tos_driver_id;
	u32 hpse_carveout_iova_lsb;
	u32 hpse_carveout_iova_msb;
	u32 msg_size;
};

struct mbox_resp {
	u32 task_opcode;
	u32 format_flag;
	u32 status;
};

struct mbox_ctx {
	void *hpse_carveout_base_va;
	uint64_t hpse_carveout_base_iova;
	uint64_t hpse_carveout_size;
	void *oesp_mbox_reg_base_va;
	uint64_t oesp_mbox_reg_size;
	struct mbox_resp resp;
} g_mbox_ctx;

int32_t oesp_mailbox_send_and_read(void *buf_ptr, uint32_t buf_len, uint32_t task_opcode,
			uint32_t driver_id)
{
	struct mbox_req req;
	u8 *oesp_reg_mem_ptr = g_mbox_ctx.oesp_mbox_reg_base_va;
	volatile u32 reg_val;
	int32_t ret = 0;
	unsigned long timeout;

	reinit_completion(&mbox_completion);

	/* Send request */
	req.task_opcode = task_opcode;
	writel(req.task_opcode, oesp_reg_mem_ptr + REQ_OPCODE_OFFSET);

	req.format_flag = MBOX_FORMAT_FLAG;
	writel(req.format_flag, oesp_reg_mem_ptr + REQ_FORMAT_FLAG_OFFSET);

	req.tos_driver_id = driver_id;
	writel(req.tos_driver_id, oesp_reg_mem_ptr + DRIVER_ID_OFFSET);

	for (int i = 0; i < buf_len; i++)
		((uint8_t *)g_mbox_ctx.hpse_carveout_base_va)[i] = ((uint8_t *)buf_ptr)[i];

	req.hpse_carveout_iova_lsb = g_mbox_ctx.hpse_carveout_base_iova & UINT32_MAX;
	writel(req.hpse_carveout_iova_lsb, oesp_reg_mem_ptr + IOVA_LOW_OFFSET);

	req.hpse_carveout_iova_msb = g_mbox_ctx.hpse_carveout_base_iova >> 32U;
	writel(req.hpse_carveout_iova_msb, oesp_reg_mem_ptr + IOVA_HIGH_OFFSET);

	req.msg_size = buf_len;
	writel(req.msg_size, oesp_reg_mem_ptr + MSG_SIZE_OFFSET);

	reg_val = readl(oesp_reg_mem_ptr + EXT_CTRL_OFFSET);
	writel((reg_val | MBOX_IN_VALID | LIC_INTR_EN), oesp_reg_mem_ptr + EXT_CTRL_OFFSET);

	/* Ensure write buffer is flushed before waiting for response */
	wmb();

	/* Wait for response */
	timeout = wait_for_completion_timeout(&mbox_completion,
				msecs_to_jiffies(MBOX_TIMEOUT_MS));
	if (timeout == 0) {
		reg_val = readl(oesp_reg_mem_ptr + PSC_CTRL_REG_OFFSET);
		if ((reg_val & MBOX_OUT_VALID) == 0x1U)
			NVTZVAULT_ERR("%s: MBOX_OUT_VALID is set\n", __func__);

		NVTZVAULT_ERR("%s: Timeout waiting for response\n", __func__);
		ret = -ETIMEDOUT;
		goto end;
	}

	if (g_mbox_ctx.resp.status != 0U) {
		ret = -EINVAL;
		NVTZVAULT_ERR("%s resp.status %u\n", __func__, g_mbox_ctx.resp.status);
		goto end;
	}

	if (g_mbox_ctx.resp.format_flag != MBOX_FORMAT_FLAG) {
		ret = -EINVAL;
		NVTZVAULT_ERR("%s invalid format flag %x\n", __func__,
				g_mbox_ctx.resp.format_flag);
		goto end;
	}

	if (g_mbox_ctx.resp.task_opcode != task_opcode) {
		ret = -EINVAL;
		NVTZVAULT_ERR("%s invalid opcode %x\n", __func__,
				g_mbox_ctx.resp.task_opcode);
		goto end;
	}

	/* Copy response data from carveout back to input buffer */
	for (int i = 0; i < buf_len; i++)
		((uint8_t *)buf_ptr)[i] = ((uint8_t *)g_mbox_ctx.hpse_carveout_base_va)[i];

end:
	return ret;
}

/**
 * @brief Interrupt handler for HPSE mailbox
 *
 * Handles interrupts from the OESP mailbox by checking status registers
 * and clearing interrupt flags.
 *
 * @param[in] irq The interrupt number
 * @param[in] dev_id The device ID pointer passed during request_irq
 *
 * @return IRQ_HANDLED if interrupt was handled
 *         IRQ_NONE if interrupt was not for this device
 */
static irqreturn_t tegra_hpse_irq_handler(int irq, void *dev_id)
{
	volatile u32 reg_val;
	u8 *oesp_reg_mem_ptr = g_mbox_ctx.oesp_mbox_reg_base_va;
	volatile struct mbox_resp *resp = &g_mbox_ctx.resp;

	/* Read PSC control register to check interrupt status */
	reg_val = readl(oesp_reg_mem_ptr + PSC_CTRL_REG_OFFSET);

	/* Check if this is our interrupt */
	if ((reg_val & MBOX_OUT_VALID) == 0U)
		return IRQ_NONE;

	/* Read response registers */
	resp->task_opcode = readl(oesp_reg_mem_ptr + RESP_OPCODE_OFFSET);
	resp->format_flag = readl(oesp_reg_mem_ptr + RESP_FORMAT_FLAG_OFFSET);
	resp->status = readl(oesp_reg_mem_ptr + RESP_STATUS_OFFSET);

	/* Ensure register read is complete before acknowledging response */
	rmb();

	/* Set MBOX_OUT_DONE to acknowledge response */
	reg_val = readl(oesp_reg_mem_ptr + EXT_CTRL_OFFSET);
	reg_val |= MBOX_OUT_DONE;
	writel(reg_val, oesp_reg_mem_ptr + EXT_CTRL_OFFSET);

	/* Signal completion to waiting thread */
	complete(&mbox_completion);

	return IRQ_HANDLED;
}

int32_t oesp_mailbox_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *hpse_node = dev->of_node;
	struct device_node *hpse_carveout_node;
	struct device_node *oesp_mbox_node;
	u64 hpse_carveout_base_iova;
	void *hpse_carveout_base_va;
	u64 hpse_carveout_size;
	u64 oesp_mbox_reg_base_iova;
	void *oesp_mbox_reg_base_va;
	u64 oesp_mbox_reg_size;
	int irq;
	int ret;

	if (!hpse_node)
		return -ENODEV;

	hpse_carveout_node = of_find_node_by_path("/reserved-memory/hpse-carveout");
	if (!hpse_carveout_node) {
		dev_err(dev, "hpse-carveout node missing\n");
		return -EINVAL;
	}

	// Read the 'reg' property from the oesp-mailbox node
	if (of_property_read_u64(hpse_carveout_node, "reg", &hpse_carveout_base_iova)) {
		dev_err(dev, "reg property missing in hpse-carveout\n");
		return -EINVAL;
	}

	if (!hpse_carveout_base_iova) {
		dev_err(dev, "hpse carveout iova is NULL\n");
		return -EINVAL;
	}

	// The size is the second u64 value in the 'reg' property
	if (of_property_read_u64_index(hpse_carveout_node, "reg", 1, &hpse_carveout_size)) {
		dev_err(dev, "reg size missing in hpse-carveout\n");
		return -EINVAL;
	}

	hpse_carveout_base_va = devm_memremap(dev, hpse_carveout_base_iova,
			hpse_carveout_size, MEMREMAP_WB);
	if (IS_ERR_OR_NULL(hpse_carveout_base_va))
		return -ENOMEM;

	// Locate the oesp-mailbox node
	oesp_mbox_node = of_find_node_by_name(hpse_node, "oesp-mailbox");
	if (!oesp_mbox_node) {
		dev_err(dev, "oesp-mailbox node missing\n");
		return -EINVAL;
	}

	// Read the 'reg' property from the oesp-mailbox node
	if (of_property_read_u64(oesp_mbox_node, "reg", &oesp_mbox_reg_base_iova)) {
		dev_err(dev, "reg property missing for oesp mailbox\n");
		return -EINVAL;
	}

	if (!oesp_mbox_reg_base_iova) {
		dev_err(dev, "oesp reg base is NULL\n");
		return -EINVAL;
	}

	// The size is the second u64 value in the 'reg' property
	if (of_property_read_u64_index(oesp_mbox_node, "reg", 1, &oesp_mbox_reg_size)) {
		dev_err(dev, "reg size missing for oesp mailbox\n");
		return -EINVAL;
	}

	oesp_mbox_reg_base_va = devm_ioremap(dev, oesp_mbox_reg_base_iova, oesp_mbox_reg_size);
	if (!oesp_mbox_reg_base_va) {
		dev_err(dev, "ioremap failed\n");
		return -EINVAL;
	}

	g_mbox_ctx.hpse_carveout_base_va = hpse_carveout_base_va;
	g_mbox_ctx.hpse_carveout_base_iova = hpse_carveout_base_iova;
	g_mbox_ctx.hpse_carveout_size = hpse_carveout_size;
	g_mbox_ctx.oesp_mbox_reg_base_va = oesp_mbox_reg_base_va;
	g_mbox_ctx.oesp_mbox_reg_size = oesp_mbox_reg_size;

	/* Get IRQ from tegra-hpse node */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "failed to get irq from tegra-hpse node: %d\n", irq);
		return irq;
	}

	ret = devm_request_irq(dev, irq, tegra_hpse_irq_handler,
			      IRQF_ONESHOT, dev_name(dev), &g_mbox_ctx);
	if (ret) {
		dev_err(dev, "Failed to request IRQ %d: %d\n", irq, ret);
		return ret;
	}

	return 0;
}
