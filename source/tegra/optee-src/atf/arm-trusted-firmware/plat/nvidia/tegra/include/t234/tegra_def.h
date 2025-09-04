/*
 * Copyright (c) 2018-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __TEGRA_DEF_H__
#define __TEGRA_DEF_H__

#include <lib/utils_def.h>

/*******************************************************************************
 * Platform BL31 specific defines.
 ******************************************************************************/
#define BL31_SIZE			U(0x80000)

/* Platform power domain constants*/
#define PLAT_MAX_PWR_LVL		MPIDR_AFFLVL2
#define PLATFORM_CORE_COUNT		(PLATFORM_CLUSTER_COUNT * \
					 PLATFORM_MAX_CPUS_PER_CLUSTER)
#define PLAT_NUM_PWR_DOMAINS		(PLATFORM_CORE_COUNT + \
					 PLATFORM_CLUSTER_COUNT + U(1))

/* Tegra CORE and CLUSTER affinity values */
#define TEGRA_CORE_AFFINITY		MPIDR_AFF1_SHIFT
#define TEGRA_CLUSTER_AFFINITY		MPIDR_AFF2_SHIFT

/*******************************************************************************
 * Chip specific page table and MMU setup constants
 ******************************************************************************/
#define PLAT_PHY_ADDR_SPACE_SIZE	(ULL(1) << 40)
#define PLAT_VIRT_ADDR_SPACE_SIZE	(ULL(1) << 40)

/*******************************************************************************
 * These values are used by the PSCI implementation during the `CPU_SUSPEND`
 * and `SYSTEM_SUSPEND` calls as the `state-id` field in the 'power state'
 * parameter.
 ******************************************************************************/
#define PSTATE_ID_CORE_IDLE		U(6)
#define PSTATE_ID_CORE_POWERDN		U(7)
#define PSTATE_ID_SOC_POWERDN		U(2)

/*******************************************************************************
 * Platform power states (used by PSCI framework)
 *
 * - PLAT_MAX_RET_STATE should be less than lowest PSTATE_ID
 * - PLAT_MAX_OFF_STATE should be greater than the highest PSTATE_ID
 ******************************************************************************/
#define PLAT_MAX_RET_STATE		U(1)
#define PLAT_MAX_OFF_STATE		U(8)

/*******************************************************************************
 * Secure IRQ definitions
 ******************************************************************************/
#define TEGRA234_RAS_PPI_ERI		U(16)
#define TEGRA234_RAS_PPI_FHI		U(17)
#define TEGRA234_TOP_WDT_IRQ		U(49)
#define TEGRA234_AON_WDT_IRQ		U(50)
#define PLATFORM_FIQ_PPI_WDT		TEGRA234_TOP_WDT_IRQ

/*******************************************************************************
 * Clock identifier for the SE device
 ******************************************************************************/
#define TEGRA234_CLK_SEU1		U(336)
#define TEGRA_CLK_SE			TEGRA234_CLK_SEU1

/*******************************************************************************
 * Tegra Miscellanous register constants
 ******************************************************************************/
#define TEGRA_MISC_BASE			U(0x00100000)

#define HARDWARE_REVISION_OFFSET	U(0x4)
#define MISCREG_EMU_REVID		U(0x3160)
#define  BOARD_MASK_BITS		U(0xFF)
#define  BOARD_SHIFT_BITS		U(24)

/*******************************************************************************
 * Tegra General Purpose Centralised DMA constants
 ******************************************************************************/
#define TEGRA_GPCDMA_BASE		U(0x02600000)

/*******************************************************************************
 * Tegra Memory Controller constants
 ******************************************************************************/
#define TEGRA_MC_STREAMID_BASE		U(0x02C00000)
#define TEGRA_MC_STREAMID_LIMIT		U(0x02C0FFFF)
#define TEGRA_MC_BASE			U(0x02C10000)
#define TEGRA_MC_LIMIT			U(0x02C1FFFF)

