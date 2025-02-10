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
};

#endif //PVA_KMD_LINUX_ISR_H
