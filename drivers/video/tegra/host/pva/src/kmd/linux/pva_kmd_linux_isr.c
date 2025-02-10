/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#include <linux/nvhost.h>
#include "pva_kmd_device.h"
#include "pva_kmd_linux_device.h"
#include "pva_kmd_linux_isr.h"

static struct pva_kmd_isr_data *get_isr(struct pva_kmd_device *pva,
					enum pva_kmd_intr_line intr_line)
{
	struct pva_kmd_linux_device_data *plat_data =
		pva_kmd_linux_device_get_data(pva);
	struct pva_kmd_isr_data *isr_data;
	ASSERT(intr_line < PVA_KMD_INTR_LINE_COUNT);
	isr_data = &plat_data->isr[intr_line];
	ASSERT(isr_data->binded);
	return isr_data;
}
static irqreturn_t pva_isr(int irq, void *dev_id)
{
	struct pva_kmd_isr_data *isr_data = (struct pva_kmd_isr_data *)dev_id;

	isr_data->handler(isr_data->handler_data);
	return IRQ_HANDLED;
}

enum pva_error pva_kmd_bind_intr_handler(struct pva_kmd_device *pva,
					 enum pva_kmd_intr_line intr_line,
					 pva_kmd_intr_handler_t handler,
					 void *data)
{
	int err = 0;
	struct pva_kmd_linux_device_data *plat_data =
		pva_kmd_linux_device_get_data(pva);
	struct pva_kmd_isr_data *isr_data = &plat_data->isr[intr_line];
	struct nvhost_device_data *props = plat_data->pva_device_properties;

	isr_data->irq = platform_get_irq(props->pdev, intr_line);
	isr_data->handler = handler;
	isr_data->handler_data = data;
	isr_data->binded = true;
	err = request_threaded_irq(isr_data->irq, NULL, pva_isr, IRQF_ONESHOT,
				   "pva-isr", isr_data);

	if (err != 0) {
		pva_kmd_log_err("Failed to bind interrupt handler");
	}

	return kernel_err2pva_err(err);
}

void pva_kmd_enable_intr(struct pva_kmd_device *pva,
			 enum pva_kmd_intr_line intr_line)
{
	struct pva_kmd_isr_data *isr_data = get_isr(pva, intr_line);
	enable_irq(isr_data->irq);
}

void pva_kmd_disable_intr(struct pva_kmd_device *pva,
			  enum pva_kmd_intr_line intr_line)
{
	struct pva_kmd_isr_data *isr_data = get_isr(pva, intr_line);
	disable_irq(isr_data->irq);
}

void pva_kmd_free_intr(struct pva_kmd_device *pva,
		       enum pva_kmd_intr_line intr_line)
{
	struct pva_kmd_isr_data *isr_data = get_isr(pva, intr_line);
	free_irq(isr_data->irq, isr_data);
	isr_data->binded = false;
}
