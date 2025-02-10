// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/nvhost.h>
#include "pva_regs.h"
#include "pva.h"
#include "pva_sec_ec.h"

static u32 pva_get_sec_ec_addrs(u32 index)
{
	u32 sec_ec_miss_addrs[] = {
		sec_ec_errslice0_missionerr_enable_r(),
		sec_ec_errslice0_latenterr_enable_r(),
		sec_ec_errslice1_missionerr_enable_r(),
		sec_ec_errslice1_latenterr_enable_r(),
		sec_ec_errslice2_missionerr_enable_r(),
		sec_ec_errslice2_latenterr_enable_r(),
		sec_ec_errslice3_missionerr_enable_r(),
		sec_ec_errslice3_latenterr_enable_r()
	};

	return sec_ec_miss_addrs[index];
};

void pva_disable_ec_err_reporting(struct pva *pva)
{

	u32 n_regs = (pva->version != PVA_HW_GEN1) ? 8 : 4;
	u32 i;

	/* save current state */
	for (i = 0; i < n_regs; i++)
		pva->ec_state[i] = host1x_readl(pva->pdev,
						pva_get_sec_ec_addrs(i));

	/* disable reporting */
	for (i = 0; i < n_regs; i++)
		host1x_writel(pva->pdev, pva_get_sec_ec_addrs(i), 0);
}

void pva_enable_ec_err_reporting(struct pva *pva)
{

	u32 n_regs = (pva->version != PVA_HW_GEN1) ? 8 : 4;
	u32 i;

	/* enable reporting */
	for (i = 0; i < n_regs; i++)
		host1x_writel(pva->pdev,
			      pva_get_sec_ec_addrs(i),
			      pva->ec_state[i]);
}
