/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SILICON_ISR_H
#define PVA_KMD_SILICON_ISR_H
#include "pva_kmd_silicon_utils.h"
#include "pva_kmd_device.h"

void pva_kmd_hyp_isr(void *data, enum pva_kmd_intr_line intr_line);

/* CCQ interrupt handler */
void pva_kmd_isr(void *data, enum pva_kmd_intr_line intr_line);

#endif // PVA_KMD_SILICON_ISR_H
