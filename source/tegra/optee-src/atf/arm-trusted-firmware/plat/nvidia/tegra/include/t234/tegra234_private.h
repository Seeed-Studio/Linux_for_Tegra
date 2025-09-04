/*
 * Copyright (c) 2020-2021, NVIDIA Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TEGRA234_PRIVATE
#define TEGRA234_PRIVATE

void tegra234_ras_init_common(void);
void tegra234_ras_init_my_cluster(void);
int tegra234_ras_inject_fault(uint64_t base, uint64_t pfgcdn, uint64_t pfgctl);

#endif /* TEGRA234_PRIVATE */
