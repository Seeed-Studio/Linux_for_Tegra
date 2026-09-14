/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_REGS_H
#define PVA_KMD_REGS_H

#include "pva_api.h"
#include "pva_constants.h"

/**
 * @brief SEC (Security) subsystem base offset
 *
 * @details Base offset for SEC subsystem registers within PVA aperture.
 * The SEC subsystem handles security-related functionality including
 * error monitoring and interrupt management.
 * Value: 0x20000 (128KB offset)
 */
#define PVA0_SEC_OFFSET 0x20000

/**
 * @brief PROC (Processor) subsystem base offset
 *
 * @details Base offset for PROC subsystem registers within PVA aperture.
 * The PROC subsystem manages the R5 processor control and status.
 * Value: 0x30000 (192KB offset)
 */
#define PVA0_PROC_OFFSET 0x30000

/**
 * @brief PM (Power Management) subsystem base offset
 *
 * @details Base offset for PM subsystem registers within PVA aperture.
 * The PM subsystem handles power management and clock control.
 * Value: 0x200000 (2MB offset)
 */
#define PVA0_PM_OFFSET 0x200000

/**
 * @brief CFG SID (Stream ID) configuration base offset
 *
 * @details Base offset for CFG SID registers within PVA aperture.
 * These registers configure Stream IDs for SMMU translation.
 * Value: 0x240000 (2.25MB offset)
 */
#define PVA0_CFG_SID_OFFSET 0x240000

/**
 * @brief CFG CCQ (Command and Control Queue) configuration base offset
 *
 * @details Base offset for CFG CCQ registers within PVA aperture.
 * These registers configure the CCQ interfaces for command submission.
 * Value: 0x260000 (2.375MB offset)
 */
#define PVA0_CFG_CCQ_OFFSET 0x260000

/**
 * @brief HSP (Hardware Synchronization Primitives) base offset
 *
 * @details Base offset for HSP registers within PVA aperture.
 * HSP provides hardware synchronization mechanisms including
 * semaphores and shared mailboxes.
 * Value: 0x160000 (1.375MB offset)
 */
#define PVA0_HSP_OFFSET 0x160000

/**
 * @brief EVP (Exception Vector Processor) base offset
 *
 * @details Base offset for EVP registers within PVA aperture.
 * EVP manages exception vectors and interrupt handling for the R5.
 * Value: 0x0 (base of aperture)
 */
#define PVA0_EVP_OFFSET 0x0

/**
 * @brief SEC subsystem aperture size
 *
 * @details Size of the SEC subsystem register aperture.
 * Value: 0x10000 (64KB)
 */
#define PVA_KMD_PVA0_SEC_SIZE 0x10000 // 64KB

/**
 * @brief PROC subsystem aperture size
 *
 * @details Size of the PROC subsystem register aperture.
 * Value: 0x10000 (64KB)
 */
#define PVA_KMD_PVA0_PROC_SIZE 0x10000 // 64KB

/**
 * @brief PM subsystem aperture size
 *
 * @details Size of the PM subsystem register aperture.
 * Value: 0x10000 (64KB)
 */
#define PVA_KMD_PVA0_PM_SIZE 0x10000 // 64KB

/**
 * @brief CFG SID configuration aperture size
 *
 * @details Size of the CFG SID register aperture.
 * Value: 0x20000 (128KB)
 */
#define PVA_KMD_PVA0_CFG_SID_SIZE 0x20000 // 128KB

/**
 * @brief CFG CCQ configuration aperture size
 *
 * @details Size of the CFG CCQ register aperture.
 * Value: 0x80000 (512KB)
 */
#define PVA_KMD_PVA0_CFG_CCQ_SIZE 0x80000 // 512KB

/**
 * @brief HSP aperture size
 *
 * @details Size of the HSP register aperture.
 * Value: 0x90000 (576KB)
 */
#define PVA_KMD_PVA0_HSP_SIZE 0x90000 // 576KB

/**
 * @brief EVP aperture size
 *
 * @details Size of the EVP register aperture.
 * Value: 0x10000 (64KB)
 */
#define PVA_KMD_PVA0_EVP_SIZE 0x10000 // 64KB

/**
 * @brief Reset exception vector address
 *
 * @details Register address for the reset exception vector entry point.
 * This vector is executed when the R5 processor is reset.
 * Value: 0x20
 */
#define PVA_REG_EVP_RESET_ADDR 0x20

