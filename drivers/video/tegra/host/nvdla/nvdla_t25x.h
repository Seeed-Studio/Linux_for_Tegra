/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Device data for T25X simulator
 */

#ifndef __NVHOST_NVDLA_T25X_H__
#define __NVHOST_NVDLA_T25X_H__

#include "port/nvdla_host_wrapper.h"
#include <dt-bindings/interconnect/tegra_icc_id.h>

#include "nvdla.h"
#include "dla_t25x_fw_version.h"

static struct nvhost_device_data t25x_nvdla0_info = {
	.devfs_name		= "nvdla0",
	.devfs_name_family	= "nvdla",
	.class			= NV_DLA0_CLASS_ID,
	.clocks			= {
		{"nvdla0", UINT_MAX},
		{"nvdla0_flcn", UINT_MAX}
	},
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.finalize_poweron	= nvdla_finalize_poweron,
	.prepare_poweroff	= nvdla_prepare_poweroff,
	.flcn_isr		= nvdla_flcn_isr,
	.self_config_flcn_isr	= true,
	.vm_regs		= {{0x30, true}, {0x34, false} },
	.firmware_name		= NV_DLA_TEGRA25X_FW,
	.version		= FIRMWARE_ENCODE_VERSION(T25X),
#if defined(NVDLA_HAVE_CONFIG_FWSUSPEND) && (NVDLA_HAVE_CONFIG_FWSUSPEND == 1)
	.autosuspend_delay      = 0,
#else
	.autosuspend_delay      = 500,
#endif /* NVDLA_HAVE_CONFIG_FWSUSPEND */
	.keepalive		= true,
	.poweron_reset		= true,
	.serialize		= true,
	.ctrl_ops		= &tegra_nvdla_ctrl_ops,
	.get_reloc_phys_addr	= NULL,
	.module_irq		= 1,
	.engine_can_cg		= false,
	.can_powergate		= true,
	.icc_id			= TEGRA_ICC_DLA_0,
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	.transcfg_addr		= 0x2244,
#else
	.transcfg_addr		= 0x0444,
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
	.transcfg_val		= 0x201,
	.firmware_not_in_subdir = true,
};

static struct nvhost_device_data t25x_nvdla1_info = {
	.devfs_name		= "nvdla1",
	.devfs_name_family	= "nvdla",
	.class			= NV_DLA1_CLASS_ID,
	.clocks			= {
		{"nvdla1", UINT_MAX},
		{"nvdla1_flcn", UINT_MAX}
	},
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.finalize_poweron	= nvdla_finalize_poweron,
	.prepare_poweroff	= nvdla_prepare_poweroff,
	.flcn_isr		= nvdla_flcn_isr,
	.self_config_flcn_isr	= true,
	.vm_regs		= {{0x30, true}, {0x34, false} },
	.firmware_name		= NV_DLA_TEGRA25X_FW,
	.version		= FIRMWARE_ENCODE_VERSION(T25X),
#if defined(NVDLA_HAVE_CONFIG_FWSUSPEND) && (NVDLA_HAVE_CONFIG_FWSUSPEND == 1)
	.autosuspend_delay      = 0,
#else
	.autosuspend_delay      = 500,
#endif /* NVDLA_HAVE_CONFIG_FWSUSPEND */
	.keepalive		= true,
	.poweron_reset		= true,
	.serialize		= true,
	.ctrl_ops		= &tegra_nvdla_ctrl_ops,
	.get_reloc_phys_addr	= NULL,
	.module_irq		= 1,
	.engine_can_cg		= false,
	.can_powergate		= true,
	.icc_id			= TEGRA_ICC_DLA_1,
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	.transcfg_addr		= 0x2244,
#else
	.transcfg_addr		= 0x0444,
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
	.transcfg_val		= 0x201,
	.firmware_not_in_subdir = true,
};

#endif /* End of __NVHOST_NVDLA_T25X_H__ */
