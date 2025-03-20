/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_CO_H
#define PVA_KMD_CO_H

struct pva_co_info {
	uint64_t base_va;
	uint64_t base_pa;
	uint64_t size;
};

#endif //PVA_KMD_CO_H