/* General Security Carveout register macros */
#define MC_GSC_CONFIG_REGS_SIZE		U(0x40)
#define MC_GSC_LOCK_CFG_SETTINGS_BIT	(U(1) << 1)
#define MC_GSC_ENABLE_TZ_LOCK_BIT	(U(1) << 0)
#define MC_GSC_SIZE_RANGE_4KB_SHIFT	U(27)
#define MC_GSC_BASE_LO_SHIFT		U(12)
#define MC_GSC_BASE_LO_MASK		U(0xFFFFF)
#define MC_GSC_BASE_HI_SHIFT		U(0)
#define MC_GSC_BASE_HI_MASK		U(3)
#define MC_GSC_ENABLE_CPU_SECURE_BIT    (U(1) << 31)

/* TZDRAM carveout configuration registers */
#define MC_SECURITY_CFG0_0		U(0x70)
#define MC_SECURITY_CFG1_0		U(0x74)
#define MC_SECURITY_CFG3_0		U(0x9BC)

#define MC_SECURITY_BOM_MASK		(U(0xFFF) << 20)
#define MC_SECURITY_SIZE_MB_MASK	(U(0x1FFF) << 0)
#define MC_SECURITY_BOM_HI_MASK		(U(0x3) << 0)

#define MC_SECURITY_CFG_REG_CTRL_0 U(0x154)
#define  SECURITY_CFG_WRITE_ACCESS_BIT     (U(0x1) << 0)
#define  SECURITY_CFG_WRITE_ACCESS_ENABLE  U(0x0)
#define  SECURITY_CFG_WRITE_ACCESS_DISABLE U(0x1)

/* Video Memory carveout configuration registers */
#define MC_VIDEO_PROTECT_BASE_HI	U(0x978)
#define MC_VIDEO_PROTECT_BASE_LO	U(0x648)
#define MC_VIDEO_PROTECT_SIZE_MB	U(0x64c)
#define MC_VIDEO_PROTECT_REG_CTRL	U(0x650)
#define MC_VIDEO_PROTECT_WRITE_ACCESS_ENABLED	U(3)

/*
 * Carveout (MC_SECURITY_CARVEOUT24) registers used to clear the
 * non-overlapping Video memory region
 */
#define MC_VIDEO_PROTECT_CLEAR_CFG	U(0x25A0)
#define MC_VIDEO_PROTECT_CLEAR_BASE_LO	U(0x25A4)
#define MC_VIDEO_PROTECT_CLEAR_BASE_HI	U(0x25A8)
#define MC_VIDEO_PROTECT_CLEAR_SIZE	U(0x25AC)
#define MC_VIDEO_PROTECT_CLEAR_ACCESS_CFG0	U(0x25B0)

/* TZRAM carveout (MC_SECURITY_CARVEOUT11) configuration registers */
#define MC_TZRAM_CARVEOUT_CFG		U(0x2190)
#define MC_TZRAM_BASE_LO		U(0x2194)
#define MC_TZRAM_BASE_HI		U(0x2198)
#define MC_TZRAM_SIZE			U(0x219C)
#define MC_TZRAM_CLIENT_ACCESS0_CFG0	U(0x21A0)
#define MC_TZRAM_CLIENT_ACCESS1_CFG0	U(0x21A4)
#define  TZRAM_ALLOW_MPCORER		(U(1) << 7)
#define  TZRAM_ALLOW_MPCOREW		(U(1) << 25)

/* CCPLEX-BPMP IPC carveout (MC_SECURITY_CARVEOUT18) configuration registers */
#define MC_CCPLEX_BPMP_IPC_BASE_LO	U(0x23c4)
#define MC_CCPLEX_BPMP_IPC_BASE_HI	U(0x23c8)

/* Memory Controller Reset Control registers */
#define  MC_CLIENT_HOTRESET_CTRL1_DLAA_FLUSH_ENB	(U(1) << 28)
#define  MC_CLIENT_HOTRESET_CTRL1_DLA1A_FLUSH_ENB	(U(1) << 29)
#define  MC_CLIENT_HOTRESET_CTRL1_PVA0A_FLUSH_ENB	(U(1) << 30)
#define  MC_CLIENT_HOTRESET_CTRL1_PVA1A_FLUSH_ENB	(U(1) << 31)

