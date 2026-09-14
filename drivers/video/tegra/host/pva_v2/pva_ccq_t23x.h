/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2025, NVIDIA CORPORATION. All rights reserved.
 *
 * PVA Command Queue Interface handling
 */

#ifndef PVA_CCQ_T23X_H
#define PVA_CCQ_T23X_H

#include <linux/kernel.h>

#include "pva.h"
#include "pva_status_regs.h"

int pva_ccq_send_task_t23x(struct pva *pva, u32 queue_id, dma_addr_t task_addr,
			   u8 batchsize, u8 *task_status, u32 flags);
void pva_ccq_isr_handler(struct pva *pva, unsigned int queue_id);
int pva_ccq_send_cmd_sync(struct pva *pva, struct pva_cmd_s *cmd, u32 nregs,
			  u32 queue_id,
			  struct pva_cmd_status_regs *ccq_status_regs);
int pva_send_cmd_sync(struct pva *pva, struct pva_cmd_s *cmd, u32 nregs,
		      u32 queue_id,
		      struct pva_cmd_status_regs *ccq_status_regs);
int pva_send_cmd_sync_locked(struct pva *pva, struct pva_cmd_s *cmd, u32 nregs,
			     u32 queue_id,
			     struct pva_cmd_status_regs *ccq_status_regs);

#endif
