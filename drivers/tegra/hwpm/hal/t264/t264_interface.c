// SPDX-License-Identifier: MIT
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <tegra_hwpm_clk_rst.h>
#include <tegra_hwpm_common.h>
#include <tegra_hwpm_kmem.h>
#include <tegra_hwpm_log.h>
#include <tegra_hwpm.h>
#include <hal/t264/t264_init.h>
#include <hal/t264/t264_internal.h>

static struct tegra_soc_hwpm_chip t264_chip_info = {
	.la_clk_rate = 648000000,
	.chip_ips = NULL,

	/* HALs */
	.validate_secondary_hals = t264_hwpm_validate_secondary_hals,

	/* Clocks-Resets  */
	.clk_rst_prepare = tegra_hwpm_clk_rst_prepare,
	.clk_rst_set_rate_enable = tegra_hwpm_clk_rst_set_rate_enable,
	.clk_rst_disable = tegra_hwpm_clk_rst_disable,
	.clk_rst_release = tegra_hwpm_clk_rst_release,

	/* IP */
	.is_ip_active = t264_hwpm_is_ip_active,
	.is_resource_active = t264_hwpm_is_resource_active,
	.get_rtr_int_idx = t264_get_rtr_int_idx,
	.get_ip_max_idx = t264_get_ip_max_idx,
	.get_rtr_pma_perfmux_ptr = t264_hwpm_get_rtr_pma_perfmux_ptr,
	.extract_ip_ops = t264_hwpm_extract_ip_ops,
	.force_enable_ips = t264_hwpm_force_enable_ips,
	.validate_current_config = t264_hwpm_validate_current_config,
	.get_fs_info = tegra_hwpm_get_fs_info,
	.get_resource_info = tegra_hwpm_get_resource_info,

	/* Clock gating */
	.init_prod_values = t264_hwpm_init_prod_values,
	.disable_cg = t264_hwpm_disable_cg,
	.enable_cg = t264_hwpm_enable_cg,

	/* Secure register programming */
	.credit_program = t264_hwpm_credit_program,
	.setup_trigger = t264_hwpm_setup_trigger,

	/* Resource reservation */
	.reserve_rtr = tegra_hwpm_reserve_rtr,
	.release_rtr = tegra_hwpm_release_rtr,

	/* Aperture */
	.perfmon_enable = t264_hwpm_perfmon_enable,
	.perfmon_disable = t264_hwpm_perfmon_disable,
	.perfmux_disable = tegra_hwpm_perfmux_disable,
	.disable_triggers = t264_hwpm_disable_triggers,
	.check_status = t264_hwpm_check_status,
	.soft_reset = NULL,

	/* Memory management */
	.disable_mem_mgmt = t264_hwpm_disable_mem_mgmt,
	.enable_mem_mgmt = t264_hwpm_enable_mem_mgmt,
	.invalidate_mem_config = t264_hwpm_invalidate_mem_config,
	.stream_mem_bytes = t264_hwpm_stream_mem_bytes,
	.disable_pma_streaming = t264_hwpm_disable_pma_streaming,
	.update_mem_bytes_get_ptr = t264_hwpm_update_mem_bytes_get_ptr,
	.get_mem_bytes_put_ptr = t264_hwpm_get_mem_bytes_put_ptr,
	.membuf_overflow_status = t264_hwpm_membuf_overflow_status,

	/* Allowlist */
	.get_alist_buf_size = tegra_hwpm_get_alist_buf_size,
	.zero_alist_regs = tegra_hwpm_zero_alist_regs,
	.copy_alist = tegra_hwpm_copy_alist,
	.check_alist = tegra_hwpm_check_alist,
};

bool t264_hwpm_validate_secondary_hals(struct tegra_soc_hwpm *hwpm)
{
	tegra_hwpm_fn(hwpm, " ");

	if (hwpm->active_chip->clk_rst_prepare == NULL) {
		tegra_hwpm_err(hwpm, "clk_rst_prepare HAL uninitialized");
		return false;
	}

	if (hwpm->active_chip->clk_rst_set_rate_enable == NULL) {
		tegra_hwpm_err(hwpm,
			"clk_rst_set_rate_enable HAL uninitialized");
		return false;
	}

	if (hwpm->active_chip->clk_rst_disable == NULL) {
		tegra_hwpm_err(hwpm, "clk_rst_disable HAL uninitialized");
		return false;
	}

	if (hwpm->active_chip->clk_rst_release == NULL) {
		tegra_hwpm_err(hwpm, "clk_rst_release HAL uninitialized");
		return false;
	}

	if (hwpm->active_chip->credit_program == NULL) {
		tegra_hwpm_err(hwpm, "credit_program HAL uninitialized");
		return false;
	}

	if (hwpm->active_chip->setup_trigger == NULL) {
		tegra_hwpm_err(hwpm, "setup_trigger HAL uninitialized");
		return false;
	}

	return true;
}

