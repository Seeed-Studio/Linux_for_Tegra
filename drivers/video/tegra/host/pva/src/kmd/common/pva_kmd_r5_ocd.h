/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_R5_OCD_H
#define PVA_KMD_R5_OCD_H

#include "pva_kmd_device.h"
#include "pva_kmd.h"

int64_t pva_kmd_r5_ocd_read(struct pva_kmd_device *dev, void *file_data,
			    uint8_t *data, uint64_t offset, uint64_t size);
int64_t pva_kmd_r5_ocd_write(struct pva_kmd_device *dev, void *file_data,
			     const uint8_t *data, uint64_t offset,
			     uint64_t size);
int pva_kmd_r5_ocd_open(struct pva_kmd_device *dev);
int pva_kmd_r5_ocd_release(struct pva_kmd_device *dev);

#endif