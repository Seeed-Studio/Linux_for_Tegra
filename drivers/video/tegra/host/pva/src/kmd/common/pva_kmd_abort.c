// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_abort.h"
#include "pva_kmd_device.h"
#include "pva_kmd_regs.h"
#include "pva_kmd_silicon_utils.h"

void pva_kmd_abort_fw(struct pva_kmd_device *pva, uint32_t error_code)
{
	// HW watchdog may fire repeatedly if PVA is hung. Therefore, disable all
	// interrupts to protect KMD from potential interrupt floods.
	pva_kmd_disable_all_interrupts_nosync(pva);

	pva_kmd_report_error_fsi(pva, error_code);
	// We will handle firmware reboot after all contexts are closed and a new
	// one is re-opened again
	pva->recovery = true;
}
