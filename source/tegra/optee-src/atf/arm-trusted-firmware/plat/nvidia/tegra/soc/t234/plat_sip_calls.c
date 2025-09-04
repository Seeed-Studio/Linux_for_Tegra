/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdbool.h>

#include <arch.h>
#include <arch_helpers.h>
#include <assert.h>
#include <common/bl_common.h>
#include <common/debug.h>
#include <common/runtime_svc.h>
#include <lib/el3_runtime/context_mgmt.h>

#include <mce.h>
#include <memctrl.h>

#include <tegra234_private.h>
#include <tegra_platform.h>
#include <tegra_private.h>

/*
 * tegra_fake_system_suspend acts as a boolean variable controlling whether
 * we are going to take fake system suspend code or normal system suspend code
 * path. This variable is set inside the sip call handlers, when the kernel
 * requests a SIP call to set the suspend debug flags.
 */
bool tegra234_fake_system_suspend = false;

/*******************************************************************************
 * Tegra234 SiP SMCs
 ******************************************************************************/
#define TEGRA_SIP_WRITE_PFG_REGS		0xC200FF01
#define TEGRA_SIP_ENABLE_FAKE_SYSTEM_SUSPEND	0xC2FFFE03

/*******************************************************************************
 * This function is responsible for handling all T194 SiP calls
 ******************************************************************************/
int32_t plat_sip_handler(uint32_t smc_fid,
		     uint64_t x1,
		     uint64_t x2,
		     uint64_t x3,
		     uint64_t x4,
		     const void *cookie,
		     void *handle,
		     uint64_t flags)
{
	int32_t ret = 0;

	(void)x1;
	(void)x4;
	(void)cookie;
	(void)flags;

	switch (smc_fid) {
#if RAS_EXTENSION && DEBUG
	case TEGRA_SIP_WRITE_PFG_REGS:
		ret = tegra234_ras_inject_fault(x1, x2, x3);
		break;
#endif
	case TEGRA_SIP_ENABLE_FAKE_SYSTEM_SUSPEND:
		/*
		 * System suspend fake mode is set if we are on FPGA and we make
		 * a debug SIP call. This mode ensures that we exercise debug
		 * path instead of the regular code path to suit the pre-silicon
		 * platform needs. These include replacing the call to WFI by
		 * a warm reset request.
		 */
		if (tegra_platform_is_fpga() || tegra_platform_is_virt_dev_kit()) {
			tegra234_fake_system_suspend = true;
		} else {
			/*
			 * Set to not support if we are not on FPGA or VDK.
			 */
			ret = -ENOTSUP;
		}

		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	return ret;
}