/*******************************************************************************
 * Tegra UART Controller constants
 ******************************************************************************/
#define TEGRA_UARTA_BASE		U(0x03100000)
#define TEGRA_UARTB_BASE		U(0x03110000)
#define TEGRA_UARTC_BASE		U(0x0C280000)
#define TEGRA_UARTD_BASE		U(0x03130000)
#define TEGRA_UARTE_BASE		U(0x03140000)
#define TEGRA_UARTF_BASE		U(0x03150000)

/*******************************************************************************
 * Tegra Fuse Controller related constants
 ******************************************************************************/
#define TEGRA_FUSE_BASE			U(0x03810000)
#define  SECURITY_MODE			U(0x1A0)
#define  OPT_SUBREVISION		U(0x248)
#define  SUBREVISION_MASK		U(0xF)

/*******************************************************************************
 * Security Engine related constants
 ******************************************************************************/
#define TEGRA_RNG1_BASE			U(0x03B70000)
#define NV_NVRNG_R_CTRL			U(0x8C)
#define SW_ENGINE_ENABLED		BIT_32(2)

#define TEGRA_SE0_BASE			U(0x03B50000)
#define TEGRA_PKA1_BASE			U(0x03AD0000)

/*******************************************************************************
 * Tegra HSP doorbell #0 constants
 ******************************************************************************/
#define TEGRA_HSP_DBELL_BASE		U(0x03C90000)
#define  HSP_DBELL_1_ENABLE		U(0x104)
#define  HSP_DBELL_3_TRIGGER		U(0x300)
#define  HSP_DBELL_3_ENABLE		U(0x304)

/*******************************************************************************
 * Tegra hardware synchronization primitives for the SPE engine
 ******************************************************************************/
#define TEGRA_AON_HSP_SM_6_7_BASE	U(0x0c190000)
#define TEGRA_CONSOLE_SPE_BASE		(TEGRA_AON_HSP_SM_6_7_BASE + U(0x8000))

/*******************************************************************************
 * Tegra micro-seconds timer constants
 ******************************************************************************/
#define TEGRA_TMRUS_BASE		U(0x0C6B0000)
#define TEGRA_TMRUS_SIZE		U(0x10000)

/*******************************************************************************
 * Tegra Power Mgmt Controller constants
 ******************************************************************************/
#define TEGRA_PMC_BASE			U(0x0C360000)
#define TEGRA_PMC_SIZE			U(0x1000)

/*******************************************************************************
 * Tegra scratch registers constants
 ******************************************************************************/
#define TEGRA_SCRATCH_BASE		U(0x0C390000)
#define  SECURE_SCRATCH_RSV28_LO	U(0x144)
#define  SECURE_SCRATCH_RSV28_HI	U(0x148)
#define  SECURE_SCRATCH_RSV33_LO	U(0x16C)
#define  SECURE_SCRATCH_RSV33_HI	U(0x170)
#define  SECURE_SCRATCH_RSV34_LO	U(0x174)
#define  SECURE_SCRATCH_RSV34_HI	U(0x178)
#define  SECURE_SCRATCH_RSV41_LO	U(0x1AC)
#define  SECURE_SCRATCH_RSV97		U(0x36C)
#define  SECURE_SCRATCH_RSV99_LO	U(0x37C)
#define  SECURE_SCRATCH_RSV99_HI	U(0x380)

