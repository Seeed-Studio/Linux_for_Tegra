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
 * @details This function performs the following operations:
 * - Writes the specified value to a memory-mapped I/O register
 * - Uses the specified aperture to determine the MMIO region
 * - Adds the register offset to the aperture base address
 * - Performs platform-appropriate memory-mapped write operation
 * - Ensures proper memory ordering and write completion
 *
 * This function provides controlled access to PVA hardware registers
 * through the memory-mapped I/O interface. Different apertures
 * correspond to different functional blocks within the PVA hardware.
 *
 * @param[in, out] pva      Pointer to @ref pva_kmd_device structure
 *                          Valid value: non-null
 * @param[in] aperture      MMIO region identifier
 *                          Valid values: @ref pva_kmd_reg_aperture
 * @param[in] addr          Register offset within the MMIO region
 *                          Valid range: aperture-specific
 * @param[in] val           Value to write to the register
 *                          Valid range: [0 .. UINT32_MAX]
 */
void pva_kmd_aperture_write(struct pva_kmd_device *pva,
			    enum pva_kmd_reg_aperture aperture, uint32_t addr,
			    uint32_t val);

/**
 * @brief Read from a register in a MMIO region.
 *
 * @details This function performs the following operations:
 * - Reads the current value from a memory-mapped I/O register
 * - Uses the specified aperture to determine the MMIO region
 * - Adds the register offset to the aperture base address
 * - Performs platform-appropriate memory-mapped read operation
 * - Ensures proper memory ordering and read completion
 * - Returns the register value to the caller
 *
 * This function provides controlled access to PVA hardware registers
 * through the memory-mapped I/O interface for status checking and
 * hardware state monitoring.
 *
 * @param[in] pva       Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 * @param[in] aperture  MMIO region identifier
 *                      Valid values: @ref pva_kmd_reg_aperture
 * @param[in] addr      Register offset within the MMIO region
 *                      Valid range: aperture-specific
 *
 * @retval register_value  Current value read from the register
 *                         Valid range: [0 .. UINT32_MAX]
 */
uint32_t pva_kmd_aperture_read(struct pva_kmd_device *pva,
			       enum pva_kmd_reg_aperture aperture,
			       uint32_t addr);

/**
 * @brief PVA's interrupt lines.
 *
 * @details This enumeration defines the interrupt lines available in the
 * PVA hardware for communication between different components and the
 * host system. Each interrupt line serves a specific purpose in the
 * PVA operation and communication protocol.
 */
enum pva_kmd_intr_line {
	/**
	 * @brief Interrupt line from SEC block
	 *
	 * @details This interrupt line receives mailbox interrupts from the
	 * SEC (Security) block. It is used for secure communication and
	 * control operations between the host and PVA firmware.
	 */
	PVA_KMD_INTR_LINE_SEC_LIC = 0U,
	/** @brief CCQ0 interrupt line for Command and Control Queue 0 */
	PVA_KMD_INTR_LINE_CCQ0,
	/** @brief CCQ1 interrupt line for Command and Control Queue 1 */
	PVA_KMD_INTR_LINE_CCQ1,
	/** @brief CCQ2 interrupt line for Command and Control Queue 2 */
	PVA_KMD_INTR_LINE_CCQ2,
	/** @brief CCQ3 interrupt line for Command and Control Queue 3 */
	PVA_KMD_INTR_LINE_CCQ3,
	/** @brief CCQ4 interrupt line for Command and Control Queue 4 */
	PVA_KMD_INTR_LINE_CCQ4,
	/** @brief CCQ5 interrupt line for Command and Control Queue 5 */
	PVA_KMD_INTR_LINE_CCQ5,
	/** @brief CCQ6 interrupt line for Command and Control Queue 6 */
	PVA_KMD_INTR_LINE_CCQ6,
	/** @brief CCQ7 interrupt line for Command and Control Queue 7 */
	PVA_KMD_INTR_LINE_CCQ7,
	/** @brief Total count of interrupt lines available */
	PVA_KMD_INTR_LINE_COUNT,
};

