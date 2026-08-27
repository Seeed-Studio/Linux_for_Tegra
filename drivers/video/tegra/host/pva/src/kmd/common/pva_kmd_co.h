/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_CO_H
#define PVA_KMD_CO_H

/**
 * @brief Compute Object information structure
 *
 * @details This structure contains memory mapping information for a compute object,
 * including both virtual and physical address mappings along with size information.
 * Compute objects represent memory regions or buffers that are used for computation
 * tasks on the PVA hardware, providing the necessary address translation information
 * for both software and hardware access.
 */
struct pva_co_info {
	/**
	 * @brief Virtual address base of the compute object
	 * Valid range: [0 .. UINT64_MAX]
	 */
	uint64_t base_va;

	/**
	 * @brief Physical address base of the compute object
	 * Valid range: [0 .. UINT64_MAX]
	 */
	uint64_t base_pa;

	/**
	 * @brief Size of the compute object in bytes
	 * Valid range: [1 .. UINT64_MAX]
	 */
	uint64_t size;
};

#endif //PVA_KMD_CO_H