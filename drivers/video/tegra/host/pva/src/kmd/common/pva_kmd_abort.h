/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_ABORT_H
#define PVA_KMD_ABORT_H
#include "pva_kmd_device.h"
#include "pva_kmd_utils.h"

void pva_kmd_abort_fw(struct pva_kmd_device *pva, uint32_t error_code);

#endif //PVA_KMD_ABORT_H
