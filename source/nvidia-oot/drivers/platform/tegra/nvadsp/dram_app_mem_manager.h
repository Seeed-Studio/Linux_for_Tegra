/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (c) 2014-2023, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef __TEGRA_NVADSP_DRAM_APP_MEM_MANAGER_H
#define __TEGRA_NVADSP_DRAM_APP_MEM_MANAGER_H

#include "mem_manager.h"

int dram_app_mem_init(unsigned long, unsigned long);
void dram_app_mem_exit(void);

void *dram_app_mem_request(const char *name, size_t size);
bool dram_app_mem_release(void *handle);

unsigned long dram_app_mem_get_address(void *handle);
void dram_app_mem_print(void);

#endif /* __TEGRA_NVADSP_DRAM_APP_MEM_MANAGER_H */
