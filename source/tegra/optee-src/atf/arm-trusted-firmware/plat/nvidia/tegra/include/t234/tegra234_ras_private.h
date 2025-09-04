/*
 * Copyright (c) 2020-2022, NVIDIA Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TEGRA234_RAS_PRIVATE
#define TEGRA234_RAS_PRIVATE

#include <stdint.h>

/* RAS error record base addresses */
#define TEGRA234_RAS_CCPMU_BASE			U(0x0E001000)
#define TEGRA234_RAS_CMU_CLOCKS_BASE		U(0x0E003000)
#define TEGRA234_RAS_IH_BASE			U(0x0E006000)
#define TEGRA234_RAS_IST_CLUSTER0_BASE		U(0x0E009000)
#define TEGRA234_RAS_IST_CLUSTER1_BASE		U(0x0E00A000)
#define TEGRA234_RAS_IST_CLUSTER2_BASE		U(0x0E00B000)
#define TEGRA234_RAS_IOB_BASE			U(0x0E010000)
#define TEGRA234_RAS_SNOC_BASE			U(0x0E011000)
#define TEGRA234_RAS_SCC_SLICE0_BASE		U(0x0E012000)
#define TEGRA234_RAS_SCC_SLICE1_BASE		U(0x0E013000)
#define TEGRA234_RAS_SCC_SLICE2_BASE		U(0x0E014000)
#define TEGRA234_RAS_SCC_SLICE3_BASE		U(0x0E015000)
#define TEGRA234_RAS_SCC_SLICE4_BASE		U(0x0E016000)
#define TEGRA234_RAS_SCC_SLICE5_BASE		U(0x0E017000)
#define TEGRA234_RAS_SCC_SLICE6_BASE		U(0x0E018000)
#define TEGRA234_RAS_SCC_SLICE7_BASE		U(0x0E019000)
#define TEGRA234_RAS_ACI_CLUSTER0_BASE		U(0x0E01A000)
#define TEGRA234_RAS_ACI_CLUSTER1_BASE		U(0x0E01B000)
#define TEGRA234_RAS_ACI_CLUSTER2_BASE		U(0x0E01C000)
#define TEGRA234_RAS_CL0_CORE0_BASE		U(0x0E030000)
#define TEGRA234_RAS_CL0_CORE1_BASE		U(0x0E031000)
#define TEGRA234_RAS_CL0_CORE2_BASE		U(0x0E032000)
#define TEGRA234_RAS_CL0_CORE3_BASE		U(0x0E033000)
#define TEGRA234_RAS_CL0_CORE_LS_PAIR1_BASE	U(0x0E034000)
#define TEGRA234_RAS_CL0_CORE_LS_PAIR2_BASE	U(0x0E035000)
#define TEGRA234_RAS_CL0_DSU_LS_PRIMARY_BASE	U(0x0E036000)
#define TEGRA234_RAS_CL0_DSU_LS_SECONDARY_BASE	U(0x0E037000)
#define TEGRA234_RAS_CL0_DSU_IFP_BASE		U(0x0E038000)
#define TEGRA234_RAS_CL1_CORE0_BASE		U(0x0E040000)
#define TEGRA234_RAS_CL1_CORE1_BASE		U(0x0E041000)
#define TEGRA234_RAS_CL1_CORE2_BASE		U(0x0E042000)
#define TEGRA234_RAS_CL1_CORE3_BASE		U(0x0E043000)
#define TEGRA234_RAS_CL1_CORE_LS_PAIR1_BASE	U(0x0E044000)
#define TEGRA234_RAS_CL1_CORE_LS_PAIR2_BASE	U(0x0E045000)
#define TEGRA234_RAS_CL1_DSU_LS_PRIMARY_BASE	U(0x0E046000)
#define TEGRA234_RAS_CL1_DSU_LS_SECONDARY_BASE	U(0x0E047000)
#define TEGRA234_RAS_CL1_DSU_IFP_BASE		U(0x0E048000)
#define TEGRA234_RAS_CL2_CORE0_BASE		U(0x0E050000)
#define TEGRA234_RAS_CL2_CORE1_BASE		U(0x0E051000)
#define TEGRA234_RAS_CL2_CORE2_BASE		U(0x0E052000)
#define TEGRA234_RAS_CL2_CORE3_BASE		U(0x0E053000)
#define TEGRA234_RAS_CL2_CORE_LS_PAIR1_BASE	U(0x0E054000)
#define TEGRA234_RAS_CL2_CORE_LS_PAIR2_BASE	U(0x0E055000)
#define TEGRA234_RAS_CL2_DSU_LS_PRIMARY_BASE	U(0x0E056000)
#define TEGRA234_RAS_CL2_DSU_LS_SECONDARY_BASE	U(0x0E057000)
#define TEGRA234_RAS_CL2_DSU_IFP_BASE		U(0x0E058000)

