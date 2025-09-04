
/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.
 */

#include <config.h>
#include <initcall.h>
#include <kernel/panic.h>
#include <drivers/tegra/tegra_se_keyslot.h>

static TEE_Result tegra_keyslots_clear(void)
{
	TEE_Result rc = TEE_SUCCESS;

	DMSG("Start to clear keyslots...");
	rc = tegra_se_clear_aes_keyslots();
	/* Some platforms don't support keyslot clearing so ignore it */
	if (rc != TEE_SUCCESS && rc != TEE_ERROR_NOT_SUPPORTED) {
		EMSG("%s: Failed to clear SE keyslots (%x).", __func__, rc);
		panic();
	}

	return rc;
}
boot_final(tegra_keyslots_clear);
