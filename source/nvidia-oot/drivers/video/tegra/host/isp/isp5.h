/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tegra ISP5
 *
 * Copyright (c) 2017-2022 NVIDIA Corporation.  All rights reserved.
 */

#ifndef __NVHOST_ISP5_H__
#define __NVHOST_ISP5_H__

#include <linux/platform_device.h>

extern const struct file_operations tegra194_isp5_ctrl_ops;

struct t194_isp5_file_private {
	struct platform_device *pdev;
};

int isp5_priv_early_probe(struct platform_device *pdev);
int isp5_priv_late_probe(struct platform_device *pdev);

#endif
