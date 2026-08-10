/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tegra VI5
 *
 * Copyright (c) 2017-2022 NVIDIA Corporation.  All rights reserved.
 */

#ifndef __NVHOST_VI5_H__
#define __NVHOST_VI5_H__

#include <linux/platform_device.h>
#include <media/mc_common.h>

int nvhost_vi5_aggregate_constraints(struct platform_device *dev,
				int clk_index,
				unsigned long floor_rate,
				unsigned long pixelrate,
				unsigned long bw_constraint);

int vi5_priv_early_probe(struct platform_device *pdev);
int vi5_priv_late_probe(struct platform_device *pdev);

#endif
