// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_utils.h"

void *pva_kmd_zalloc_nofail(uint64_t size)
{
	void *ptr = pva_kmd_zalloc(size);
	ASSERT(ptr != NULL);
	return ptr;
}
