/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEGRA_DRIVER_FUSE_H__
#define __TEGRA_DRIVER_FUSE_H__

#include <drivers/tegra/tegra_fuse.h>

#if defined(PLATFORM_FLAVOR_t194)
#define TEGRA_FUSE_BASE         0x03820000
#define TEGRA_FUSE_SIZE         0x600
#define TEGRA_INT_CID		7ULL
#endif

#if defined(PLATFORM_FLAVOR_t234)
#define TEGRA_FUSE_BASE         0x03810000
#define TEGRA_FUSE_SIZE         0x600
#define TEGRA_INT_CID		8ULL
#endif

#define FUSE_OPT_VENDOR_CODE_0		0x200
#define FUSE_OPT_FAB_CODE_0		0x204
#define FUSE_OPT_LOT_CODE_0_0		0x208
#define FUSE_OPT_LOT_CODE_1_0		0x20c
#define FUSE_OPT_WAFER_ID_0		0x210
#define FUSE_OPT_X_COORDINATE_0		0x214
#define FUSE_OPT_Y_COORDINATE_0		0x218
#define FUSE_OPT_OPS_RESERVED_0		0x220
#define OPT_VENDOR_CODE_MASK		0xF
#define OPT_FAB_CODE_MASK		0x3F
#define OPT_LOT_CODE_1_MASK		0xfffffff
#define OPT_WAFER_ID_MASK		0x3F
#define OPT_X_COORDINATE_MASK		0x1FF
#define OPT_Y_COORDINATE_MASK		0x1FF
#define OPT_OPS_RESERVED_MASK		0x3F
#define ECID_ECID0_0_RSVD1_MASK		0x3F
#define ECID_ECID0_0_Y_MASK		0x1FF
#define ECID_ECID0_0_Y_RANGE		6
#define ECID_ECID0_0_X_MASK		0x1FF
#define ECID_ECID0_0_X_RANGE		15
#define ECID_ECID0_0_WAFER_MASK		0x3F
#define ECID_ECID0_0_WAFER_RANGE	24
#define ECID_ECID0_0_LOT1_MASK		0x3
#define ECID_ECID0_0_LOT1_RANGE		30
#define ECID_ECID1_0_LOT1_MASK		0x3FFFFFF
#define ECID_ECID1_0_LOT0_MASK		0x3F
#define ECID_ECID1_0_LOT0_RANGE		26
#define ECID_ECID2_0_LOT0_MASK		0x3FFFFFF
#define ECID_ECID2_0_FAB_MASK		0x3F
#define ECID_ECID2_0_FAB_RANGE		26
#define ECID_ECID3_0_VENDOR_MASK	0xF
#define FUSE_SN_SIZE			10U
#define FUSE_ODMID0_0			0x408
#define FUSE_ODMID1_0			0x40c
#define FUSE_ODM_INFO_0			0x29c

TEE_Result tegra_fuse_map_regs(vaddr_t *va, size_t *size);
TEE_Result tegra_fuse_unmap_regs(vaddr_t va, size_t size);
TEE_Result fuse_generate_ecid(vaddr_t fuse_va_base, fuse_ecid_t *ecid_128, uint64_t *ecid_64);
TEE_Result fuse_generate_sn(vaddr_t fuse_va_base, uint8_t *sn, uint32_t *size);

#endif
