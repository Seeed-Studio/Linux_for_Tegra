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

#ifndef PVA_KMD_PM_H
#define PVA_KMD_PM_H

struct pva_kmd_device;
enum pva_error pva_kmd_prepare_suspend(struct pva_kmd_device *pva);
enum pva_error pva_kmd_complete_resume(struct pva_kmd_device *pva);

#endif