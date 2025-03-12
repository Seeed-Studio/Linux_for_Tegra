/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_PM_H
#define PVA_KMD_PM_H

struct pva_kmd_device;
enum pva_error pva_kmd_prepare_suspend(struct pva_kmd_device *pva);
enum pva_error pva_kmd_complete_resume(struct pva_kmd_device *pva);

#endif