/**
 * @brief Undefined instruction exception vector address
 *
 * @details Register address for the undefined instruction exception vector.
 * This vector is executed when an undefined instruction is encountered.
 * Value: 0x24
 */
#define PVA_REG_EVP_UNDEF_ADDR 0x24

/**
 * @brief Software interrupt exception vector address
 *
 * @details Register address for the software interrupt exception vector.
 * This vector is executed for software-generated interrupts (SWI).
 * Value: 0x28
 */
#define PVA_REG_EVP_SWI_ADDR 0x28

/**
 * @brief Prefetch abort exception vector address
 *
 * @details Register address for the prefetch abort exception vector.
 * This vector is executed when a prefetch abort occurs.
 * Value: 0x2c
 */
#define PVA_REG_EVP_PREFETCH_ABORT_ADDR 0x2c

/**
 * @brief Data abort exception vector address
 *
 * @details Register address for the data abort exception vector.
 * This vector is executed when a data abort occurs.
 * Value: 0x30
 */
#define PVA_REG_EVP_DATA_ABORT_ADDR 0x30

/**
 * @brief Reserved exception vector address
 *
 * @details Register address for the reserved exception vector slot.
 * This vector is reserved for future use.
 * Value: 0x34
 */
#define PVA_REG_EVP_RSVD_ADDR 0x34

/**
 * @brief IRQ exception vector address
 *
 * @details Register address for the IRQ exception vector.
 * This vector is executed for interrupt requests (IRQ).
 * Value: 0x38
 */
#define PVA_REG_EVP_IRQ_ADDR 0x38

/**
 * @brief FIQ exception vector address
 *
 * @details Register address for the FIQ exception vector.
 * This vector is executed for fast interrupt requests (FIQ).
 * Value: 0x3c
 */
#define PVA_REG_EVP_FIQ_ADDR 0x3c

/**
 * @brief R5 processor CPU halt control register address
 *
 * @details Register address for controlling R5 processor halt state.
 * Writing to this register can halt or resume the R5 processor.
 * Value: 0x30000
 */
#define PVA_REG_PROC_CPUHALT_ADDR 0x30000

/**
 * @brief SEC external interrupt event SCR address
 *
 * @details Register address for SEC external interrupt event control.
 * This SCR (Secure Configuration Register) manages external interrupts.
 * Value: 0x28804
 */
#define PVA_SEC_SCR_SECEXT_INTR_EVENT 0x28804

/**
 * @brief PROC subsystem SCR address
 *
 * @details Register address for PROC subsystem control.
 * This SCR manages processor-related security configuration.
 * Value: 0x30800
 */
#define PVA_PROC_SCR_PROC 0x30800

/**
 * @brief EVP SCR register address
 *
 * @details Register address for EVP (Exception Vector Processor) control.
 * Corresponds to PVA_EVP_SCR_EVP_0 hardware register.
 * Value: 0x40
 */
#define PVA_REG_EVP_SCR_ADDR 0x40 //PVA_EVP_SCR_EVP_0

/**
 * @brief CFG status control SCR address
 *
 * @details Register address for configuration status control.
 * Corresponds to PVA_CFG_SCR_STATUS_CNTL_0 hardware register.
 * Value: 0x258000
 */
#define PVA_CFG_SCR_STATUS_CNTL 0x258000 //PVA_CFG_SCR_STATUS_CNTL_0

/**
 * @brief CFG privilege control SCR address
 *
 * @details Register address for configuration privilege control.
 * Corresponds to PVA_CFG_SCR_PRIV_0 hardware register.
 * Value: 0x258008
 */
#define PVA_CFG_SCR_PRIV 0x258008 //PVA_CFG_SCR_PRIV_0

/**
 * @brief CFG CCQ control SCR address
 *
 * @details Register address for CCQ (Command and Control Queue) control.
 * Corresponds to PVA_CFG_SCR_CCQ_CNTL_0 hardware register.
 * Value: 0x258010
 */
#define PVA_CFG_SCR_CCQ_CNTL 0x258010 //PVA_CFG_SCR_CCQ_CNTL_0

/**
 * @brief HSP common control register address
 *
 * @details Base register address for HSP common control functionality.
 * This controls overall HSP operation and configuration.
 * Value: 0x160000
 */
#define PVA_REG_HSP_COMMON_ADDR 0x160000

/**
 * @brief HSP interrupt enable 0 register address
 *
 * @details Register address for HSP interrupt enable control (channel 0).
 * Controls which HSP interrupts are enabled.
 * Value: 0x160100
 */
