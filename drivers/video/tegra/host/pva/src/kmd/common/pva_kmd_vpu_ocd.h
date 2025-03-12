/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_VPU_OCD_H
#define PVA_KMD_VPU_OCD_H

#define VPU_OCD_MAX_NUM_DATA_ACCESS 7U

struct pva_vpu_ocd_write_param {
	uint32_t instr;
	uint32_t n_write;
	uint32_t data[VPU_OCD_MAX_NUM_DATA_ACCESS];
};

struct pva_vpu_ocd_read_param {
	uint32_t n_read;
	uint32_t data[VPU_OCD_MAX_NUM_DATA_ACCESS];
};

int64_t pva_kmd_vpu_ocd_read(struct pva_kmd_device *dev, void *file_data,
			     uint8_t *data, uint64_t offset, uint64_t size);
int64_t pva_kmd_vpu_ocd_write(struct pva_kmd_device *dev, void *file_data,
			      const uint8_t *data, uint64_t offset,
			      uint64_t size);
int pva_kmd_vpu_ocd_open(struct pva_kmd_device *dev);
int pva_kmd_vpu_ocd_release(struct pva_kmd_device *dev);

#endif