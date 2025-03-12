// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_abort.h"
#include "pva_kmd_shim_init.h"

void pva_kmd_abort(struct pva_kmd_device *pva)
{
	//TODO: Report to FSI first about the SW error code.
	pva_kmd_log_err("Abort: FW Reset Assert");
	/* Put the FW in reset ASSERT so the user space
    cannot access the CCQ and thus force them to 
    destroy the contexts. On destroy all the contexts.
    KMD poweroff the FW whereas on first new contexts creation,
    KMD will load the firmware image & poweron device */
	pva_kmd_fw_reset_assert(pva);
	pva->recovery = true;
}