#define SCRATCH_BL31_PARAMS_HI_ADDR	SECURE_SCRATCH_RSV33_HI
#define  SCRATCH_BL31_PARAMS_HI_ADDR_MASK  U(0xFFFF)
#define  SCRATCH_BL31_PARAMS_HI_ADDR_SHIFT U(0)
#define SCRATCH_BL31_PARAMS_LO_ADDR	SECURE_SCRATCH_RSV33_LO
#define SCRATCH_BL31_PLAT_PARAMS_HI_ADDR SECURE_SCRATCH_RSV34_HI
#define  SCRATCH_BL31_PLAT_PARAMS_HI_ADDR_MASK  U(0xFFFF)
#define  SCRATCH_BL31_PLAT_PARAMS_HI_ADDR_SHIFT U(0)
#define SCRATCH_BL31_PLAT_PARAMS_LO_ADDR SECURE_SCRATCH_RSV34_LO
#define SCRATCH_IST_CLK_ENABLE		SECURE_SCRATCH_RSV41_LO
#define SCRATCH_MC_TABLE_ADDR_LO	SECURE_SCRATCH_RSV99_LO
#define SCRATCH_MC_TABLE_ADDR_HI	SECURE_SCRATCH_RSV99_HI
#define SCRATCH_RESET_VECTOR_LO		SECURE_SCRATCH_RSV28_LO
#define SCRATCH_RESET_VECTOR_HI		SECURE_SCRATCH_RSV28_HI

/*******************************************************************************
 * Tegra AON Fabric constants
 ******************************************************************************/
#define TEGRA_AON_FABRIC_FIREWALL_BASE	U(0x0C630000)
#define  AON_FIREWALL_ARF_0_WRITE_CTL	U(0x8)
#define   WRITE_CTL_MSTRID_TZ_BIT	BIT(0)
#define   WRITE_CTL_MSTRID_BPMP_FW_BIT	BIT(3)
#define  AON_FIREWALL_ARF_0_CTL		U(0x10)
#define   ARF_CTL_OWNER_BPMP_FW		U(3)

/*******************************************************************************
 * Tegra Memory Mapped Control Register Access Bus constants
 ******************************************************************************/
#define TEGRA_MMCRAB_BASE		U(0x0E000000)

/*******************************************************************************
 * MCE apertures used by the ARI interface
 *
 * Aperture 0  - cluster0 Cpu0
 * Aperture 1  - cluster0 Cpu1
 * Aperture 2  - cluster0 Cpu2
 * Aperture 3  - cluster0 Cpu3
 * Aperture 4  - cluster1 Cpu0
 * Aperture 5  - cluster1 Cpu1
 * Aperture 6  - cluster1 Cpu2
 * Aperture 7  - cluster1 Cpu3
 * Aperture 8  - cluster2 Cpu0
 * Aperture 9  - cluster2 Cpu1
 * Aperture 10 - cluster2 Cpu2
 * Aperture 11 - cluster2 Cpu3
 ******************************************************************************/
#define MCE_ARI_APERTURE_0_OFFSET	U(0x200000)
#define MCE_ARI_APERTURE_1_OFFSET	U(0x210000)
#define MCE_ARI_APERTURE_2_OFFSET	U(0x220000)
#define MCE_ARI_APERTURE_3_OFFSET	U(0x230000)
#define MCE_ARI_APERTURE_4_OFFSET	U(0x240000)
#define MCE_ARI_APERTURE_5_OFFSET	U(0x250000)
#define MCE_ARI_APERTURE_6_OFFSET	U(0x260000)
#define MCE_ARI_APERTURE_7_OFFSET	U(0x270000)
#define MCE_ARI_APERTURE_8_OFFSET	U(0x280000)
#define MCE_ARI_APERTURE_9_OFFSET	U(0x290000)
#define MCE_ARI_APERTURE_10_OFFSET	U(0x2A0000)
#define MCE_ARI_APERTURE_11_OFFSET	U(0x2B0000)

/*******************************************************************************
 * GICv3 constants
 ******************************************************************************/
#define TEGRA_GICD_BASE			U(0x0F400000)
#define TEGRA_GICR_BASE			U(0x0F440000)
#define TEGRA_GICFMU_BASE		U(0x0F7F0000)

/*******************************************************************************
 * Tegra SMMU Controller constants
 ******************************************************************************/
#define TEGRA_SMMU0_BASE		U(0x12000000)
#define TEGRA_SMMU1_BASE		U(0x11000000)
#define TEGRA_SMMU2_BASE		U(0x10000000)
#define TEGRA_SMMU3_BASE		U(0x07000000)
#define TEGRA_SMMU4_BASE		U(0x08000000)