#define PVA_REG_HSP_INT_IE0_ADDR 0x160100

/**
 * @brief HSP interrupt enable 1 register address
 *
 * @details Register address for HSP interrupt enable control (channel 1).
 * Value: 0x160104
 */
#define PVA_REG_HSP_INT_IE1_ADDR 0x160104

/**
 * @brief HSP interrupt enable 2 register address
 *
 * @details Register address for HSP interrupt enable control (channel 2).
 * Value: 0x160108
 */
#define PVA_REG_HSP_INT_IE2_ADDR 0x160108

/**
 * @brief HSP interrupt enable 3 register address
 *
 * @details Register address for HSP interrupt enable control (channel 3).
 * Value: 0x16010c
 */
#define PVA_REG_HSP_INT_IE3_ADDR 0x16010c

/**
 * @brief HSP interrupt enable 4 register address
 *
 * @details Register address for HSP interrupt enable control (channel 4).
 * Value: 0x160110
 */
#define PVA_REG_HSP_INT_IE4_ADDR 0x160110

/**
 * @brief HSP external interrupt register address
 *
 * @details Register address for HSP external interrupt status and control.
 * Value: 0x160300
 */
#define PVA_REG_HSP_INT_EXTERNAL_ADDR 0x160300

/**
 * @brief HSP internal interrupt register address
 *
 * @details Register address for HSP internal interrupt status and control.
 * Value: 0x160304
 */
#define PVA_REG_HSP_INT_INTERNAL_ADDR 0x160304

/**
 * @brief HSP shared mailbox 0 base address
 *
 * @details Base register address for HSP shared mailbox 0.
 * Used for inter-processor communication.
 * Value: 0x170000
 */
#define PVA_REG_HSP_SM0_ADDR 0x170000

/**
 * @brief HSP shared mailbox 1 base address
 *
 * @details Base register address for HSP shared mailbox 1.
 * Value: 0x178000
 */
#define PVA_REG_HSP_SM1_ADDR 0x178000

/**
 * @brief HSP shared mailbox 2 base address
 *
 * @details Base register address for HSP shared mailbox 2.
 * Value: 0x180000
 */
#define PVA_REG_HSP_SM2_ADDR 0x180000

/**
 * @brief HSP shared mailbox 3 base address
 *
 * @details Base register address for HSP shared mailbox 3.
 * Value: 0x188000
 */
#define PVA_REG_HSP_SM3_ADDR 0x188000

/**
 * @brief HSP shared mailbox 4 base address
 *
 * @details Base register address for HSP shared mailbox 4.
 * Value: 0x190000
 */
#define PVA_REG_HSP_SM4_ADDR 0x190000

/**
 * @brief HSP shared mailbox 5 base address
 *
 * @details Base register address for HSP shared mailbox 5.
 * Value: 0x198000
 */
#define PVA_REG_HSP_SM5_ADDR 0x198000

/**
 * @brief HSP shared mailbox 6 base address
 *
 * @details Base register address for HSP shared mailbox 6.
 * Value: 0x1a0000
 */
#define PVA_REG_HSP_SM6_ADDR 0x1a0000

/**
 * @brief HSP shared mailbox 7 base address
 *
 * @details Base register address for HSP shared mailbox 7.
 * Value: 0x1a8000
 */
#define PVA_REG_HSP_SM7_ADDR 0x1a8000

/**
 * @brief HSP shared semaphore 0 state register address
 *
 * @details Register address for HSP shared semaphore 0 state.
 * Used for hardware synchronization between processors.
 * Value: 0x1b0000
 */
#define PVA_REG_HSP_SS0_STATE_ADDR 0x1b0000

/**
 * @brief HSP shared semaphore 0 set register address
 *
 * @details Register address for setting HSP shared semaphore 0.
 * Value: 0x1b0004
 */
#define PVA_REG_HSP_SS0_SET_ADDR 0x1b0004

/**
 * @brief HSP shared semaphore 0 clear register address
 *
 * @details Register address for clearing HSP shared semaphore 0.
 * Value: 0x1b0008
 */
#define PVA_REG_HSP_SS0_CLR_ADDR 0x1b0008

/**
 * @brief HSP shared semaphore 1 state register address
 *
 * @details Register address for HSP shared semaphore 1 state.
 * Value: 0x1c0000
 */
#define PVA_REG_HSP_SS1_STATE_ADDR 0x1c0000

/**
 * @brief HSP shared semaphore 1 set register address
 *
 * @details Register address for setting HSP shared semaphore 1.
 * Value: 0x1c0004
 */
