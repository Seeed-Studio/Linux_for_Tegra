/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SILICON_UTILS_H
#define PVA_KMD_SILICON_UTILS_H
#include "pva_utils.h"
#include "pva_kmd_regs.h"
#include "pva_kmd_shim_silicon.h"
#include "pva_math_utils.h"

/**
 * @brief Write a 32-bit value to PVA device register
 *
 * @details This inline function writes a 32-bit value to a register within the
 * PVA device address space. It uses the PVA cluster aperture for register
 * access and includes debug logging for register write operations. The function
 * provides a convenient interface for writing to PVA hardware registers.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must be initialized
 * @param[in] addr Register address offset within PVA address space
 *                 Valid range: [0 .. PVA_REGISTER_SPACE_SIZE-1]
 * @param[in] val  32-bit value to write to the register
 *                 Valid range: [0 .. UINT32_MAX]
 */
static inline void pva_kmd_write(struct pva_kmd_device *pva, uint32_t addr,
				 uint32_t val)
{
	pva_dbg_printf("pva_kmd_write: addr=0x%x, val=0x%x\n", addr, val);
	pva_kmd_aperture_write(pva, PVA_KMD_APERTURE_PVA_CLUSTER, addr, val);
}

/**
 * @brief Read a 32-bit value from PVA device register
 *
 * @details This inline function reads a 32-bit value from a register within
 * the PVA device address space. It uses the PVA cluster aperture for register
 * access and returns the value read from the specified register address. The
 * function provides a convenient interface for reading from PVA hardware
 * registers.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must be initialized
 * @param[in] addr Register address offset within PVA address space
 *                 Valid range: [0 .. PVA_REGISTER_SPACE_SIZE-1]
 *
 * @retval uint32_t Value read from the register
 *                  Valid range: [0 .. UINT32_MAX]
 */
static inline uint32_t pva_kmd_read(struct pva_kmd_device *pva, uint32_t addr)
{
	uint32_t val;

	val = pva_kmd_aperture_read(pva, PVA_KMD_APERTURE_PVA_CLUSTER, addr);
	return val;
}

/**
 * @brief Write a value to HSP shared mailbox register
 *
 * @details This inline function writes a value to a specific HSP (Hardware
 * Synchronization Primitive) shared mailbox register. It calculates the
 * appropriate register address based on the mailbox index and writes the
 * value to the corresponding mailbox register. Mailboxes are used for
 * communication between different processing units.
 *
 * @param[in] pva          Pointer to @ref pva_kmd_device structure
 *                         Valid value: non-null, must be initialized
 * @param[in] mailbox_idx  Index of the mailbox to write to
 *                         Valid range: [0 .. MAX_MAILBOX_COUNT-1]
 * @param[in] val          32-bit value to write to the mailbox
 *                         Valid range: [0 .. UINT32_MAX]
 */
static inline void pva_kmd_write_mailbox(struct pva_kmd_device *pva,
					 uint32_t mailbox_idx, uint32_t val)
{
	uint32_t gap = PVA_REG_HSP_SM1_ADDR - PVA_REG_HSP_SM0_ADDR;
	uint32_t offset = safe_mulu32(gap, mailbox_idx);
	uint32_t addr = safe_addu32(PVA_REG_HSP_SM0_ADDR, offset);
	pva_kmd_write(pva, addr, val);
}

/**
 * @brief Read a value from HSP shared mailbox register
 *
 * @details This inline function reads a value from a specific HSP (Hardware
 * Synchronization Primitive) shared mailbox register. It calculates the
 * appropriate register address based on the mailbox index and reads the
 * value from the corresponding mailbox register. Mailboxes are used for
 * communication between different processing units.
 *
 * @param[in] pva          Pointer to @ref pva_kmd_device structure
 *                         Valid value: non-null, must be initialized
 * @param[in] mailbox_idx  Index of the mailbox to read from
 *                         Valid range: [0 .. MAX_MAILBOX_COUNT-1]
 *
 * @retval uint32_t Value read from the mailbox register
 *                  Valid range: [0 .. UINT32_MAX]
 */
static inline uint32_t pva_kmd_read_mailbox(struct pva_kmd_device *pva,
					    uint32_t mailbox_idx)
{
	uint32_t gap = PVA_REG_HSP_SM1_ADDR - PVA_REG_HSP_SM0_ADDR;
	uint32_t offset = safe_mulu32(gap, mailbox_idx);
	uint32_t addr = safe_addu32(PVA_REG_HSP_SM0_ADDR, offset);
	return pva_kmd_read(pva, addr);
}

#endif // PVA_KMD_SILICON_UTILS_H
