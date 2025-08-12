// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

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

void pva_kmd_log_err_hex32(const char *msg, uint32_t val)
{
	pva_kmd_print_str_hex32(msg, val);
}
