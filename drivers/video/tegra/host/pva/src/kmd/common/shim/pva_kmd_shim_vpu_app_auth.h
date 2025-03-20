/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SHIM_VPU_APP_AUTH_H
#define PVA_KMD_SHIM_VPU_APP_AUTH_H

#include "pva_api_types.h"
struct pva_kmd_device;
const char *pva_kmd_get_default_allowlist(void);
enum pva_error pva_kmd_auth_allowlist_load(struct pva_kmd_device *pva,
					   const char *file_name,
					   uint8_t **hash_keys_data,
					   uint64_t *psize);
void pva_kmd_update_allowlist_path(struct pva_kmd_device *pva,
				   const char *allowlist_path);
#endif