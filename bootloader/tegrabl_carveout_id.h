/*
 * Copyright (c) 2016-2022, NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef INCLUDED_CARVEOUT_ID_H
#define INCLUDED_CARVEOUT_ID_H

#if !defined(IN_DTS_CONTEXT)
/**
 * Tracks the base and size of the Carveout
 */

struct tegrabl_carveout_info {
	uint64_t base;
	uint64_t size;
	union {
		struct {
			uint64_t ecc_protected:1;
			uint64_t reserved:63;
		};
		uint64_t flags;
	};
};

#endif /* IN_DTS_CONTEXT */

#define CARVEOUT_NVDEC							1U
#define CARVEOUT_WPR1							2U
#define CARVEOUT_WPR2							3U
#define CARVEOUT_TSEC							4U
#define CARVEOUT_XUSB							5U
#define CARVEOUT_BPMP							6U
#define CARVEOUT_APE							7U
#define CARVEOUT_SPE							8U
#define CARVEOUT_SCE							9U
#define CARVEOUT_APR							10U
#define CARVEOUT_BPMP_DCE						11U
#define CARVEOUT_UNUSED3						12U
#define CARVEOUT_BPMP_RCE						13U
#define CARVEOUT_BPMP_MCE						14U
#define CARVEOUT_ETR							15U
#define CARVEOUT_BPMP_SPE						16U
#define CARVEOUT_RCE							17U
#define CARVEOUT_BPMP_CPUTZ						18U
#define CARVEOUT_UNUSED1						19U
#define CARVEOUT_DCE							20U
#define CARVEOUT_BPMP_PSC						21U
#define CARVEOUT_PSC							22U
#define CARVEOUT_NV_SC7							23U
#define CARVEOUT_CAMERA_TASKLIST				24U
#define CARVEOUT_BPMP_SCE						25U
#define CARVEOUT_CV_GOS							26U
#define CARVEOUT_PSC_TSEC						27U
#define CARVEOUT_CCPLEX_INTERWORLD_SHMEM		28U
#define CARVEOUT_FSI							29U
#define CARVEOUT_MCE							30U
#define CARVEOUT_CCPLEX_IST						31U
#define CARVEOUT_TSEC_HOST1X					32U
#define CARVEOUT_PSC_TZ							33U
#define CARVEOUT_SCE_CPU_NS						34U
#define CARVEOUT_OEM_SC7						35U
#define CARVEOUT_SYNCPT_IGPU_RO					36U
#define CARVEOUT_SYNCPT_IGPU_NA					37U
#define CARVEOUT_VM_ENCRYPT						38U
#define CARVEOUT_BLANKET_NSDRAM					CARVEOUT_VM_ENCRYPT
#define CARVEOUT_CCPLEX_SMMU_PTW				39U
#define CARVEOUT_BPMP_CPU_NS					40U
#define CARVEOUT_FSI_CPU_NS						41U
#define CARVEOUT_TSEC_DCE						42U
#define CARVEOUT_TZDRAM							43U
#define CARVEOUT_VPR							44U
#define CARVEOUT_MTS							45U
#define CARVEOUT_RCM_BLOB						46U
#define CARVEOUT_UEFI							47U
#define CARVEOUT_UEFI_MM_IPC					48U
#define CARVEOUT_DRAM_ECC_TEST					49U
#define CARVEOUT_PROFILING						50U
#define CARVEOUT_OS								51U
#define CARVEOUT_FSI_KEY_BLOB					52U
#define CARVEOUT_TEMP_MB2RF						53U
#define CARVEOUT_TEMP_MB2_LOAD					54U
#define CARVEOUT_TEMP_MB2_PARAMS				55U
#define CARVEOUT_TEMP_MB2_IO_BUFFERS			56U
#define CARVEOUT_TEMP_MB2RF_DATA				57U
#define CARVEOUT_TEMP_MB2						58U
#define CARVEOUT_TEMP_MB2_SYSRAM_DATA			59U
#define CARVEOUT_TSEC_CCPLEX					60U
#define CARVEOUT_TEMP_MB2_APLT_LOAD				61U
#define CARVEOUT_TEMP_MB2_APLT_PARAMS			62U
#define CARVEOUT_TEMP_MB2_APLT_IO_BUFFERS		63U
#define CARVEOUT_TEMP_MB2_APLT_SYSRAM_DATA		64U
#define CARVEOUT_GR								65U
#define CARVEOUT_TEMP_QB_DATA					66U
#define CARVEOUT_TEMP_QB_IO_BUFFER				67U
#define CARVEOUT_ATF_FSI						68U
#define CARVEOUT_OPTEE_DTB						69U
#define CARVEOUT_UNUSED2						70U
#define CARVEOUT_UNUSED4						71U
#define CARVEOUT_RAM_OOPS						72U
#define CARVEOUT_OEM_COUNT						73U

#endif
