// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_silicon_utils.h"
#include "pva_kmd_device.h"
#include "pva_math_utils.h"

void pva_kmd_ccq_push(struct pva_kmd_device *pva, uint8_t ccq_id,
		      uint32_t ccq_entry)
{
	pva_kmd_write(pva, pva->regspec.ccq_regs[ccq_id].fifo, ccq_entry);
}

uint32_t pva_kmd_get_ccq_space(struct pva_kmd_device *pva, uint8_t ccq_id)
{
	uint32_t status2 =
		pva_kmd_read(pva, pva->regspec.ccq_regs[ccq_id].status[2]);
	uint32_t len =
		PVA_EXTRACT(status2, PVA_REG_CCQ_STATUS2_NUM_ENTRIES_MSB,
			    PVA_REG_CCQ_STATUS2_NUM_ENTRIES_LSB, uint32_t);
	return safe_subu32((uint32_t)PVA_CCQ_DEPTH, len) / 2U;
}

void pva_kmd_disable_all_interrupts_nosync(struct pva_kmd_device *pva)
{
	for (uint8_t i = 0; i < (uint8_t)PVA_KMD_INTR_LINE_COUNT; i++) {
		pva_kmd_disable_intr_nosync(pva, (enum pva_kmd_intr_line)i);
	}
}
