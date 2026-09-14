// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022 NVIDIA CORPORATION. All rights reserved.

#ifndef __SOC_TEGRA_KFUSE_H__
#define __SOC_TEGRA_KFUSE_H__

/* there are 144 32-bit values in total */
#define KFUSE_DATA_SZ (144 * 4)

int tegra_kfuse_read(void *dest, size_t len);
void tegra_kfuse_disable_sensing(void);
int tegra_kfuse_enable_sensing(void);
#endif /* __SOC_TEGRA_KFUSE_H__ */
