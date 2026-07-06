// SPDX-License-Identifier: MIT
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <tegra_hwpm_common.h>
#include <tegra_hwpm_kmem.h>
#include <tegra_hwpm_log.h>
#include <tegra_hwpm.h>
#include <hal/th500/th500_init.h>
#include <hal/th500/th500_internal.h>
#include <hal/th500/soc/th500_soc_internal.h>

static struct tegra_soc_hwpm_chip th500_chip_info = {
	.la_clk_rate = 625000000,
	.chip_ips = NULL,

	/* HALs */
	.validate_secondary_hals = th500_hwpm_validate_secondary_hals,

	/* Clocks and resets are configured by UEFI */
	.clk_rst_prepare = NULL,
	.clk_rst_set_rate_enable = NULL,
	.clk_rst_disable = NULL,
	.clk_rst_release = NULL,

	.is_ip_active = th500_hwpm_is_ip_active,
	.is_resource_active = th500_hwpm_is_resource_active,

	.get_rtr_int_idx = th500_get_rtr_int_idx,
	.get_ip_max_idx = th500_get_ip_max_idx,
	.get_rtr_pma_perfmux_ptr = th500_hwpm_soc_get_rtr_pma_perfmux_ptr,

	.extract_ip_ops = th500_hwpm_extract_ip_ops,
	.force_enable_ips = th500_hwpm_force_enable_ips,
	.validate_current_config = th500_hwpm_validate_current_config,
	.get_fs_info = tegra_hwpm_get_fs_info,
	.get_resource_info = tegra_hwpm_get_resource_info,

	.init_prod_values = th500_hwpm_soc_init_prod_values,
	.disable_cg = th500_hwpm_soc_disable_cg,
	.enable_cg = th500_hwpm_soc_enable_cg,

	.credit_program = NULL,
	.setup_trigger = NULL,

	.reserve_rtr = tegra_hwpm_reserve_rtr,
	.release_rtr = tegra_hwpm_release_rtr,

	.perfmon_enable = th500_hwpm_soc_perfmon_enable,
	.perfmon_disable = th500_hwpm_soc_perfmon_disable,
	.perfmux_disable = tegra_hwpm_perfmux_disable,
	.disable_triggers = th500_hwpm_soc_disable_triggers,
	.check_status = th500_hwpm_soc_check_status,
	.soft_reset = NULL,

	.disable_mem_mgmt = th500_hwpm_soc_disable_mem_mgmt,
	.enable_mem_mgmt = th500_hwpm_soc_enable_mem_mgmt,
	.invalidate_mem_config = th500_hwpm_soc_invalidate_mem_config,
	.stream_mem_bytes = th500_hwpm_soc_stream_mem_bytes,
	.disable_pma_streaming = th500_hwpm_soc_disable_pma_streaming,
	.update_mem_bytes_get_ptr = th500_hwpm_soc_update_mem_bytes_get_ptr,
	.get_mem_bytes_put_ptr = th500_hwpm_soc_get_mem_bytes_put_ptr,
	.membuf_overflow_status = th500_hwpm_soc_membuf_overflow_status,

	.get_alist_buf_size = tegra_hwpm_get_alist_buf_size,
	.zero_alist_regs = tegra_hwpm_zero_alist_regs,
	.copy_alist = tegra_hwpm_copy_alist,
	.check_alist = tegra_hwpm_check_alist,
};

bool th500_hwpm_validate_secondary_hals(struct tegra_soc_hwpm *hwpm)
{
	/*
	 * Clocks and resets are configured by UEFI
	 * So clock-reset HALs are expected to be NULL
	 */
	if (hwpm->active_chip->clk_rst_prepare != NULL) {
		tegra_hwpm_err(hwpm, "clk_rst_prepare HAL initialized");
		return false;
	}

	if (hwpm->active_chip->clk_rst_set_rate_enable != NULL) {
		tegra_hwpm_err(hwpm,
			"clk_rst_set_rate_enable HAL initialized");
		return false;
	}

	if (hwpm->active_chip->clk_rst_disable != NULL) {
		tegra_hwpm_err(hwpm, "clk_rst_disable HAL initialized");
		return false;
	}

	if (hwpm->active_chip->clk_rst_release != NULL) {
		tegra_hwpm_err(hwpm, "clk_rst_release HAL initialized");
		return false;
	}

	return true;
}

