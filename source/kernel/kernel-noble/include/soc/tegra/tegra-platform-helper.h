/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef TEGRA_PLATFORM_HELPER_H
#define TEGRA_PLATFORM_HELPER_H

static inline bool tegra_is_hypervisor_mode(void)
{
#ifdef CONFIG_OF
	return of_property_read_bool(of_chosen,
		"nvidia,tegra-hypervisor-mode");
#else
	return false;
#endif
}

#endif /* TEGRA_PLATFORM_HELPER_H */