/* IH RAS status masks */
#define TEGRA234_IH_STATUS_MASK_CMU		(BIT(1) | BIT(0))
#define TEGRA234_IH_STATUS_MASK_SCF		(BIT(3) | BIT(2))
#define TEGRA234_IH_STATUS_MASK_CL0		(BIT(4) | BIT(8) | BIT(9) | \
						 BIT(16) | BIT(17) | \
						 (ULL(0xFF) << 24))
#define TEGRA234_IH_STATUS_MASK_CL1		(BIT(5) | BIT(10) | BIT(11) | \
						 BIT(18) | BIT(19) | \
						 (ULL(0xFF) << 32))
#define TEGRA234_IH_STATUS_MASK_CL2		(BIT(6) | BIT(12) | BIT(13) | \
						 BIT(20) | BIT(21) | \
						 (ULL(0xFF) << 40))

/* Implementation defined RAS error and corresponding error message */
struct ras_error {
	const char *error_msg;
	/* IERR(bits[15:8]) from ERR<n>STATUS */
	uint8_t error_code;
};

/* RAS error node-specific auxiliary data */
struct ras_aux_data {
	/* name of the current RAS node */
	const char *name;
	/*
	 * pointer to null-terminated ras_error array to convert error
	 * code to msg
	 */
	const struct ras_error *error_records;
	/*
	 * Number of error records
	 */
	unsigned int num_err_records;
	/*
	 * function to return value which needs to be programmed into
	 * ERR<n>CTLR to enable uncorrectable RAS errors for current
	 * node
	 */
	uint64_t (*get_ue_mask)(void);
	/*
	 * function to return value which needs to be programmed into
	 * ERR<n>CTLR to enable corrected RAS errors for the current
	 * node
	*/
	uint64_t (*get_ce_mask)(void);
};

/* CCPMU Uncorrectable RAS ERROR */
#define CCPMU_UNCORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(CCPMU, 42, 0x0A, "IL1 Parity Error")					\
	X(CCPMU, 41, 0x09, "Invalid uOp Error")					\
	X(CCPMU, 40, 0x08, "MCE Request Error")					\
	X(CCPMU, 39, 0x07, "MCE Request Timeout Error")				\
	X(CCPMU, 38, 0x06, "CRAB Master Error")					\
	X(CCPMU, 37, 0x05, "uCode Error")					\
	X(CCPMU, 36, 0x04, "ARI Request NS Error")				\
	X(CCPMU, 35, 0x03, "CCPMU unit Lockable CREG Error")			\
	X(CCPMU, 34, 0x02, "CCPMU RAS Lockable CREG Error")			\
	X(CCPMU, 33, 0x01, "DC Lockable CREG Error")				\
	X(CCPMU, 32, 0x00, "CMULA lockable CREG Error")

/* Empty CCPMU Corrected RAS ERROR */
#define CCPMU_CORR_RAS_ERROR_LIST(X)

