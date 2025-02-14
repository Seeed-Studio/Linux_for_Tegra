/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA Power Management
 */

#ifndef __NVDLA_PM_H_
#define __NVDLA_PM_H_

#include <linux/platform_device.h>

/**
 * @brief Initializes power management unit
 *
 * @param[in] pdev Platform device with which power management is done.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise.
 **/
int32_t nvdla_pm_init(struct platform_device *pdev);

/**
 * @brief Deinitializes power management unit
 *
 * @param[in] pdev Platform device with which power management is done.
 **/
void nvdla_pm_deinit(struct platform_device *pdev);

/**
 * @brief Triggers request to rail gate after specified 'timeout_us'.
 *
 * @param[in] pdev Platform device that is to be rail gated.
 * @param[in] timeout_us Timeout after which DLA must be rail gated.
 * @param[in] blocking if set, waits for the gating to complete.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_rail_gate(struct platform_device *pdev,
	uint32_t timeout_us,
	bool blocking);

/**
 * @brief Power un-gating.
 *
 * @param[in] pdev Platform device that is to be rail ungated.
 *
 * @return
 * - zero, up on successful rail ungating.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_rail_ungate(struct platform_device *pdev);

/**
 * @brief Checks if the rail is gated or not.
 *
 * @param[in] pdev Platform device.
 * @param[out] gated Pointer where the status is saved.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_rail_is_gated(struct platform_device *pdev,
	bool *gated);

/**
 * @brief Triggers request to power gate after specified 'timeout_us'.
 *
 * @param[in] pdev Platform device that is to be power gated.
 * @param[in] timeout_us Timeout after which DLA must be power gated.
 * @param[in] blocking if set, waits for the gating to complete.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_power_gate(struct platform_device *pdev,
	uint32_t timeout_us,
	bool blocking);

/**
 * @brief Power un-gating.
 *
 * @param[in] pdev Platform device that is to be power ungated.
 *
 * @return
 * - zero, up on successful power ungating.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_power_ungate(struct platform_device *pdev);

/**
 * @brief Checks if the power is gated or not.
 *
 * @param[in] pdev Platform device.
 * @param[out] gated Pointer where the status is saved.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_power_is_gated(struct platform_device *pdev,
	bool *gated);

/**
 * @brief Triggers request to clock gate after specified 'timeout_us'.
 *
 * @param[in] pdev Platform device that is to be clock gated.
 * @param[in] timeout_us Timeout after which DLA must be clock gated.
 * @param[in] blocking if set, waits for the gating to complete.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_clock_gate(struct platform_device *pdev,
	uint32_t timeout_us,
	bool blocking);

/**
 * @brief Power un-gating.
 *
 * @param[in] pdev Platform device that is to be clock ungated.
 *
 * @return
 * - zero, up on successful clock ungating.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_clock_ungate(struct platform_device *pdev);

/**
 * @brief Checks if the clock is gated or not.
 *
 * @param[in] pdev Platform device.
 * @param[out] gated Pointer where the status is saved.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_clock_is_gated(struct platform_device *pdev,
	bool *gated);

/**
 * @brief Sets DLA MCU frequency.
 *
 * @param[in] pdev platform device.
 * @param[in] freq_khz frequency (in khz) that is to be set.
 *
 * @return
 * - zero, with successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_clock_set_mcu_freq(struct platform_device *pdev,
	uint32_t freq_khz);

/**
 * @brief Gets current DLA MCU frequency.
 *
 * @param[in] pdev platform device.
 * @param[out] freq_khz frequency (in khz).
 *
 * @return
 * - zero, with successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_clock_get_mcu_freq(struct platform_device *pdev,
	uint32_t *freq_khz);

/**
 * @brief Sets DLA core frequency.
 *
 * @param[in] pdev Platform device.
 * @param[in] freq_khz Frequency (in KHz) that is to be set.
 *
 * @return
 * - zero, with successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_clock_set_core_freq(struct platform_device *pdev,
	uint32_t freq_khz);

/**
 * @brief Gets current DLA core frequency.
 *
 * @param[in] pdev platform device.
 * @param[out] freq_khz frequency (in khz).
 *
 * @return
 * - zero, with successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_clock_get_core_freq(struct platform_device *pdev,
	uint32_t *freq_khz);

#endif /* __NVDLA_PM_H_ */