/**
 * @brief Interrupt handler function prototype.
 *
 * @details This typedef defines the signature for interrupt handler
 * functions that can be registered with the PVA interrupt system.
 * Handler functions are called when their corresponding interrupt
 * line is triggered by the hardware.
 *
 * @param[in, out] data       User-provided context data passed to handler
 *                            Valid value: as provided during registration
 * @param[in] intr_line       Interrupt line that triggered the handler
 *                            Valid values: @ref pva_kmd_intr_line
 */
typedef void (*pva_kmd_intr_handler_t)(void *data,
				       enum pva_kmd_intr_line intr_line);

/**
 * @brief Bind an interrupt handler to an interrupt line.
 *
 * @details This function performs the following operations:
 * - Registers the specified handler function for the interrupt line
 * - Associates user-provided context data with the handler
 * - Configures hardware interrupt routing and priority
 * - Enables the interrupt line after successful binding
 * - Sets up platform-specific interrupt handling mechanisms
 *
 * The handler function will be called whenever the specified interrupt
 * line is triggered by the PVA hardware. The interrupt is automatically
 * enabled after successful binding.
 *
 * @param[in, out] pva       Pointer to @ref pva_kmd_device structure
 *                           Valid value: non-null
 * @param[in] intr_line      Interrupt line to bind the handler to
 *                           Valid values: @ref pva_kmd_intr_line
 * @param[in] handler        Handler function to call on interrupt
 *                           Valid value: non-null function pointer
 * @param[in] data           User context data passed to handler
 *                           Valid value: any pointer value or NULL
 *
 * @retval PVA_SUCCESS           Handler bound successfully and interrupt enabled
 * @retval PVA_INVAL             Invalid interrupt line or handler
 * @retval PVA_AGAIN             Interrupt line already has a bound handler
 * @retval PVA_INTERNAL          Hardware configuration failed
 */
enum pva_error pva_kmd_bind_intr_handler(struct pva_kmd_device *pva,
					 enum pva_kmd_intr_line intr_line,
					 pva_kmd_intr_handler_t handler,
					 void *data);

/**
 * @brief Enable an interrupt line.
 *
 * @details This function performs the following operations:
 * - Enables the specified interrupt line at the hardware level
 * - Allows interrupt generation from the PVA hardware
 * - Configures interrupt controller settings if necessary
 * - Ensures proper interrupt routing to the handler
 *
 * This function enables interrupts for a line that has been previously
 * bound with @ref pva_kmd_bind_intr_handler() but may have been disabled.
 *
 * @param[in, out] pva       Pointer to @ref pva_kmd_device structure
 *                           Valid value: non-null
 * @param[in] intr_line      Interrupt line to enable
 *                           Valid values: @ref pva_kmd_intr_line
 */
void pva_kmd_enable_intr(struct pva_kmd_device *pva,
			 enum pva_kmd_intr_line intr_line);

/**
 * @brief Disable an interrupt line without waiting for running interrupt
 * handlers to complete.
 *
 * @details This function performs the following operations:
 * - Immediately disables the specified interrupt line at hardware level
 * - Prevents new interrupts from being generated on this line
 * - Does not wait for currently executing handlers to complete
 * - Provides emergency shutdown capability for the interrupt
 *
 * This function is designed for situations where immediate interrupt
 * shutdown is required without waiting for handler completion. It can
 * be safely called from interrupt context.
 *
 * @param[in, out] pva       Pointer to @ref pva_kmd_device structure
 *                           Valid value: non-null
 * @param[in] intr_line      Interrupt line to disable
 *                           Valid values: @ref pva_kmd_intr_line
 */
void pva_kmd_disable_intr_nosync(struct pva_kmd_device *pva,
				 enum pva_kmd_intr_line intr_line);

/**
 * @brief Free an interrupt line.
 *
 * @details This function performs the following operations:
 * - Disables the interrupt line at the hardware level
 * - Unbinds the previously registered interrupt handler
 * - Cleans up interrupt controller configuration
 * - Releases resources associated with the interrupt line
 * - Ensures the interrupt line is ready for future binding
 *
 * This function reverses the operations performed by
 * @ref pva_kmd_bind_intr_handler() and provides complete cleanup
 * of interrupt resources.
 *
 * @param[in, out] pva       Pointer to @ref pva_kmd_device structure
 *                           Valid value: non-null
 * @param[in] intr_line      Interrupt line to free
 *                           Valid values: @ref pva_kmd_intr_line
 */
