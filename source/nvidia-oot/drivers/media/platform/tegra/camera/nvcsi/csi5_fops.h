/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tegra CSI5 device common APIs
 *
 * Copyright (c) 2016-2022, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __CSI5_H__
#define __CSI5_H__

extern struct tegra_csi_fops csi5_fops;

int csi5_tpg_set_gain(struct tegra_csi_channel *chan, int gain_ratio_tpg);

#endif