#define PVA_REG_HSP_SS1_SET_ADDR 0x1c0004

/**
 * @brief HSP shared semaphore 1 clear register address
 *
 * @details Register address for clearing HSP shared semaphore 1.
 * Value: 0x1c0008
 */
#define PVA_REG_HSP_SS1_CLR_ADDR 0x1c0008

/**
 * @brief HSP shared semaphore 2 state register address
 *
 * @details Register address for HSP shared semaphore 2 state.
 * Value: 0x1d0000
 */
#define PVA_REG_HSP_SS2_STATE_ADDR 0x1d0000

/**
 * @brief HSP shared semaphore 2 set register address
 *
 * @details Register address for setting HSP shared semaphore 2.
 * Value: 0x1d0004
 */
#define PVA_REG_HSP_SS2_SET_ADDR 0x1d0004

/**
 * @brief HSP shared semaphore 2 clear register address
 *
 * @details Register address for clearing HSP shared semaphore 2.
 * Value: 0x1d0008
 */
#define PVA_REG_HSP_SS2_CLR_ADDR 0x1d0008

/**
 * @brief HSP shared semaphore 3 state register address
 *
 * @details Register address for HSP shared semaphore 3 state.
 * Value: 0x1e0000
 */
#define PVA_REG_HSP_SS3_STATE_ADDR 0x1e0000

/**
 * @brief HSP shared semaphore 3 set register address
 *
 * @details Register address for setting HSP shared semaphore 3.
 * Value: 0x1e0004
 */
#define PVA_REG_HSP_SS3_SET_ADDR 0x1e0004

/**
 * @brief HSP shared semaphore 3 clear register address
 *
 * @details Register address for clearing HSP shared semaphore 3.
 * Value: 0x1e0008
 */
#define PVA_REG_HSP_SS3_CLR_ADDR 0x1e0008

/**
 * @brief SEC error slice 0 mission error enable register address
 *
 * @details Register address for enabling mission-critical error detection
 * on SEC error slice 0. Mission errors are critical system errors.
 * Value: 0x20030
 */
#define PVA_REG_SEC_ERRSLICE0_MISSIONERR_ENABLE_ADDR 0x20030

/**
 * @brief SEC error slice 1 mission error enable register address
 *
 * @details Register address for enabling mission-critical error detection
 * on SEC error slice 1.
 * Value: 0x20060
 */
#define PVA_REG_SEC_ERRSLICE1_MISSIONERR_ENABLE_ADDR 0x20060

/**
 * @brief SEC error slice 2 mission error enable register address
 *
 * @details Register address for enabling mission-critical error detection
 * on SEC error slice 2.
 * Value: 0x20090
 */
#define PVA_REG_SEC_ERRSLICE2_MISSIONERR_ENABLE_ADDR 0x20090

/**
 * @brief SEC error slice 3 mission error enable register address
 *
 * @details Register address for enabling mission-critical error detection
 * on SEC error slice 3.
 * Value: 0x200c0
 */
#define PVA_REG_SEC_ERRSLICE3_MISSIONERR_ENABLE_ADDR 0x200c0

/**
 * @brief SEC error slice 0 latent error enable register address
 *
 * @details Register address for enabling latent error detection
 * on SEC error slice 0. Latent errors are non-critical but monitored.
 * Value: 0x20040
 */
#define PVA_REG_SEC_ERRSLICE0_LATENTERR_ENABLE_ADDR 0x20040

/**
 * @brief SEC error slice 1 latent error enable register address
 *
 * @details Register address for enabling latent error detection
 * on SEC error slice 1.
 * Value: 0x20070
 */
#define PVA_REG_SEC_ERRSLICE1_LATENTERR_ENABLE_ADDR 0x20070

/**
 * @brief SEC error slice 2 latent error enable register address
 *
 * @details Register address for enabling latent error detection
 * on SEC error slice 2.
 * Value: 0x200a0
 */
#define PVA_REG_SEC_ERRSLICE2_LATENTERR_ENABLE_ADDR 0x200a0

/**
 * @brief SEC error slice 3 latent error enable register address
 *
 * @details Register address for enabling latent error detection
 * on SEC error slice 3.
 * Value: 0x200d0
 */
#define PVA_REG_SEC_ERRSLICE3_LATENTERR_ENABLE_ADDR 0x200d0

/**
 * @brief SEC LIC INTR H1X field MSB bit position
 *
 * @details Most significant bit position for H1X interrupt field
 * in SEC LIC interrupt status register.
 * Value: 7
 */