/* CMU Clocks Uncorrectable RAS ERROR */
#define CMU_CLOCKS_UNCORR_RAS_ERROR_LIST(X)					\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(CMU_CLOCKS, 41, 0x09, "Cluster2 FMON Error")				\
	X(CMU_CLOCKS, 40, 0x08, "Cluster2 Core FMON Error")			\
	X(CMU_CLOCKS, 39, 0x07, "Cluster1 FMON Error")				\
	X(CMU_CLOCKS, 38, 0x06, "Cluster1 Core FMON Error")			\
	X(CMU_CLOCKS, 37, 0x05, "Cluster0 FMON Error")				\
	X(CMU_CLOCKS, 36, 0x04, "Cluster0 Core FMON Error")			\
	X(CMU_CLOCKS, 35, 0x03, "LUT0 Parity Error")				\
	X(CMU_CLOCKS, 34, 0x02, "LUT1 Parity Error")				\
	X(CMU_CLOCKS, 33, 0x01, "ADC0 VMON Error")				\
	X(CMU_CLOCKS, 32, 0x00, "Lock Error")

/* Empty CMU Clocks Corrected RAS ERROR */
#define CMU_CLOCKS_CORR_RAS_ERROR_LIST(X)

/* IH Uncorrectable RAS ERROR */
#define IH_UNCORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(IH, 35, 0x03, "RAS MUX Error")					\
	X(IH, 34, 0x02, "GIC FMU Error")					\
	X(IH, 33, 0x01, "Unit lockable CREG Error")				\
	X(IH, 32, 0x00, "RAS lockable CREG Error")

/* Empty IH Corrected RAS ERROR */
#define IH_CORR_RAS_ERROR_LIST(X)

/* IST Uncorrectable RAS ERROR */
#define IST_UNCORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(IST, 50, 0x12, "Lock Error")						\
	X(IST, 49, 0x11, "MCE Error")						\
	X(IST, 48, 0x10, "MISR Mismatch Error")					\
	X(IST, 47, 0x0F, "Security Error")					\
	X(IST, 46, 0x0E, "RAM Data Error")					\
	X(IST, 45, 0x0D, "JTAG Controller Error")				\
	X(IST, 44, 0x0C, "Timeout Error")					\
	X(IST, 43, 0x0B, "LBIST Controller Error")				\
	X(IST, 42, 0x0A, "DMA Error")						\
	X(IST, 41, 0x09, "ECC Security Error")

/* IST Corrected RAS ERROR */
#define IST_CORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(IST, 40, 0x08, "MCE Error")						\
	X(IST, 39, 0x07, "MISR Mismatch Error")					\
	X(IST, 38, 0x06, "Security Error")					\
	X(IST, 37, 0x05, "RAM Data Error")					\
	X(IST, 36, 0x04, "JTAG Controller Error")				\
	X(IST, 35, 0x03, "Timeout Error")					\
	X(IST, 34, 0x02, "LBIST Controller Error")				\
	X(IST, 33, 0x01, "DMA Error")						\
	X(IST, 32, 0x00, "ECC Security Error")

/* IOB Uncorrectable RAS ERROR */
#define IOB_UNCORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(IOB, 47, 0x0F, "APB Response Parity Error")				\
	X(IOB, 46, 0x0E, "IHI Write Data Ready Error")				\
	X(IOB, 45, 0x0D, "IHI Write Address Ready Error")			\
	X(IOB, 44, 0x0C, "IHI Read Address Ready Error")			\
	X(IOB, 43, 0x0B, "IHI Write Response Parity Error")			\
	X(IOB, 42, 0x0A, "IHI Read Data Parity Error")				\
	X(IOB, 41, 0x09, "Request Parity Error")				\
	X(IOB, 40, 0x08, "PutData Parity Error")				\
	X(IOB, 39, 0x07, "PutData Uncorrectable ECC Error")			\
	X(IOB, 38, 0x06, "CBB Interface Error")					\
	X(IOB, 37, 0x05, "ARM MMCRAB Access Error")				\
	X(IOB, 36, 0x04, "APB Interface Error")					\
	X(IOB, 35, 0x03, "IHI (GIC ACE-Lite) Interface Error")			\
	X(IOB, 34, 0x02, "Trickbox Interface Error")				\
	X(IOB, 33, 0x01, "Lock Error")