bool th500_hwpm_is_ip_active(struct tegra_soc_hwpm *hwpm,
	u32 ip_enum, u32 *config_ip_index)
{
	u32 config_ip = TEGRA_HWPM_IP_INACTIVE;

	switch (ip_enum) {
#if defined(CONFIG_TH500_HWPM_IP_MSS_CHANNEL)
	case TEGRA_HWPM_IP_MSS_CHANNEL:
		config_ip = TH500_HWPM_IP_MSS_CHANNEL;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MSS_HUB)
	case TEGRA_HWPM_IP_MSS_HUB:
		config_ip = TH500_HWPM_IP_MSS_HUB;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_CL2)
	case TEGRA_HWPM_IP_CL2:
		config_ip = TH500_HWPM_IP_CL2;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_CORE)
	case TEGRA_HWPM_IP_MCF_CORE:
		config_ip = TH500_HWPM_IP_MCF_CORE;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_CLINK)
	case TEGRA_HWPM_IP_MCF_CLINK:
		config_ip = TH500_HWPM_IP_MCF_CLINK;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_C2C)
	case TEGRA_HWPM_IP_MCF_C2C:
		config_ip = TH500_HWPM_IP_MCF_C2C;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_SOC)
	case TEGRA_HWPM_IP_MCF_SOC:
		config_ip = TH500_HWPM_IP_MCF_SOC;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_SMMU)
	case TEGRA_HWPM_IP_SMMU:
		config_ip = TH500_HWPM_IP_SMMU;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_C_NVLINK)
	case TEGRA_HWPM_IP_NVLCTRL:
		config_ip = TH500_HWPM_IP_NVLCTRL;
		break;
	case TEGRA_HWPM_IP_NVLRX:
		config_ip = TH500_HWPM_IP_NVLRX;
		break;
	case TEGRA_HWPM_IP_NVLTX:
		config_ip = TH500_HWPM_IP_NVLTX;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_PCIE)
	case TEGRA_HWPM_IP_PCIE_XTLQ:
		config_ip = TH500_HWPM_IP_PCIE_XTLQ;
		break;
	case TEGRA_HWPM_IP_PCIE_XTLRC:
		config_ip = TH500_HWPM_IP_PCIE_XTLRC;
		break;
	case TEGRA_HWPM_IP_PCIE_XALRC:
		config_ip = TH500_HWPM_IP_PCIE_XALRC;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_C2C)
	case TEGRA_HWPM_IP_C2C:
		config_ip = TH500_HWPM_IP_C2C;
		break;
#endif
	default:
		tegra_hwpm_err(hwpm,
			"Queried enum tegra_soc_hwpm_ip %d invalid", ip_enum);
		break;
	}

	*config_ip_index = config_ip;
	return (config_ip != TEGRA_HWPM_IP_INACTIVE);
}

bool th500_hwpm_is_resource_active(struct tegra_soc_hwpm *hwpm,
	u32 res_index, u32 *config_ip_index)
{
	u32 config_ip = TEGRA_HWPM_IP_INACTIVE;

	switch (res_index) {
#if defined(CONFIG_TH500_HWPM_IP_MSS_CHANNEL)
	case TEGRA_HWPM_RESOURCE_MSS_CHANNEL:
		config_ip = TH500_HWPM_IP_MSS_CHANNEL;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MSS_HUB)
	case TEGRA_HWPM_RESOURCE_MSS_HUB:
		config_ip = TH500_HWPM_IP_MSS_HUB;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_CL2)
	case TEGRA_HWPM_RESOURCE_CL2:
		config_ip = TH500_HWPM_IP_CL2;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_CORE)
	case TEGRA_HWPM_RESOURCE_MCF_CORE:
		config_ip = TH500_HWPM_IP_MCF_CORE;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_CLINK)
	case TEGRA_HWPM_RESOURCE_MCF_CLINK:
		config_ip = TH500_HWPM_IP_MCF_CLINK;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_C2C)
	case TEGRA_HWPM_RESOURCE_MCF_C2C:
		config_ip = TH500_HWPM_IP_MCF_C2C;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_SOC)
	case TEGRA_HWPM_RESOURCE_MCF_SOC:
		config_ip = TH500_HWPM_IP_MCF_SOC;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_SMMU)
	case TEGRA_HWPM_RESOURCE_SMMU:
		config_ip = TH500_HWPM_IP_SMMU;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_C_NVLINK)
	case TEGRA_HWPM_RESOURCE_NVLCTRL:
		config_ip = TH500_HWPM_IP_NVLCTRL;
		break;
	case TEGRA_HWPM_RESOURCE_NVLRX:
		config_ip = TH500_HWPM_IP_NVLRX;
		break;
	case TEGRA_HWPM_RESOURCE_NVLTX:
		config_ip = TH500_HWPM_IP_NVLTX;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_PCIE)
	case TEGRA_HWPM_RESOURCE_PCIE_XTLQ:
		config_ip = TH500_HWPM_IP_PCIE_XTLQ;
		break;
	case TEGRA_HWPM_RESOURCE_PCIE_XTLRC:
		config_ip = TH500_HWPM_IP_PCIE_XTLRC;
		break;
	case TEGRA_HWPM_RESOURCE_PCIE_XALRC:
		config_ip = TH500_HWPM_IP_PCIE_XALRC;
		break;
