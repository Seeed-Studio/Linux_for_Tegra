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

#ifndef PVA_PLAT_FAULTS_H
#define PVA_PLAT_FAULTS_H

#include "pva_kmd_shim_utils.h"

#define ASSERT(x)                                                              \
	if (!(x)) {                                                            \
		pva_kmd_print_str_u64("PVA KMD ASSERT at " __FILE__,           \
				      __LINE__);                               \
		pva_kmd_fault();                                               \
	}

#define FAULT(msg)                                                             \
	{                                                                      \
		pva_kmd_print_str_u64("PVA KMD FAULT at " __FILE__, __LINE__); \
		pva_kmd_print_str(msg);                                        \
		pva_kmd_fault();                                               \
	}                                                                      \
	while (0)

#define ASSERT_WITH_LOC(x, err_file, err_line)                                 \
	if (!(x)) {                                                            \
		pva_kmd_print_str_u64("Error at line", err_line);              \
		pva_kmd_print_str(err_file);                                   \
		pva_kmd_print_str("PVA KMD ASSERT");                           \
		pva_kmd_fault();                                               \
	}

#endif