/*******************************************************************************
 * Tegra PSC mailbox
 ******************************************************************************/
#define TEGRA_PSC_MBOX_TZ_BASE		U(0x0E830000)
#define PSC_MBOX_TZ_EXT_CTRL		U(0x0004)
#define PSC_MBOX_TZ_PSC_CTRL		U(0x0008)
#define PSC_MBOX_TZ_IN			U(0x0800)
#define PSC_MBOX_TZ_OUT_0		U(0x1000)
#define PSC_MBOX_TZ_OUT_1		U(0x1004)
#define PSC_MBOX_OUT_DONE		U(0x10)
#define PSC_MBOX_IN_VALID		U(0x1)
#define PSC_MBOX_IN_DONE		U(0x10)
#define PSC_MBOX_OUT_VALID		U(0x1)
#define PSC_MBOX_TZ_OPCODE_ENTER_SC7	U(0x53433745)
#define PSC_MBOX_SC7_ENTRY_PASS		U(0x0)
#define PSC_MBOX_ACK_TIMEOUT_MAX_USEC	U(1000000)


/*******************************************************************************
 * RAS records
 ******************************************************************************/
#define TEGRA_CCPMU_RAS_BASE		U(0x0E001000)
#define TEGRA_RAS_CL0_CORE0_BASE	U(0x0E030000)
#define TEGRA_RAS_CL1_CORE0_BASE	U(0x0E040000)
#define TEGRA_RAS_CL2_CORE0_BASE	U(0x0E050000)

/*******************************************************************************
 * Tegra TZRAM constants
 ******************************************************************************/
#define TEGRA_TZRAM_BASE		U(0x50000000)
#define TEGRA_TZRAM_SIZE		U(0x40000)

/*******************************************************************************
 * Tegra CCPLEX-BPMP IPC constants
 ******************************************************************************/
#define TEGRA_BPMP_IPC_CH_MAP_SIZE	U(0x1000) /* 4KB */

/*******************************************************************************
 * Tegra Clock and Reset Controller constants
 ******************************************************************************/
#define TEGRA_CAR_RESET_BASE		U(0x20000000)
#define TEGRA_GPU_RESET_REG_OFFSET	U(0x0)
#define TEGRA_GPU_RESET_GPU_SET_OFFSET  U(0x4)
#define  GPU_RESET_BIT			(U(1) << 0)
#define  GPU_SET_BIT                    (U(1) << 0)
#define TEGRA_GPCDMA_RST_SET_REG_OFFSET	U(0x6A0004)
#define TEGRA_GPCDMA_RST_CLR_REG_OFFSET	U(0x6A0008)

/*******************************************************************************
 * Tegra DRAM memory base address
 ******************************************************************************/
#define TEGRA_DRAM_BASE			ULL(0x80000000)
#define TEGRA_DRAM_END			ULL(0x207FFFFFFF)

/*******************************************************************************
 * XUSB STREAMIDs
 ******************************************************************************/
#define TEGRA_SID_XUSB_HOST			U(0x1b)
#define TEGRA_SID_XUSB_DEV			U(0x1c)
#define TEGRA_SID_XUSB_VF0			U(0x5d)
#define TEGRA_SID_XUSB_VF1			U(0x5e)
#define TEGRA_SID_XUSB_VF2			U(0x5f)
#define TEGRA_SID_XUSB_VF3			U(0x60)

/*******************************************************************************
 * Tegra SYSRAM base and limit.
 ******************************************************************************/
#define TEGRA_SYSRAM_BASE		U(0x40000000)
#define TEGRA_SYSRAM_SIZE		U(0x10000000)

/*******************************************************************************
 * Tegra Debug APB base address and ETM related constants
 ******************************************************************************/
#define TEGRA_DBGAPB_BASE			ULL(0x209F0000)
#define TEGRA_TRCPRGCTLR_OFFSET			U(0x4)
#define TRCPRGCTLR_EN_BIT			BIT_32(0)

#endif /* __TEGRA_DEF_H__ */
