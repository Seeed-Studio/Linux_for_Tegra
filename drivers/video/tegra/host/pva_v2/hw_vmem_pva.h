/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef _hw_vmem_pva_h_
#define _hw_vmem_pva_h_

#define NUM_HEM_GEN		3U
#define VMEM_REGION_COUNT_T19x	3U
#define VMEM_REGION_COUNT_T23x	3U

#define T19X_VMEM0_START	0x40U
#define T19X_VMEM0_END		0x10000U
#define T19X_VMEM1_START	0x40000U
#define T19X_VMEM1_END		0x50000U
#define T19X_VMEM2_START	0x80000U
#define T19X_VMEM2_END		0x90000U

#define T23x_VMEM0_START	0x40U
#define T23x_VMEM0_END		0x20000U
#define T23x_VMEM1_START	0x40000U
#define T23x_VMEM1_END		0x60000U
#define T23x_VMEM2_START	0x80000U
#define T23x_VMEM2_END		0xA0000U

#endif