/* IOB Corrected RAS ERROR */
#define IOB_CORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(IOB, 32, 0x00, "Putdata Corrected ECC Error")

/* SNOC Uncorrectable RAS ERROR */
#define SNOC_UNCORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(SNOC, 47, 0x0F, "MCF MSI Error")					\
	X(SNOC, 46, 0x0E, "MISC IST CPE Access Error")				\
	X(SNOC, 45, 0x0D, "MISC MMIO Access Error")				\
	X(SNOC, 44, 0x0C, "MISC MMP Access Error")				\
	X(SNOC, 43, 0x0B, "MISC Response Parity Error")				\
	X(SNOC, 42, 0x0A, "MISC FillData Parity Error")				\
	X(SNOC, 41, 0x09, "MISC FillData Uncorrectable ECC Error")		\
	X(SNOC, 40, 0x08, "DVMU Parity Error")					\
	X(SNOC, 39, 0x07, "DVMU Timeout Error")					\
	X(SNOC, 38, 0x06, "CPE Request Error")					\
	X(SNOC, 37, 0x05, "CPE Response Error")					\
	X(SNOC, 36, 0x04, "CPE Timeout Error")					\
	X(SNOC, 35, 0x03, "Carveout Uncorrectable Error")			\
	X(SNOC, 34, 0x02, "Lockable CREG Error")

/* SNOC Corrected RAS ERROR */
#define SNOC_CORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(SNOC, 33, 0x01, "MISC FillData Corrected ECC Error")			\
	X(SNOC, 32, 0x00, "Carveout Corrected Error")

/* SCC Uncorrectable RAS ERROR */
#define SCC_UNCORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(SCC, 51, 0x13, "SNOC Interface Parity Error")				\
	X(SCC, 50, 0x12, "MCF Interface Parity Error")				\
	X(SCC, 49, 0x11, "L3 Tag Parity Error")					\
	X(SCC, 48, 0x10, "L2 Dir Parity Error")					\
	X(SCC, 47, 0x0F, "URT Parity Error")					\
	X(SCC, 46, 0x0E, "SWAT Parity Error")					\
	X(SCC, 45, 0x0D, "L3 Data Parity Error")				\
	X(SCC, 44, 0x0C, "URT FC Error")					\
	X(SCC, 43, 0x0B, "SWAT FC Error")					\
	X(SCC, 42, 0x0A, "Uncorrected ECC Error")				\
	X(SCC, 41, 0x09, "Address Range Error")					\
	X(SCC, 40, 0x08, "Unsupported Request Error")				\
	X(SCC, 39, 0x07, "Multiple Hit CAM Error")				\
	X(SCC, 38, 0x06, "Multiple Hit Tag Error")				\
	X(SCC, 37, 0x05, "Protocol Error")					\
	X(SCC, 36, 0x04, "Timeout Error")					\
	X(SCC, 35, 0x03, "Destination Error")					\
	X(SCC, 34, 0x02, "URT FC Timeout Error")				\
	X(SCC, 33, 0x01, "SWAT FC Timeout Error")				\
	X(SCC, 32, 0x00, "Lock Error")

/* SCC Corrected RAS ERROR */
#define SCC_CORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(SCC, 52, 0x14, "Corrected ECC Error")

/* ACI Uncorrectable RAS ERROR */
#define ACI_UNCORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(ACI, 49, 0x11, "Uncorrectable ECC Error")				\
	X(ACI, 48, 0x10, "MST ADB error")					\
	X(ACI, 47, 0x0F, "AXI FuSa Error")					\
	X(ACI, 46, 0x0E, "AXI Parity Error")					\
	X(ACI, 45, 0x0D, "SNOC Write Error")					\
	X(ACI, 44, 0x0C, "ACE AW Decode Error")					\
	X(ACI, 43, 0x0B, "Unexpected WU Error")					\
	X(ACI, 42, 0x0A, "Snoop Error")						\
	X(ACI, 41, 0x09, "FillWrite Error")					\
	X(ACI, 40, 0x08, "DVM Error")						\
	X(ACI, 39, 0x07, "ACE Parity Error")					\
	X(ACI, 38, 0x06, "Internal ACI parity Error")				\
	X(ACI, 37, 0x05, "AR decode Error")					\
	X(ACI, 36, 0x04, "SNOC Protocol Error")					\
	X(ACI, 35, 0x03, "Internal ACI FIFO Error")				\
	X(ACI, 34, 0x02, "SNOC Parity Error")					\
	X(ACI, 33, 0x01, "Lockable CREG Error")

