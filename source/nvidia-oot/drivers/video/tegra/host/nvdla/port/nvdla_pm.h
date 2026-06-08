/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA Power Management
 */

#ifndef __NVDLA_PM_H_
#define __NVDLA_PM_H_

#include <linux/platform_device.h>
#include "../dla_os_interface.h"

struct nvdla_pm_stat {
	uint64_t clock_idle_count;
	uint64_t clock_idle_time_us;
	uint64_t cg_entry_latency_us_min;
	uint64_t cg_entry_latency_us_max;
	uint64_t cg_entry_latency_us_total;
	uint64_t clock_active_count;
	uint64_t clock_active_time_us;
	uint64_t cg_exit_latency_us_min;
	uint64_t cg_exit_latency_us_max;
	uint64_t cg_exit_latency_us_total;

	uint64_t power_idle_count;
	uint64_t power_idle_time_us;
	uint64_t pg_entry_latency_us_min;
	uint64_t pg_entry_latency_us_max;
	uint64_t pg_entry_latency_us_total;
	uint64_t power_active_count;
	uint64_t power_active_time_us;
	uint64_t pg_exit_latency_us_min;
	uint64_t pg_exit_latency_us_max;
	uint64_t pg_exit_latency_us_total;

	uint64_t rail_idle_count;
	uint64_t rail_idle_time_us;
	uint64_t rg_entry_latency_us_min;
	uint64_t rg_entry_latency_us_max;
	uint64_t rg_entry_latency_us_total;
	uint64_t rail_active_count;
	uint64_t rail_active_time_us;
	uint64_t rg_exit_latency_us_min;
	uint64_t rg_exit_latency_us_max;
	uint64_t rg_exit_latency_us_total;
};

struct nvdla_pm_info {
#define NVDLA_PM_MAX_VFTABLE_ENTRIES 8U
	uint32_t num_vftable_entries;
	uint32_t vftable_freq_kHz[NVDLA_PM_MAX_VFTABLE_ENTRIES];
	uint32_t vftable_voltage_mV[NVDLA_PM_MAX_VFTABLE_ENTRIES];
};

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
 * @brief Triggers request to rail gate after 'delay_us'.
 *
 * The delay_us is configured through nvdla_pm_rail_gate_set_delay_us API. In
 * the event of ungating request within delay_us, the outstanding gating
 * requests are cancelled.
 *
 * @param[in] pdev Platform device that is to be rail gated.
 * @param[in] blocking if set, waits for the gating to complete.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_rail_gate(struct platform_device *pdev,
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
 * @brief Sets delay (in us) for rail gating
 *
 * @param[in] pdev Platform device.
 * @param[in] delay_us Delay after which DLA must be rail gated if requested.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_rail_gate_set_delay_us(struct platform_device *pdev,
	uint32_t delay_us);

/**
 * @brief Gets delay (in us) for rail gating
 *
 * @param[in] pdev Platform device.
 * @param[out] delay_us Rail gating delay in us.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_rail_gate_get_delay_us(struct platform_device *pdev,
	uint32_t *delay_us);

/**
 * @brief Triggers request to power gate after 'delay_us'.
 *
 * The delay_us is configured through nvdla_pm_power_gate_set_delay_us API. In
 * the event of ungating request within delay_us, the outstanding gating
 * requests are cancelled.
 *
 * @param[in] pdev Platform device that is to be power gated.
 * @param[in] blocking if set, waits for the gating to complete.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_power_gate(struct platform_device *pdev,
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
 * @brief Sets delay (in us) for power gating
 *
 * @param[in] pdev Platform device.
 * @param[in] delay_us Delay after which DLA must be power gated if requested.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_power_gate_set_delay_us(struct platform_device *pdev,
	uint32_t delay_us);

/**
 * @brief Gets delay (in us) for power gating
 *
 * @param[in] pdev Platform device.
 * @param[out] delay_us Rail gating delay in us.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_power_gate_get_delay_us(struct platform_device *pdev,
	uint32_t *delay_us);

/**
 * @brief Triggers request to clock gate after 'delay_us'.
 *
 * The delay_us is configured through nvdla_pm_clock_gate_set_delay_us API. In
 * the event of ungating request within delay_us, the outstanding gating
 * requests are cancelled.
 *
 * @param[in] pdev Platform device that is to be clock gated.
 * @param[in] blocking if set, waits for the gating to complete.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_clock_gate(struct platform_device *pdev,
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
 * @brief Sets delay (in us) for clock gating
 *
 * @param[in] pdev Platform device.
 * @param[in] delay_us Delay after which DLA must be clock gated if requested.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_clock_gate_set_delay_us(struct platform_device *pdev,
	uint32_t delay_us);

/**
 * @brief Gets delay (in us) for clock gating
 *
 * @param[in] pdev Platform device.
 * @param[out] delay_us Rail gating delay in us.
 *
 * @return
 * - zero, up on successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_clock_gate_get_delay_us(struct platform_device *pdev,
	uint32_t *delay_us);

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

/**
 * @brief Gets the PM stats
 *
 * @param[in] pdev platform device.
 * @param[out] stat Location where the statistics are dumped.
 *
 * @return
 * - zero, with successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_get_stat(struct platform_device *pdev,
	struct nvdla_pm_stat *stat);

/**
 * @brief Sets LPWR config
 *
 * @param[in] pdev platform device.
 * @param[in] config Low power configuration that is to be set.
 *
 * @return
 * - zero, with successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_set_lpwr_config(struct platform_device *pdev,
	struct dla_lpwr_config *config);

/**
 * @brief Gets LPWR config
 *
 * @param[in] pdev platform device.
 * @param[out] config Location where the config is outputted.
 *
 * @return
 * - zero, with successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_get_lpwr_config(struct platform_device *pdev,
	struct dla_lpwr_config *config);

/**
 * @brief Reset configurations upon poweron
 *
 * @param[in] pdev platform device to reset PM.
 *
 * @return
 * - zero, with successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_reset(struct platform_device *pdev);

/**
 * @brief Gets the current voltage
 *
 * @param[in] pdev platform device.
 * @param[out] voltage_mV Location where voltage (in mV) is dumped.
 *
 * @return
 * - zero, with successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_get_current_voltage(struct platform_device *pdev,
	uint32_t *voltage_mV);

/**
 * @brief Gets the current power_draw
 *
 * @param[in] pdev platform device.
 * @param[out] power_draw_mW Location where power_draw (in mW) is dumped.
 *
 * @return
 * - zero, with successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_get_current_power_draw(struct platform_device *pdev,
	uint32_t *power_draw_mW);


/**
 * @brief Gets the static information - VF Curve
 *
 * @param[in] pdev platform device
 * @param[out] info Location where the info is dumped.
 *
 * @return
 * - zero, with successful operation.
 * - non-zero, otherwise
 **/
int32_t nvdla_pm_get_info(struct platform_device *pdev,
	struct nvdla_pm_info *info);
#endif /* __NVDLA_PM_H_ */
