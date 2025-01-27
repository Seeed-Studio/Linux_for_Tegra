// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA device stub implementation.
 */

#include "../nvdla_device.h"

uint32_t nvdla_device_register_read(struct platform_device *pdev,
	uint32_t reg)
{
	(void) pdev;
	(void) reg;

	return 0U;
}

void nvdla_device_register_write(struct platform_device *pdev,
	uint32_t reg,
	uint32_t value)
{
	(void) pdev;
	(void) reg;
	(void) value;
}

int32_t nvdla_module_init(struct platform_device *pdev)
{
	(void) pdev;

	return -1;
}

void nvdla_module_deinit(struct platform_device *pdev)
{
	(void) pdev;
}

int32_t nvdla_module_client_register(struct platform_device *pdev,
	void *context)
{
	(void) pdev;
	(void) context;

	return -1;
}

void nvdla_module_client_unregister(struct platform_device *pdev,
	void *context)
{
	(void) pdev;
	(void) context;
}

int32_t nvdla_module_busy(struct platform_device *pdev)
{
	(void) pdev;
	return -1;
}

void nvdla_module_idle(struct platform_device *pdev)
{
	(void) pdev;
}

void nvdla_module_idle_mult(struct platform_device *pdev, int32_t refs)
{
	(void) pdev;
	(void) refs;
}

void nvdla_module_reset(struct platform_device *pdev, bool reboot)
{
	(void) pdev;
	(void) reboot;
}

int32_t nvdla_module_runtime_suspend(struct device *dev)
{
	(void) dev;

	return -1;
}

int32_t nvdla_module_runtime_resume(struct device *dev)
{
	(void) dev;

	return -1;
}

int32_t nvdla_module_suspend(struct device *dev)
{
	(void) dev;

	return -1;
}

int32_t nvdla_module_resume(struct device *dev)
{
	(void) dev;

	return -1;
}

int32_t nvdla_module_prepare_suspend(struct device *dev)
{
	(void) dev;

	return -1;
}

void nvdla_module_complete_resume(struct device *dev)
{
	(void) dev;
}

int32_t nvdla_driver_register(struct platform_driver *pdriver)
{
	(void) pdriver;

	return -1;
}

/**
 * @brief Unregisters the platform driver.
 *
 * @param[in] pdriver Platform driver that is to be unregistered.
 **/
void nvdla_driver_unregister(struct platform_driver *pdriver)
{
	(void) pdriver;
}
