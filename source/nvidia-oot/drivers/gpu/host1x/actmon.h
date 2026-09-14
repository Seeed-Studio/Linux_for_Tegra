/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tegra host1x Actmon
 *
 * Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef HOST1X_ACTMON_H
#define HOST1X_ACTMON_H

#include <linux/device.h>
#include <linux/types.h>

enum host1x_actmon_module_type {
	HOST1X_ACTMON_MODULE_ACTIVE,
	HOST1X_ACTMON_MODULE_STALL,
};

struct host1x_actmon;

struct host1x_actmon_module {
	enum host1x_actmon_module_type type;
	u32 k;
	u32 consec_upper_num;
	u32 consec_lower_num;
	void __iomem *regs;
	struct host1x_actmon *actmon;
	struct dentry *debugfs;
};

struct host1x_client;

struct host1x_actmon {
	unsigned int irq;
	unsigned int num_modules;
	unsigned long rate;
	u32 usecs_per_sample;
	void __iomem *regs;
	struct host1x_client *client;
	struct host1x_actmon_module modules[8];
	struct dentry *debugfs;
	struct list_head list;
};

struct host1x;

#ifdef CONFIG_PM_DEVFREQ
void host1x_actmon_handle_interrupt(struct host1x *host, int classid);
#else
static inline void host1x_actmon_handle_interrupt(struct host1x *host, int classid)
{
}
#endif

#endif
