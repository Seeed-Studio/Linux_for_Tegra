/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA KMD-FW interface
 */

#ifndef __NVDLA_FW_H__
#define __NVDLA_FW_H__

#include <linux/platform_device.h>

/**
 * data structure to keep command data
 *
 * @method_id		method id with command and other info
 * @method_data		method data for command
 * @wait		If set to true then wait for command completion
 */
struct nvdla_cmd_data {
	uint32_t method_id;
	uint32_t method_data;
	bool wait;
};

/**
 * @brief Powers on microcontroller.
 *
 * @param[in] pdev Platform device.
 *
 * @return
 * - zero, if successfully powered-on the microcontroller.
 * - non-zero, otherwise
 **/
int32_t nvdla_fw_poweron(struct platform_device *pdev);

/**
 * @brief Powers off microcontroller.
 *
 * @param[in] pdev Platform device.
 *
 * @return
 * - zero, if successfully powered-off the microcontroller.
 * - non-zero, otherwise
 **/
int32_t nvdla_fw_poweroff(struct platform_device *pdev);

/**
 * @brief Initializes the firmware ISRs.
 *
 * @param[in] pdev Platform device.
 *
 * @return
 * - zero, if successfully initialized
 * - non-zero, otherwise
 **/
int32_t nvdla_fw_init(struct platform_device *pdev);

/**
 * @brief Deinitializes the firmware.
 *
 * @param[in] pdev Platform device.
 **/
void nvdla_fw_deinit(struct platform_device *pdev);

/**
 * @brief Reloads the firmware.
 *
 * @param[in] pdev Platform device.
 *
 * @return
 * - zero, if successfully reloaded.
 * - non-zero, otherwise
 **/
int32_t nvdla_fw_reload(struct platform_device *pdev);

/**
 * @brief Sends the command id and data to the firmware.
 *
 * Sends the command to the firmware and blocks if wait is set.
 *
 * @param[in] pdev Platform device using which command needs to be sent.
 * @param[in] cmd_id Command identifier to denote the type of request.
 * @param[in] cmd_data Data associated with the command being sent.
 * @param[in] wait Waits for the firmware acknowledgement after command
 *                 is sent.
 *
 * @return
 * - zero, if successfully sent the command to the firmware.
 * - non-zero, otherwise
 **/
int32_t nvdla_fw_send_cmd(struct platform_device *pdev,
	struct nvdla_cmd_data *cmd_data);

/**
 * @brief Sends acknowledgement to the firmware.
 *
 * @param[in] pdev Platform device using which ack needs to be sent.
 * @param[in] ack Acknowledgment data to be sent.
 *
 * @return
 * - zero, if successfully sent ack to the firmware.
 * - non-zero, otherwise
 **/
int32_t nvdla_fw_send_ack(struct platform_device *pdev,
	int32_t ack);

/**
 * @brief Reads the interrupt stat registers.
 *
 * @param[in] pdev Platform device
 * @param[out] stat Pointer where the fetched stat data is stored.
 *
 * @return
 * - zero, if successfully read the interrupt stat.
 * - non-zero, otherwise
 **/
int32_t nvdla_fw_interrupt_stat_read(struct platform_device *pdev,
	int32_t *stat);

/**
 * @brief Clears the interrupt stat registers.
 *
 * @param[in] pdev Platform device
 *
 * @return
 * - zero, if successfully cleared the interrupt stat.
 * - non-zero, otherwise
 **/
int32_t nvdla_fw_interrupt_stat_clear(struct platform_device *pdev);

/**
 * @brief Injects corrected error
 *
 * @param[in] pdev Platform device
 *
 * @return
 * - zero, if corrected error is injected successfully.
 * - non-zero, otherwise
 **/
int32_t nvdla_fw_inject_corrected_error(struct platform_device *pdev);

/**
 * @brief Injects uncorrected error
 *
 * @param[in] pdev Platform device
 *
 * @return
 * - zero, if uncorrected error is injected successfully.
 * - non-zero, otherwise
 **/
int32_t nvdla_fw_inject_uncorrected_error(struct platform_device *pdev);

#endif /*__NVDLA_FW_H__ */
