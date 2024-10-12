/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_OS_WORK_H
#define DCE_OS_WORK_H

#include <linux/workqueue.h>

struct dce_os_work_struct {
	struct tegra_dce *d;
	struct work_struct work;
	void (*dce_os_work_fn)(struct tegra_dce *d);
};

int dce_os_work_init(struct tegra_dce *d,
		   struct dce_os_work_struct *work,
		   void (*work_fn)(struct tegra_dce *d));
void dce_os_work_schedule(struct dce_os_work_struct *work);

#endif /* DCE_OS_WORK_H */
