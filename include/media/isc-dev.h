/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __ISC_DEV_H__
#define __ISC_DEV_H__

#include <uapi/media/isc-dev.h>
#include <linux/regmap.h>

#define MAX_ISC_NAME_LENGTH	32

struct isc_dev_platform_data {
	struct device *pdev; /* parent device of isc_dev */
	int reg_bits;
	int val_bits;
	char drv_name[MAX_ISC_NAME_LENGTH];
};

#endif  /* __ISC_DEV_H__ */
