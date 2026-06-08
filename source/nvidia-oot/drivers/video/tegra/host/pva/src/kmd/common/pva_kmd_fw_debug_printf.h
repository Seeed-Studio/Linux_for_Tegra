/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_FW_DEBUG_PRINTF_H
#define PVA_KMD_FW_DEBUG_PRINTF_H
#include "pva_api.h"
#include "pva_fw.h"
#include "pva_kmd_device.h"

/**
 * @brief Structure for managing firmware print buffer access
 *
 * @details This structure provides access to the firmware's print/debug output
 * buffer, which is used for collecting debug messages, trace information, and
 * other diagnostic output from the firmware. The structure maintains pointers
 * to both the buffer header (containing metadata) and the actual content data.
*/
struct pva_kmd_fw_print_buffer {
	/**
	 * @brief Pointer to firmware print buffer header containing metadata
	 * Valid value: non-null when buffer is active
	 */
	struct pva_fw_print_buffer_header *buffer_info;

	/**
	 * @brief Pointer to the actual print buffer content data
	 * Valid value: non-null when buffer is active
	 */
	char const *content;
};

/**
 * @brief Drain and process firmware print buffer contents
 *
 * @details This function performs the following operations:
 * - Reads new content from the firmware print buffer
 * - Processes and formats the debug output for display or logging
 * - Updates buffer read pointers to mark content as consumed
 * - Handles buffer wraparound and overflow conditions
 * - Outputs firmware messages to appropriate debug/log channels
 * - Maintains proper synchronization with firmware buffer updates
 *
 * This function is typically called periodically or in response to firmware
 * notifications to collect and display debug output from the firmware. The
 * print buffer must be properly initialized before calling this function.
 *
 * @param[in, out] print_buffer  Pointer to @ref pva_kmd_fw_print_buffer structure
 *                               Valid value: non-null, must be initialized
 */
void pva_kmd_drain_fw_print(struct pva_kmd_device *pva);

void pva_kmd_init_fw_print_buffer(struct pva_kmd_device *pva,
				  void *debug_buffer_va);
#endif // PVA_KMD_FW_DEBUG_PRINTF_H