/* ACI Corrected RAS ERROR */
#define ACI_CORR_RAS_ERROR_LIST(X)						\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(ACI, 32, 0x00, "SNOC Corrected ECC Error")

/* CORE DCLS Uncorrectable RAS ERROR */
#define CORE_DCLS_UNCORR_RAS_ERROR_LIST(X)					\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(CORE_DCLS, 48, 0x10, "Redundant Clock, Power and Reset logic error")	\
	X(CORE_DCLS, 47, 0x0F, "Redundant System Register Timer logic error")	\
	X(CORE_DCLS, 46, 0x0E, "Redundant EVENT logic error")			\
	X(CORE_DCLS, 45, 0x0D, "Redundant Timestamp logic error")		\
	X(CORE_DCLS, 44, 0x0C, "Redundant APB group error")			\
	X(CORE_DCLS, 42, 0x0A, "Redundant GIC group error")			\
	X(CORE_DCLS, 41, 0x09, "Redundant CHI logic error")			\
	X(CORE_DCLS, 40, 0x08, "Primary Clock, Power and Reset logic error")	\
	X(CORE_DCLS, 39, 0x07, "Primary System Register Timer logic error")	\
	X(CORE_DCLS, 38, 0x06, "Primary Event logic error")			\
	X(CORE_DCLS, 37, 0x05, "Primary Timestamp logic error")			\
	X(CORE_DCLS, 36, 0x04, "Primary APB group error")			\
	X(CORE_DCLS, 35, 0x03, "Primary ATB group error")			\
	X(CORE_DCLS, 34, 0x02, "Primary GIC group error")			\
	X(CORE_DCLS, 33, 0x01, "Primary CHI logic error")			\
	X(CORE_DCLS, 32, 0x00, "Lock error")

/* Empty CORE DCLS Corrected RAS ERROR */
#define CORE_DCLS_CORR_RAS_ERROR_LIST(X)

/* DSU DCLS Uncorrectable RAS ERROR */
#define DSU_DCLS_UNCORR_RAS_ERROR_LIST(X)					\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(DSU_DCLS, 57, 0x19, "Illegal mode (CEMODE=0) error")			\
	X(DSU_DCLS, 56, 0x18, "Core3 HM DCLS error")				\
	X(DSU_DCLS, 55, 0x17, "Core2 HM DCLS error")				\
	X(DSU_DCLS, 54, 0x16, "Core1 HM DCLS error")				\
	X(DSU_DCLS, 53, 0x15, "Core0 HM DCLS error")				\
	X(DSU_DCLS, 52, 0x14, "SB DBG group in DCLSFAULT vector")		\
	X(DSU_DCLS, 51, 0x13, "SB GIC group in DCLSFAULT vector")		\
	X(DSU_DCLS, 50, 0x12, "SB ATB group in DCLSFAULT vector")		\
	X(DSU_DCLS, 49, 0x11, "SB EVENT group in DCLSFAULT vector")		\
	X(DSU_DCLS, 48, 0x10, "SB CPM PERIPHCLK in DCLSFAULT vector")		\
	X(DSU_DCLS, 47, 0x0F, "SB CPM SCLK in DCLSFAULT vector")		\
	X(DSU_DCLS, 46, 0x0E, "SB slice in DCLSFAULT vector")			\
	X(DSU_DCLS, 45, 0x0D, "SCU LTDB RAM in DCLSFAULT vector")		\
	X(DSU_DCLS, 44, 0x0C, "SCU VICTIM RAM in DCLSFAULT vector")		\
	X(DSU_DCLS, 43, 0x0B, "SCU DATA RAM in DCLSFAULT vector")		\
	X(DSU_DCLS, 42, 0x0A, "SCU SF RAM in DCLSFAULT vector")			\
	X(DSU_DCLS, 41, 0x09, "SCU TAG RAM in DCLSFAULT vector")		\
	X(DSU_DCLS, 37, 0x05, "SCU MISC group in DCLSFAULT vector")		\
	X(DSU_DCLS, 34, 0x02, "ACE Master 1 in DCLSFAULT vector")		\
	X(DSU_DCLS, 33, 0x01, "ACE Master 0 in DCLSFAULT vector")		\
	X(DSU_DCLS, 32, 0x00, "Lock Error")

