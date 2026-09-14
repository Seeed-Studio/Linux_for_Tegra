/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SHIM_CCQ_H
#define PVA_KMD_SHIM_CCQ_H
#include "pva_api.h"
struct pva_kmd_device;

/**
 * @brief Push a 64 bit entry to CCQ FIFO.
 *
 * @details This function performs the following operations:
 * - Writes a 64-bit entry to the specified CCQ (Command and Control Queue)
 * - Pushes the low 32 bits first, followed by the high 32 bits
 * - Uses hardware-specific write operations to the CCQ FIFO
 * - Triggers hardware notification to the PVA firmware
 * - Enables direct user space to firmware communication
 *
 * The CCQ mechanism provides low-latency submission of command buffer
 * notifications directly from user space to the PVA firmware, bypassing
 * the need for system calls. The caller must ensure sufficient space
 * exists in the CCQ before calling this function.
 *
 * @note The caller is responsible for checking CCQ space availability
 * using @ref pva_kmd_get_ccq_space() before pushing entries.
 *
 * @param[in, out] pva       Pointer to @ref pva_kmd_device structure
 *                           Valid value: non-null
 * @param[in] ccq_id         CCQ identifier specifying which queue to use
 *                           Valid range: [0 .. 7] for user CCQs
 * @param[in] ccq_entry      32-bit entry data to push to the CCQ
 *                           Valid range: [0 .. UINT32_MAX]
 */
void pva_kmd_ccq_push(struct pva_kmd_device *pva, uint8_t ccq_id,
		      uint32_t ccq_entry);

/**
 * @brief Get the number of available spaces in the CCQ.
 *
 * @details This function performs the following operations:
 * - Reads the current fill level of the specified CCQ FIFO
 * - Calculates the number of available entry slots remaining
 * - Provides information for flow control and submission management
 * - Accesses hardware status registers to determine queue state
 * - Returns current availability without modifying queue state
 *
 * Each CCQ can hold up to 4 entries, where each entry is 64 bits.
 * This function enables callers to check availability before pushing
 * entries to prevent overflow and ensure reliable submission.
 *
 * The return value indicates how many 64-bit entries can be pushed
 * to the CCQ without overflow.
 *
 * @param[in] pva     Pointer to @ref pva_kmd_device structure
 *                    Valid value: non-null
 * @param[in] ccq_id  CCQ identifier specifying which queue to check
 *                    Valid range: [0 .. 7] for user CCQs
 *
 * @retval space_count  Number of available 64-bit entry slots
 *                      Valid range: [0 .. 4]
 */
uint32_t pva_kmd_get_ccq_space(struct pva_kmd_device *pva, uint8_t ccq_id);

#endif // PVA_KMD_SHIM_CCQ_H
