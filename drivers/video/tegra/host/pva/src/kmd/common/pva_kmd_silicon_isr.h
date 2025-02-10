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
#ifndef PVA_KMD_SILICON_ISR_H
#define PVA_KMD_SILICON_ISR_H
#include "pva_kmd_silicon_utils.h"
#include "pva_kmd_device.h"

void pva_kmd_hyp_isr(void *data);

void pva_kmd_isr(void *data);

#endif // PVA_KMD_SILICON_ISR_H
