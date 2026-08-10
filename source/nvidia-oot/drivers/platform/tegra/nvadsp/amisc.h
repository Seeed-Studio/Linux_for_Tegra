/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * amisc.h - AMISC register access
 *
 * Copyright (c) 2022 NVIDIA CORPORATION. All rights reserved.
 *
 */

#ifndef __TEGRA_NVADSP_AMISC_H
#define __TEGRA_NVADSP_AMISC_H

#include "dev.h"

#define AMISC_ADSP_STATUS		(0x14)
#define   AMISC_ADSP_L2_CLKSTOPPED	(1 << 30)
#define   AMISC_ADSP_L2_IDLE		(1 << 31)

static inline u32 amisc_readl(struct nvadsp_drv_data *drv_data, u32 reg)
{
	return readl(drv_data->base_regs[AMISC] + reg);
}

#endif /* __TEGRA_NVADSP_AMISC_H */
