/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef _LINUX_TEGRA_AON_H
#define _LINUX_TEGRA_AON_H

struct tegra_aon_mbox_msg {
	int length;
	void *data;
};

#endif