#endif
#if defined(CONFIG_TH500_HWPM_IP_C2C)
	case TEGRA_HWPM_RESOURCE_C2C:
		config_ip = TH500_HWPM_IP_C2C;
		break;
#endif
	case TEGRA_HWPM_RESOURCE_PMA:
		config_ip = TH500_HWPM_IP_PMA;
		break;
	case TEGRA_HWPM_RESOURCE_CMD_SLICE_RTR:
		config_ip = TH500_HWPM_IP_RTR;
		break;
	default:
		tegra_hwpm_err(hwpm, "Queried resource %d invalid",
			res_index);
		break;
	}

	*config_ip_index = config_ip;
	return (config_ip != TEGRA_HWPM_IP_INACTIVE);
}

u32 th500_get_rtr_int_idx(void)
{
	return TH500_HWPM_IP_RTR;
}

u32 th500_get_ip_max_idx(void)
{
	return TH500_HWPM_IP_MAX;
}

int th500_hwpm_init_chip_info(struct tegra_soc_hwpm *hwpm)
{
	struct hwpm_ip **th500_active_ip_info;

	/* Allocate array of pointers to hold active IP structures */
	th500_chip_info.chip_ips = tegra_hwpm_kcalloc(
		hwpm, TH500_HWPM_IP_MAX, sizeof(struct hwpm_ip *));

	/* Add active chip structure link to hwpm super-structure */
	hwpm->active_chip = &th500_chip_info;

	/* Temporary pointer to make below assignments legible */
	th500_active_ip_info = th500_chip_info.chip_ips;

	th500_active_ip_info[TH500_HWPM_IP_PMA] = &th500_hwpm_ip_pma;
	th500_active_ip_info[TH500_HWPM_IP_RTR] = &th500_hwpm_ip_rtr;

#if defined(CONFIG_TH500_HWPM_IP_MSS_CHANNEL)
	th500_active_ip_info[TH500_HWPM_IP_MSS_CHANNEL] =
		&th500_hwpm_ip_mss_channel;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MSS_HUB)
	th500_active_ip_info[TH500_HWPM_IP_MSS_HUB] =
		&th500_hwpm_ip_mss_hub;
#endif
#if defined(CONFIG_TH500_HWPM_IP_CL2)
	th500_active_ip_info[TH500_HWPM_IP_CL2] = &th500_hwpm_ip_cl2;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_CORE)
	th500_active_ip_info[TH500_HWPM_IP_MCF_CORE] = &th500_hwpm_ip_mcf_core;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_CLINK)
	th500_active_ip_info[TH500_HWPM_IP_MCF_CLINK] =
		&th500_hwpm_ip_mcf_clink;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_C2C)
	th500_active_ip_info[TH500_HWPM_IP_MCF_C2C] =
		&th500_hwpm_ip_mcf_c2c;
#endif
#if defined(CONFIG_TH500_HWPM_IP_MCF_SOC)
	th500_active_ip_info[TH500_HWPM_IP_MCF_SOC] =
		&th500_hwpm_ip_mcf_soc;
#endif
#if defined(CONFIG_TH500_HWPM_IP_SMMU)
	th500_active_ip_info[TH500_HWPM_IP_SMMU] = &th500_hwpm_ip_smmu;
#endif
#if defined(CONFIG_TH500_HWPM_IP_C_NVLINK)
	th500_active_ip_info[TH500_HWPM_IP_NVLCTRL] = &th500_hwpm_ip_nvlctrl;
	th500_active_ip_info[TH500_HWPM_IP_NVLRX] = &th500_hwpm_ip_nvlrx;
	th500_active_ip_info[TH500_HWPM_IP_NVLTX] = &th500_hwpm_ip_nvltx;
#endif
#if defined(CONFIG_TH500_HWPM_IP_PCIE)
	th500_active_ip_info[TH500_HWPM_IP_PCIE_XTLQ] = &th500_hwpm_ip_pcie_xtlq;
	th500_active_ip_info[TH500_HWPM_IP_PCIE_XTLRC] = &th500_hwpm_ip_pcie_xtlrc;
	th500_active_ip_info[TH500_HWPM_IP_PCIE_XALRC] = &th500_hwpm_ip_pcie_xalrc;
#endif
#if defined(CONFIG_TH500_HWPM_IP_C2C)
	th500_active_ip_info[TH500_HWPM_IP_C2C] = &th500_hwpm_ip_c2c;
#endif
	if (!tegra_hwpm_validate_primary_hals(hwpm)) {
		return -EINVAL;
	}
	return 0;
}
