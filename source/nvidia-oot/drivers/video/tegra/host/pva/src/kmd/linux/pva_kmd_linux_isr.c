// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_device.h"
#include "pva_kmd_linux_device.h"
#include "pva_kmd_linux_isr.h"
#include "pva_kmd_linux_device_api.h"

static struct pva_kmd_isr_data *get_isr(struct pva_kmd_device *pva,
					enum pva_kmd_intr_line intr_line)
{
	struct nvpva_device_data *pdata = pva_kmd_linux_device_get_data(pva);
	struct pva_kmd_isr_data *isr_data;

	ASSERT(intr_line < PVA_KMD_INTR_LINE_COUNT);
	isr_data = &pdata->isr[intr_line];
	if (!isr_data->binded) {
		return NULL;
	}

	return isr_data;
}

static irqreturn_t pva_isr(int irq, void *dev_id)
{
	struct pva_kmd_isr_data *isr_data = (struct pva_kmd_isr_data *)dev_id;

	isr_data->handler(isr_data->handler_data, isr_data->intr_line);
	return IRQ_HANDLED;
}

enum pva_error pva_kmd_bind_intr_handler(struct pva_kmd_device *pva,
					 enum pva_kmd_intr_line intr_line,
					 pva_kmd_intr_handler_t handler,
					 void *data)
{
	int err = 0;
	struct nvpva_device_data *pdata = pva_kmd_linux_device_get_data(pva);
	struct pva_kmd_isr_data *isr_data = &pdata->isr[intr_line];
	enum pva_error pva_err = PVA_SUCCESS;
	int irq;

	ASSERT(isr_data->binded == false);
	irq = platform_get_irq(pdata->pdev, intr_line);
	if (irq < 0) {
		pva_kmd_log_err("Failed to get irq number");
		pva_err = kernel_err2pva_err(irq);
		goto err_out;
	}

	isr_data->irq = irq;
	isr_data->handler = handler;
	isr_data->handler_data = data;
	isr_data->intr_line = intr_line;
	err = request_threaded_irq(isr_data->irq, NULL, pva_isr, IRQF_ONESHOT,
				   "pva-isr", isr_data);
	if (err != 0) {
		pva_kmd_log_err("Failed to bind interrupt handler");
		pva_err = kernel_err2pva_err(err);
		goto err_out;
	}

	isr_data->binded = true;

	return PVA_SUCCESS;
err_out:
	return pva_err;
}

void pva_kmd_enable_intr(struct pva_kmd_device *pva,
			 enum pva_kmd_intr_line intr_line)
{
	struct pva_kmd_isr_data *isr_data = get_isr(pva, intr_line);
	if (isr_data != NULL) {
		enable_irq(isr_data->irq);
	}
}

void pva_kmd_disable_intr_nosync(struct pva_kmd_device *pva,
				 enum pva_kmd_intr_line intr_line)
{
	struct pva_kmd_isr_data *isr_data = get_isr(pva, intr_line);
	if (isr_data != NULL) {
		disable_irq_nosync(isr_data->irq);
	}
}

void pva_kmd_free_intr(struct pva_kmd_device *pva,
		       enum pva_kmd_intr_line intr_line)
{
	struct pva_kmd_isr_data *isr_data = get_isr(pva, intr_line);
	ASSERT(isr_data != NULL);

	(void)free_irq(isr_data->irq, isr_data);
	isr_data->binded = false;
}
