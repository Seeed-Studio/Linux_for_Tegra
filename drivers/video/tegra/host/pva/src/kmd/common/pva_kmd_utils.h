/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

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
void pva_kmd_log_err_hex32(const char *msg, uint32_t val);
void *pva_kmd_zalloc_nofail(uint64_t size);

#endif // PVA_KMD_UTILS_H
