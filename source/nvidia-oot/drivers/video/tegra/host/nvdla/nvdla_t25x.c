// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Device data for T25X simulator
 */

#include "port/nvdla_host_wrapper.h"
#include <dt-bindings/interconnect/tegra_icc_id.h>

#include "nvdla.h"
#include "nvdla_ctx.h"
#include "dla_t25x_fw_version.h"

struct nvhost_device_data t25x_nvdla0_info = {
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

struct nvhost_device_data t25x_nvdla0_ctx0_info = {
	.devfs_name_family	= "nvdla",
	.class			= NV_DLA0_CTX0_CLASS_ID,
	.version		= FIRMWARE_ENCODE_VERSION(T25X_V2),
	.ctrl_ops		= &tegra_nvdla_ctrl_ops,
	.engine_can_cg		= false,
	.can_powergate		= false,
	.flcn_isr		= nvdla_flcn_ctx_tx_isr,
};

struct nvhost_device_data t25x_nvdla0_ctx1_info = {
	.devfs_name_family	= "nvdla",
	.class			= NV_DLA0_CTX1_CLASS_ID,
	.version		= FIRMWARE_ENCODE_VERSION(T25X_V2),
	.ctrl_ops		= &tegra_nvdla_ctrl_ops,
	.engine_can_cg		= false,
	.can_powergate		= false,
	.flcn_isr		= nvdla_flcn_ctx_tx_isr,
};

struct nvhost_device_data t25x_nvdla0_ctx2_info = {
	.devfs_name_family	= "nvdla",
	.class			= NV_DLA0_CTX2_CLASS_ID,
	.version		= FIRMWARE_ENCODE_VERSION(T25X_V2),
	.ctrl_ops		= &tegra_nvdla_ctrl_ops,
	.engine_can_cg		= false,
	.can_powergate		= false,
	.flcn_isr		= nvdla_flcn_ctx_tx_isr,
};

struct nvhost_device_data t25x_nvdla0_ctx3_info = {
	.devfs_name_family	= "nvdla",
	.class			= NV_DLA0_CTX3_CLASS_ID,
	.version		= FIRMWARE_ENCODE_VERSION(T25X_V2),
	.ctrl_ops		= &tegra_nvdla_ctrl_ops,
	.engine_can_cg		= false,
	.can_powergate		= false,
	.flcn_isr		= nvdla_flcn_ctx_tx_isr,
};