/* Empty DSU DCLS Corrected RAS ERROR */
#define DSU_DCLS_CORR_RAS_ERROR_LIST(X)

/* DSU_IFP Uncorrectable RAS ERROR */
#define DSU_IFP_UNCORR_RAS_ERROR_LIST(X)					\
	/* Name, ERR_CTRL, IERR, ISA Desc */					\
	X(DSU_IFP, 60, 0x1C, "Errors on POD reset output - it raises error if POD asserts the reset")	\
	X(DSU_IFP, 59, 0x1B, "Errors on AXI4 Stream. Flagged for ADB, parity and redundancy errors")	\
	X(DSU_IFP, 58, 0x1A, "NV defined core RAS pinned out interface error")	\
	X(DSU_IFP, 57, 0x19, "NV defined cluster RAS interrupt interface error") \
	X(DSU_IFP, 56, 0x18, "NV defined cluster reqack interface error")	\
	X(DSU_IFP, 55, 0x17, "NV defined core status interface error")		\
	X(DSU_IFP, 54, 0x16, "NV defined cluster config interface error")	\
	X(DSU_IFP, 53, 0x15, "NV defined cluster power management interface error") \
	X(DSU_IFP, 52, 0x14, "Miscellaneous interface parity error")		\
	X(DSU_IFP, 51, 0x13, "Clock/Power related interface parity error")	\
	X(DSU_IFP, 49, 0x11, "GIC interface parity error")			\
	X(DSU_IFP, 44, 0x0C, "ACE Master 1 interface parity error")		\
	X(DSU_IFP, 43, 0x0B, "ACE Master 0 interface parity error")		\
	X(DSU_IFP, 42, 0x0A, "Miscellaneous interface parity error")		\
	X(DSU_IFP, 41, 0x09, "Clock/Power related interface parity error")	\
	X(DSU_IFP, 39, 0x07, "GIC interface parity error")			\
	X(DSU_IFP, 34, 0x02, "ACE Master 1 interface parity error")		\
	X(DSU_IFP, 33, 0x01, "ACE Master 0 interface parity error")		\
	X(DSU_IFP, 32, 0x00, "Lock error")

/* Empty DSU_IFP Corrected RAS ERROR */
#define DSU_IFP_CORR_RAS_ERROR_LIST(X)

/* Empty GIC600_FMU Uncorrected RAS ERROR */
#define GIC600_FMU_UNCORR_RAS_ERROR_LIST(X)

/* Empty GIC600_FMU Corrected RAS ERROR */
#define GIC600_FMU_CORR_RAS_ERROR_LIST(X)

/*
 * Define one ras_error entry.
 *
 * This macro wille be used to to generate ras_error records for each node
 * defined by <NODE_NAME>_UNCORR_RAS_ERROR_LIST macro.
 */
#define DEFINE_ONE_RAS_ERROR_MSG(unit, err_ctrl_bit, ierr, msg)			\
	{									\
		.error_msg = (msg),						\
		.error_code = (ierr)						\
	},

