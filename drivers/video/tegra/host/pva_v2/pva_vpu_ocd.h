/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, NVIDIA Corporation.  All rights reserved.
 */

#ifndef PVA_VPU_OCD_H
#define PVA_VPU_OCD_H
#include <linux/types.h>
#include "pva.h"

int pva_vpu_ocd_init(struct pva *pva);
int pva_vpu_ocd_io(struct pva_vpu_dbg_block *block, u32 instr, const u32 *wdata,
		   u32 nw, u32 *rdata, u32 nr);
#endif // PVA_VPU_OCD_H
