// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved. */

#include "mods_internal.h"
#include <linux/arm-smccc.h>

#define SMCCC_VERSION			0x80000000
#define TEGRA_SIP_RIST_SETUP		0xC200FF08
#define TEGRA_SIP_RIST_STATUS		0xC200FF04
#define TEGRA_SIP_RIST_STATUS_v2	0xC200FF09

/*
 * MODS_ESC_OIST_STATUS is used to make SMC calls to the ATF driver for
 * OIST/RIST.
 *
 * The 'smc_func_id' is used to controal the SMC function ID. Potential SMC IDs
 * include SMCCC_VERSION, TEGRA_SIP_RIST_STATUS, TEGRA_SIP_RIST_STATUS_v2,
 * and TEGRA_SIP_RIST_SETUP.
 *
 * The 'aX' fields are used to populate the input parameters to the various SMC
 * calls. The remaining fields ('smc_status', 'smc_retval', ist_status',
 * 'rist_setup_done') are used to relay return information back to the caller
 *
 * Input parameters --
 * SMCCC_VERSION:
 *     a1 - unused, but a default value of 0 is passed
 * TEGRA_SIP_RIST_STATUS:
 *     a1 - logical core ID to retrieve status for
 *     a2 - unused but expected to pass value of 1
 * TEGRA_SIP_RIST_STATUS_v2:
 *     a1 - physical core ID to retrieve status for
 * TEGRA_SIP_RIST_SETUP:
 *     a1 - test vector data physical address
 *     a2 - test vector data size
 *     a3 - event timer buffer physical address
 *     a4 - 'pll_sel' (bit 0) feature enable/disable
 */
int esc_mods_oist_status(struct mods_client             *client,
			 struct MODS_TEGRA_OIST_STATUS  *p)
{
	int ret = 0;
	struct arm_smccc_res res = { 0 };

	LOG_ENT();
	switch (p->smc_func_id) {
	case SMCCC_VERSION:
		// For SMC version, We are only reading res.a0 value, not a1,a2,a3
		arm_smccc_1_1_smc(p->smc_func_id, res.a0, &res);
		p->smc_status = res.a0;
		break;

	case TEGRA_SIP_RIST_STATUS:
		// a1 - logical core ID
		// a2 - unused on some implementations, but passed to be compatible
		arm_smccc_1_1_smc(p->smc_func_id, p->a1, p->a2, &res);
		p->smc_retval = res.a0;
		p->smc_status = res.a1;
		p->ist_status = res.a2;
		break;

	case TEGRA_SIP_RIST_STATUS_v2:
		// a1 - physical core ID
		// a2 - unused on some implementations, but passed to be compatible
		arm_smccc_1_1_smc(p->smc_func_id, p->a1, p->a2, &res);
		p->smc_retval = res.a0;
		p->smc_status = res.a1;
		p->ist_status = res.a2;
		break;

	case TEGRA_SIP_RIST_SETUP:
		// a1 - tvt_addr
		// a2 - tvt_size
		// a3 - evt_buff
		// a4 - pll_sel (bit 0)
		arm_smccc_1_1_smc(p->smc_func_id, p->a1, p->a2, p->a3, p->a4, &res);
		p->smc_retval = res.a0;
		p->rist_setup_done = res.a1;
		break;

	default:
		cl_error("invalid smc_func_id 0x%llx\n",
			 (unsigned long long)p->smc_func_id);
		LOG_EXT();
		ret = -EINVAL;
	}

	LOG_EXT();
	return ret;
}
