/* SPDX-License-Identifier: LicenseRef-NvidiaProprietary */
/* SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Device data for T25X simulator
 */

#ifndef __NVHOST_NVDLA_T25X_H__
#define __NVHOST_NVDLA_T25X_H__

#include <linux/nvhost.h>
#include <dt-bindings/interconnect/tegra_icc_id.h>

#include "nvdla.h"
#include "dla_t25x_fw_version.h"

/* REVISIT the registers */
static struct nvhost_gating_register nvdla_t25x_gating_registers[] = {
	{}
};

static struct nvhost_device_data t25x_nvdla0_info = {
	.devfs_name_family	= "nvdla",
	.class			= NV_DLA0_CLASS_ID,
	.clocks			= {
		{"nvdla0", UINT_MAX},
		{"nvdla0_flcn", UINT_MAX}
	},
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.finalize_poweron	= nvdla_finalize_poweron,
	.prepare_poweroff	= nvdla_prepare_poweroff,
	.flcn_isr               = nvdla_flcn_isr,
	.self_config_flcn_isr	= true,
	.vm_regs		= {{0x30, true}, {0x34, false} },
	.firmware_name		= NV_DLA_TEGRA25X_FW,
	.version		= FIRMWARE_ENCODE_VERSION(T25X),
	.autosuspend_delay      = 500,
	.keepalive		= true,
	.poweron_reset		= true,
	.serialize		= true,
	.ctrl_ops		= &tegra_nvdla_ctrl_ops,
	.get_reloc_phys_addr	= NULL,
	.module_irq		= 1,
	.engine_cg_regs		= nvdla_t25x_gating_registers,
	.engine_can_cg		= true,
	.can_powergate		= true,
	.icc_id			= TEGRA_ICC_DLA_0,
	.transcfg_addr		= 0x0444,
	.transcfg_val		= 0x201,
	.firmware_not_in_subdir = true,
};

#endif /* End of __NVHOST_NVDLA_T25X_H__ */
