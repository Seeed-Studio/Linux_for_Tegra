/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SHIM_CCQ_H
#define PVA_KMD_SHIM_CCQ_H
#include "pva_api.h"
struct pva_kmd_device;

/**
 * @brief Push a 64 bit entry to CCQ FIFO.
 *
 * Push low 32 bits first and then high 32 bits.
 *
 * @note The caller is responsible for checking if CCQ has enough spaces.
 *
 */
void pva_kmd_ccq_push(struct pva_kmd_device *pva, uint8_t ccq_id,
		      uint32_t ccq_entry);

/**
 * @brief Get the number of available spaces in the CCQ.
 *
 * One CCQ entry is 64 bits. One CCQ can hold up to 4 entries. Therefore, this
 * function returns values from 0 to 4.
 */
uint32_t pva_kmd_get_ccq_space(struct pva_kmd_device *pva, uint8_t ccq_id);

#endif // PVA_KMD_SHIM_CCQ_H
