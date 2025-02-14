// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA Power Management - stub implementation
 */

#include "../nvdla_pm.h"

int32_t nvdla_pm_init(struct platform_device *pdev)
{
	(void) pdev;
	return -1;
}

void nvdla_pm_deinit(struct platform_device *pdev)
{
	(void) pdev;
}


int32_t nvdla_pm_rail_gate(struct platform_device *pdev,
	uint32_t timeout_us,
	bool blocking)
{
	(void) pdev;
	(void) timeout_us;
	(void) blocking;

	return -1;
}

int32_t nvdla_pm_rail_ungate(struct platform_device *pdev)
{
	(void) pdev;

	return -1;
}

int32_t nvdla_pm_rail_is_gated(struct platform_device *pdev,
	bool *gated)
{
	(void) pdev;
	(void) gated;

	return -1;
}

int32_t nvdla_pm_power_gate(struct platform_device *pdev,
	uint32_t timeout_us,
	bool blocking)
{
	(void) pdev;
	(void) timeout_us;
	(void) blocking;

	return -1;
}

int32_t nvdla_pm_power_ungate(struct platform_device *pdev)
{
	(void) pdev;

	return -1;
}

int32_t nvdla_pm_power_is_gated(struct platform_device *pdev,
	bool *gated)
{
	(void) pdev;
	(void) gated;

	return -1;
}

int32_t nvdla_pm_clock_gate(struct platform_device *pdev,
	uint32_t timeout_us,
	bool blocking)
{
	(void) pdev;
	(void) timeout_us;
	(void) blocking;

	return -1;
}

int32_t nvdla_pm_clock_ungate(struct platform_device *pdev)
{
	(void) pdev;

	return -1;
}

int32_t nvdla_pm_clock_is_gated(struct platform_device *pdev,
	bool *gated)
{
	(void) pdev;
	(void) gated;

	return -1;
}

int32_t nvdla_pm_clock_set_mcu_freq(struct platform_device *pdev,
	uint32_t freq_khz)
{
	(void) pdev;
	(void) freq_khz;

	return -1;
}

int32_t nvdla_pm_clock_get_mcu_freq(struct platform_device *pdev,
	uint32_t *freq_khz)
{
	(void) pdev;
	(void) freq_khz;

	return -1;
}

int32_t nvdla_pm_clock_set_core_freq(struct platform_device *pdev,
	uint32_t freq_khz)
{
	(void) pdev;
	(void) freq_khz;

	return -1;
}

int32_t nvdla_pm_clock_get_core_freq(struct platform_device *pdev,
	uint32_t *freq_khz)
{
	(void) pdev;
	(void) freq_khz;

	return -1;
}