void pva_kmd_free_intr(struct pva_kmd_device *pva,
		       enum pva_kmd_intr_line intr_line);

/**
 * @brief Read firmware binary from file system.
 *
 * @details This function performs the following operations:
 * - Locates the PVA firmware binary file in the file system
 * - Reads the entire firmware binary into memory
 * - Allocates memory accessible by the R5 processor
 * - Stores the firmware in @ref pva_kmd_device::fw_bin_mem
 * - Validates firmware format and integrity
 * - Prepares firmware for loading into R5 processor
 *
 * The firmware binary is loaded into memory that is directly accessible
 * by the R5 processor for execution. The memory allocation is managed
 * by the KMD and will be freed during firmware deinitialization.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 *
 * @retval PVA_SUCCESS              Firmware binary read successfully
 * @retval PVA_NOENT                Firmware file not found
 * @retval PVA_NOMEM                Insufficient memory for firmware
 * @retval PVA_INVAL                Invalid firmware binary format
 * @retval PVA_INTERNAL             File system I/O error
 */
enum pva_error pva_kmd_read_fw_bin(struct pva_kmd_device *pva);

/**
 * @brief Get starting IOVA of the memory shared by R5 and KMD.
 *
 * @details This function performs the following operations:
 * - Returns the base IOVA address for R5-KMD shared memory region
 * - Provides platform-specific IOVA allocation information
 * - Enables proper memory mapping configuration for communication
 * - Returns addresses compatible with R5 virtual address space
 *
 * The IOVA range is platform-dependent:
 * - Linux platforms use 0-2GB range
 * - QNX platforms use 2GB-4GB range (configured in device tree)
 *
 * This memory region corresponds to the 2GB-4GB region of the R5
 * virtual address space, enabling efficient communication between
 * the host KMD and the R5 firmware.
 *
 * @retval iova_start  Starting IOVA address for shared memory region
 *                     Valid range: platform-specific
 */
uint64_t pva_kmd_get_r5_iova_start(void);

/**
 * @brief Configure EVP, Segment config registers and SCR registers.
 *
 * @details This function performs the following operations:
 * - Configures the Exception Vector Processor (EVP) registers
 * - Sets up memory segment configuration registers
 * - Configures Secure Configuration Registers (SCR)
 * - Establishes proper memory mapping for R5 processor
 * - Sets up security and access control configurations
 * - Enables proper hardware initialization sequence
 *
 * These configurations are essential for proper R5 processor operation
 * and secure access to PVA hardware resources. The function sets up
 * the memory layout and security policies required for firmware execution.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 */
void pva_kmd_config_evp_seg_scr_regs(struct pva_kmd_device *pva);

/**
 * @brief Configure SID registers.
 *
 * @details This function performs the following operations:
 * - Configures Stream ID (SID) registers for SMMU access control
 * - Sets up hardware stream identifiers for memory transactions
 * - Establishes proper IOMMU/SMMU configuration
 * - Enables secure memory access control for PVA operations
 * - Configures hardware identity for memory protection
 *
 * SID registers are used by the SMMU (System Memory Management Unit)
 * to identify and control memory access from different PVA hardware
 * components, ensuring proper isolation and security.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 */
void pva_kmd_config_sid_regs(struct pva_kmd_device *pva);

/**
 * @brief Set the PVA HW reset line.
 *
 * @details This function performs the following operations:
 * - Asserts the hardware reset signal for the PVA block
 * - Places PVA hardware in a known reset state
 * - Ensures all PVA hardware components are properly reset
 * - Prepares hardware for subsequent initialization
 * - Clears any previous hardware state or errors
 *
 * This function is typically used during hardware initialization
 * or error recovery procedures to ensure the PVA hardware starts
 * from a clean, known state.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 */
void pva_kmd_set_reset_line(struct pva_kmd_device *pva);

#endif // PVA_KMD_SHIM_SILICON_H
