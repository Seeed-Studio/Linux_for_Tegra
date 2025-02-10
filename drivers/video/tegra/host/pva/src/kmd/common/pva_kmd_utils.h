/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#ifndef PVA_KMD_UTILS_H
#define PVA_KMD_UTILS_H
#include "pva_kmd.h"
#include "pva_api.h"
#include "pva_kmd_shim_utils.h"
#include "pva_bit.h"
#include "pva_utils.h"
#include "pva_plat_faults.h"
#include "pva_math_utils.h"

#define SIZE_4KB (4 * 1024)

void pva_kmd_log_err(const char *msg);
void pva_kmd_log_err_u64(const char *msg, uint64_t val);
void *pva_kmd_zalloc_nofail(uint64_t size);

#endif // PVA_KMD_UTILS_H
