/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_LINUX_ISR_H
#define PVA_KMD_LINUX_ISR_H
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include "pva_kmd_shim_silicon.h"

struct pva_kmd_device;

struct pva_kmd_isr_data {
	struct pva_kmd_device *pva;
	bool binded;
	int irq; /*< Hardware IRQ number */

	pva_kmd_intr_handler_t handler;
	void *handler_data;
	enum pva_kmd_intr_line intr_line;
};

#endif //PVA_KMD_LINUX_ISR_H
