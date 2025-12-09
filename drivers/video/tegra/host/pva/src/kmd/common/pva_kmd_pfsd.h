/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_PFSD_H
#define PVA_KMD_PFSD_H

#include "pva_kmd_cmdbuf.h"
#include "pva_api_dma.h"
#include "pva_utils.h"

struct pva_kmd_context;

#define PVA_PFSD_DATA_ARRAY_IN_LEN (512U)
#define PVA_PFSD_DATA_ARRAY_HIST_SHORT_LEN (512U)
#define PVA_PFSD_DATA_ARRAY_HIST_INT_LEN (256U)

extern const uint32_t in1[PVA_PFSD_DATA_ARRAY_IN_LEN];
extern const uint32_t in2[PVA_PFSD_DATA_ARRAY_IN_LEN];
extern const uint32_t in3[PVA_PFSD_DATA_ARRAY_IN_LEN];
extern const uint16_t hist_short_a[PVA_PFSD_DATA_ARRAY_HIST_SHORT_LEN];
extern const uint16_t hist_short_b[PVA_PFSD_DATA_ARRAY_HIST_SHORT_LEN];
extern const uint16_t hist_short_c[PVA_PFSD_DATA_ARRAY_HIST_SHORT_LEN];
extern const uint32_t hist_int_a[PVA_PFSD_DATA_ARRAY_HIST_INT_LEN];
extern const uint32_t hist_int_b[PVA_PFSD_DATA_ARRAY_HIST_INT_LEN];
extern const uint32_t hist_int_c[PVA_PFSD_DATA_ARRAY_HIST_INT_LEN];

#define DLUT_TBL0_NUM_ENTRIES (256U)
#define DLUT_TBL1_NUM_ENTRIES (28672U)
#define DLUT_TBL2_NUM_ENTRIES (9114U)
#define DLUT_TBL3_NUM_ENTRIES (2048U)
#define PVA_PFSD_DLUT_TBL0_SIZE (DLUT_TBL0_NUM_ENTRIES * sizeof(int16_t))
#define PVA_PFSD_DLUT_TBL1_SIZE (DLUT_TBL1_NUM_ENTRIES * sizeof(uint16_t))
#define PVA_PFSD_DLUT_TBL2_SIZE                                                \
	PVA_ROUND_UP((DLUT_TBL2_NUM_ENTRIES * sizeof(uint16_t)), 64U)
#define PVA_PFSD_DLUT_TBL3_SIZE (DLUT_TBL3_NUM_ENTRIES * sizeof(uint8_t))

extern const int16_t pva_pfsd_data_dlut_tbl0[];
extern const uint16_t pva_pfsd_data_dlut_tbl1[];
extern const uint16_t pva_pfsd_data_dlut_tbl2[];
extern const uint8_t pva_pfsd_data_dlut_tbl3[];

#define DLUT_IDX0_NUM_ENTRIES (8192U)
#define DLUT_IDX1_NUM_ENTRIES (4096U)
#define DLUT_IDX2_NUM_ENTRIES (8192U)
#define DLUT_IDX3_NUM_ENTRIES (4096U)
#define PVA_PFSD_DLUT_INDICES0_SIZE (DLUT_IDX0_NUM_ENTRIES * sizeof(int16_t))
#define PVA_PFSD_DLUT_INDICES1_SIZE (DLUT_IDX1_NUM_ENTRIES * sizeof(int32_t))
#define PVA_PFSD_DLUT_INDICES2_SIZE (DLUT_IDX2_NUM_ENTRIES * sizeof(int32_t))
#define PVA_PFSD_DLUT_INDICES3_SIZE (DLUT_IDX3_NUM_ENTRIES * sizeof(int32_t))

extern const int16_t pva_pfsd_data_dlut_indices0[];
extern const int32_t pva_pfsd_data_dlut_indices1[];
extern const int32_t pva_pfsd_data_dlut_indices2[];
extern const int32_t pva_pfsd_data_dlut_indices3[];

#define MISR_SEED_0_T23X 0xA5A5A5A5U
#define MISR_SEED_1_T23X 0x5A5A5A5AU
#define MISR_REF_0_T23X 0xFF9DB35CU
#define MISR_REF_1_T23X 0x9840285EU
#define MISR_REF_2_T23X 0x2035EA07U
#define MISR_TIMEOUT_T23X 0x1C9C38U

#define MISR_SEED_0_T26X 0xA5A5A5A5U
#define MISR_SEED_1_T26X 0x5A5A5A5AU
#define MISR_REF_0_T26X 0x31E64312U
#define MISR_REF_1_T26X 0x585AAACDU
#define MISR_REF_2_T26X 0x065AFA90U
#define MISR_TIMEOUT_T26X 0x2DC6C0U

struct pva_pfsd_resource_ids {
	// Input buffer resource IDs
	uint32_t in1_resource_id;
	uint32_t in2_resource_id;
	uint32_t in3_resource_id;
	uint32_t hist_a_resource_id;
	uint32_t hist_b_resource_id;
	uint32_t hist_c_resource_id;
	uint32_t dlut_tbl_resource_id;
	uint32_t dlut_indices_resource_id;
	// VPU executable resource ID
	uint32_t vpu_elf_resource_id;
	// PPE executable resource ID
	uint32_t ppe_elf_resource_id;
	// DMA configuration resource ID
	uint32_t dma_config_resource_id;
	// Command buffer resource ID
	uint32_t cmd_buffer_resource_id;
	// Command buffer size
	uint32_t cmd_buffer_size;
};

#define PVA_PFSD_VPU_ELF_SIZE_T23X (1259220U)
extern const uint8_t pva_pfsd_vpu_elf_t23x[];

#define PVA_PFSD_VPU_ELF_SIZE_T26X (1279424U)
extern const uint8_t pva_pfsd_vpu_elf_t26x[];

#define PVA_PFSD_PPE_ELF_SIZE_T26X (222468U)
extern const uint8_t pva_pfsd_ppe_elf_t26x[];

extern const struct pva_dma_config pfsd_dma_cfg_t23x;
extern const struct pva_dma_config pfsd_dma_cfg_t26x;

/**
 * Register PFSD input buffers with the resource table
 *
 * @param ctx PVA KMD context
 * @return PVA_SUCCESS on success, error code otherwise
 */
enum pva_error pva_kmd_pfsd_register_input_buffers(struct pva_kmd_context *ctx);

/**
 * Register PFSD ELF executables with the resource table
 *
 * @param ctx PVA KMD context
 * @param chip_id Chip ID to determine which ELF to register
 * @return PVA_SUCCESS on success, error code otherwise
 */
enum pva_error pva_kmd_pfsd_register_elf(struct pva_kmd_context *ctx);

/**
 * Register PFSD DMA configuration with the resource table
 *
 * @param ctx PVA KMD context
 * @param chip_id Chip ID to determine which DMA config to register
 * @return PVA_SUCCESS on success, error code otherwise
 */
enum pva_error pva_kmd_pfsd_register_dma_config(struct pva_kmd_context *ctx);

/**
 * Register PFSD command buffer for T23X with the resource table
 *
 * @param ctx PVA KMD context
 * @return PVA_SUCCESS on success, error code otherwise
 */
enum pva_error pva_kmd_pfsd_t23x_register_cmdbuf(struct pva_kmd_context *ctx);

/**
 * Register PFSD command buffer for T26X with the resource table
 *
 * @param ctx PVA KMD context
 * @return PVA_SUCCESS on success, error code otherwise
 */
enum pva_error pva_kmd_pfsd_t26x_register_cmdbuf(struct pva_kmd_context *ctx);

#endif