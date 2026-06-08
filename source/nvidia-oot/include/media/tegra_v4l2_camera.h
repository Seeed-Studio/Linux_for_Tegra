/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2022, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef _TEGRA_CAMERA_H_
#define _TEGRA_CAMERA_H_

#include <linux/regulator/consumer.h>
#include <linux/i2c.h>

enum tegra_camera_port {
	TEGRA_CAMERA_PORT_CSI_A = 0,
	TEGRA_CAMERA_PORT_CSI_B,
	TEGRA_CAMERA_PORT_CSI_C,
	TEGRA_CAMERA_PORT_CSI_D,
	TEGRA_CAMERA_PORT_CSI_E,
	TEGRA_CAMERA_PORT_CSI_F,
	TEGRA_CAMERA_PORT_VIP,
};

struct tegra_camera_platform_data {
	int			(*enable_camera)(struct platform_device *pdev);
	void			(*disable_camera)(struct platform_device *pdev);
	bool			flip_h;
	bool			flip_v;
	enum tegra_camera_port	port;
	int			lanes;		/* For CSI port only */
	bool			continuous_clk;	/* For CSI port only */
};

struct i2c_camera_ctrl {
	int	(*new_devices)(struct platform_device *pdev);
	void	(*remove_devices)(struct platform_device *pdev);
};
#endif /* _TEGRA_CAMERA_H_ */
