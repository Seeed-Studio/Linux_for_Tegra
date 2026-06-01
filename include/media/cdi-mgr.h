// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef __TEGRA_CDI_MGR_H__
#define __TEGRA_CDI_MGR_H__

#include <uapi/media/cdi-mgr.h>

#define MAX_CDI_GPIOS	8

struct cdi_mgr_client {
	struct mutex mutex;
	struct list_head list;
	struct i2c_client *client;
	struct cdi_mgr_new_dev cfg;
	struct cdi_dev_platform_data pdata;
	int id;
};

struct cdi_mgr_platform_data {
	int bus;
	int num_pwr_gpios;
	u32 pwr_gpios[MAX_CDI_GPIOS];
	u32 pwr_flags[MAX_CDI_GPIOS];
	int num_pwr_map;
	u32 pwr_mapping[MAX_CDI_GPIOS];
	int num_mcdi_gpios;
	u32 mcdi_gpios[MAX_CDI_GPIOS];
	u32 mcdi_flags[MAX_CDI_GPIOS];
	int csi_port;
	bool default_pwr_on;
	bool runtime_pwrctrl_off;
	char *drv_name;
	u8 ext_pwr_ctrl; /* bit 0 - des, bit 1 - sensor */
	bool max20087_pwrctl;
};

int cdi_delete_lst(struct device *dev, struct i2c_client *client);

#endif  /* __TEGRA_CDI_MGR_H__ */
