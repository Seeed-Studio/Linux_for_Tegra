/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 */

#ifndef _TEGRA_OOT_PRODS_H
#define _TEGRA_OOT_PRODS_H

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
#ifdef CONFIG_TEGRA_PROD_LEGACY
#include <soc/tegra/tegra-prod.h>
#include <linux/tegra_prod.h>
#else
#include <linux/tegra-prod-upstream-dummy.h>
#include <linux/tegra-prod-legacy-dummy.h>
#endif
#else /* LINUX_VERSION_CODE */
#ifdef CONFIG_TEGRA_PROD_LEGACY
#include <linux/tegra_prod.h>
#else
#include <linux/tegra-prod-legacy-dummy.h>
#endif
#endif /* LINUX_VERSION_CODE */
#endif /* _TEGRA_OOT_PRODS_H */