#define PVA_REG_SEC_LIC_INTR_H1X_MSB 7

/**
 * @brief SEC LIC INTR H1X field LSB bit position
 *
 * @details Least significant bit position for H1X interrupt field
 * in SEC LIC interrupt status register.
 * Value: 5
 */
#define PVA_REG_SEC_LIC_INTR_H1X_LSB 5

/**
 * @brief SEC LIC INTR HSP field MSB bit position
 *
 * @details Most significant bit position for HSP interrupt field
 * in SEC LIC interrupt status register.
 * Value: 4
 */
#define PVA_REG_SEC_LIC_INTR_HSP_MSB 4

/**
 * @brief SEC LIC INTR HSP field LSB bit position
 *
 * @details Least significant bit position for HSP interrupt field
 * in SEC LIC interrupt status register.
 * Value: 1
 */
#define PVA_REG_SEC_LIC_INTR_HSP_LSB 1

/**
 * @brief SEC LIC INTR WDT field MSB bit position
 *
 * @details Most significant bit position for watchdog timer interrupt field
 * in SEC LIC interrupt status register.
 * Value: 0
 */
#define PVA_REG_SEC_LIC_INTR_WDT_MSB 0

/**
 * @brief SEC LIC INTR WDT field LSB bit position
 *
 * @details Least significant bit position for watchdog timer interrupt field
 * in SEC LIC interrupt status register.
 * Value: 0
 */
#define PVA_REG_SEC_LIC_INTR_WDT_LSB 0

/**
 * @brief CCQ status 2 interrupt overflow bit
 *
 * @details Bit mask for CCQ interrupt overflow condition in status register 2.
 * Indicates CCQ overflow condition requiring attention.
 */
#define PVA_REG_CCQ_STATUS2_INTR_OVERFLOW_BIT PVA_BIT(28)

/**
 * @brief CCQ status 2 interrupt status 8 bit
 *
 * @details Bit mask for CCQ interrupt status 8 in status register 2.
 */
#define PVA_REG_CCQ_STATUS2_INTR_STATUS8_BIT PVA_BIT(24)

/**
 * @brief CCQ status 2 interrupt status 7 bit
 *
 * @details Bit mask for CCQ interrupt status 7 in status register 2.
 */
#define PVA_REG_CCQ_STATUS2_INTR_STATUS7_BIT PVA_BIT(20)

/**
 * @brief CCQ status 2 all interrupt bits combined
 *
 * @details Combined bit mask for all CCQ interrupt conditions in status register 2.
 * Includes overflow, status8, and status7 interrupt bits.
 */
#define PVA_REG_CCQ_STATUS2_INTR_ALL_BITS                                      \
	(PVA_REG_CCQ_STATUS2_INTR_OVERFLOW_BIT |                               \
	 PVA_REG_CCQ_STATUS2_INTR_STATUS8_BIT |                                \
	 PVA_REG_CCQ_STATUS2_INTR_STATUS7_BIT)

/**
 * @brief CCQ status 2 number of entries field MSB bit position
 *
 * @details Most significant bit position for number of entries field
 * in CCQ status register 2.
 * Value: 4
 */
#define PVA_REG_CCQ_STATUS2_NUM_ENTRIES_MSB 4

/**
 * @brief CCQ status 2 number of entries field LSB bit position
 *
 * @details Least significant bit position for number of entries field
 * in CCQ status register 2.
 * Value: 0
 */
#define PVA_REG_CCQ_STATUS2_NUM_ENTRIES_LSB 0

/**
 * @brief CCQ register specification structure
 *
 * @details This structure defines the register layout for a single CCQ
 * (Command and Control Queue) interface including status registers and
 * the command FIFO register. Each CCQ provides an independent command
 * submission interface between software and firmware.
 */
struct pva_kmd_ccq_regspec {
	/**
	 * @brief Number of status registers for this CCQ
	 * Valid range: [1 .. PVA_CFG_CCQ_STATUS_COUNT]
	 */
	uint32_t status_count;

	/**
	 * @brief Array of CCQ status register addresses
	 */
	uint32_t status[PVA_CFG_CCQ_STATUS_COUNT];

	/**
	 * @brief CCQ FIFO register address for command submission
	 */
	uint32_t fifo;
};

/**
 * @brief Complete PVA register specification structure
 *
 * @details This structure contains the complete register specification for
 * a PVA device including all subsystem registers, security configuration,
 * Stream ID configuration, and CCQ interfaces. It provides a comprehensive
 * mapping of all hardware registers needed for PVA operation.
 */
