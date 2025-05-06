/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SHIM_DEBUGFS_H
#define PVA_KMD_SHIM_DEBUGFS_H
#include "pva_api.h"
#include "pva_kmd_tegra_stats.h"

void pva_kmd_debugfs_create_bool(struct pva_kmd_device *pva, const char *name,
				 bool *val);
void pva_kmd_debugfs_create_u32(struct pva_kmd_device *pva, const char *name,
				uint32_t *val);
enum pva_error pva_kmd_debugfs_create_file(struct pva_kmd_device *pva,
					   const char *name,
					   struct pva_kmd_file_ops *fops);
void pva_kmd_debugfs_remove_nodes(struct pva_kmd_device *pva);
unsigned long pva_kmd_copy_data_from_user(void *dst, const void *src,
					  uint64_t size);
unsigned long pva_kmd_copy_data_to_user(void *to, const void *from,
					unsigned long size);
unsigned long pva_kmd_strtol(const char *str, int base);

enum pva_error pva_kmd_simulate_enter_sc7(struct pva_kmd_device *pva);
enum pva_error pva_kmd_simulate_exit_sc7(struct pva_kmd_device *pva);

#endif //PVA_KMD_SHIM_DEBUGFS_H