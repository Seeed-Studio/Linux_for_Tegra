// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA Power Management - stub implementation
 */

#include "../nvdla_pm.h"

int32_t nvdla_pm_init(struct platform_device *pdev)
{
	(void) pdev;
	return 0;
}

void nvdla_pm_deinit(struct platform_device *pdev)
{
	(void) pdev;
}


int32_t nvdla_pm_rail_gate(struct platform_device *pdev,
	bool blocking)
{
	(void) pdev;
	(void) blocking;

	return 0;
}

int32_t nvdla_pm_rail_ungate(struct platform_device *pdev)
{
	(void) pdev;

	return 0;
}

int32_t nvdla_pm_rail_is_gated(struct platform_device *pdev,
	bool *gated)
{
	(void) pdev;
	(void) gated;

	return 0;
}

int32_t nvdla_pm_rail_gate_set_delay_us(struct platform_device *pdev,
	uint32_t delay_us)
{
	(void) pdev;
	(void) delay_us;

	return 0;
}

int32_t nvdla_pm_rail_gate_get_delay_us(struct platform_device *pdev,
	uint32_t *delay_us)
{
	(void) pdev;
	(void) delay_us;

	return 0;
}

int32_t nvdla_pm_power_gate(struct platform_device *pdev,
	bool blocking)
{
	(void) pdev;
	(void) blocking;

	return 0;
}

int32_t nvdla_pm_power_ungate(struct platform_device *pdev)
{
	(void) pdev;

	return 0;
}

int32_t nvdla_pm_power_is_gated(struct platform_device *pdev,
	bool *gated)
{
	(void) pdev;
	(void) gated;

	return 0;
}

int32_t nvdla_pm_power_gate_set_delay_us(struct platform_device *pdev,
	uint32_t delay_us)
{
	(void) pdev;
	(void) delay_us;

	return 0;
}

int32_t nvdla_pm_power_gate_get_delay_us(struct platform_device *pdev,
	uint32_t *delay_us)
{
	(void) pdev;
	(void) delay_us;

	return 0;
}

int32_t nvdla_pm_clock_gate(struct platform_device *pdev,
	bool blocking)
{
	(void) pdev;
	(void) blocking;

	return 0;
}

int32_t nvdla_pm_clock_ungate(struct platform_device *pdev)
{
	(void) pdev;

	return 0;
}

int32_t nvdla_pm_clock_is_gated(struct platform_device *pdev,
	bool *gated)
{
	(void) pdev;
	(void) gated;

	return 0;
}

int32_t nvdla_pm_clock_gate_set_delay_us(struct platform_device *pdev,
	uint32_t delay_us)
{
	(void) pdev;
	(void) delay_us;

	return 0;
}

int32_t nvdla_pm_clock_gate_get_delay_us(struct platform_device *pdev,
	uint32_t *delay_us)
{
	(void) pdev;
	(void) delay_us;

	return 0;
}

int32_t nvdla_pm_clock_set_mcu_freq(struct platform_device *pdev,
	uint32_t freq_khz)
{
	(void) pdev;
	(void) freq_khz;

	return 0;
}

int32_t nvdla_pm_clock_get_mcu_freq(struct platform_device *pdev,
	uint32_t *freq_khz)
{
	(void) pdev;
	(void) freq_khz;

	return 0;
}

int32_t nvdla_pm_clock_set_core_freq(struct platform_device *pdev,
	uint32_t freq_khz)
{
	(void) pdev;
	(void) freq_khz;

	return 0;
}

int32_t nvdla_pm_clock_get_core_freq(struct platform_device *pdev,
	uint32_t *freq_khz)
{
	(void) pdev;
	(void) freq_khz;

	return 0;
}

int32_t nvdla_pm_get_stat(struct platform_device *pdev,
	struct nvdla_pm_stat *stat)
{
	(void) pdev;
	(void) stat;

	return 0;
}

int32_t nvdla_pm_set_lpwr_config(struct platform_device *pdev,
	struct dla_lpwr_config *config)
{
	(void) pdev;
	(void) config;

	return 0;
}

int32_t nvdla_pm_get_lpwr_config(struct platform_device *pdev,
	struct dla_lpwr_config *config)
{
	(void) pdev;
	(void) config;

	return 0;
}

int32_t nvdla_pm_reset(struct platform_device *pdev)
{
	(void) pdev;

	return 0;
}

int32_t nvdla_pm_get_current_voltage(struct platform_device *pdev,
	uint32_t *voltage_mV)
{
	(void) pdev;
	(void) voltage_mV;

	return 0;
}

int32_t nvdla_pm_get_current_power_draw(struct platform_device *pdev,
	uint32_t *power_draw_mW)
{
	(void) pdev;
	(void) power_draw_mW;

	return 0;
}

int32_t nvdla_pm_get_info(struct platform_device *pdev,
	struct nvdla_pm_info *info)
{
	(void) pdev;
	(void) info;

	return 0;
}