struct pva_kmd_regspec {
	/**
	 * @brief SEC LIC interrupt enable register address
	 */
	uint32_t sec_lic_intr_enable;

	/**
	 * @brief SEC LIC interrupt status register address
	 */
	uint32_t sec_lic_intr_status;

	/**
	 * @brief CFG R5 user lower segment register address
	 */
	uint32_t cfg_r5user_lsegreg;

	/**
	 * @brief CFG R5 user upper segment register address
	 */
	uint32_t cfg_r5user_usegreg;

	/**
	 * @brief CFG privilege AR1 lower segment register address
	 */
	uint32_t cfg_priv_ar1_lsegreg;

	/**
	 * @brief CFG privilege AR1 upper segment register address
	 */
	uint32_t cfg_priv_ar1_usegreg;

	/**
	 * @brief CFG privilege AR2 lower segment register address
	 */
	uint32_t cfg_priv_ar2_lsegreg;

	/**
	 * @brief CFG privilege AR2 upper segment register address
	 */
	uint32_t cfg_priv_ar2_usegreg;

	/**
	 * @brief CFG privilege AR1 start address register
	 */
	uint32_t cfg_priv_ar1_start;

	/**
	 * @brief CFG privilege AR1 end address register
	 */
	uint32_t cfg_priv_ar1_end;

	/**
	 * @brief CFG privilege AR2 start address register
	 */
	uint32_t cfg_priv_ar2_start;

	/**
	 * @brief CFG privilege AR2 end address register
	 */
	uint32_t cfg_priv_ar2_end;

	/**
	 * @brief CFG user Stream ID base register address
	 */
	uint32_t cfg_user_sid_base;

	/**
	 * @brief CFG privilege Stream ID register address
	 */
	uint32_t cfg_priv_sid;

	/**
	 * @brief CFG VPS Stream ID register address
	 */
	uint32_t cfg_vps_sid;

	/**
	 * @brief CFG performance monitor register address
	 */
	uint32_t cfg_perf_mon;

	/**
	 * @brief CFG SCR privilege 0 register address
	 */
	uint32_t cfg_scr_priv_0;

	/**
	 * @brief Number of CCQ interfaces available
	 * Valid range: [1 .. PVA_MAX_NUM_CCQ]
	 */
	uint32_t ccq_count;

	/**
	 * @brief Array of VPU debug instruction register offsets
	 */
	uint32_t vpu_dbg_instr_reg_offset[PVA_NUM_ENGINES];

	/**
	 * @brief Array of CCQ register specifications
	 */
	struct pva_kmd_ccq_regspec ccq_regs[PVA_MAX_NUM_CCQ];
};

/**
 * @brief Enumeration of PVA register apertures
 *
 * @details This enumeration defines the different register apertures within
 * the PVA hardware that can be accessed for device control and monitoring.
 * Each aperture corresponds to a specific subsystem or functional block
 * within the PVA cluster.
 */
enum pva_kmd_reg_aperture {
	/** @brief Main PVA cluster aperture containing all subsystems */
	PVA_KMD_APERTURE_PVA_CLUSTER = 0,
	/** @brief SEC (Security) subsystem aperture */
	PVA_KMD_APERTURE_PVA_CLUSTER_SEC,
	/** @brief PROC (Processor) subsystem aperture */
	PVA_KMD_APERTURE_PVA_CLUSTER_PROC,
	/** @brief PM (Power Management) subsystem aperture */
	PVA_KMD_APERTURE_PVA_CLUSTER_PM,
	/** @brief HSP (Hardware Synchronization Primitives) aperture */
	PVA_KMD_APERTURE_PVA_CLUSTER_HSP,
	/** @brief EVP (Exception Vector Processor) aperture */
	PVA_KMD_APERTURE_PVA_CLUSTER_EVP,
	/** @brief CFG SID (Stream ID configuration) aperture */
	PVA_KMD_APERTURE_PVA_CLUSTER_CFG_SID,
	/** @brief CFG CCQ (Command and Control Queue configuration) aperture */
	PVA_KMD_APERTURE_PVA_CLUSTER_CFG_CCQ,
	/** @brief VPU debug aperture for debugging support */
	PVA_KMD_APERTURE_VPU_DEBUG,
	/** @brief Total number of apertures */
	PVA_KMD_APERTURE_COUNT,
};

#endif // PVA_KMD_REGS_H
