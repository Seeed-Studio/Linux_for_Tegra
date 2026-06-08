// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA KMD-uC stub implementation
 */

#include "../nvdla_fw.h"

int32_t nvdla_fw_poweron(struct platform_device *pdev)
{
	(void) pdev;

	return -1;
}

int32_t nvdla_fw_poweroff(struct platform_device *pdev)
{
	(void) pdev;

	return -1;
}

int32_t nvdla_fw_init(struct platform_device *pdev)
{
	(void) pdev;
	return -1;
}

void nvdla_fw_deinit(struct platform_device *pdev)
{
	(void) pdev;
}

int32_t nvdla_fw_reload(struct platform_device *pdev)
{
	(void) pdev;
	return -1;
}

int32_t nvdla_fw_send_cmd(struct platform_device *pdev,
	struct nvdla_cmd_data *cmd_data)
{
	(void) pdev;
	(void) cmd_data;

	return -1;
}

int32_t nvdla_fw_send_ack(struct platform_device *pdev,
	int32_t ack)
{
	(void) pdev;

	return -1;
}

int32_t nvdla_fw_interrupt_stat_read(struct platform_device *pdev,
	int32_t *stat)
{
	(void) pdev;

	return -1;
}

int32_t nvdla_fw_interrupt_stat_clear(struct platform_device *pdev)
{
	(void) pdev;

	return -1;
}

int32_t nvdla_fw_inject_corrected_error(struct platform_device *pdev)
{
	(void) pdev;

	return -1;
}

int32_t nvdla_fw_inject_uncorrected_error(struct platform_device *pdev)
{
	(void) pdev;

	return -1;
}
