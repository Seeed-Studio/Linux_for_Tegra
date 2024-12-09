/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2025, NVIDIA CORPORATION. All rights reserved.
 *
 * PVA Command Queue Interface handling
 */

#ifndef PVA_CCQ_T19X_H
#define PVA_CCQ_T19X_H

#include <linux/kernel.h>

#include "pva.h"

int pva_ccq_send_task_t19x(struct pva *pva, u32 queue_id, dma_addr_t task_addr,
			   u8 batchsize, u8 *task_status, u32 flags);

#endif
