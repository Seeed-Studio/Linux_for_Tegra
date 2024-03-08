/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef __TEGRA_NVADSP_ARAM_MANAGER_H
#define __TEGRA_NVADSP_ARAM_MANAGER_H

#include "mem_manager.h"
#include "dev.h"

int nvadsp_aram_init(struct nvadsp_drv_data *drv_data,
		unsigned long addr, unsigned long size);
void nvadsp_aram_exit(struct nvadsp_drv_data *drv_data);

#endif /* __TEGRA_NVADSP_ARAM_MANAGER_H */
