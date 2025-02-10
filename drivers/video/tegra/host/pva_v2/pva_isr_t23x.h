/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * PVA ISR interface for T23X
 */
#ifndef __NVHOST_PVA_ISR_T23X_H__
#define __NVHOST_PVA_ISR_T23X_H__

#include <linux/irqreturn.h>

irqreturn_t pva_ccq_isr(int irq, void *dev_id);

#endif
