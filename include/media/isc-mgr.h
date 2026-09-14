/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __TEGRA_ISC_MGR_H__
#define __TEGRA_ISC_MGR_H__

#include <uapi/media/isc-mgr.h>

#define MAX_ISC_GPIOS	8

struct isc_mgr_client {
	struct mutex mutex;
	struct list_head list;
	struct i2c_client *client;
	struct isc_mgr_new_dev cfg;
	struct isc_dev_platform_data pdata;
	int id;
};

struct isc_mgr_platform_data {
	int bus;
	int num_pwr_gpios;
	u32 pwr_gpios[MAX_ISC_GPIOS];
	u32 pwr_flags[MAX_ISC_GPIOS];
	int num_pwr_map;
	u32 pwr_mapping[MAX_ISC_GPIOS];
	int num_misc_gpios;
	u32 misc_gpios[MAX_ISC_GPIOS];
	u32 misc_flags[MAX_ISC_GPIOS];
	int csi_port;
	bool default_pwr_on;
	bool runtime_pwrctrl_off;
	char *drv_name;
	u8 ext_pwr_ctrl; /* bit 0 - des, bit 1 - sensor */
};

int isc_delete_lst(struct device *dev, struct i2c_client *client);

#endif  /* __TEGRA_ISC_MGR_H__ */