/*
 * Set one implementation defined bit in ERR<n>CTLR
 *
 * This macro will be used to collect all defined ERR_CTRL bits for each node
 * defined by <NODE_NAME>_UNCORR_RAS_ERROR_LIST macro.
 */
#define DEFINE_ENABLE_BIT(unit, err_ctrl_bit, ierr, msg)			\
	do {									\
		val |= (1ULL << err_ctrl_bit##U);				\
	} while (0);

/* Represent one RAS node with 0 or more error bits (ERR_CTLR) enabled */
#define DEFINE_ONE_RAS_NODE(node)						\
static struct ras_error node##_uncorr_ras_errors[] = {				\
	node##_UNCORR_RAS_ERROR_LIST(DEFINE_ONE_RAS_ERROR_MSG)			\
};										\
static inline uint64_t node##_get_ue_mask(void)					\
{										\
	uint64_t val = 0ULL;							\
	node##_UNCORR_RAS_ERROR_LIST(DEFINE_ENABLE_BIT)				\
	return val;								\
};										\
static inline uint64_t node##_get_ce_mask(void)					\
{										\
	uint64_t val = 0ULL;							\
	node##_CORR_RAS_ERROR_LIST(DEFINE_ENABLE_BIT)				\
	return val;								\
}

#define DEFINE_ONE_RAS_AUX_DATA(node)						\
static struct ras_aux_data node##_aux_data = {					\
		.name = #node,							\
		.error_records = node##_uncorr_ras_errors,			\
		.num_err_records = ARRAY_SIZE(node##_uncorr_ras_errors),	\
		.get_ue_mask = &node##_get_ue_mask,				\
		.get_ce_mask = &node##_get_ce_mask				\
	}

/*
 * ERR<n>FR bits[63:32], it indicates supported RAS errors which can be enabled
 * by setting corresponding bits in ERR<n>CTLR
 */
#define ERR_FR_EN_BITS_MASK			U(0xFFFFFFFF00000000)

/* IH RAS MUX registers */
#define IH_RAS_MUX_BASE				U(0x0E005000)
#define  CORE0_RAS_GENERAL_MASK			U(0x000)
#define  CORE0_RAS_GENERAL_ENABLE		U(0x008)
#define  CORE0_RAS_INSTATUS			U(0x010)
#define  CORE0_RAS_INSTATUS_CLR			U(0x018)
#define  RAS_GLOBAL_EN				U(0x820)

/* IH RAS MUX register settings */
#define ENB_ALL_INTR_SOURCES			U(0x0000FFFFFF3F3F7F)
#define IH_INTR_ROUTING_ENABLED			(BIT(17) | BIT(16)| U(0xFFF))

/* Per core offset to get to the core's IH registers */
#define PER_CORE_IH_OFFSET			U(0x28)

static inline uint64_t ih_read_ras_intstatus(int core)
{
	uint32_t offset = (PER_CORE_IH_OFFSET * core) + CORE0_RAS_INSTATUS;
	return mmio_read_64(IH_RAS_MUX_BASE + offset);
}

static inline void ih_write_intstatus_clr(int core, uint64_t val)
{
	uint32_t offset = (PER_CORE_IH_OFFSET * core) + CORE0_RAS_INSTATUS_CLR;
	mmio_write_64(IH_RAS_MUX_BASE + offset, val);
}

static inline void ih_write_ras_general_mask(int core, uint64_t val)
{
	uint32_t offset = (PER_CORE_IH_OFFSET * core) + CORE0_RAS_GENERAL_MASK;
	mmio_write_64(IH_RAS_MUX_BASE + offset, val);
	assert(mmio_read_64(IH_RAS_MUX_BASE + offset) == val);
}

static inline void ih_write_ras_general_enb(int core, uint64_t val)
{
	uint32_t offset = (PER_CORE_IH_OFFSET * core) + CORE0_RAS_GENERAL_ENABLE;
	mmio_write_64(IH_RAS_MUX_BASE + offset, val);
	assert(mmio_read_64(IH_RAS_MUX_BASE + offset) == val);
}

#endif /* TEGRA234_RAS_PRIVATE */
