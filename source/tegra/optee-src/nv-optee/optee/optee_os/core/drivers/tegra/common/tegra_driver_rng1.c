/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES.
 */

#include <config.h>
#include <io.h>
#include <kernel/boot.h>
#include <kernel/dt.h>
#include <libfdt.h>
#include <stdlib.h>
#include <crypto/crypto.h>
#include <rng_support.h>
#include <drivers/tegra/tegra_se_rng.h>
#include <tegra_driver_rng1.h>
#include <tegra_driver_common.h>

#ifdef CFG_DT
static const char *const tegra_se_rng1_dt_match_table[] = {
	"nvidia,tegra194-se0-elp-rng1",
	"nvidia,tegra234-rng1",
};

static TEE_Result tegra_rng1_dt_init(vaddr_t *va)
{
	void *fdt = NULL;
	int node = -1;
	uint32_t i = 0;
	size_t size;
	enum dt_map_dev_directive mapping = DT_MAP_AUTO;

	if (va == NULL)
		return TEE_ERROR_BAD_PARAMETERS;

	fdt = get_dt();
	if (!fdt) {
		EMSG("%s: DTB is not present.", __func__);
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	for (i = 0; i < ARRAY_SIZE(tegra_se_rng1_dt_match_table); i++) {
		node = fdt_node_offset_by_compatible(fdt, 0,
					tegra_se_rng1_dt_match_table[i]);
		if (node >= 0)
			break;
	}

	if (node < 0) {
		EMSG("%s: DT not found (%x).", __func__, node);
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	if (dt_map_dev(fdt, node, va, &size, mapping) < 0) {
		EMSG("%s: DT unable to map device address.", __func__);
		return TEE_ERROR_GENERIC;
	}

	return TEE_SUCCESS;
}
#else
static TEE_Result tegra_rng1_dt_init(vaddr_t *va)
{
	(void) va;
	return TEE_ERROR_NOT_SUPPORTED;
}
#endif

TEE_Result tegra_rng1_map_regs(vaddr_t *va)
{
	if (IS_ENABLED(CFG_DT))
		return tegra_rng1_dt_init(va);
	else
		return iomap_pa2va(TEGRA_RNG1_BASE, TEGRA_RNG1_SIZE, va);
}

/* Override the weak hw_get_random_bytes */
TEE_Result hw_get_random_bytes(void *buf, size_t blen)
{
	return tegra_se_rng_get_random(buf, blen);
}