bool t264_hwpm_is_ip_active(struct tegra_soc_hwpm *hwpm,
	u32 ip_enum, u32 *config_ip_index)
{
	u32 config_ip = TEGRA_HWPM_IP_INACTIVE;

	switch (ip_enum) {
#if defined(CONFIG_T264_HWPM_IP_VIC)
	case TEGRA_HWPM_IP_VIC:
		config_ip = T264_HWPM_IP_VIC;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_MSS_CHANNEL)
	case TEGRA_HWPM_IP_MSS_CHANNEL:
		config_ip = T264_HWPM_IP_MSS_CHANNEL;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_PVA)
	case TEGRA_HWPM_IP_PVA:
		config_ip = T264_HWPM_IP_PVA;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_MSS_HUBS)
	case TEGRA_HWPM_IP_MSS_HUB:
		config_ip = T264_HWPM_IP_MSS_HUBS;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_OCU)
	case TEGRA_HWPM_IP_MCF_OCU:
		config_ip = T264_HWPM_IP_OCU;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_VI)
	case TEGRA_HWPM_IP_VI:
		config_ip = T264_HWPM_IP_VI;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_ISP)
	case TEGRA_HWPM_IP_ISP:
		config_ip = T264_HWPM_IP_ISP;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_SMMU)
	case TEGRA_HWPM_IP_SMMU:
		config_ip = T264_HWPM_IP_SMMU;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_UCF_MSW)
	case TEGRA_HWPM_IP_UCF_MSW:
		config_ip = T264_HWPM_IP_UCF_MSW;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_UCF_PSW)
	case TEGRA_HWPM_IP_UCF_PSW:
		config_ip = T264_HWPM_IP_UCF_PSW;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_UCF_CSW)
	case TEGRA_HWPM_IP_UCF_CSW:
		config_ip = T264_HWPM_IP_UCF_CSW;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_CPU)
	case TEGRA_HWPM_IP_CPU:
		config_ip = T264_HWPM_IP_CPU;
#endif
		break;
	default:
		tegra_hwpm_err(hwpm, "Queried enum tegra_hwpm_ip %d invalid",
			ip_enum);
		break;
	}

	*config_ip_index = config_ip;
	return (config_ip != TEGRA_HWPM_IP_INACTIVE);
}

bool t264_hwpm_is_resource_active(struct tegra_soc_hwpm *hwpm,
	u32 res_enum, u32 *config_ip_index)
{
	u32 config_ip = TEGRA_HWPM_IP_INACTIVE;

	switch (res_enum) {
#if defined(CONFIG_T264_HWPM_IP_VIC)
	case TEGRA_HWPM_RESOURCE_VIC:
		config_ip = T264_HWPM_IP_VIC;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_MSS_CHANNEL)
	case TEGRA_HWPM_RESOURCE_MSS_CHANNEL:
		config_ip = T264_HWPM_IP_MSS_CHANNEL;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_PVA)
	case TEGRA_HWPM_RESOURCE_PVA:
		config_ip = T264_HWPM_IP_PVA;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_MSS_HUBS)
	case TEGRA_HWPM_RESOURCE_MSS_HUB:
		config_ip = T264_HWPM_IP_MSS_HUBS;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_OCU)
	case TEGRA_HWPM_RESOURCE_MCF_OCU:
		config_ip = T264_HWPM_IP_OCU;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_VI)
	case TEGRA_HWPM_RESOURCE_VI:
		config_ip = T264_HWPM_IP_VI;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_ISP)
	case TEGRA_HWPM_RESOURCE_ISP:
		config_ip = T264_HWPM_IP_ISP;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_SMMU)
	case TEGRA_HWPM_RESOURCE_SMMU:
		config_ip = T264_HWPM_IP_SMMU;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_UCF_MSW)
	case TEGRA_HWPM_RESOURCE_UCF_MSW:
		config_ip = T264_HWPM_IP_UCF_MSW;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_UCF_PSW)
	case TEGRA_HWPM_RESOURCE_UCF_PSW:
		config_ip = T264_HWPM_IP_UCF_PSW;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_UCF_CSW)
	case TEGRA_HWPM_RESOURCE_UCF_CSW:
		config_ip = T264_HWPM_IP_UCF_CSW;
