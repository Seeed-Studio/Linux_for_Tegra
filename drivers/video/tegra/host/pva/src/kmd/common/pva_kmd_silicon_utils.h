/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SILICON_UTILS_H
#define PVA_KMD_SILICON_UTILS_H
#include "pva_utils.h"
#include "pva_kmd_regs.h"
#include "pva_kmd_shim_silicon.h"
#include "pva_math_utils.h"

static inline void pva_kmd_write(struct pva_kmd_device *pva, uint32_t addr,
				 uint32_t val)
{
	pva_dbg_printf("pva_kmd_write: addr=0x%x, val=0x%x\n", addr, val);
	pva_kmd_aperture_write(pva, PVA_KMD_APERTURE_PVA_CLUSTER, addr, val);
}

static inline uint32_t pva_kmd_read(struct pva_kmd_device *pva, uint32_t addr)
{
	uint32_t val;

	val = pva_kmd_aperture_read(pva, PVA_KMD_APERTURE_PVA_CLUSTER, addr);
	return val;
}

static inline void pva_kmd_write_mailbox(struct pva_kmd_device *pva,
					 uint32_t mailbox_idx, uint32_t val)
{
	uint32_t gap = PVA_REG_HSP_SM1_ADDR - PVA_REG_HSP_SM0_ADDR;
	uint32_t offset = safe_mulu32(gap, mailbox_idx);
	uint32_t addr = safe_addu32(PVA_REG_HSP_SM0_ADDR, offset);
	pva_kmd_write(pva, addr, val);
}

static inline uint32_t pva_kmd_read_mailbox(struct pva_kmd_device *pva,
					    uint32_t mailbox_idx)
{
	uint32_t gap = PVA_REG_HSP_SM1_ADDR - PVA_REG_HSP_SM0_ADDR;
	uint32_t offset = safe_mulu32(gap, mailbox_idx);
	uint32_t addr = safe_addu32(PVA_REG_HSP_SM0_ADDR, offset);
	return pva_kmd_read(pva, addr);
}

#endif // PVA_KMD_SILICON_UTILS_H
