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

#include "pva_kmd_utils.h"

void *pva_kmd_zalloc_nofail(uint64_t size)
{
	void *ptr = pva_kmd_zalloc(size);
	ASSERT(ptr != NULL);
	return ptr;
}

void pva_kmd_log_err(const char *msg)
{
	pva_kmd_print_str(msg);
}

void pva_kmd_log_err_u64(const char *msg, uint64_t val)
{
	pva_kmd_print_str_u64(msg, val);
}
