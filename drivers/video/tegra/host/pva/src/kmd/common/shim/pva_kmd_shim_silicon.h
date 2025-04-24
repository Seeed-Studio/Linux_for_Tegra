/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SHIM_SILICON_H
#define PVA_KMD_SHIM_SILICON_H
#include "pva_api.h"
#include "pva_kmd_regs.h"
struct pva_kmd_device;

/**
 * @file This file defines silicon APIs.
 *
 * Silicon APIs are only implemented by platforms that closely resemble the
 * silicon PVA, a.k.a Linux, QNX and SIM platforms. Silicon APIs are used to
 * implement message APIs and some init APIs.
 *
 * On native platform, message APIs are implemented differently. Therefore,
 * native platform does not need to implement silicon APIs.
 */

/**
 * @brief Write to a register in a MMIO region.
 *
 * @param pva pointer the PVA cluser.
 * @param aperture the MMIO region.
 * @param addr the register offset in the MMIO region.
 * @param val value to write.
 */
void pva_kmd_aperture_write(struct pva_kmd_device *pva,
			    enum pva_kmd_reg_aperture aperture, uint32_t addr,
			    uint32_t val);
/**
 * @brief Read from a register in a MMIO region.
 *
 * @param pva pointer the PVA cluser.
 * @param aperture the MMIO region.
 * @param addr the register offset in the MMIO region.
 *
 * @return the value of the register.
 */
uint32_t pva_kmd_aperture_read(struct pva_kmd_device *pva,
			       enum pva_kmd_reg_aperture aperture,
			       uint32_t addr);

/**
 * @brief PVA's interrupt lines.
 */
enum pva_kmd_intr_line {
	/** Interrupt line from SEC block. We receive mailbox interrupts from
	 * this line. */
	PVA_KMD_INTR_LINE_SEC_LIC = 0,
	PVA_KMD_INTR_LINE_CCQ0,
	PVA_KMD_INTR_LINE_CCQ1,
	PVA_KMD_INTR_LINE_CCQ2,
	PVA_KMD_INTR_LINE_CCQ3,
	PVA_KMD_INTR_LINE_CCQ4,
	PVA_KMD_INTR_LINE_CCQ5,
	PVA_KMD_INTR_LINE_CCQ6,
	PVA_KMD_INTR_LINE_CCQ7,
	PVA_KMD_INTR_LINE_COUNT,
};

/**
 * @brief Interrupt handler function prototype.
 */
typedef void (*pva_kmd_intr_handler_t)(void *data,
				       enum pva_kmd_intr_line intr_line);

/**
 * @brief Bind an interrupt handler to an interrupt line.
 *
 * Interrupt will be enabled after binding.
 */
enum pva_error pva_kmd_bind_intr_handler(struct pva_kmd_device *pva,
					 enum pva_kmd_intr_line intr_line,
					 pva_kmd_intr_handler_t handler,
					 void *data);
/**
 * @brief Enable an interrupt line.
 */
void pva_kmd_enable_intr(struct pva_kmd_device *pva,
			 enum pva_kmd_intr_line intr_line);

/**
 * @brief Disable an interrupt line without waiting for running interrupt handlers to complete.
 */
void pva_kmd_disable_intr_nosync(struct pva_kmd_device *pva,
				 enum pva_kmd_intr_line intr_line);

/**
 * @brief Free an interrupt line.
 *
 * This will disable the interrupt line and unbind the handler.
 */
void pva_kmd_free_intr(struct pva_kmd_device *pva,
		       enum pva_kmd_intr_line intr_line);

/**
 * @brief Read firmware binary from file system.
 *
 * Firmware binary is loaded into pva->fw_bin_mem, which is directly accessible
 * by R5.
 *
 * KMD will free pva->fw_bin_mem during firmware deinit.
 */
enum pva_error pva_kmd_read_fw_bin(struct pva_kmd_device *pva);

/**
 * @brief Get starting IOVA of the memory shared by R5 and KMD.
 *
 * The starting IOVA is determined by the IOVA allocator on different platforms.
 * On Linux, the IOVA range is 0-2GB. On QNX, the IOVA range is 2GB-4GB
 * (configured in DTS).
 *
 * This memory region corresponds to the 2GB-4GB region of the R5 virtual
 * address space.
 */
uint64_t pva_kmd_get_r5_iova_start(void);

/**
 * @brief Configure EVP, Segment config registers and SCR registers.
 *
 * This function configures the EVP, Segment config registers and SCR registers.
 *
 * @param pva Pointer to the PVA device.
 */
void pva_kmd_config_evp_seg_scr_regs(struct pva_kmd_device *pva);

/**
 * @brief Configure SID registers.
 *
 * This function configures the SID registers.
 *
 * @param pva Pointer to the PVA device.
 */
void pva_kmd_config_sid_regs(struct pva_kmd_device *pva);

/**
 * @brief Set the PVA HW reset line.
 */
void pva_kmd_set_reset_line(struct pva_kmd_device *pva);

#endif // PVA_KMD_SHIM_SILICON_H
