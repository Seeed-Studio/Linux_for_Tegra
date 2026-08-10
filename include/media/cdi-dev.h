// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef __CDI_DEV_H__
#define __CDI_DEV_H__

#include <uapi/media/cdi-dev.h>
#include <linux/regmap.h>

#define MAX_CDI_NAME_LENGTH	32

struct cdi_dev_platform_data {
	struct device *pdev; /* parent device of cdi_dev */
	struct device_node *np;
	int reg_bits;
	int val_bits;
	char drv_name[MAX_CDI_NAME_LENGTH];
};

#endif  /* __CDI_DEV_H__ */