#endif
		break;
#if defined(CONFIG_T264_HWPM_IP_CPU)
	case TEGRA_HWPM_RESOURCE_CPU:
		config_ip = T264_HWPM_IP_CPU;
#endif
		break;
	case TEGRA_HWPM_RESOURCE_PMA:
		config_ip = T264_HWPM_IP_PMA;
		break;
	case TEGRA_HWPM_RESOURCE_CMD_SLICE_RTR:
		config_ip = T264_HWPM_IP_RTR;
		break;
	default:
		tegra_hwpm_dbg(hwpm, hwpm_dbg_ip_register,
			"Queried resource %d invalid",
			res_enum);
		break;
	}

	*config_ip_index = config_ip;
	return (config_ip != TEGRA_HWPM_IP_INACTIVE);
}

u32 t264_get_rtr_int_idx(void)
{
	return T264_HWPM_IP_RTR;
}

u32 t264_get_ip_max_idx(void)
{
	return T264_HWPM_IP_MAX;
}

int t264_hwpm_init_chip_info(struct tegra_soc_hwpm *hwpm)
{
	struct hwpm_ip **t264_active_ip_info;

	/* Allocate array of pointers to hold active IP structures */
	t264_chip_info.chip_ips = tegra_hwpm_kcalloc(hwpm,
		T264_HWPM_IP_MAX, sizeof(struct hwpm_ip *));

	/* Add active chip structure link to hwpm super-structure */
	hwpm->active_chip = &t264_chip_info;

	/* Temporary pointer to make below assignments legible */
	t264_active_ip_info = t264_chip_info.chip_ips;

	t264_active_ip_info[T264_HWPM_IP_PMA] = &t264_hwpm_ip_pma;
	t264_active_ip_info[T264_HWPM_IP_RTR] = &t264_hwpm_ip_rtr;

#if defined(CONFIG_T264_HWPM_IP_VIC)
	t264_active_ip_info[T264_HWPM_IP_VIC] = &t264_hwpm_ip_vic;
#endif
#if defined(CONFIG_T264_HWPM_IP_MSS_CHANNEL)
	t264_active_ip_info[T264_HWPM_IP_MSS_CHANNEL] =
		&t264_hwpm_ip_mss_channel;
#endif
#if defined(CONFIG_T264_HWPM_IP_MSS_HUBS)
	t264_active_ip_info[T264_HWPM_IP_MSS_HUBS] =
		&t264_hwpm_ip_mss_hubs;
#endif
#if defined(CONFIG_T264_HWPM_IP_PVA)
	t264_active_ip_info[T264_HWPM_IP_PVA] = &t264_hwpm_ip_pva;
#endif
#if defined(CONFIG_T264_HWPM_IP_OCU)
	t264_active_ip_info[T264_HWPM_IP_OCU] = &t264_hwpm_ip_ocu;
#endif
#if defined(CONFIG_T264_HWPM_IP_SMMU)
	t264_active_ip_info[T264_HWPM_IP_SMMU] = &t264_hwpm_ip_smmu;
#endif
#if defined(CONFIG_T264_HWPM_IP_UCF_MSW)
	t264_active_ip_info[T264_HWPM_IP_UCF_MSW] = &t264_hwpm_ip_ucf_msw;
#endif
#if defined(CONFIG_T264_HWPM_IP_UCF_PSW)
	t264_active_ip_info[T264_HWPM_IP_UCF_PSW] = &t264_hwpm_ip_ucf_psw;
#endif
#if defined(CONFIG_T264_HWPM_IP_UCF_CSW)
	t264_active_ip_info[T264_HWPM_IP_UCF_CSW] = &t264_hwpm_ip_ucf_csw;
#endif
#if defined(CONFIG_T264_HWPM_IP_CPU)
	t264_active_ip_info[T264_HWPM_IP_CPU] = &t264_hwpm_ip_cpu;
#endif
#if defined(CONFIG_T264_HWPM_IP_VI)
	t264_active_ip_info[T264_HWPM_IP_VI] = &t264_hwpm_ip_vi;
#endif
#if defined(CONFIG_T264_HWPM_IP_ISP)
	t264_active_ip_info[T264_HWPM_IP_ISP] = &t264_hwpm_ip_isp;
#endif
	if (!tegra_hwpm_validate_primary_hals(hwpm)) {
		return -EINVAL;
	}
	return 0;
}
